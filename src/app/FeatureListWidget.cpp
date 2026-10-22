// FabriCAD app: FEATURES panel body (feature list).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/FeatureListWidget.h"

#include "app/AppActions.h"
#include "app/Icons.h"
#include "app/InlineTip.h"
#include "app/ThemeManager.h"
#include "app/TimelineActions.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/GroupCommands.h"
#include "core/Json.h"
#include "core/Selection.h"
#include "core/UndoStack.h"
#include "theme/Tokens.h"

#include <QContextMenuEvent>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QTimer>
#include <QToolTip>

#include <algorithm>

namespace fabricad::app {

using core::ObjectId;
using core::ShapeRef;
using theme::Tok;
using Kind = FeatureRow::Kind;

namespace {
constexpr int kRightPad = 12;
constexpr int kIconX = 22;
const char* kPlanes[] = {"XY", "XZ", "YZ"};

QColor alpha(QColor c, double a) {
  c.setAlphaF(c.alphaF() * a);
  return c;
}
}  // namespace

FeatureListWidget::FeatureListWidget(core::Document& doc, QWidget* parent) : QAbstractScrollArea(parent), doc_(doc) {
  setObjectName("FeatureList");
  setFrameShape(QFrame::NoFrame);
  setFocusPolicy(Qt::StrongFocus);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  viewport()->setMouseTracking(true);
  viewport()->setAttribute(Qt::WA_Hover);
  viewport()->setObjectName("FeatureListViewport");
  actions_ = new TimelineActions(doc_, TimelineActions::View::FeatureList, this);
  connect(actions_, &TimelineActions::editRequested, this, &FeatureListWidget::editRequested);
  connect(actions_, &TimelineActions::deleteRequested, this, &FeatureListWidget::deleteRequested);
  connect(actions_, &TimelineActions::findInBrowserRequested, this, &FeatureListWidget::findInBrowserRequested);
  connect(actions_, &TimelineActions::messageRequested, this, &FeatureListWidget::messageRequested);
  connect(actions_, &TimelineActions::renameRequested, this, [this](ObjectId id) { startRename(id); });

  auto ref = [this] { scheduleRefresh(); };
  auto refId = [this](ObjectId) { scheduleRefresh(); };
  connect(&doc_, &core::Document::featureAdded, this, [this](ObjectId, int) { scheduleRefresh(); });
  connect(&doc_, &core::Document::featureRemoved, this, [this](ObjectId, int) { scheduleRefresh(); });
  connect(&doc_, &core::Document::featureMoved, this, [this](ObjectId, int, int) { scheduleRefresh(); });
  connect(&doc_, &core::Document::featureChanged, this, refId);
  connect(&doc_, &core::Document::featureStatusChanged, this, refId);
  connect(&doc_, &core::Document::rollbackChanged, this, [this](int) { scheduleRefresh(); });
  connect(&doc_, &core::Document::groupsChanged, this, ref);
  connect(&doc_, &core::Document::recomputeFinished, this, ref);
  connect(&doc_, &core::Document::documentReset, this, ref);
  connect(&doc_, &core::Document::unitsChanged, this, ref);
  connect(&doc_, &core::Document::parametersChanged, this, ref);
  connect(&doc_.selection(), &core::SelectionModel::changed, this, &FeatureListWidget::syncFromSelection);
  connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this] {
    relayout();
    viewport()->update();
  });
  refresh();
}

FeatureListWidget::~FeatureListWidget() { hideTip(); }

// =========================================================================================
// rows and layout
// =========================================================================================

void FeatureListWidget::scheduleRefresh() {
  if (pending_) return;
  pending_ = true;
  QTimer::singleShot(0, this, [this] {
    if (pending_) refresh();
  });
}

void FeatureListWidget::refresh() {
  pending_ = false;
  // keep the current row on the same item across the rebuild
  ObjectId currentId = core::kInvalidId;
  Kind currentKind = Kind::Feature;
  if (const FeatureRow* r = model_.row(current_)) {
    currentId = r->id;
    currentKind = r->kind;
  }
  model_.rebuild(doc_);
  // drop groups that no longer exist from the local selection
  for (auto it = selectedGroups_.begin(); it != selectedGroups_.end();)
    it = doc_.group(*it) ? std::next(it) : selectedGroups_.erase(it);
  if (currentKind == Kind::Rollback) current_ = model_.rollbackRow();
  else if (currentId != core::kInvalidId) current_ = model_.rowOfItem(currentId);
  else if (currentKind == Kind::Origin) current_ = 0;
  if (current_ >= model_.rowCount()) current_ = -1;
  if (drag_ != Drag::None) drag_ = Drag::None;  // the rows moved under the drag
  relayout();
  viewport()->update();
  emit rowsChanged();
}

int FeatureListWidget::rowHeight(const FeatureRow& r) const {
  switch (r.kind) {
    case Kind::Rollback: return kBarH;
    case Kind::Insert: return kInsertH;
    default: return kRowH;
  }
}

bool FeatureListWidget::rowShown(int row) const {
  return row >= 0 && row < static_cast<int>(y_.size()) && y_[static_cast<size_t>(row)] >= 0;
}

void FeatureListWidget::relayout() {
  y_.assign(static_cast<size_t>(model_.rowCount()), -1);
  int y = kTop;
  for (int i = 0; i < model_.rowCount(); ++i) {
    const FeatureRow& r = model_.rows()[static_cast<size_t>(i)];
    if (compact_ && !r.live) continue;
    y_[static_cast<size_t>(i)] = y;
    y += rowHeight(r);
  }
  contentHeight_ = y + (compact_ ? 0 : 6);
  if (compact_ && y == kTop) contentHeight_ = 0;
  verticalScrollBar()->setRange(0, std::max(0, contentHeight_ - viewport()->height()));
  verticalScrollBar()->setPageStep(viewport()->height());
  verticalScrollBar()->setSingleStep(kRowH);
  updateGeometry();
}

int FeatureListWidget::contentHeight() const { return contentHeight_; }

int FeatureListWidget::compactHeight() const {
  int h = 0;
  for (const FeatureRow& r : model_.rows())
    if (r.live) h += rowHeight(r);
  return h > 0 ? h + kTop : 0;
}

QSize FeatureListWidget::sizeHint() const { return QSize(320, std::max(compact_ ? 0 : 120, contentHeight_)); }
QSize FeatureListWidget::minimumSizeHint() const { return QSize(200, compact_ ? 0 : kRowH * 2); }

void FeatureListWidget::resizeEvent(QResizeEvent* e) {
  QAbstractScrollArea::resizeEvent(e);
  relayout();
  if (editor_) {
    const int row = model_.rowOfItem(editing_);
    if (row >= 0) editor_->setGeometry(rowRect(row).adjusted(kIconX + 20 + model_.rows()[static_cast<size_t>(row)].depth * kIndent, 3, -kRightPad, -3));
  }
}

QRect FeatureListWidget::contentRect(int row) const {
  if (!rowShown(row)) return {};
  return QRect(0, y_[static_cast<size_t>(row)], viewport()->width(), rowHeight(model_.rows()[static_cast<size_t>(row)]));
}

QRect FeatureListWidget::rowRect(int row) const {
  QRect r = contentRect(row);
  if (r.isNull()) return r;
  return r.translated(0, -verticalScrollBar()->value());
}

int FeatureListWidget::rowAt(const QPoint& p) const {
  const int y = p.y() + verticalScrollBar()->value();
  for (int i = 0; i < model_.rowCount(); ++i) {
    if (!rowShown(i)) continue;
    const int top = y_[static_cast<size_t>(i)];
    if (y >= top && y < top + rowHeight(model_.rows()[static_cast<size_t>(i)])) return i;
  }
  return -1;
}

QRect FeatureListWidget::checkBoxRect(int row) const {
  const FeatureRow* r = model_.row(row);
  if (!r || r->kind != Kind::Group) return {};
  const QRect rr = rowRect(row);
  return QRect(rr.right() - kRightPad - kBox + 1, rr.center().y() - kBox / 2, kBox, kBox);
}

QRect FeatureListWidget::chevronRect(int row) const {
  const FeatureRow* r = model_.row(row);
  if (!r || r->kind != Kind::Group) return {};
  const QRect rr = rowRect(row);
  return QRect(4 + r->depth * kIndent, rr.top(), 16, rr.height());
}

QRect FeatureListWidget::originPlaneRect(int index) const {
  if (index < 0 || index > 2 || model_.rowCount() == 0 || model_.rows().front().kind != Kind::Origin) return {};
  const QRect rr = rowRect(0);
  if (rr.isNull()) return {};
  const QFontMetrics fm(theme::font(theme::TextStyle::Caption));
  const int w = fm.horizontalAdvance("XY") + 8;
  const int right = rr.right() - kRightPad + 4;
  return QRect(right - (3 - index) * (w + 4) + 4, rr.top() + 4, w, rr.height() - 8);
}

int FeatureListWidget::depthAtX(int x) const { return std::max(0, (x - kIconX + kIndent / 2) / kIndent); }

// =========================================================================================
// state set by the shell
// =========================================================================================

void FeatureListWidget::setLiveFeature(ObjectId id) {
  if (model_.liveFeature() == id) return;
  model_.setLiveFeature(id);
  refresh();
  if (id != core::kInvalidId) ensureVisible(model_.rowOfFeature(id));
}

void FeatureListWidget::setPendingLive(std::optional<FeatureListModel::PendingLive> live) {
  if (!live && !model_.pendingLive()) return;
  model_.setPendingLive(std::move(live));
  refresh();
  if (model_.pendingLive()) ensureVisible(model_.liveRow());
}

void FeatureListWidget::setCompact(bool on) {
  if (compact_ == on) return;
  compact_ = on;
  cancelRename();
  relayout();
  viewport()->update();
  emit rowsChanged();
}

void FeatureListWidget::setSearch(const QString& text) {
  if (model_.search() == text) return;
  model_.setSearch(text);
  refresh();
}

void FeatureListWidget::setHideSuppressed(bool on) {
  model_.setHideSuppressed(on);
  refresh();
}

void FeatureListWidget::setProblemsOnly(bool on) {
  model_.setProblemsOnly(on);
  refresh();
}

// =========================================================================================
// selection
// =========================================================================================

bool FeatureListWidget::isSelected(const FeatureRow& r) const {
  if (r.kind == Kind::Group) return selectedGroups_.count(r.id) > 0;
  if (r.kind != Kind::Feature) return false;
  for (const ShapeRef& s : doc_.selection().items())
    if (featureOfRef(s) == r.id || (s.kind == core::ShapeKind::Feature && s.owner == r.id)) return true;
  return false;
}

std::vector<ObjectId> FeatureListWidget::selectedItems() const {
  std::vector<ObjectId> out;
  for (const FeatureRow& r : model_.rows())
    if (r.isItem() && isSelected(r)) out.push_back(r.id);
  return out;
}

void FeatureListWidget::syncFromSelection() {
  if (selecting_) {
    viewport()->update();
    return;
  }
  // a selection made elsewhere (viewer, MODEL) replaces a group selection
  if (!doc_.selection().isEmpty()) selectedGroups_.clear();
  viewport()->update();
}

void FeatureListWidget::selectRow(int row, Qt::KeyboardModifiers mods) {
  const FeatureRow* r = model_.row(row);
  if (!r) return;
  current_ = row;
  if (!r->isItem()) {
    viewport()->update();
    return;
  }
  std::vector<int> rows;
  if ((mods & Qt::ShiftModifier) && model_.row(anchor_) && model_.row(anchor_)->isItem()) {
    for (int i = std::min(anchor_, row); i <= std::max(anchor_, row); ++i)
      if (model_.rows()[static_cast<size_t>(i)].isItem() && rowShown(i)) rows.push_back(i);
  } else {
    anchor_ = row;
    rows.push_back(row);
  }
  std::set<ObjectId> groups;
  std::vector<ShapeRef> refs;
  const bool toggle = (mods & Qt::ControlModifier) && !(mods & Qt::ShiftModifier);
  if (toggle) {
    groups = selectedGroups_;
    refs = doc_.selection().items();
  }
  for (int i : rows) {
    const FeatureRow& x = model_.rows()[static_cast<size_t>(i)];
    if (x.kind == Kind::Group) {
      if (toggle && groups.count(x.id)) groups.erase(x.id);
      else groups.insert(x.id);
    } else if (const core::Feature* f = doc_.feature(x.id)) {
      const ShapeRef fr = featureRef(*f);
      auto it = std::find(refs.begin(), refs.end(), fr);
      if (toggle && it != refs.end()) refs.erase(it);
      else if (it == refs.end()) refs.push_back(fr);
    }
  }
  selectedGroups_ = groups;
  selecting_ = true;
  doc_.selection().set(refs);
  selecting_ = false;
  viewport()->update();
}

void FeatureListWidget::setCurrentRow(int row) {
  if (!model_.row(row)) return;
  current_ = row;
  ensureVisible(row);
  viewport()->update();
}

void FeatureListWidget::ensureVisible(int row) {
  const QRect r = contentRect(row);
  if (r.isNull()) return;
  QScrollBar* sb = verticalScrollBar();
  if (r.top() < sb->value()) sb->setValue(r.top() - kTop);
  else if (r.bottom() > sb->value() + viewport()->height()) sb->setValue(r.bottom() - viewport()->height() + 1);
}

void FeatureListWidget::scrollToFeature(ObjectId id) {
  bool expanded = false;
  for (ObjectId g : doc_.groupChain(id))
    if (const core::TimelineGroup* grp = doc_.group(g); grp && grp->collapsed) {
      actions_->setCollapsed(g, false);
      expanded = true;
    }
  if (expanded || pending_) refresh();
  const int row = model_.rowOfFeature(id);
  if (row < 0) return;
  setCurrentRow(row);
}

int FeatureListWidget::nextFocusable(int from, int step) const {
  for (int i = from + step; i >= 0 && i < model_.rowCount(); i += step)
    if (rowShown(i)) return i;
  return from;
}

// =========================================================================================
// rename
// =========================================================================================

void FeatureListWidget::startRename(ObjectId id) {
  cancelRename();
  if (pending_) refresh();
  if (doc_.feature(id)) scrollToFeature(id);
  const int row = model_.rowOfItem(id);
  if (row < 0 || !rowShown(row)) return;
  const FeatureRow& r = model_.rows()[static_cast<size_t>(row)];
  current_ = row;
  ensureVisible(row);
  editing_ = id;
  editor_ = new QLineEdit(r.name, viewport());
  editor_->setObjectName("FeatureListRename");
  editor_->setFont(theme::font(theme::TextStyle::Value));
  editor_->setGeometry(rowRect(row).adjusted(kIconX + 20 + r.depth * kIndent, 3, -kRightPad, -3));
  editor_->selectAll();
  connect(editor_, &QLineEdit::editingFinished, this, [this] {
    if (editor_ && !editor_->hasFocus()) commitRename();
  });
  editor_->installEventFilter(this);
  editor_->show();
  editor_->setFocus();
}

void FeatureListWidget::commitRename() {
  if (!editor_) return;
  const QString text = editor_->text();
  const ObjectId id = editing_;
  QLineEdit* e = editor_;
  editor_ = nullptr;
  editing_ = core::kInvalidId;
  e->hide();
  e->deleteLater();
  setFocus();
  actions_->rename(id, text);
}

void FeatureListWidget::cancelRename() {
  if (!editor_) return;
  QLineEdit* e = editor_;
  editor_ = nullptr;
  editing_ = core::kInvalidId;
  e->hide();
  e->deleteLater();
}

bool FeatureListWidget::eventFilter(QObject* o, QEvent* e) {
  if (editor_ && o == editor_ && e->type() == QEvent::ShortcutOverride) {
    // Return / Enter / Escape belong to the editor, not to window shortcuts (repeat, accept)
    const int key = static_cast<QKeyEvent*>(e)->key();
    if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Escape) {
      e->accept();
      return true;
    }
  }
  if (editor_ && o == editor_ && e->type() == QEvent::KeyPress) {
    const int key = static_cast<QKeyEvent*>(e)->key();
    if (key == Qt::Key_Escape) {
      cancelRename();
      setFocus();
      return true;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
      // handled here: QLineEdit ignores Return, which would reach the list and edit the feature
      commitRename();
      return true;
    }
  }
  return QAbstractScrollArea::eventFilter(o, e);
}

// =========================================================================================
// painting
// =========================================================================================

void FeatureListWidget::paintEvent(QPaintEvent*) {
  QPainter p(viewport());
  p.fillRect(viewport()->rect(), theme::color(Tok::SurfacePanel));
  p.setRenderHint(QPainter::Antialiasing, false);
  const int scroll = verticalScrollBar()->value();
  for (int i = 0; i < model_.rowCount(); ++i) {
    if (!rowShown(i)) continue;
    const QRect r = rowRect(i);
    if (r.bottom() < 0 || r.top() > viewport()->height()) continue;
    const FeatureRow& row = model_.rows()[static_cast<size_t>(i)];
    if (row.kind == Kind::Rollback) {
      const bool dragging = drag_ == Drag::Bar && dragStarted_;
      paintBar(p, r, dragging ? false : hover_ == i);
      if (dragging) p.fillRect(r, alpha(theme::color(Tok::SurfacePanel), 0.6));
    } else if (row.kind == Kind::Insert) {
      paintInsert(p, r, hover_ == i);
    } else {
      paintRow(p, i, r);
    }
    if (hasFocus() && current_ == i && !editor_) {
      p.setPen(QPen(theme::color(Tok::AccentFocus), 2));
      p.setBrush(Qt::NoBrush);
      p.drawRect(QRectF(r).adjusted(1, 1, -1, -1));
    }
  }
  // drag feedback
  if (drag_ == Drag::Bar && dragStarted_ && barGap_ >= 0) {
    const int gy = (barGap_ < model_.rowCount() && rowShown(barGap_) ? y_[static_cast<size_t>(barGap_)] : contentHeight_ - 6) - scroll;
    paintBar(p, QRect(0, gy - kBarH / 2, viewport()->width(), kBarH), true);
  } else if (drag_ == Drag::Row && dragStarted_ && drop_.gap >= 0) {
    int gy = drop_.gap < model_.rowCount() && rowShown(drop_.gap) ? y_[static_cast<size_t>(drop_.gap)] : contentHeight_ - 6;
    gy -= scroll;
    const QColor c = drop_.valid ? theme::color(Tok::AccentIndicator) : theme::color(Tok::StatusError);
    const int x0 = kIconX - 4 + drop_.depth * kIndent;
    p.fillRect(QRect(x0, gy - 1, viewport()->width() - x0 - 8, 2), c);
    p.fillRect(QRect(x0 - 3, gy - 4, 3, 8), c);
    if (!drop_.valid) {
      // inline tip under the drop line: CANNOT MOVE HERE / the core's reason
      const QFont tf = theme::font(theme::TextStyle::Body);
      const QFont rf = theme::font(theme::TextStyle::Caption);
      const int w = viewport()->width() - 24;
      const QRect reasonRect = QFontMetrics(rf).boundingRect(QRect(0, 0, w - 18, 200), Qt::TextWordWrap, drop_.reason);
      QRect box(12, gy + 6, w, 12 + QFontMetrics(tf).height() + reasonRect.height());
      if (box.bottom() > viewport()->height() - 2) box.moveBottom(gy - 6);
      p.fillRect(box, theme::color(Tok::SurfaceOverlay));
      p.setPen(theme::color(Tok::BorderDefault));
      p.drawRect(box.adjusted(0, 0, -1, -1));
      p.fillRect(QRect(box.left(), box.top(), 3, box.height()), theme::color(Tok::StatusError));
      p.setFont(tf);
      p.setPen(theme::color(Tok::TextPrimary));
      p.drawText(box.adjusted(11, 5, -6, 0), Qt::AlignTop | Qt::AlignLeft, "Cannot move here");
      p.setFont(rf);
      p.setPen(theme::color(Tok::TextSecondary));
      p.drawText(box.adjusted(11, 5 + QFontMetrics(tf).height(), -6, -4), Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap, drop_.reason);
    }
  }
  if (model_.rowCount() <= 2 && !compact_ && doc_.featureCount() == 0) {
    p.setFont(theme::font(theme::TextStyle::Caption));
    p.setPen(theme::color(Tok::TextTertiary));
    p.drawText(QRect(12, kTop + kRowH * 3, viewport()->width() - 24, 40), Qt::AlignHCenter | Qt::TextWordWrap,
               "No features yet");
  }
}

void FeatureListWidget::paintBar(QPainter& p, const QRect& r, bool hot) {
  const QColor line = hot ? theme::color(Tok::LiveBase) : theme::color(Tok::TextPrimary);
  const int cy = r.center().y();
  p.fillRect(QRect(r.left() + 4, cy - 2, r.width() - 8, 4), line);
  // start knob (8 x 8 square)
  p.fillRect(QRect(r.left() + 4, cy - 4, 8, 8), line);
  // ROLLBACK tag
  const QFont f = theme::font(theme::TextStyle::Caption);
  const QFontMetrics fm(f);
  const QString tag = "Rollback";
  const int tw = fm.horizontalAdvance(tag.toUpper()) + 12;
  const QRect t(r.right() - 4 - tw - 6, cy - 7, tw, 14);
  p.setPen(hot ? theme::color(Tok::LiveInk) : theme::color(Tok::BorderDefault));
  p.setBrush(hot ? theme::color(Tok::LiveBase) : theme::color(Tok::SurfaceOverlay));
  p.drawRect(t.adjusted(0, 0, -1, -1));
  p.setFont(f);
  p.setPen(hot ? theme::color(Tok::LiveInk) : theme::color(Tok::TextPrimary));
  p.drawText(t, Qt::AlignCenter, tag);
  // end tick right of the tag
  p.fillRect(QRect(t.right() + 3, cy - 2, 3, 4), line);
}

void FeatureListWidget::paintInsert(QPainter& p, const QRect& r, bool hot) {
  const QRect box = r.adjusted(12, 8, -12, -8);
  QPen pen(theme::color(hot ? Tok::AccentText : Tok::BorderControl), 1, hot ? Qt::SolidLine : Qt::DashLine);
  p.setPen(pen);
  p.setBrush(hot ? theme::color(Tok::SurfaceHover) : Qt::transparent);
  p.drawRect(QRectF(box).adjusted(0.5, 0.5, -0.5, -0.5));
  p.setFont(theme::font(theme::TextStyle::Caption));
  p.setPen(theme::color(hot ? Tok::TextPrimary : Tok::TextSecondary));
  p.drawText(box, Qt::AlignCenter, "+ Insert feature at rollback");
}

void FeatureListWidget::paintRow(QPainter& p, int i, const QRect& r) {
  const FeatureRow& row = model_.rows()[static_cast<size_t>(i)];
  const bool selected = isSelected(row);
  const bool live = row.live;
  const bool error = row.kind == Kind::Feature && row.status == core::FeatureStatus::Error;
  const bool warning = row.kind == Kind::Feature && row.status == core::FeatureStatus::Warning;
  const bool dragged = drag_ == Drag::Row && dragStarted_ && dragRow_ == i;

  // background and the 3 px state bar (4.6): live > selected > error > warning
  QColor bg = theme::color(Tok::SurfacePanel);
  if (hover_ == i) bg = theme::color(Tok::SurfaceHover);
  if (row.kind == Kind::Group && row.suppressed && hover_ != i) bg = theme::color(Tok::SurfaceHeader);
  if (selected || dragged) bg = theme::color(Tok::AccentSelFill);
  if (live) bg = theme::color(Tok::LiveFill);
  p.fillRect(r, bg);
  Tok barTok = Tok::Count;
  if (live) barTok = Tok::LiveBase;
  else if (selected) barTok = Tok::AccentIndicator;
  else if (error) barTok = Tok::StatusError;
  else if (warning) barTok = Tok::StatusWarning;
  if (barTok != Tok::Count) p.fillRect(QRect(r.left(), r.top(), 3, r.height()), theme::color(barTok));

  // text colour and decoration
  Tok textTok = Tok::TextPrimary;
  double iconOpacity = 1.0;
  bool strike = false;
  if (row.suppressed) {
    textTok = Tok::TextDisabled;
    iconOpacity = 0.4;
    strike = true;
  } else if (row.rolledBack && !live) {
    textTok = Tok::TextTertiary;
    iconOpacity = 0.55;
  }
  if (live) textTok = Tok::LiveText;
  else if (error) textTok = Tok::StatusError;
  else if (warning) textTok = Tok::StatusWarning;

  const int indent = row.depth * kIndent;
  const int cy = r.center().y();
  // chevron (groups)
  if (row.kind == Kind::Group) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(theme::color(Tok::TextSecondary), 1.5));
    const QPointF c(4 + indent + 8, cy);
    QPainterPath path;
    if (row.collapsed) {
      path.moveTo(c + QPointF(-2, -4));
      path.lineTo(c + QPointF(2, 0));
      path.lineTo(c + QPointF(-2, 4));
    } else {
      path.moveTo(c + QPointF(-4, -2));
      path.lineTo(c + QPointF(0, 2));
      path.lineTo(c + QPointF(4, -2));
    }
    p.drawPath(path);
    p.restore();
  }
  // icon
  const QRect iconRect(kIconX + indent, cy - 8, 16, 16);
  QIcon icon;
  if (row.kind == Kind::Feature) icon = error ? resourceIcon("error") : warning ? resourceIcon("warning") : featureIcon(row.icon, row.type, row.folder);
  else if (row.kind == Kind::Live) icon = iconOr(row.icon, row.name.toStdString());
  else icon = resourceIcon(QString::fromStdString(row.icon));
  p.setOpacity(iconOpacity);
  icon.paint(&p, iconRect);
  p.setOpacity(1.0);

  // right side: value column / count + checkbox / origin planes
  int right = r.right() - kRightPad;
  const QFont valueFont = theme::font(theme::TextStyle::Value);
  if (row.kind == Kind::Group) {
    const QRect box = checkBoxRect(i);
    const bool boxHot = hover_ == i && box.contains(viewport()->mapFromGlobal(QCursor::pos()));
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const bool included = row.groupState == core::GroupSuppression::None;
    const bool partial = row.groupState == core::GroupSuppression::Some;
    if (included || partial) {
      p.setPen(Qt::NoPen);
      p.setBrush(theme::color(Tok::AccentSolid));
      p.drawRect(box);
      p.setPen(QPen(theme::color(Tok::AccentOnSolid), 1.8, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
      if (included) {
        QPainterPath tick;
        tick.moveTo(box.left() + 3, box.center().y() + 0.5);
        tick.lineTo(box.left() + 6, box.bottom() - 3);
        tick.lineTo(box.right() - 2.5, box.top() + 3.5);
        p.setBrush(Qt::NoBrush);
        p.drawPath(tick);
      } else {
        p.drawLine(QPointF(box.left() + 3.5, box.center().y() + 0.5), QPointF(box.right() - 2.5, box.center().y() + 0.5));
      }
    } else {
      p.setPen(QPen(theme::color(boxHot ? Tok::AccentText : Tok::BorderControl), 1));
      p.setBrush(theme::color(Tok::SurfaceInput));
      p.drawRect(QRectF(box).adjusted(0.5, 0.5, -0.5, -0.5));
    }
    if (boxHot && (included || partial)) {
      p.setPen(QPen(theme::color(Tok::AccentText), 1));
      p.setBrush(Qt::NoBrush);
      p.drawRect(QRectF(box).adjusted(-1.5, -1.5, 1.5, 1.5));
    }
    p.restore();
    right = box.left() - 10;
  }
  int valueLeft = right;
  if (row.kind == Kind::Origin) {
    p.setFont(theme::font(theme::TextStyle::Caption));
    for (int k = 0; k < 3; ++k) {
      const QRect pr = originPlaneRect(k);
      const bool hot = hover_ == i && pr.contains(viewport()->mapFromGlobal(QCursor::pos()));
      bool sel = false;
      for (const ShapeRef& s : doc_.selection().items())
        if (s.kind == core::ShapeKind::OriginPlane && s.name == kPlanes[k]) sel = true;
      if (sel) p.fillRect(pr, theme::color(Tok::AccentSelFill));
      p.setPen(theme::color(hot || sel ? Tok::AccentText : Tok::TextSecondary));
      p.drawText(pr, Qt::AlignCenter, kPlanes[k]);
      valueLeft = std::min(valueLeft, pr.left());
    }
  } else if (!row.value.isEmpty()) {
    const QFontMetrics vfm(valueFont);
    const QString v = row.value;  // never elided (<= 10 chars by design)
    const int vw = vfm.horizontalAdvance(v);
    p.setFont(valueFont);
    p.setPen(theme::color(live ? Tok::LiveText : row.suppressed ? Tok::TextDisabled : Tok::TextTertiary));
    p.drawText(QRect(right - vw, r.top(), vw, r.height()), Qt::AlignVCenter | Qt::AlignRight, v);
    valueLeft = right - vw;
  }

  // name (as typed; elided in the middle, full name in the tooltip)
  const int nameLeft = iconRect.right() + 9;
  const int nameWidth = std::max(0, valueLeft - 10 - nameLeft);
  QFont nf = valueFont;
  nf.setStrikeOut(strike);
  if (row.kind == Kind::Group) nf.setWeight(QFont::Bold);
  p.setFont(nf);
  p.setPen(theme::color(textTok));
  const QString name = QFontMetrics(nf).elidedText(row.name, Qt::ElideMiddle, nameWidth);
  p.drawText(QRect(nameLeft, r.top(), nameWidth, r.height()), Qt::AlignVCenter | Qt::AlignLeft, name);
  if (row.kind == Kind::Live && row.value.isEmpty()) {
    // a creating command: gold square marks the live row
    p.fillRect(QRect(r.right() - kRightPad - 8, cy - 4, 8, 8), theme::color(Tok::LiveBase));
  }
}

// =========================================================================================
// mouse
// =========================================================================================

void FeatureListWidget::hideTip() {
  if (!tipShown_) return;
  tipShown_ = false;
  if (InlineTip* t = window() ? window()->findChild<InlineTip*>("InlineTip", Qt::FindDirectChildrenOnly) : nullptr) t->hide();
}

void FeatureListWidget::updateHoverTip(int row) {
  const FeatureRow* r = model_.row(row);
  if (r && r->kind == Kind::Group && r->suppressed && drag_ == Drag::None && isVisible()) {
    InlineTip::forWidget(this)->showBeside(viewport(), rowRect(row), "Group suppressed",
                                           QString("Tick to include its %1 feature%2 again").arg(r->memberCount).arg(r->memberCount == 1 ? "" : "s"));
    tipShown_ = true;
  } else if (drag_ == Drag::None) {
    hideTip();
  }
}

void FeatureListWidget::mousePressEvent(QMouseEvent* e) {
  const QPoint pos = e->position().toPoint();
  pressPos_ = pos;
  dragStarted_ = false;
  drag_ = Drag::None;
  if (editor_) commitRename();
  const int row = rowAt(pos);
  const FeatureRow* r = model_.row(row);
  if (e->button() != Qt::LeftButton) {
    if (e->button() == Qt::RightButton && r && r->isItem() && !isSelected(*r)) selectRow(row, {});
    return;
  }
  if (!r) return;
  if (r->kind == Kind::Insert) {
    current_ = row;
    emit insertRequested();
    return;
  }
  if (r->kind == Kind::Rollback) {
    current_ = row;
    drag_ = Drag::Bar;
    barGap_ = row;
    viewport()->setCursor(Qt::SizeVerCursor);
    viewport()->update();
    return;
  }
  if (r->kind == Kind::Origin) {
    current_ = row;
    for (int k = 0; k < 3; ++k)
      if (originPlaneRect(k).contains(pos)) {
        selectedGroups_.clear();
        const ShapeRef plane = ShapeRef::originPlane(kPlanes[k]);
        if (e->modifiers() & Qt::ControlModifier) doc_.selection().toggle(plane);
        else doc_.selection().set(plane);
      }
    viewport()->update();
    return;
  }
  if (r->kind == Kind::Group && checkBoxRect(row).contains(pos)) {
    current_ = row;
    // ticked = included: a click includes all (from none or some) or suppresses all
    actions_->setSuppressed({r->id}, r->groupState == core::GroupSuppression::None);
    return;
  }
  if (r->kind == Kind::Group && chevronRect(row).contains(pos)) {
    current_ = row;
    actions_->setCollapsed(r->id, !r->collapsed);
    return;
  }
  if (r->kind == Kind::Live) {
    current_ = row;
    viewport()->update();
    return;
  }
  // an item row: select (Ctrl toggles, Shift extends); a drag reorders
  if (!(isSelected(*r) && !(e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)))) selectRow(row, e->modifiers());
  else current_ = row;
  drag_ = Drag::Row;
  dragRow_ = row;
  viewport()->update();
}

void FeatureListWidget::mouseMoveEvent(QMouseEvent* e) {
  const QPoint pos = e->position().toPoint();
  if (drag_ == Drag::None) {
    const int row = rowAt(pos);
    if (row != hover_) {
      hover_ = row;
      updateHoverTip(row);
    }
    const FeatureRow* r = model_.row(row);
    viewport()->setCursor(r && r->kind == Kind::Rollback ? Qt::SizeVerCursor : Qt::ArrowCursor);
    viewport()->update();
    return;
  }
  if (!dragStarted_ && (pos - pressPos_).manhattanLength() < 5) return;
  dragStarted_ = true;
  // nearest gap (row boundary) under the pointer
  const int y = pos.y() + verticalScrollBar()->value();
  int gap = model_.rowCount() - 1;  // above the Insert row = the end
  for (int i = 0; i < model_.rowCount(); ++i) {
    if (!rowShown(i)) continue;
    const int top = y_[static_cast<size_t>(i)];
    const int h = rowHeight(model_.rows()[static_cast<size_t>(i)]);
    if (y < top + h / 2) {
      gap = i;
      break;
    }
  }
  if (drag_ == Drag::Bar) {
    // snap to the gaps that change the rollback index
    const std::vector<int> gaps = model_.rollbackGaps();
    int best = barGap_;
    int bestDist = 1 << 30;
    for (int g : gaps) {
      const int gy = g < model_.rowCount() && rowShown(g) ? y_[static_cast<size_t>(g)] : contentHeight_;
      if (std::abs(gy - y) < bestDist) {
        bestDist = std::abs(gy - y);
        best = g;
      }
    }
    barGap_ = best;
  } else if (drag_ == Drag::Row) {
    if (model_.row(gap) && model_.row(gap)->kind == Kind::Origin) gap = 1;
    drop_ = model_.dropForGap(doc_, dragRow_, gap, depthAtX(pos.x()));
    // the refusal is painted inside the list (paintEvent): a separate tip widget shown during
    // the drag can take the pointer grab on X11 (native GL view next to it) and lose the release
    hideTip();
  }
  // auto-scroll near the edges
  if (pos.y() < 12) verticalScrollBar()->setValue(verticalScrollBar()->value() - 8);
  else if (pos.y() > viewport()->height() - 12) verticalScrollBar()->setValue(verticalScrollBar()->value() + 8);
  viewport()->update();
}

void FeatureListWidget::mouseReleaseEvent(QMouseEvent* e) {
  const Drag d = drag_;
  drag_ = Drag::None;
  viewport()->setCursor(Qt::ArrowCursor);
  hideTip();
  if (e->button() != Qt::LeftButton || !dragStarted_) {
    // a plain click on a row of a multi-selection selects just that row
    if (e->button() == Qt::LeftButton && d == Drag::Row && !(e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) &&
        selectedItems().size() > 1)
      selectRow(dragRow_, {});
    dragStarted_ = false;
    viewport()->update();
    if (e->button() == Qt::LeftButton && d == Drag::Row) emit rowClicked();
    return;
  }
  dragStarted_ = false;
  if (d == Drag::Bar) {
    actions_->setRollback(model_.rollbackIndexForGap(barGap_));
  } else if (d == Drag::Row && dragRow_ >= 0) {
    const FeatureRow* r = model_.row(dragRow_);
    if (r && drop_.move && drop_.valid) {
      if (r->kind == Kind::Group)
        doc_.undoStack().push(std::make_unique<core::MoveGroupCommand>(doc_, r->id, drop_.newIndex));
      else
        doc_.undoStack().push(std::make_unique<core::MoveFeatureCommand>(doc_, r->id, drop_.newIndex, drop_.targetGroup));
    } else if (r && !drop_.valid) {
      emit messageRequested("Cannot move " + r->name + ": " + drop_.reason, 1);
    }
  }
  drop_ = {};
  viewport()->update();
}

void FeatureListWidget::mouseDoubleClickEvent(QMouseEvent* e) {
  drag_ = Drag::None;
  const QPoint pos = e->position().toPoint();
  const int row = rowAt(pos);
  const FeatureRow* r = model_.row(row);
  if (!r || e->button() != Qt::LeftButton) return;
  if (r->kind == Kind::Feature) {
    emit editRequested(r->id);
  } else if (r->kind == Kind::Group && !checkBoxRect(row).contains(pos) && !chevronRect(row).contains(pos)) {
    startRename(r->id);
  }
}

void FeatureListWidget::leaveEvent(QEvent* e) {
  hover_ = -1;
  if (drag_ == Drag::None) hideTip();
  viewport()->update();
  QAbstractScrollArea::leaveEvent(e);
}

void FeatureListWidget::focusInEvent(QFocusEvent* e) {
  if (current_ < 0 || !rowShown(current_)) {
    const int sel = selectedItems().empty() ? -1 : model_.rowOfItem(selectedItems().front());
    current_ = sel >= 0 ? sel : nextFocusable(-1, 1);
  }
  viewport()->update();
  QAbstractScrollArea::focusInEvent(e);
}

void FeatureListWidget::focusOutEvent(QFocusEvent* e) {
  viewport()->update();
  QAbstractScrollArea::focusOutEvent(e);
}

bool FeatureListWidget::viewportEvent(QEvent* e) {
  if (e->type() == QEvent::ToolTip) {
    auto* he = static_cast<QHelpEvent*>(e);
    const int row = rowAt(he->pos());
    const FeatureRow* r = model_.row(row);
    QString tip;
    if (r && r->kind == Kind::Feature) {
      tip = r->name;
      if (r->suppressed) tip += " (suppressed)";
      else if (r->rolledBack) tip += " (rolled back)";
      // p2p3-check 8: the status line only when it says more than the state in brackets
      const QString status = r->statusMessage.trimmed();
      if (!status.isEmpty() && !tip.contains("(" + status + ")", Qt::CaseInsensitive)) tip += "\n" + status;
    } else if (r && r->kind == Kind::Group) {
      if (checkBoxRect(row).contains(he->pos()))
        tip = r->groupState == core::GroupSuppression::None ? "Untick to suppress the group" : "Tick to include the group";
      else if (!r->suppressed)
        tip = QString("%1 (%2 features)").arg(r->name).arg(r->memberCount);
    } else if (r && r->kind == Kind::Rollback) {
      tip = "Rollback: drag, or focus it and press Up / Down";
    } else if (r && r->kind == Kind::Insert) {
      tip = "Open the command search; the new feature goes in at the rollback bar";
    } else if (r && r->kind == Kind::Origin) {
      tip = "Origin planes: click to select";
    }
    if (tip.isEmpty()) QToolTip::hideText();
    else QToolTip::showText(he->globalPos(), tip, viewport());
    return true;
  }
  return QAbstractScrollArea::viewportEvent(e);
}

// =========================================================================================
// keyboard
// =========================================================================================

bool FeatureListWidget::handlesKey(int key, int modifiers) {
  const int mods = modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
  if (mods & (Qt::AltModifier | Qt::MetaModifier)) return false;
  switch (key) {
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Home:
    case Qt::Key_End:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown: return true;
    case Qt::Key_F2:
    case Qt::Key_Space:
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Delete:
    case Qt::Key_Menu: return mods == 0;
    default: return false;
  }
}

void FeatureListWidget::moveBar(int step) {
  const int bar = model_.rollbackRow();
  if (bar < 0) return;
  int idx = doc_.rollbackIndex();
  if (step < 0) {
    for (int i = bar - 1; i >= 0; --i)
      if (const FeatureRow& r = model_.rows()[static_cast<size_t>(i)]; r.isItem()) {
        idx = r.first;
        break;
      }
  } else {
    for (int i = bar + 1; i < model_.rowCount(); ++i)
      if (const FeatureRow& r = model_.rows()[static_cast<size_t>(i)]; r.isItem()) {
        idx = r.last + 1;
        break;
      }
  }
  actions_->setRollback(idx);
  refresh();  // the bar's row moved; it keeps the focus
  current_ = model_.rollbackRow();
  ensureVisible(current_);
}

void FeatureListWidget::keyPressEvent(QKeyEvent* e) {
  const FeatureRow* r = model_.row(current_);
  const bool shift = e->modifiers() & Qt::ShiftModifier;
  const auto selectionOrCurrent = [&] {
    std::vector<ObjectId> items = selectedItems();
    if (items.empty() && r && r->isItem()) items.push_back(r->id);
    return items;
  };
  switch (e->key()) {
    case Qt::Key_Up:
    case Qt::Key_Down: {
      const int step = e->key() == Qt::Key_Up ? -1 : 1;
      if (r && r->kind == Kind::Rollback && !shift) {
        moveBar(step);
        return;
      }
      const int next = nextFocusable(current_ < 0 ? (step > 0 ? -1 : model_.rowCount()) : current_, step);
      if (next != current_ || current_ < 0) {
        if (model_.row(next) && model_.row(next)->isItem()) selectRow(next, shift ? Qt::ShiftModifier : Qt::NoModifier);
        current_ = next;
        ensureVisible(next);
        viewport()->update();
      }
      return;
    }
    case Qt::Key_Home:
    case Qt::Key_End:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown: {
      int next = current_;
      const int n = e->key() == Qt::Key_PageUp || e->key() == Qt::Key_PageDown ? std::max(1, viewport()->height() / kRowH) : model_.rowCount();
      const int step = e->key() == Qt::Key_Home || e->key() == Qt::Key_PageUp ? -1 : 1;
      for (int k = 0; k < n; ++k) next = nextFocusable(next, step);
      if (model_.row(next) && model_.row(next)->isItem()) selectRow(next, shift ? Qt::ShiftModifier : Qt::NoModifier);
      current_ = next;
      ensureVisible(next);
      viewport()->update();
      return;
    }
    case Qt::Key_Left:
      if (r && r->kind == Kind::Group && !r->collapsed) actions_->setCollapsed(r->id, true);
      else if (r && r->parent != core::kInvalidId) setCurrentRow(model_.rowOfGroup(r->parent));
      return;
    case Qt::Key_Right:
      if (r && r->kind == Kind::Group && r->collapsed) actions_->setCollapsed(r->id, false);
      else if (r && r->kind == Kind::Group) setCurrentRow(nextFocusable(current_, 1));
      return;
    case Qt::Key_F2:
      if (r && r->isItem()) startRename(r->id);
      return;
    case Qt::Key_Space:
      if (r && r->kind == Kind::Origin) return;
      actions_->toggleSuppress(selectionOrCurrent());
      return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
      if (!r) return;
      if (r->kind == Kind::Feature) emit editRequested(r->id);
      else if (r->kind == Kind::Group) actions_->setCollapsed(r->id, !r->collapsed);
      else if (r->kind == Kind::Insert) emit insertRequested();
      else if (r->kind == Kind::Rollback) setCurrentRow(nextFocusable(current_, 1));  // leave the bar
      return;
    case Qt::Key_Delete:
      if (const std::vector<ObjectId> fs = actions_->featuresOf(selectionOrCurrent()); !fs.empty()) emit deleteRequested(fs);
      return;
    case Qt::Key_Menu:
      if (r) {
        QContextMenuEvent ce(QContextMenuEvent::Keyboard, rowRect(current_).center(), viewport()->mapToGlobal(rowRect(current_).center()));
        contextMenuEvent(&ce);
      }
      return;
    default: break;
  }
  QAbstractScrollArea::keyPressEvent(e);
}

void FeatureListWidget::contextMenuEvent(QContextMenuEvent* e) {
  hideTip();  // p2p3-check 8: the GROUP SUPPRESSED tip does not stay behind the menu
  const QPoint pos = e->reason() == QContextMenuEvent::Keyboard ? e->pos() : viewport()->mapFrom(this, e->pos());
  const int row = rowAt(pos);
  const FeatureRow* r = model_.row(row);
  std::vector<ObjectId> items;
  if (r && r->isItem()) {
    if (!isSelected(*r)) selectRow(row, {});
    items.push_back(r->id);
    for (ObjectId id : selectedItems())
      if (id != r->id) items.push_back(id);
  }
  QMenu menu(this);
  actions_->populate(menu, items);
  if (r && r->kind == Kind::Feature) {
    menu.addSeparator();
    menu.addAction("Insert Feature After", this, [this, idx = r->first + 1] {
      actions_->setRollback(idx);
      emit insertRequested();
    });
  }
  if (!menu.isEmpty()) menu.exec(e->globalPos());
}

}  // namespace fabricad::app
