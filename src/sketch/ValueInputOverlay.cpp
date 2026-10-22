// FabriCAD sketch: ValueInputOverlay implementation (see ValueInputOverlay.h).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sketch/ValueInputOverlay.h"

#include "core/ViewerContext.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QStyle>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace fabricad::sketch {

namespace {
/// Re-apply the theme QSS after a dynamic property changed.
void repolish(QWidget* w) {
  if (!w) return;
  w->style()->unpolish(w);
  w->style()->polish(w);
  for (QWidget* c : w->findChildren<QWidget*>()) {
    c->style()->unpolish(c);
    c->style()->polish(c);
  }
  w->update();
}
/// Point of `r` nearest to `p`.
QPoint nearestOn(const QRect& r, QPoint p) {
  return {std::clamp(p.x(), r.left(), r.right()), std::clamp(p.y(), r.top(), r.bottom())};
}
}  // namespace

// ---- ValueLeader ------------------------------------------------------------------------------

ValueLeader::ValueLeader(QWidget* parent) : QWidget(parent) {
  setObjectName("fcValueLeader");
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setAttribute(Qt::WA_NoSystemBackground);
  setFocusPolicy(Qt::NoFocus);
  hide();
}

void ValueLeader::setLine(QPoint from, QPoint to) {
  if (from == from_ && to == to_ && !geometry().isEmpty()) return;  // no repaint loop with the parent
  from_ = from;
  to_ = to;
  setGeometry(QRect(from, to).normalized().adjusted(-3, -3, 3, 3));
  update();
}

void ValueLeader::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QPointF a = from_ - pos(), b = to_ - pos();
  if (halo_.alpha() > 0) {
    p.setPen(QPen(halo_, 3.0, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(a, b);
  }
  p.setPen(QPen(line_, 1.0, Qt::SolidLine, Qt::RoundCap));
  p.drawLine(a, b);
  // a small dot on the anchor end, like the mockup's tick at the handle
  p.setPen(Qt::NoPen);
  p.setBrush(line_);
  p.drawEllipse(a, 1.8, 1.8);
}

// ---- ValueInputOverlay ------------------------------------------------------------------------

QRect ValueInputOverlay::placeClear(QPoint anchor, QSize size, QPoint offset, const QVector<QRect>& keepOut,
                                    const QRect& bounds) {
  auto clampIn = [&bounds](QRect r) {
    r.moveLeft(std::clamp(r.left(), bounds.left(), std::max(bounds.left(), bounds.right() + 1 - r.width())));
    r.moveTop(std::clamp(r.top(), bounds.top(), std::max(bounds.top(), bounds.bottom() + 1 - r.height())));
    return r;
  };
  auto clear = [&keepOut](const QRect& r) {
    return std::none_of(keepOut.begin(), keepOut.end(), [&r](const QRect& k) { return k.intersects(r); });
  };
  const int w = size.width(), h = size.height(), dx = offset.x(), dy = offset.y();
  std::vector<QRect> candidates{QRect(anchor.x() + dx, anchor.y() + dy, w, h), QRect(anchor.x() - dx - w, anchor.y() + dy, w, h),
                                QRect(anchor.x() + dx, anchor.y() - dy - h, w, h), QRect(anchor.x() - dx - w, anchor.y() - dy - h, w, h)};
  for (QRect& c : candidates) c = clampIn(c);
  for (const QRect& c : candidates)
    if (clear(c)) return c;
  // the anchor is behind an overlay (the card): beside the overlay the first choice hits, the
  // clear spot nearest to that choice
  const QRect first = candidates.front();
  constexpr int kGap = 8;
  std::vector<QRect> beside;
  for (const QRect& k : keepOut) {
    if (!k.intersects(first)) continue;
    beside.push_back(clampIn(QRect(k.left() - kGap - w, first.top(), w, h)));
    beside.push_back(clampIn(QRect(k.right() + 1 + kGap, first.top(), w, h)));
    beside.push_back(clampIn(QRect(first.left(), k.bottom() + 1 + kGap, w, h)));
    beside.push_back(clampIn(QRect(first.left(), k.top() - kGap - h, w, h)));
  }
  auto dist = [&first](const QRect& r) { return (r.topLeft() - first.topLeft()).manhattanLength(); };
  std::sort(beside.begin(), beside.end(), [&dist](const QRect& a, const QRect& b) { return dist(a) < dist(b); });
  for (const QRect& c : beside)
    if (clear(c)) return c;
  return first;  // nowhere clear (a tiny viewport): as before
}

QVector<QRect> ValueInputOverlay::keepOut() const {
  QVector<QRect> out;
  if (!parent_) return out;
  for (const QVariant& v : parent_->property("fcOverlayKeepOut").toList())
    if (const QRect r = v.toRect(); r.isValid()) out.push_back(r);
  return out;
}

ValueInputOverlay::ValueInputOverlay(ValueInputModel& model, core::IViewerContext* viewer, QWidget* parent)
    : QObject(parent), model_(model), viewer_(viewer), parent_(parent) {
  repositionTimer_.setSingleShot(true);
  repositionTimer_.setInterval(0);
  connect(&repositionTimer_, &QTimer::timeout, this, &ValueInputOverlay::reposition);
  if (parent_) parent_->installEventFilter(this);
  sync();
}

ValueInputOverlay::~ValueInputOverlay() {
  for (Box& b : boxes_)
    if (b.frame) b.frame->deleteLater();
  boxes_.clear();
  if (leader_) leader_->deleteLater();
}

QLineEdit* ValueInputOverlay::editAt(int fieldIndex) const {
  for (const Box& b : boxes_)
    if (b.fieldIndex == fieldIndex) return b.edit;
  return nullptr;
}

QFrame* ValueInputOverlay::frameAt(int fieldIndex) const {
  for (const Box& b : boxes_)
    if (b.fieldIndex == fieldIndex) return b.frame;
  return nullptr;
}

void ValueInputOverlay::ensureBoxes() {
  if (!parent_) return;
  const int n = model_.fieldCount();
  while (static_cast<int>(boxes_.size()) > n) {
    if (boxes_.back().frame) boxes_.back().frame->deleteLater();
    boxes_.pop_back();
  }
  while (static_cast<int>(boxes_.size()) < n) {
    Box b;
    b.fieldIndex = static_cast<int>(boxes_.size());
    // value box (docs/plans/ui-redesign.md 5.3): a caption tag above the value field, styled by
    // the theme QSS through objectNames and the dynamic properties state (idle | active |
    // error) and locked; no inline colours
    auto* frame = new QFrame(parent_);
    frame->setObjectName("fcValueBox");
    frame->setAttribute(Qt::WA_StyledBackground, true);
    frame->setFocusPolicy(Qt::NoFocus);
    frame->setProperty("state", "idle");
    frame->setProperty("locked", false);
    auto* col = new QVBoxLayout(frame);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    b.label = new QLabel(frame);
    b.label->setObjectName("fcValueTag");
    col->addWidget(b.label, 0, Qt::AlignLeft);
    auto* field = new QFrame(frame);
    field->setObjectName("fcValueField");
    field->setAttribute(Qt::WA_StyledBackground, true);
    col->addWidget(field);
    auto* lay = new QHBoxLayout(field);
    lay->setContentsMargins(6, 3, 6, 3);
    lay->setSpacing(4);
    b.edit = new QLineEdit(field);
    b.edit->setFocusPolicy(Qt::ClickFocus);
    b.edit->setFrame(false);
    b.lock = new QLabel(QString::fromUtf8("\xF0\x9F\x94\x92"), field);  // lock mark
    b.lock->hide();
    lay->addWidget(b.edit);
    lay->addWidget(b.lock);
    b.frame = frame;
    const int idx = b.fieldIndex;
    connect(b.edit, &QLineEdit::textEdited, this, [this, idx](const QString& t) {
      if (syncing_) return;
      model_.setText(idx, t.toStdString());
    });
    connect(b.edit, &QLineEdit::returnPressed, this, [this] {
      if (forward_) forward_(Qt::Key_Return, 0, "\r");
    });
    b.edit->installEventFilter(this);
    boxes_.push_back(b);
  }
}

void ValueInputOverlay::applyStyle(Box& b, const ValueField& f, bool active) {
  if (!b.frame) return;
  const char* state = !f.valid ? "error" : (active ? "active" : "idle");
  if (b.frame->property("state").toString() != QLatin1String(state) || b.frame->property("locked").toBool() != f.locked) {
    b.frame->setProperty("state", state);
    b.frame->setProperty("locked", f.locked);
    repolish(b.frame);
  }
  b.lock->setVisible(f.locked);
  b.edit->setToolTip(f.valid ? QString() : QString::fromStdString(f.error));
}

void ValueInputOverlay::sync() {
  if (!parent_) return;
  syncing_ = true;
  ensureBoxes();
  for (Box& b : boxes_) {
    if (b.fieldIndex >= model_.fieldCount() || !b.frame) continue;
    const ValueField& f = model_.field(b.fieldIndex);
    b.label->setText(QString::fromStdString(f.label).toUpper());
    b.label->setVisible(!f.label.empty());
    QString t = QString::fromStdString(model_.displayText(b.fieldIndex));
    if (b.edit->text() != t) {
      b.edit->setText(t);
      b.edit->setCursorPosition(t.size());
    }
    applyStyle(b, f, b.fieldIndex == model_.activeIndex());
    b.frame->adjustSize();
    b.frame->setVisible(visible_ && f.visible && f.hasAnchor);
  }
  syncing_ = false;
  reposition();
}

void ValueInputOverlay::reposition() {
  if (!parent_ || !viewer_) return;
  // Fusion stacks the boxes under each other at the first anchor that has a position; a field
  // with its own anchor (e.g. the arc radius at the arc centre) is placed there instead. Boxes
  // keep clear of the published overlays (card, dial, tool column, top row, readouts).
  const QVector<QRect> keep = keepOut();
  const QRect bounds = parent_->rect();
  bool havePrev = false;
  double prevSx = 0, prevSy = 0;
  QRect prev;
  QPoint leaderFrom, leaderTo;
  bool leaderOn = false;
  for (Box& b : boxes_) {
    if (!b.frame || !b.frame->isVisible() || b.fieldIndex >= model_.fieldCount()) continue;
    const ValueField& f = model_.field(b.fieldIndex);
    double sx = 0, sy = 0;
    const bool onScreen = viewer_->worldToScreen(f.anchor.toPnt(), sx, sy);
    if (!onScreen) {
      // no 3D view (offscreen, GL not ready): the viewport centre, not its top-left corner
      sx = parent_->width() / 2.0;
      sy = parent_->height() / 2.0;
    }
    const QSize size(b.frame->width(), std::max(b.frame->height(), b.frame->sizeHint().height()));
    const QPoint anchor(static_cast<int>(sx), static_cast<int>(sy));
    QRect r;
    if (havePrev && std::abs(sx - prevSx) < 4 && std::abs(sy - prevSy) < 4) {
      // same anchor: stack below the previous box, still clear of the overlays
      r = QRect(QPoint(prev.left(), prev.bottom() + 1 + 2), size);
      const bool hits = std::any_of(keep.begin(), keep.end(), [&r](const QRect& k) { return k.intersects(r); });
      if (hits || !bounds.contains(r)) r = placeClear(r.topLeft(), size, QPoint(0, 0), keep, bounds);
    } else {
      r = placeClear(anchor, size, QPoint(dx_, dy_), keep, bounds);
    }
    b.frame->move(r.topLeft());
    b.frame->raise();
    if (onScreen && (!leaderOn || b.fieldIndex == model_.activeIndex())) {
      leaderOn = true;
      leaderFrom = anchor;
      leaderTo = nearestOn(r, anchor);
    }
    havePrev = true;
    prevSx = sx;
    prevSy = sy;
    prev = r;
  }
  // the gold leader (5.3): only when the box sits away from its anchor, and not from an anchor
  // hidden behind an overlay (it would cross the card)
  const bool anchorHidden =
      std::any_of(keep.begin(), keep.end(), [&leaderFrom](const QRect& k) { return k.contains(leaderFrom); });
  if (leaderOn && !anchorHidden && (leaderTo - leaderFrom).manhattanLength() > 6) {
    if (!leader_) leader_ = new ValueLeader(parent_);
    leader_->setLine(leaderFrom, leaderTo);
    if (leader_->isHidden()) {
      leader_->show();
      // under the boxes, above the GL view (restacked only when it appears: no paint loop)
      for (Box& b : boxes_)
        if (b.frame && b.frame->isVisible()) {
          leader_->stackUnder(b.frame);
          break;
        }
    }
  } else if (leader_) {
    leader_->hide();
  }
}

void ValueInputOverlay::setVisible(bool visible) {
  if (visible_ == visible) return;
  visible_ = visible;
  sync();
}

bool ValueInputOverlay::eventFilter(QObject* watched, QEvent* event) {
  if (watched == parent_) {
    switch (event->type()) {
      case QEvent::Resize:
      case QEvent::Paint:
      case QEvent::Wheel:
      case QEvent::MouseMove:
      case QEvent::MouseButtonRelease:
      case QEvent::DynamicPropertyChange:  // fcOverlayKeepOut republished (card moved, resized)
        if (!repositionTimer_.isActive()) repositionTimer_.start();
        break;
      default: break;
    }
    return false;
  }
  if (event->type() == QEvent::FocusIn) {
    for (const Box& b : boxes_)
      if (b.edit == watched) model_.setActive(b.fieldIndex);
  } else if (event->type() == QEvent::KeyPress) {
    // Tab / Shift+Tab inside a box cycle the fields instead of moving the Qt focus chain
    auto* k = static_cast<QKeyEvent*>(event);
    if (k->key() == Qt::Key_Tab || k->key() == Qt::Key_Backtab) {
      if (forward_) forward_(k->key(), k->modifiers() & Qt::ShiftModifier ? 1 : 0, {});
      else if (k->key() == Qt::Key_Tab) model_.next();
      else model_.previous();
      return true;
    }
  }
  return false;
}

}  // namespace fabricad::sketch
