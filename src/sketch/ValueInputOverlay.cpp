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
#include <QWidget>

#include <algorithm>

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
}  // namespace

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
  // with its own anchor (e.g. the arc radius at the arc centre) is placed there instead.
  bool havePrev = false;
  double prevSx = 0, prevSy = 0;
  int nextY = 0;
  for (Box& b : boxes_) {
    if (!b.frame || !b.frame->isVisible() || b.fieldIndex >= model_.fieldCount()) continue;
    const ValueField& f = model_.field(b.fieldIndex);
    double sx = 0, sy = 0;
    if (!viewer_->worldToScreen(f.anchor.toPnt(), sx, sy)) continue;
    int x = static_cast<int>(sx) + dx_, y = static_cast<int>(sy) + dy_;
    if (havePrev && std::abs(sx - prevSx) < 4 && std::abs(sy - prevSy) < 4) y = nextY;  // same anchor: stack below
    const int w = b.frame->width(), h = std::max(b.frame->height(), b.frame->sizeHint().height());
    x = std::clamp(x, 0, std::max(0, parent_->width() - w));
    y = std::clamp(y, 0, std::max(0, parent_->height() - h));
    b.frame->move(x, y);
    b.frame->raise();
    havePrev = true;
    prevSx = sx;
    prevSy = sy;
    nextY = y + h + 2;
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
