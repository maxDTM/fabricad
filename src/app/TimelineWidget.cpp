// FabriCAD app: timeline strip.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/TimelineWidget.h"

#include "app/Icons.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Json.h"
#include "core/UndoStack.h"

#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>

#include <algorithm>

namespace fabricad::app {

namespace {
constexpr int kHeight = 36;
}

// =========================================================================================
// TimelineStrip
// =========================================================================================

TimelineStrip::TimelineStrip(core::Document& doc, QWidget* parent) : QWidget(parent), doc_(doc) {
  setMouseTracking(true);
  setFixedHeight(kHeight);
  setFocusPolicy(Qt::ClickFocus);
  refresh();
}

void TimelineStrip::refresh() {
  layout_.setFeatureCount(doc_.featureCount());
  layout_.setEntries(TimelineLayout::entriesFrom(doc_), doc_.rollbackIndex());
  setMinimumWidth(layout_.totalWidth());
  updateGeometry();
  update();
}

QSize TimelineStrip::sizeHint() const { return QSize(layout_.totalWidth(), kHeight); }

void TimelineStrip::setHighlightedFeature(core::ObjectId id) {
  if (highlighted_ == id) return;
  highlighted_ = id;
  update();
}

void TimelineStrip::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const auto& entries = layout_.entries();
  for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
    const TimelineEntry& e = entries[static_cast<size_t>(i)];
    const QRect r = layout_.itemRect(i, height());
    QColor bg("#FFFFFF"), border("#C9CED6");
    if (e.status == core::FeatureStatus::Error) {
      bg = QColor("#FBD5D2");
      border = QColor("#D93025");
    } else if (e.status == core::FeatureStatus::Warning) {
      bg = QColor("#FDEFC6");
      border = QColor("#F2A900");
    }
    if (e.id == highlighted_) border = QColor("#0B6CFF");
    if (drag_ == Drag::Item && dragStarted_ && i == dragItem_) bg = QColor("#E3F1FA");
    p.setOpacity(e.active ? 1.0 : 0.38);
    p.setPen(QPen(border, e.id == highlighted_ ? 2.0 : 1.0));
    p.setBrush(bg);
    p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    featureIcon(e.icon, e.type, e.folder).paint(&p, r.adjusted(4, 4, -4, -4));
    if (e.suppressed) {
      p.setPen(QPen(QColor("#6B7079"), 1.5));
      p.drawLine(r.bottomLeft() + QPoint(3, -3), r.topRight() + QPoint(-3, 3));
    }
  }
  p.setOpacity(1.0);
  // rollback marker (drawn where the drag would drop it)
  QRect m = layout_.markerRect(height());
  if (drag_ == Drag::Marker && dropGap_ >= 0) m.moveCenter(QPoint(layout_.gapX(dropGap_), m.center().y()));
  p.setPen(Qt::NoPen);
  p.setBrush(QColor("#0696D7"));
  const int cx = m.center().x();
  p.drawRect(QRect(cx - 1, m.top() + 6, 3, m.height() - 6));
  QPolygon handle;
  handle << QPoint(cx - 5, m.top()) << QPoint(cx + 5, m.top()) << QPoint(cx + 5, m.top() + 5) << QPoint(cx, m.top() + 9)
         << QPoint(cx - 5, m.top() + 5);
  p.drawPolygon(handle);
  // insertion line while reordering
  if (drag_ == Drag::Item && dragStarted_ && dropGap_ >= 0) {
    const int x = layout_.gapX(dropGap_);
    p.setPen(QPen(dropValid_ ? QColor("#0B6CFF") : QColor("#D93025"), 2));
    p.drawLine(x, 3, x, height() - 3);
  }
}

bool TimelineStrip::event(QEvent* e) {
  if (e->type() == QEvent::ToolTip) {
    auto* he = static_cast<QHelpEvent*>(e);
    const int i = layout_.hitItem(he->pos(), height());
    if (i >= 0) {
      const TimelineEntry& en = layout_.entries()[static_cast<size_t>(i)];
      QString t = core::qs(en.name);
      if (en.suppressed) t += " (suppressed)";
      if (!en.statusMessage.empty()) t += "\n" + core::qs(en.statusMessage);
      QToolTip::showText(he->globalPos(), t, this);
    } else if (layout_.hitMarker(he->pos(), height())) {
      QToolTip::showText(he->globalPos(), "History marker: drag to roll back", this);
    } else {
      QToolTip::hideText();
    }
    return true;
  }
  return QWidget::event(e);
}

void TimelineStrip::mousePressEvent(QMouseEvent* e) {
  pressPos_ = e->position().toPoint();
  dragStarted_ = false;
  dropGap_ = -1;
  if (e->button() != Qt::LeftButton) return;
  if (layout_.hitMarker(pressPos_, height())) {
    drag_ = Drag::Marker;
    markerStart_ = layout_.markerGap();
    dropGap_ = markerStart_;
    setCursor(Qt::SizeHorCursor);
    return;
  }
  const int i = layout_.hitItem(pressPos_, height());
  if (i >= 0) {
    drag_ = Drag::Item;
    dragItem_ = i;
    emit selectRequested(layout_.entries()[static_cast<size_t>(i)].id, e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier));
  } else {
    drag_ = Drag::None;
  }
}

void TimelineStrip::mouseMoveEvent(QMouseEvent* e) {
  const QPoint pos = e->position().toPoint();
  if (drag_ == Drag::None) {
    setCursor(layout_.hitMarker(pos, height()) ? Qt::SizeHorCursor : Qt::ArrowCursor);
    return;
  }
  if (!dragStarted_ && (pos - pressPos_).manhattanLength() < 4) return;
  dragStarted_ = true;
  dropGap_ = layout_.gapForX(pos.x());
  if (drag_ == Drag::Item) {
    const int target = layout_.moveTargetForGap(dragItem_, dropGap_);
    dropValid_ = true;
    dropReason_.clear();
    if (target >= 0) {
      std::string why;
      dropValid_ = doc_.canMoveFeature(layout_.entries()[static_cast<size_t>(dragItem_)].id, target, &why);
      dropReason_ = core::qs(why);
    }
    if (!dropValid_) QToolTip::showText(e->globalPosition().toPoint(), "Cannot move: " + dropReason_, this);
    else QToolTip::hideText();
  }
  update();
}

void TimelineStrip::mouseReleaseEvent(QMouseEvent* e) {
  const Drag d = drag_;
  drag_ = Drag::None;
  setCursor(Qt::ArrowCursor);
  if (e->button() != Qt::LeftButton || !dragStarted_) {
    update();
    return;
  }
  if (d == Drag::Marker && dropGap_ >= 0 && dropGap_ != markerStart_) {
    const int idx = layout_.rollbackIndexForGap(dropGap_);
    if (idx != doc_.rollbackIndex()) doc_.undoStack().push(std::make_unique<core::SetRollbackCommand>(doc_, idx));
  } else if (d == Drag::Item && dropGap_ >= 0 && dropValid_) {
    const int target = layout_.moveTargetForGap(dragItem_, dropGap_);
    const core::ObjectId id = layout_.entries()[static_cast<size_t>(dragItem_)].id;
    if (target >= 0 && doc_.canMoveFeature(id, target))
      doc_.undoStack().push(std::make_unique<core::MoveFeatureCommand>(doc_, id, target));
  }
  dropGap_ = -1;
  dragStarted_ = false;
  QToolTip::hideText();
  update();
}

void TimelineStrip::mouseDoubleClickEvent(QMouseEvent* e) {
  const int i = layout_.hitItem(e->position().toPoint(), height());
  drag_ = Drag::None;
  if (i >= 0) emit editRequested(layout_.entries()[static_cast<size_t>(i)].id);
}

void TimelineStrip::contextMenuEvent(QContextMenuEvent* e) {
  const int i = layout_.hitItem(e->pos(), height());
  QMenu menu(this);
  if (i < 0) {
    menu.addAction(resourceIcon("timeline-last"), "Move to End", this, [this] {
      if (doc_.rollbackIndex() != doc_.featureCount())
        doc_.undoStack().push(std::make_unique<core::SetRollbackCommand>(doc_, doc_.featureCount()));
    });
    menu.exec(e->globalPos());
    return;
  }
  const TimelineEntry en = layout_.entries()[static_cast<size_t>(i)];
  emit selectRequested(en.id, false);
  menu.addAction(resourceIcon("feature"), en.type == "Sketch" ? "Edit Sketch" : "Edit Feature", this,
                 [this, id = en.id] { emit editRequested(id); });
  menu.addAction("Roll History Marker Here", this, [this, idx = en.docIndex + 1] {
    if (idx != doc_.rollbackIndex()) doc_.undoStack().push(std::make_unique<core::SetRollbackCommand>(doc_, idx));
  });
  menu.addAction(en.suppressed ? "Unsuppress Feature" : "Suppress Feature", this,
                 [this, id = en.id, s = !en.suppressed] { emit suppressRequested(id, s); });
  menu.addAction("Rename", this, [this, id = en.id] { emit renameRequested(id); });
  menu.addAction("Find in Browser", this, [this, id = en.id] { emit findInBrowserRequested(id); });
  menu.addSeparator();
  menu.addAction(resourceIcon("delete"), "Delete", this, [this, id = en.id] { emit deleteRequested(id); });
  menu.exec(e->globalPos());
}

// =========================================================================================
// TimelineWidget
// =========================================================================================

TimelineWidget::TimelineWidget(core::Document& doc, QWidget* parent) : QWidget(parent), doc_(doc) {
  setObjectName("Timeline");
  setAttribute(Qt::WA_StyledBackground);
  auto* row = new QHBoxLayout(this);
  row->setContentsMargins(4, 1, 4, 1);
  row->setSpacing(1);
  auto button = [&](const char* icon, const char* tip, auto fn) {
    auto* b = new QToolButton(this);
    b->setIcon(resourceIcon(icon));
    b->setIconSize(QSize(16, 16));
    b->setToolTip(tip);
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);
    connect(b, &QToolButton::clicked, this, fn);
    row->addWidget(b);
  };
  button("timeline-first", "Go to beginning", [this] { stepToStart(); });
  button("timeline-prev", "Step back", [this] { stepBack(); });
  button("timeline-next", "Step forward", [this] { stepForward(); });
  button("timeline-last", "Go to end", [this] { stepToEnd(); });
  row->addSpacing(6);
  strip_ = new TimelineStrip(doc_, this);
  scroll_ = new QScrollArea(this);
  scroll_->setWidget(strip_);
  scroll_->setWidgetResizable(true);
  scroll_->setFrameShape(QFrame::NoFrame);
  scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll_->setFixedHeight(kHeight + 8);
  // Scoped by object name: an unscoped rule would also reach the strip's context menu and its
  // tooltips (children / tip labels of the strip) and make them transparent, i.e. black (DOC-10).
  scroll_->setObjectName("TimelineScroll");
  scroll_->viewport()->setObjectName("TimelineViewport");
  strip_->setObjectName("TimelineStrip");
  scroll_->setStyleSheet("#TimelineScroll, #TimelineViewport, #TimelineStrip { background: transparent; }");
  row->addWidget(scroll_, 1);

  auto ref = [this] { scheduleRefresh(); };
  auto refId = [this](core::ObjectId) { scheduleRefresh(); };
  connect(&doc_, &core::Document::featureAdded, this, [this](core::ObjectId, int) { scheduleRefresh(); });
  connect(&doc_, &core::Document::featureRemoved, this, [this](core::ObjectId, int) { scheduleRefresh(); });
  connect(&doc_, &core::Document::featureMoved, this, [this](core::ObjectId, int, int) { scheduleRefresh(); });
  connect(&doc_, &core::Document::featureChanged, this, refId);
  connect(&doc_, &core::Document::featureStatusChanged, this, refId);
  connect(&doc_, &core::Document::rollbackChanged, this, [this](int) { scheduleRefresh(); });
  connect(&doc_, &core::Document::recomputeFinished, this, ref);
  connect(&doc_, &core::Document::documentReset, this, ref);
  // a feature tooltip must not stay up over a timeline that undo/redo just changed
  connect(&doc_.undoStack(), &core::UndoStack::changed, this, [this] {
    if (QToolTip::isVisible() && strip_->underMouse()) QToolTip::hideText();
  });
}

void TimelineWidget::scheduleRefresh() {
  if (pending_) return;
  pending_ = true;
  QTimer::singleShot(0, this, [this] {
    pending_ = false;
    strip_->refresh();
  });
}

namespace {
void pushRollback(core::Document& doc, int idx) {
  idx = std::clamp(idx, 0, doc.featureCount());
  if (idx != doc.rollbackIndex()) doc.undoStack().push(std::make_unique<core::SetRollbackCommand>(doc, idx));
}
}  // namespace

void TimelineWidget::stepToStart() { pushRollback(doc_, 0); }
void TimelineWidget::stepToEnd() { pushRollback(doc_, doc_.featureCount()); }

void TimelineWidget::stepBack() {
  // previous visible entry boundary
  int idx = doc_.rollbackIndex() - 1;
  while (idx > 0 && doc_.featureAt(idx - 1) && !doc_.featureAt(idx - 1)->isTimelineVisible()) --idx;
  pushRollback(doc_, idx);
}

void TimelineWidget::stepForward() {
  int idx = doc_.rollbackIndex() + 1;
  while (idx < doc_.featureCount() && doc_.featureAt(idx - 1) && !doc_.featureAt(idx - 1)->isTimelineVisible()) ++idx;
  pushRollback(doc_, idx);
}

void TimelineWidget::scrollToFeature(core::ObjectId id) {
  strip_->refresh();
  const auto& es = strip_->layout().entries();
  for (int i = 0; i < static_cast<int>(es.size()); ++i)
    if (es[static_cast<size_t>(i)].id == id) {
      scroll_->ensureVisible(strip_->layout().itemRect(i).center().x(), kHeight / 2, 40, 0);
      strip_->setHighlightedFeature(id);
    }
}

}  // namespace fabricad::app
