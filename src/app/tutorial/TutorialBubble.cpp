// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/tutorial/TutorialBubble.h"

#include "app/ThemeManager.h"
#include "app/TitleBar.h"
#include "theme/Tokens.h"

#include <QAccessible>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QToolButton>
#include <QVBoxLayout>

namespace fabricad::app::tutorial {

/// Thin progress bar: accent.indicator on border.subtle.
class ProgressStrip : public QWidget {
 public:
  explicit ProgressStrip(QWidget* parent) : QWidget(parent) {
    setFixedHeight(3);
    setAttribute(Qt::WA_TransparentForMouseEvents);
  }
  void setValue(int step, int total) {
    step_ = step;
    total_ = total;
    update();
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.fillRect(rect(), theme::color(theme::Tok::BorderSubtle));
    if (total_ > 0) {
      QRect r = rect();
      r.setWidth(static_cast<int>(r.width() * double(step_) / total_));
      p.fillRect(r, theme::color(theme::Tok::AccentIndicator));
    }
  }

 private:
  int step_ = 0, total_ = 0;
};

namespace {

QPushButton* button(const QString& text, const QString& name, QWidget* parent) {
  auto* b = new QPushButton(text, parent);
  b->setObjectName(name);
  b->setFocusPolicy(Qt::NoFocus);  // the bubble never takes the keyboard
  b->setAutoDefault(false);
  return b;
}

}  // namespace

TutorialBubble::TutorialBubble(QWidget* window) : QFrame(window), window_(window) {
  setObjectName("TutorialBubble");
  setFocusPolicy(Qt::NoFocus);
  setAttribute(Qt::WA_ShowWithoutActivating);
  setAccessibleName("Tutorial");
  setFixedWidth(kWidth);
  auto* v = new QVBoxLayout(this);
  v->setContentsMargins(14, 12, 14, 12);
  v->setSpacing(8);
  auto* head = new QHBoxLayout;
  head->setSpacing(8);
  chapter_ = new QLabel(this);
  chapter_->setObjectName("TutorialChapter");
  chapter_->setProperty("textStyle", "title");
  counter_ = new QLabel(this);
  counter_->setObjectName("TutorialCounter");
  counter_->setProperty("textStyle", "caption");
  head->addWidget(chapter_, 1);
  head->addWidget(counter_, 0, Qt::AlignRight | Qt::AlignVCenter);
  v->addLayout(head);
  progress_ = new ProgressStrip(this);
  v->addWidget(progress_);
  body_ = new QLabel(this);
  body_->setObjectName("TutorialText");
  body_->setWordWrap(true);
  body_->setTextFormat(Qt::RichText);
  body_->setTextInteractionFlags(Qt::NoTextInteraction);
  body_->setMinimumWidth(kWidth - 28);
  v->addWidget(body_);
  keysRow_ = new QWidget(this);
  keysLayout_ = new QHBoxLayout(keysRow_);
  keysLayout_->setContentsMargins(0, 0, 0, 0);
  keysLayout_->setSpacing(4);
  v->addWidget(keysRow_);
  hint_ = new QLabel(this);
  hint_->setObjectName("TutorialHint");
  hint_->setWordWrap(true);
  hint_->setTextFormat(Qt::RichText);
  hint_->hide();
  v->addWidget(hint_);
  auto* buttons = new QHBoxLayout;
  buttons->setSpacing(6);
  back_ = button("Back", "TutorialBack", this);
  back_->setToolTip("Previous step (Alt+Left)");
  showMe_ = button("Show me", "TutorialShowMe", this);
  showMe_->setToolTip("Show the input with a ghost cursor (F7). Nothing is changed.");
  next_ = button("Skip", "TutorialNext", this);
  next_->setToolTip("Next step (Alt+Right)");
  more_ = new QToolButton(this);
  more_->setObjectName("TutorialMore");
  more_->setText(QString::fromUtf8("⋯"));
  more_->setToolTip("More");
  more_->setFocusPolicy(Qt::NoFocus);
  more_->setPopupMode(QToolButton::InstantPopup);
  auto* menu = new QMenu(more_);
  menu->setObjectName("TutorialMoreMenu");
  restore_ = menu->addAction("Restore chapter (new tab)", this, &TutorialBubble::restoreClicked);
  menu->addAction("Undo to step start", this, &TutorialBubble::undoToStartClicked);
  menu->addSeparator();
  menu->addAction("Exit tour", this, &TutorialBubble::exitClicked);
  more_->setMenu(menu);
  buttons->addWidget(back_);
  buttons->addWidget(showMe_);
  buttons->addStretch(1);
  buttons->addWidget(next_);
  buttons->addWidget(more_);
  v->addLayout(buttons);
  connect(back_, &QPushButton::clicked, this, &TutorialBubble::backClicked);
  connect(showMe_, &QPushButton::clicked, this, &TutorialBubble::showMeClicked);
  connect(next_, &QPushButton::clicked, this, &TutorialBubble::nextClicked);
  connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this] { applyTheme(); });
  applyTheme();
}

void TutorialBubble::applyTheme() {
  // body text as typed (decision: mixed case); titles and buttons keep the caps rule
  QFont f = theme::font(theme::TextStyle::Body);
  f.setCapitalization(QFont::MixedCase);
  f.setWeight(QFont::Medium);
  body_->setFont(f);
  hint_->setFont(f);
  for (QWidget* w : {static_cast<QWidget*>(chapter_), static_cast<QWidget*>(counter_)}) ThemeManager::applyTextStyle(w);
  if (!markup_.isEmpty()) body_->setText(richText(markup_));
  if (!hint_->text().isEmpty()) setHint(hint());
  update();
}

QString TutorialBubble::richText(const QString& markup) {
  QString s = markup.toHtmlEscaped();
  static const QRegularExpression bold(R"(\*\*(.+?)\*\*)");
  s.replace(bold, "<b>\\1</b>");
  static const QRegularExpression key(R"(\[\[(.+?)\]\])");
  // keycap look of QLabel#Keycap (surface.header, border.default, text.secondary), from tokens
  const QString style = QString("background-color:%1; color:%2; border:1px solid %3; font-weight:700;")
                            .arg(theme::color(theme::Tok::SurfaceHeader).name(), theme::color(theme::Tok::TextPrimary).name(),
                                 theme::color(theme::Tok::BorderDefault).name());
  s.replace(key, QString("<span style=\"%1\">&nbsp;\\1&nbsp;</span>").arg(style));
  s.replace('\n', "<br>");
  return s;
}

QString TutorialBubble::plain(const QString& markup) {
  QString s = markup;
  s.remove(QStringLiteral("**"));
  s.replace(QStringLiteral("[["), QString());
  s.replace(QStringLiteral("]]"), QString());
  return s;
}

void TutorialBubble::setContent(const Content& c) {
  chapter_->setText(c.chapter);
  counter_->setText(c.total > 0 ? QString("Step %1 / %2").arg(c.step).arg(c.total) : QString());
  progress_->setValue(c.step, c.total);
  markup_ = c.text;
  plain_ = plain(c.text);
  body_->setText(richText(c.text));
  while (QLayoutItem* it = keysLayout_->takeAt(0)) {
    delete it->widget();
    delete it;
  }
  for (const QString& k : c.keys) keysLayout_->addWidget(new Keycap(k, keysRow_));
  keysLayout_->addStretch(1);
  keysRow_->setVisible(!c.keys.isEmpty());
  back_->setEnabled(c.canBack);
  showMe_->setVisible(c.canShowMe);
  next_->setText(c.last ? "Finish" : c.nextIsManual ? "Next" : "Skip");
  next_->setDefault(c.nextIsManual || c.last);  // primary look (QPushButton:default)
  restore_->setEnabled(c.canRestore);
  setHint({});
  adjustSize();
  setAccessibleDescription(plain_);
  QAccessibleEvent ev(this, QAccessible::DescriptionChanged);
  QAccessible::updateAccessibility(&ev);
}

void TutorialBubble::setHint(const QString& text) {
  if (text.isEmpty()) {
    hint_->clear();
    hint_->hide();
  } else {
    hint_->setText(QString("<span style=\"color:%1\">%2</span>").arg(theme::color(theme::Tok::StatusWarning).name(), richText(text)));
    hint_->setProperty("plainHint", plain(text));
    hint_->show();
  }
  adjustSize();
}

QString TutorialBubble::hint() const { return hint_->isVisible() ? hint_->property("plainHint").toString() : QString(); }

void TutorialBubble::setFloating(bool floating) {
  if (floating_ == floating || !window_) return;
  floating_ = floating;
  const bool shown = isVisible();
  const QPoint g = mapToGlobal(QPoint(0, 0));
  if (floating) {
    // opaque, rectangular, frameless tool window: never translucent (no compositor -> black)
    setParent(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
    move(g);
  } else {
    setParent(window_, Qt::Widget);
    move(window_->mapFromGlobal(g));
  }
  setAttribute(Qt::WA_ShowWithoutActivating);
  if (shown) show();
}

void TutorialBubble::paintEvent(QPaintEvent* e) {
  QPainter p(this);
  p.fillRect(rect(), theme::color(theme::Tok::SurfaceOverlay));
  p.setPen(QPen(theme::color(theme::Tok::BorderDefault), 1));
  p.setBrush(Qt::NoBrush);
  p.drawRect(rect().adjusted(0, 0, -1, -1));
  // accent stripe on the left edge (the coach mark's identity)
  p.fillRect(QRect(0, 0, 3, height()), theme::color(theme::Tok::AccentIndicator));
  QFrame::paintEvent(e);
}

}  // namespace fabricad::app::tutorial
