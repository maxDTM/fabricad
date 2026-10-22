// FabriCAD app: command card host (was CommandDialog).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/CommandCard.h"

#include "app/CardFrame.h"
#include "app/CardSlot.h"
#include "app/SegmentedControl.h"
#include "app/StepIndicator.h"
#include "app/ThemeManager.h"
#include "app/TitleBar.h"
#include "theme/Tokens.h"
#include "viewer/OverlayHost.h"
#include "viewer/ViewerWidget.h"

#include <QApplication>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace fabricad::app {

using theme::Tok;
using viewer::CardPlacement;
using viewer::FreePos;
using viewer::OverlayLayout;

namespace {
constexpr int kBleed = CardFrame::kBleed;
constexpr int kPad = 16;             // card padding (5.3)
constexpr int kHeaderH = 40;
constexpr int kStepsH = StepIndicator::kHeight + 8;
constexpr int kFooterH = 52;
constexpr int kMaxBodyHeight = 560;  // taller bodies (Joint) scroll
constexpr int kWrapChars = 40;       // body labels longer than this word-wrap
constexpr int kLabelColumn = 96;     // form label column (3.4)
constexpr int kSnap = 64;            // a dropped card snaps to a float slot this close (px)
// enums that stay combos although they have <= 4 values: EXTENT is a combo in the v6 mockup
// (its choices swap the rows below it, like a menu of modes)
const QStringList kComboEnums = {"prop_extentType", "prop_extentType2"};

// ---- header glyph buttons (pin, close): painted from the tokens --------------------------------
class CardGlyphButton : public QToolButton {
 public:
  enum Glyph { Pin, Close };
  CardGlyphButton(Glyph g, QWidget* parent) : QToolButton(parent), glyph_(g) {
    setFixedSize(24, 24);
    setFocusPolicy(Qt::TabFocus);
    setAutoRaise(true);
    setAttribute(Qt::WA_Hover);
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    const bool hover = underMouse() && isEnabled();
    if (isChecked()) p.fillRect(rect(), theme::color(Tok::AccentSelFill));
    else if (hover) p.fillRect(rect(), theme::color(Tok::SurfaceHover));
    const QColor ink = isChecked() ? theme::color(Tok::AccentText) : hover ? theme::color(Tok::TextPrimary) : theme::color(Tok::TextSecondary);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(ink, 1.6);
    pen.setCapStyle(Qt::SquareCap);
    p.setPen(pen);
    if (glyph_ == Close) {
      p.drawLine(QPointF(7.5, 7.5), QPointF(16.5, 16.5));
      p.drawLine(QPointF(16.5, 7.5), QPointF(7.5, 16.5));
    } else {  // push pin: head, collar, needle
      p.drawRect(QRectF(9.5, 5.5, 5, 6));
      p.drawLine(QPointF(7, 11.5), QPointF(17, 11.5));
      p.drawLine(QPointF(12, 12), QPointF(12, 19));
      if (isChecked()) p.fillRect(QRectF(9.5, 5.5, 5, 6), ink);
    }
    if (hasFocus()) {
      p.setRenderHint(QPainter::Antialiasing, false);
      p.setPen(QPen(theme::color(Tok::AccentFocus), 2));
      p.drawRect(rect().adjusted(1, 1, -1, -1));
    }
  }

 private:
  Glyph glyph_;
};

/// Check mark for Apply, in accent.onSolid (repainted on theme change).
class CheckGlyph : public QWidget {
 public:
  explicit CheckGlyph(QWidget* parent) : QWidget(parent) {
    setFixedSize(16, 16);
    setAttribute(Qt::WA_TransparentForMouseEvents);
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(theme::color(isEnabled() ? Tok::AccentOnSolid : Tok::TextDisabled), 1.8);
    pen.setJoinStyle(Qt::MiterJoin);
    p.setPen(pen);
    QPainterPath path;
    path.moveTo(2.5, 8.5);
    path.lineTo(6.5, 12.5);
    path.lineTo(13.5, 4.5);
    p.drawPath(path);
  }
};

/// Footer button: label left, keycap right (5.4). The text is the accessible name.
QPushButton* footerButton(const QString& text, const QString& keys, bool primary, QWidget* parent) {
  auto* b = new QPushButton(parent);
  b->setAccessibleName(text);
  b->setAutoDefault(false);
  auto* h = new QHBoxLayout(b);
  h->setContentsMargins(12, 0, 8, 0);
  h->setSpacing(8);
  if (primary) h->addWidget(new CheckGlyph(b));
  auto* label = new QLabel(text, b);
  label->setObjectName("CardButtonText");
  label->setAttribute(Qt::WA_TransparentForMouseEvents);
  label->setProperty("primary", primary);  // styled by its own state (QSS has no ancestor states)
  h->addWidget(label);
  h->addStretch(1);
  auto* cap = new Keycap(keys, b);
  cap->setAttribute(Qt::WA_TransparentForMouseEvents);
  cap->setFixedHeight(18);
  cap->setProperty("primary", primary);
  cap->setProperty("footer", true);
  h->addWidget(cap, 0, Qt::AlignVCenter);
  b->setMinimumHeight(32);
  b->setMinimumWidth(h->sizeHint().width());
  return b;
}

/// Dashed drop targets while the card is dragged (6.4): a transparent overlay over the window.
class DropTargets : public QWidget {
 public:
  struct Target {
    QRect global;
    QString caption;
    bool hot = false;
  };
  explicit DropTargets(QWidget* window) : QWidget(window) {
    setObjectName("CardDropTargets");
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setGeometry(window->rect());
    show();
    raise();
  }
  void setTargets(std::vector<Target> t) {
    targets_ = std::move(t);
    update();
  }
  const std::vector<Target>& targets() const { return targets_; }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.setFont(theme::font(theme::TextStyle::Caption));
    for (const Target& t : targets_) {
      if (t.global.isEmpty()) continue;
      const QRect r(mapFromGlobal(t.global.topLeft()), t.global.size());
      if (t.hot) p.fillRect(r, theme::color(Tok::AccentSelFill, 0.55));
      QPen pen(theme::color(Tok::AccentIndicator), t.hot ? 2 : 1.5, t.hot ? Qt::SolidLine : Qt::DashLine);
      p.setPen(pen);
      p.setBrush(Qt::NoBrush);
      p.drawRect(r.adjusted(1, 1, -1, -1));
      p.setPen(theme::color(Tok::AccentText));
      p.drawText(r.adjusted(8, 6, -8, -6), Qt::AlignTop | Qt::AlignLeft, t.caption);
    }
  }

 private:
  std::vector<Target> targets_;
};

/// "0", "0 mm", "0.00 deg": a value field that is not set yet.
bool isZeroValue(const QString& text) {
  bool ok = false;
  const double v = text.section(' ', 0, 0).toDouble(&ok);
  return ok && v == 0.0;
}

/// The form label next to (or above) `field` in any QFormLayout under `root`.
QString formLabelFor(QWidget* root, QWidget* field) {
  for (QFormLayout* form : root->findChildren<QFormLayout*>()) {
    for (int i = 0; i < form->rowCount(); ++i) {
      QLayoutItem* f = form->itemAt(i, QFormLayout::FieldRole);
      QLayoutItem* s = form->itemAt(i, QFormLayout::SpanningRole);
      QWidget* w = f ? f->widget() : s ? s->widget() : nullptr;
      if (!w || (w != field && !w->isAncestorOf(field))) continue;
      if (QLayoutItem* l = form->itemAt(i, QFormLayout::LabelRole))
        if (auto* label = qobject_cast<QLabel*>(l->widget())) return label->text();
      if (auto* label = w->findChild<QLabel*>("SegmentedLabel")) return label->text();
    }
  }
  return field->objectName().mid(5);  // prop_<name>
}
}  // namespace

// =================================================================================================

CommandCard::CommandCard(QWidget* viewport) : QFrame(viewport), viewport_(viewport) {
  setObjectName("CommandDialog");
  setProperty("card", true);
  setFrameShape(QFrame::NoFrame);
  setFocusPolicy(Qt::StrongFocus);
  setFixedWidth(kCardWidth + 2 * kBleed);
  auto* v = new QVBoxLayout(this);
  v->setContentsMargins(kBleed, kBleed, kBleed, kBleed);
  v->setSpacing(0);

  // ---- header: icon, title, keycap, context, pin, close (drag to move)
  title_ = new QWidget(this);
  title_->setObjectName("CommandDialogTitle");
  title_->setAttribute(Qt::WA_StyledBackground);
  title_->setCursor(Qt::SizeAllCursor);
  title_->setFixedHeight(kHeaderH);
  title_->installEventFilter(this);
  auto* th = new QHBoxLayout(title_);
  th->setContentsMargins(kPad, 0, 8, 0);
  th->setSpacing(8);
  titleIcon_ = new QLabel(title_);
  titleIcon_->setFixedSize(18, 18);
  titleText_ = new QLabel(title_);
  titleText_->setObjectName("CardTitle");
  keycap_ = new Keycap(QString(), title_);
  keycap_->setFixedHeight(18);
  keycap_->hide();
  context_ = new QLabel(title_);
  context_->setObjectName("CardContext");
  context_->setProperty("textStyle", "label.sm");
  docName_ = new QLabel(title_);
  docName_->setObjectName("CardDocName");
  // user-entered names keep their case (decision 3): the font is set here, not by the caps rules
  auto docFont = [this] {
    QFont f = theme::font(theme::TextStyle::LabelSm);
    f.setCapitalization(QFont::MixedCase);
    docName_->setFont(f);
  };
  docFont();
  connect(&ThemeManager::instance(), &ThemeManager::themeChanged, docName_, docFont);
  th->addWidget(titleIcon_);
  th->addWidget(titleText_);
  th->addWidget(keycap_, 0, Qt::AlignVCenter);
  th->addSpacing(2);
  th->addWidget(context_);
  th->addWidget(docName_, 1);
  pin_ = new CardGlyphButton(CardGlyphButton::Pin, title_);
  pin_->setObjectName("CardPin");
  pin_->setCheckable(true);
  pin_->setToolTip("Pin: keep the card open and repeat the command after Apply");
  close_ = new CardGlyphButton(CardGlyphButton::Close, title_);
  close_->setObjectName("CardClose");
  close_->setToolTip("Close (Esc)");
  th->addWidget(pin_);
  th->addWidget(close_);
  v->addWidget(title_);
  connect(pin_, &QToolButton::toggled, this, &CommandCard::pinnedChanged);
  connect(close_, &QToolButton::clicked, this, &CommandCard::cancelled);

  // ---- step indicator
  steps_ = new StepIndicator(this);
  steps_->setContentsMargins(0, 0, 0, 0);
  auto* stepsRow = new QWidget(this);
  stepsRow->setObjectName("CardSteps");
  stepsRow->setFixedHeight(kStepsH);
  auto* sh = new QHBoxLayout(stepsRow);
  sh->setContentsMargins(kPad, 0, kPad, 8);
  sh->addWidget(steps_);
  stepsRow->hide();
  v->addWidget(stepsRow);
  connect(steps_, &StepIndicator::stepClicked, this, &CommandCard::focusStep);

  // ---- body
  scroll_ = new QScrollArea(this);
  scroll_->setObjectName("CommandDialogScroll");
  scroll_->setFrameShape(QFrame::NoFrame);
  scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll_->setWidgetResizable(true);
  scroll_->setFocusPolicy(Qt::NoFocus);
  inner_ = new QWidget(scroll_);
  inner_->setObjectName("CardBody");
  bodyLayout_ = new QVBoxLayout(inner_);
  bodyLayout_->setContentsMargins(kPad - 6, 4, kPad - 6, 4);  // the PropertyPanel adds 6 px itself
  scroll_->setWidget(inner_);
  inner_->setAutoFillBackground(false);  // the card's panel shows through
  scroll_->viewport()->setAutoFillBackground(false);
  inner_->installEventFilter(this);
  scroll_->hide();
  v->addWidget(scroll_);

  // The message line stays in the layout (empty when there is nothing to say) so that the
  // first prompt does not make the card jump.
  message_ = new QLabel(this);
  message_->setObjectName("CommandDialogMessage");
  message_->setWordWrap(true);
  message_->setContentsMargins(kPad, 2, kPad, 2);
  message_->setMinimumHeight(message_->fontMetrics().lineSpacing());
  v->addWidget(message_);
  v->addStretch(1);  // extra (stable) height goes above the footer

  // ---- footer: Cancel (ESC) and Apply (ENTER); OkButton kept (8.2)
  buttonRow_ = new QWidget(this);
  buttonRow_->setObjectName("CardFooter");
  buttonRow_->setAttribute(Qt::WA_StyledBackground);
  buttonRow_->setFixedHeight(kFooterH);
  auto* bh = new QHBoxLayout(buttonRow_);
  bh->setContentsMargins(kPad, 10, kPad, 10);
  bh->setSpacing(8);
  cancel_ = footerButton("Cancel", "Esc", false, buttonRow_);
  cancel_->setObjectName("CancelButton");
  ok_ = footerButton("Apply", "Return", true, buttonRow_);
  ok_->setObjectName("OkButton");
  bh->addWidget(cancel_, 1);
  bh->addWidget(ok_, 2);
  v->addWidget(buttonRow_);
  connect(ok_, &QPushButton::clicked, this, [this] {
    if (acceptEnabled_) emit accepted();
  });
  connect(cancel_, &QPushButton::clicked, this, &CommandCard::cancelled);
  if (viewport_) viewport_->installEventFilter(this);
  connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this] { update(); });
  hide();
}

CommandCard::~CommandCard() {
  if (pressed_) qApp->removeEventFilter(this);
  if (dropOverlay_) delete dropOverlay_.data();
  if (slotHost_ && slotHost_->dockedCard() == this) slotHost_->dockCard(nullptr);
}

QRect CommandCard::cardRect() const { return rect().adjusted(kBleed, kBleed, -kBleed, -kBleed); }

QRect CommandCard::viewportRect() const { return viewport_ ? viewport_->rect() : QRect(); }

// ---- presenting ----------------------------------------------------------------------------------

void CommandCard::present(const QString& title, const QIcon& icon, QWidget* body, bool buttons) {
  // G3 PART-04/07: a field of the previous command's body must not keep (or hand on) the
  // keyboard focus: digits typed next would land in a field of the new card. The viewport
  // takes it, so typing goes to the new command's on-canvas value box. Focus outside the card
  // (the command line) is never taken.
  if (viewport_) {
    QWidget* f = QApplication::focusWidget();
    if (f && (f == this || isAncestorOf(f))) viewport_->setFocus(Qt::OtherFocusReason);
  }
  clearBody();  // the next body takes over
  stableHeight_ = 0;
  titleText_->setText(title.toUpper());
  titleIcon_->setPixmap(icon.isNull() ? QPixmap() : icon.pixmap(18, 18));
  if (body) adoptBody(body);
  scroll_->setVisible(body != nullptr);
  scroll_->verticalScrollBar()->setValue(0);
  buttonRow_->setVisible(buttons);
  setAcceptEnabled(true);
  setMessage({});
  updateSteps();
  applyDocking();
  show();
  raise();
  relayout();
}

void CommandCard::adoptBody(QWidget* body) {
  // long hints/prompts inside the body wrap instead of being clipped by the fixed width
  for (QLabel* l : body->findChildren<QLabel*>())
    if (!l->wordWrap() && l->pixmap().isNull() && l->text().size() > kWrapChars) l->setWordWrap(true);
  body->setParent(inner_);
  bodyLayout_->addWidget(body);
  body_ = body;
  body->installEventFilter(this);
  upgradeForms();
  body->show();
}

void CommandCard::upgradeForms() {
  if (!body_) return;
  // the active selection input's bar (paintEvent) follows the buttons
  for (QPushButton* b : body_->findChildren<QPushButton*>())
    if (b->property("selectionInput").toBool() && !b->property("cardHooked").toBool()) {
      b->setProperty("cardHooked", true);
      connect(b, &QPushButton::toggled, this, [this] { update(); });
    }
  for (QFormLayout* form : body_->findChildren<QFormLayout*>()) {
    for (int i = 0; i < form->rowCount(); ++i) {
      QLayoutItem* labelItem = form->itemAt(i, QFormLayout::LabelRole);
      QLayoutItem* fieldItem = form->itemAt(i, QFormLayout::FieldRole);
      auto* label = labelItem ? qobject_cast<QLabel*>(labelItem->widget()) : nullptr;
      if (label && !label->property("cardLabel").toBool()) {
        // 96 px label column (3.4); longer labels wrap to two lines
        label->setProperty("cardLabel", true);
        label->setWordWrap(true);
        label->setFixedWidth(kLabelColumn);
      }
      auto* combo = fieldItem ? qobject_cast<QComboBox*>(fieldItem->widget()) : nullptr;
      if (!combo || !combo->objectName().startsWith("prop_") || combo->count() < 2 ||
          combo->count() > SegmentedControl::kMaxSegments || combo->isEditable() ||
          kComboEnums.contains(combo->objectName()))
        continue;
      // enum with <= 4 values: a segmented row with the label above it at full card width (5.4).
      // The combo stays (hidden) inside the row as the model: the tool and its tests drive it,
      // and a rebuild of the panel retires the row together with both widgets.
      QFormLayout::TakeRowResult row = form->takeRow(i);
      auto* holder = new QWidget(combo->parentWidget());
      holder->setObjectName("SegmentedRow");
      auto* hv = new QVBoxLayout(holder);
      hv->setContentsMargins(0, 4, 0, 2);
      hv->setSpacing(4);
      auto* above = new QLabel(label ? label->text() : QString(), holder);
      above->setObjectName("SegmentedLabel");
      above->setProperty("textStyle", "caption");
      hv->addWidget(above);
      auto* seg = new SegmentedControl(holder);
      seg->setObjectName(combo->objectName());  // prop_<name> (8.2)
      seg->bindCombo(combo);
      hv->addWidget(seg);
      // a choice that rebuilds the panel (Two Sides adds rows) must not drop the keyboard focus
      connect(seg, &SegmentedControl::aboutToChange, this, [this, seg] {
        if (seg->hasFocus()) refocusSegment_ = seg->objectName();
      });
      if (!refocusSegment_.isEmpty() && seg->objectName() == refocusSegment_) {
        refocusSegment_.clear();
        QTimer::singleShot(0, seg, [seg] { seg->setFocus(Qt::OtherFocusReason); });
      }
      combo->setParent(holder);
      combo->hide();
      combo->setProperty("segmented", true);
      if (label) {
        label->hide();
        label->setParent(holder);  // retired with the row
      }
      delete row.labelItem;
      delete row.fieldItem;
      form->insertRow(i, holder);
    }
  }
}

void CommandCard::dismiss() {
  // Give the keyboard back to the viewport when the focus was inside the card, so the next
  // Esc reaches the canvas (and the tool) instead of whatever widget is next in the chain
  // (SK-08). Focus outside the card (the command line while typing) is never taken.
  if (isVisible() && viewport_) {
    QWidget* f = QApplication::focusWidget();
    if (f && (f == this || isAncestorOf(f))) viewport_->setFocus(Qt::OtherFocusReason);
  }
  clearBody();
  hide();
  if (isDocked()) {
    slotHost_->dockCard(nullptr);
    setParent(viewport_);
    hide();
  }
  layout_ = OverlayLayout();
  publishLayout();
}

void CommandCard::clearBody() {
  if (body_) {
    QWidget* b = body_;
    body_.clear();
    bodyLayout_->removeWidget(b);
    b->removeEventFilter(this);
    b->hide();
    b->setParent(nullptr);  // out of the scroll area right away (deleted below)
    b->deleteLater();       // tools may still hold QPointers; never delete synchronously
  }
  stepTargets_.clear();
  refocusSegment_.clear();
  scroll_->hide();
}

void CommandCard::setAcceptEnabled(bool enabled) {
  acceptEnabled_ = enabled;
  ok_->setEnabled(enabled);
}

void CommandCard::setMessage(const QString& text, bool error) {
  message_->setText(text);
  message_->setVisible(!text.isEmpty());
  message_->setProperty("error", error);
  message_->style()->unpolish(message_);
  message_->style()->polish(message_);
  if (isVisible()) relayout();
}

void CommandCard::setShortcut(const QString& keys) {
  static_cast<Keycap*>(keycap_)->setKeys(keys);
  keycap_->setVisible(!keys.isEmpty());
}

void CommandCard::setContext(const QString& workspace, const QString& documentName) {
  context_->setText(workspace.isEmpty() ? QString() : workspace + QStringLiteral(" ·"));
  context_->setVisible(!workspace.isEmpty());
  docName_->setText(documentName);
  docName_->setVisible(!documentName.isEmpty());
}

QString CommandCard::contextText() const {
  QString s = context_->text();
  if (!docName_->text().isEmpty()) s += (s.isEmpty() ? "" : " ") + docName_->text();
  return s;
}

void CommandCard::setLive(bool live) {
  if (live_ == live) return;
  live_ = live;
  update();
}

bool CommandCard::isPinned() const { return pin_->isChecked(); }
void CommandCard::setPinned(bool pinned) { pin_->setChecked(pinned); }

// ---- steps (5.4) -------------------------------------------------------------------------------

void CommandCard::updateSteps() {
  std::vector<StepIndicator::Step> steps;
  stepTargets_.clear();
  if (body_) {
    // derived steps (plan section 9 fallback): the selection inputs above the first value, that
    // value, then CONFIRM. Selection inputs below the value (Extrude's "merge with" bodies) are
    // optional refinements, not steps.
    int current = -1;
    QLineEdit* value = nullptr;
    for (QWidget* w : body_->findChildren<QWidget*>()) {  // creation order = row order
      if (!w->isVisibleTo(body_)) continue;
      if (auto* e = qobject_cast<QLineEdit*>(w); e && e->objectName().startsWith("prop_")) {
        // the main value: the first field that is set (Extrude: DISTANCE, not a zero START OFFSET)
        if (!value) value = e;
        if (!isZeroValue(e->text())) {
          value = e;
          break;
        }
        continue;
      }
      if (value) continue;  // only selection inputs above the first value are steps
      auto* b = qobject_cast<QPushButton*>(w);
      if (!b || !b->property("selectionInput").toBool()) continue;
      const bool done = b->text().compare("Select", Qt::CaseInsensitive) != 0 && !b->text().isEmpty();
      if (!done && current < 0) current = static_cast<int>(steps.size());
      steps.push_back({formLabelFor(body_, b), done ? StepIndicator::State::Done : StepIndicator::State::Todo});
      stepTargets_.push_back(b);
    }
    if (value) {
      if (current < 0) current = static_cast<int>(steps.size());
      steps.push_back({formLabelFor(body_, value), StepIndicator::State::Todo});
      stepTargets_.push_back(value);
    }
    if (!steps.empty()) {
      if (current < 0) current = static_cast<int>(steps.size());
      steps.push_back({"Confirm", StepIndicator::State::Todo});
      stepTargets_.push_back(ok_);
      steps[current].state = StepIndicator::State::Current;
    }
  }
  if (steps.size() < 2) steps.clear();
  steps_->setSteps(steps);
  steps_->parentWidget()->setVisible(!steps.empty());
}

void CommandCard::focusStep(int index) {
  if (index < 0 || index >= static_cast<int>(stepTargets_.size()) || !stepTargets_[index]) return;
  QWidget* w = stepTargets_[index];
  if (auto* b = qobject_cast<QPushButton*>(w); b && b->property("selectionInput").toBool()) {
    b->click();  // makes it the active selection input
    return;
  }
  w->setFocus(Qt::MouseFocusReason);
  if (auto* e = qobject_cast<QLineEdit*>(w)) e->selectAll();
}

void CommandCard::syncControls() {
  if (!body_) return;
  upgradeForms();
  for (SegmentedControl* s : body_->findChildren<SegmentedControl*>()) s->sync();
  const auto before = steps_->steps();
  updateSteps();
  if (before.empty() != steps_->steps().empty() && isVisible()) relayout();
  update();  // the active-input bar
}

// ---- size and placement ----------------------------------------------------------------------

int CommandCard::chromeHeight() const {
  int h = 2 * kBleed + kHeaderH;
  if (steps_->parentWidget()->isVisibleTo(this)) h += kStepsH;
  const int w = width() - 2 * kBleed;
  if (message_->isVisibleTo(this)) h += std::max(message_->minimumHeight(), message_->heightForWidth(w));
  if (buttonRow_->isVisibleTo(this)) h += kFooterH;
  return h;
}

void CommandCard::buildLayout(OverlayLayout& l, const QSize& size, Placement p) const {
  l.clear();
  l.setViewport(viewportRect());
  OverlayLayout::Options o;
  o.card = p;
  o.free = free_;
  o.cardBleed = kBleed;
  l.setOptions(o);
  // the viewer's own overlays (orientation widget, breadcrumb + filter row, readout, scale bar)
  if (auto* v = qobject_cast<const viewer::ViewerWidget*>(viewport_)) v->addOverlayRegions(l);
  if (regions_) regions_(l);
  if (p != Placement::Sidebar && !size.isEmpty()) l.add({OverlayLayout::Slot::Card, size});
  l.compute();
}

void CommandCard::relayout() {
  relayoutPending_ = false;
  if (!viewport_) return;
  const bool docked = isDocked();
  const int w = docked ? std::max(200, slotHost_->cardSlotWidth()) : kCardWidth + 2 * kBleed;
  if (!docked) setFixedWidth(w);
  const int innerW = w - 2 * kBleed;
  int contentH = 0;
  if (body_) {
    bodyLayout_->activate();
    contentH = inner_->hasHeightForWidth() ? inner_->heightForWidth(innerW) : inner_->sizeHint().height();
    contentH = std::max(contentH, inner_->minimumSizeHint().height());
  }
  const int chrome = chromeHeight();
  const Placement p = effectivePlacement();
  int maxTotal = 0;
  if (docked) {
    maxTotal = slotHost_->cardSlotMaxHeight();
  } else {
    OverlayLayout probe;
    buildLayout(probe, QSize(w, chrome + contentH), p);
    maxTotal = probe.cardMaxHeight() > 0 ? probe.cardMaxHeight() : viewport_->height();
  }
  const int cap = std::max(60, std::min(kMaxBodyHeight, maxTotal - chrome));
  scroll_->setFixedHeight(body_ ? std::min(contentH, cap) : 0);
  if (QLayout* l = layout()) l->activate();
  const int natural = chrome + (body_ ? std::min(contentH, cap) : 0);
  // grow freely; never shrink while the same command runs (unless the room got smaller)
  const int h = std::max(natural, std::min(stableHeight_, std::max(natural, maxTotal)));
  stableHeight_ = h;
  setFixedHeight(h);
  if (!docked && !dragging_) {
    buildLayout(layout_, QSize(w, h), p);
    const QRect r = layout_.rect(OverlayLayout::Slot::Card);
    if (r.isValid()) move(r.topLeft());
    publishLayout();
  }
  emit placed();
}

void CommandCard::publishLayout() {
  if (viewport_) viewer::OverlayHost::of(viewport_)->publish(layout_);
}

void CommandCard::reposition() {
  if (!viewport_ || !isPresenting()) return;
  applyDocking();
  if (!isVisible() && body_) show();
  relayout();
}

CommandCard::Placement CommandCard::effectivePlacement() const {
  switch (requested_) {
    case Placement::Sidebar:
      if (slotHost_ && slotAllowed_ && slotHost_->cardSlotAvailable()) return Placement::Sidebar;
      return sidebarLeft_ ? Placement::FloatLeft : Placement::FloatRight;  // 6.4 fallback
    case Placement::Free: return free_.valid ? Placement::Free : Placement::FloatRight;
    default: return requested_;
  }
}

bool CommandCard::isDocked() const { return slotHost_ && slotHost_->dockedCard() == this; }

void CommandCard::applyDocking() {
  const bool dock = effectivePlacement() == Placement::Sidebar && slotHost_;
  if (dock) {
    if (!isDocked()) slotHost_->dockCard(this);  // reparents into the slot
    setMinimumWidth(0);
    setMaximumWidth(QWIDGETSIZE_MAX);
  } else {
    if (isDocked()) slotHost_->dockCard(nullptr);
    if (parentWidget() != viewport_) setParent(viewport_);  // hidden until shown again
    setFixedWidth(kCardWidth + 2 * kBleed);
  }
}

void CommandCard::setPlacement(Placement requested, const FreePos& free) {
  requested_ = requested;
  if (free.valid) free_ = free;
  if (body_ || isVisible()) {
    applyDocking();
    show();
    raise();
    relayout();
  }
}

void CommandCard::setSlotHost(CardSlotHost* host) {
  if (slotHost_ == host) return;
  if (isDocked()) {
    slotHost_->dockCard(nullptr);
    setParent(viewport_);
  }
  slotHost_ = host;
  reposition();
}

void CommandCard::setSlotAllowed(bool allowed) {
  if (slotAllowed_ == allowed) return;
  slotAllowed_ = allowed;
  if (!allowed && isDocked()) {
    const bool vis = isVisible() || body_;
    slotHost_->dockCard(nullptr);
    setParent(viewport_);
    if (!vis) return;
  }
  reposition();
}

void CommandCard::setSidebarOnLeft(bool left) {
  if (sidebarLeft_ == left) return;
  sidebarLeft_ = left;
  reposition();
}

// ---- drag to move (6.4) ------------------------------------------------------------------------

QRect CommandCard::dropTargetGlobal(Placement p) const {
  if (!viewport_) return {};
  if (p == Placement::Sidebar) {
    if (!slotHost_ || !slotAllowed_ || !slotHost_->cardSlotAvailable()) return {};
    return slotHost_->cardSlotGlobalRect();
  }
  if (p != Placement::FloatLeft && p != Placement::FloatRight) return {};
  OverlayLayout l;
  const QSize size(kCardWidth + 2 * kBleed, isDocked() ? std::min(height(), viewport_->height()) : height());
  buildLayout(l, {}, p);
  const QRect r = l.cardRectFor(p, size).adjusted(kBleed, kBleed, -kBleed, -kBleed);
  return QRect(viewport_->mapToGlobal(r.topLeft()), r.size());
}

CommandCard::Placement CommandCard::dropTargetAt(const QPoint& global, bool* hit) const {
  *hit = false;
  if (const QRect s = dropTargetGlobal(Placement::Sidebar); !s.isEmpty() && s.contains(global)) {
    *hit = true;
    return Placement::Sidebar;
  }
  // a float slot catches the card when its top-left lands near the slot's top-left
  const QPoint cardTopLeft = mapToGlobal(cardRect().topLeft());
  for (Placement p : {Placement::FloatRight, Placement::FloatLeft}) {
    const QRect r = dropTargetGlobal(p);
    if (!r.isEmpty() && (r.topLeft() - cardTopLeft).manhattanLength() <= kSnap) {
      *hit = true;
      return p;
    }
  }
  return Placement::Free;
}

void CommandCard::beginDrag() {
  dragging_ = true;
  if (isDocked()) {
    // undock under the cursor: the header stays in hand
    slotHost_->dockCard(nullptr);
    setParent(viewport_);
    setFixedWidth(kCardWidth + 2 * kBleed);
    grabOffset_.setX(std::clamp(grabOffset_.x(), kBleed + 8, width() - 60));
    show();
    raise();
  }
  if (QWidget* win = viewport_ ? viewport_->window() : nullptr) dropOverlay_ = new DropTargets(win);
  raise();
}

void CommandCard::dragTo(const QPoint& global) {
  const QRect vp = viewportRect();
  QPoint tl = viewport_->mapFromGlobal(global - grabOffset_);
  tl.setX(std::clamp(tl.x(), vp.left(), std::max(vp.left(), vp.right() - width() + 1)));
  tl.setY(std::clamp(tl.y(), vp.top(), std::max(vp.top(), vp.bottom() - std::min(height(), vp.height()) + 1)));
  move(tl);
  if (auto* o = static_cast<DropTargets*>(dropOverlay_.data())) {
    bool hit = false;
    const Placement hot = dropTargetAt(global, &hit);
    std::vector<DropTargets::Target> t;
    t.push_back({dropTargetGlobal(Placement::Sidebar), "SIDEBAR", hit && hot == Placement::Sidebar});
    t.push_back({dropTargetGlobal(Placement::FloatLeft), "FLOAT LEFT", hit && hot == Placement::FloatLeft});
    t.push_back({dropTargetGlobal(Placement::FloatRight), "FLOAT RIGHT", hit && hot == Placement::FloatRight});
    o->setTargets(std::move(t));
    if (slotHost_) slotHost_->setDropHighlight(hit && hot == Placement::Sidebar);
    o->raise();
    raise();
  }
}

void CommandCard::endDrag(const QPoint& global) {
  bool hit = false;
  const Placement target = dropTargetAt(global, &hit);
  dragging_ = false;
  if (slotHost_) slotHost_->setDropHighlight(false);
  if (dropOverlay_) {
    dropOverlay_->hide();  // repaints what it covered (the sidebar, the viewport)
    dropOverlay_->deleteLater();
    dropOverlay_ = nullptr;
  }
  if (hit) {
    setPlacement(target);
    emit placementChosen(target, free_);
    return;
  }
  const Placement base = requested_ == Placement::Free ? free_.base : requested_;
  const FreePos f = FreePos::fromRect(geometry(), viewportRect(), base);
  setPlacement(Placement::Free, f);
  emit placementChosen(Placement::Free, f);
}

// ---- events ----------------------------------------------------------------------------------

void CommandCard::keyPressEvent(QKeyEvent* e) {
  if (e->key() == Qt::Key_Escape) {
    emit cancelled();
    return;
  }
  if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
    QWidget* f = QApplication::focusWidget();
    if (qobject_cast<QTextEdit*>(f) || qobject_cast<QPlainTextEdit*>(f)) return;
    if (acceptEnabled_) emit accepted();
    return;
  }
  QFrame::keyPressEvent(e);
}

void CommandCard::paintEvent(QPaintEvent*) {
  QPainter p(this);
  CardFrame::Style st;
  st.live = live_;
  st.shadow = !isDocked();
  CardFrame::paint(p, cardRect(), st);
  // the active selection input gets a 2 px accent bar at the card's left edge (5.4)
  if (body_) {
    for (QPushButton* b : body_->findChildren<QPushButton*>()) {
      if (!b->property("selectionInput").toBool() || !b->isChecked() || !b->isVisibleTo(this)) continue;
      const QRect r(b->mapTo(this, QPoint(0, 0)), b->size());
      const QRect clip = scroll_->geometry();
      const QRect bar = QRect(cardRect().left(), r.top() - 2, 2, r.height() + 4).intersected(clip);
      if (!bar.isEmpty()) p.fillRect(bar, theme::color(Tok::AccentIndicator));
    }
  }
  // header / footer separators (border.subtle)
  p.setPen(QPen(theme::color(Tok::BorderSubtle), 1));
  const QRect c = cardRect();
  if (buttonRow_->isVisible()) {
    const int y = buttonRow_->geometry().top();
    p.drawLine(c.left() + 1, y, c.right() - 1, y);
  }
}

bool CommandCard::eventFilter(QObject* o, QEvent* e) {
  // drag in progress: follow the mouse wherever it is (the card may have been undocked)
  if (pressed_ && o != this && (e->type() == QEvent::MouseMove || e->type() == QEvent::MouseButtonRelease)) {
    auto* me = static_cast<QMouseEvent*>(e);
    const QPoint g = me->globalPosition().toPoint();
    if (e->type() == QEvent::MouseMove) {
      if (!dragging_ && (g - pressGlobal_).manhattanLength() >= QApplication::startDragDistance()) beginDrag();
      if (dragging_) dragTo(g);
      return true;
    }
    if (me->button() == Qt::LeftButton) {
      pressed_ = false;
      qApp->removeEventFilter(this);
      if (dragging_) endDrag(g);
      return true;
    }
  }
  if (o == viewport_ && e->type() == QEvent::Resize) {
    if (isVisible() || body_) reposition();
    return false;
  }
  if ((o == inner_ || o == body_) && e->type() == QEvent::LayoutRequest && body_ && !relayoutPending_) {
    // the tool added/removed rows: refit once the layout has settled
    relayoutPending_ = true;
    QTimer::singleShot(0, this, [this] {
      if (!relayoutPending_) return;
      upgradeForms();
      updateSteps();
      relayout();
    });
    return false;
  }
  if (o == title_) {
    if (e->type() == QEvent::MouseButtonPress) {
      auto* me = static_cast<QMouseEvent*>(e);
      if (me->button() != Qt::LeftButton) return true;
      pressed_ = true;
      pressGlobal_ = me->globalPosition().toPoint();
      grabOffset_ = pressGlobal_ - mapToGlobal(QPoint(0, 0));
      qApp->installEventFilter(this);
      return true;
    }
    if (e->type() == QEvent::MouseButtonDblClick) {
      // back to the chosen placement (a free card returns to the placement it was dragged from)
      const Placement base = requested_ == Placement::Free ? free_.base : requested_;
      setPlacement(base);
      emit placementChosen(base, free_);
      return true;
    }
  }
  return QFrame::eventFilter(o, e);
}

}  // namespace fabricad::app
