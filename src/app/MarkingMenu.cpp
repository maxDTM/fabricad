// FabriCAD app: marking menu widget.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/MarkingMenu.h"

#include "core/Json.h"

#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace fabricad::app {

namespace {
constexpr int kMinSize = 320;  // widget square (wider for long labels)
constexpr int kMinSlotW = 120, kMaxSlotW = 170, kSlotH = 26;
constexpr double kRadiusX = 112, kRadiusY = 88;
constexpr double kLabelPointSize = 9;
constexpr int kRowH = 22;      // overflow rows
constexpr int kListW = 180;
}  // namespace

MarkingMenu::MarkingMenu(QWidget* parent) : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint) {
  setAttribute(Qt::WA_TranslucentBackground);
  setMouseTracking(true);
  setFixedSize(kMinSize, kMinSize);
}

void MarkingMenu::popup(const MarkingMenuModel& model, const QPoint& globalCentre) {
  model_ = model;
  // slots as wide as the longest label ("Repeat Rectangle"), within limits; the square grows
  // so the east and west slots still fit (DOC-12). Picking goes by angle, so it is unaffected.
  QFont f = font();
  f.setPointSizeF(kLabelPointSize);
  const QFontMetrics fm(f);
  int widest = 0;
  for (const MarkingEntry& e : model_.radial)
    if (!e.command.empty()) widest = std::max(widest, fm.horizontalAdvance(core::qs(e.label)));
  slotW_ = std::clamp(widest + 16, kMinSlotW, kMaxSlotW);
  size_ = std::max(kMinSize, static_cast<int>(2 * (kRadiusX + slotW_ / 2.0)) + 6);
  hover_ = -1;
  hoverRow_ = -1;
  const int rows = static_cast<int>(model_.overflow.size());
  setFixedSize(size_, size_ + (rows ? rows * kRowH + 8 : 0));
  move(globalCentre - QPoint(size_ / 2, size_ / 2));
  opening_ = true;
  show();
  setFocus();
  QTimer::singleShot(150, this, [this] { opening_ = false; });
}

QRect MarkingMenu::rowRect(int row) const {
  return QRect((size_ - kListW) / 2, size_ + 4 + row * kRowH, kListW, kRowH);
}

QRect MarkingMenu::slotRect(int slot) const {
  // ellipse layout: diagonals pulled in vertically so neighbouring labels never overlap
  constexpr double rx = kRadiusX, ry = kRadiusY;
  double dx = 0, dy = 0;
  slotDirection(slot, dx, dy);
  const bool diagonal = slot % 2 == 1;
  const double x = diagonal ? (dx > 0 ? 1 : -1) * rx * 0.72 : dx * rx;
  const double y = diagonal ? (dy > 0 ? 1 : -1) * ry * 0.58 : dy * ry;
  const QPoint c(size_ / 2 + static_cast<int>(x), size_ / 2 + static_cast<int>(y));
  return QRect(c.x() - slotW_ / 2, c.y() - kSlotH / 2, slotW_, kSlotH);
}

int MarkingMenu::rowAt(const QPoint& p) const {
  for (int i = 0; i < static_cast<int>(model_.overflow.size()); ++i)
    if (rowRect(i).contains(p) && model_.overflow[static_cast<size_t>(i)].command != kCmdSeparator) return i;
  return -1;
}

int MarkingMenu::slotAt(const QPoint& p) const {
  if (p.y() > size_) return -1;
  const int s = slotForOffset(p.x() - size_ / 2.0, p.y() - size_ / 2.0, 18.0);
  return s >= 0 && !model_.radial[static_cast<size_t>(s)].command.empty() ? s : -1;
}

void MarkingMenu::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QPointF c(size_ / 2.0, size_ / 2.0);
  // centre hub and direction pointer
  p.setPen(QPen(QColor("#9AA0A8"), 1.5));
  p.setBrush(QColor(255, 255, 255, 230));
  p.drawEllipse(c, 14, 14);
  if (hover_ >= 0) {
    double dx = 0, dy = 0;
    slotDirection(hover_, dx, dy);
    p.setPen(QPen(QColor("#0696D7"), 3));
    p.drawLine(c + QPointF(dx * 8, dy * 8), c + QPointF(dx * 16, dy * 16));
  }
  QFont f = font();
  f.setPointSizeF(kLabelPointSize);
  p.setFont(f);
  for (int s = 0; s < kSlotCount; ++s) {
    const MarkingEntry& e = model_.radial[static_cast<size_t>(s)];
    if (e.command.empty()) continue;
    const QRectF r = slotRect(s);
    const bool hot = s == hover_;
    p.setPen(QPen(hot ? QColor("#0696D7") : QColor("#C3C8CF"), 1));
    p.setBrush(hot ? QColor("#E3F1FA") : QColor(255, 255, 255, 240));
    p.drawRoundedRect(r, 4, 4);
    p.setPen(QColor("#2B2E33"));
    p.drawText(r.adjusted(6, 0, -6, 0), Qt::AlignCenter,
               p.fontMetrics().elidedText(core::qs(e.label), Qt::ElideRight, static_cast<int>(r.width()) - 12));
  }
  if (!model_.overflow.empty()) {
    const QRect box = rowRect(0).united(rowRect(static_cast<int>(model_.overflow.size()) - 1)).adjusted(0, -3, 0, 3);
    p.setPen(QPen(QColor("#C3C8CF"), 1));
    p.setBrush(QColor(255, 255, 255, 245));
    p.drawRoundedRect(box, 4, 4);
    for (int i = 0; i < static_cast<int>(model_.overflow.size()); ++i) {
      const MarkingEntry& e = model_.overflow[static_cast<size_t>(i)];
      const QRect r = rowRect(i);
      if (e.command == kCmdSeparator) {
        p.setPen(QColor("#E1E4E8"));
        p.drawLine(r.left() + 6, r.center().y(), r.right() - 6, r.center().y());
        continue;
      }
      if (i == hoverRow_) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#E3F1FA"));
        p.drawRect(r.adjusted(1, 0, -1, 0));
      }
      p.setPen(QColor("#2B2E33"));
      p.drawText(r.adjusted(10, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, core::qs(e.label));
    }
  }
}

void MarkingMenu::mouseMoveEvent(QMouseEvent* e) {
  const int s = slotAt(e->position().toPoint());
  const int r = rowAt(e->position().toPoint());
  if (s != hover_ || r != hoverRow_) {
    hover_ = s;
    hoverRow_ = r;
    update();
  }
}

void MarkingMenu::mouseReleaseEvent(QMouseEvent* e) {
  if (opening_ && e->button() == Qt::RightButton) return;  // release of the opening click
  const QPoint pos = e->position().toPoint();
  if (!rect().contains(pos)) {
    hide();
    return;
  }
  const int s = slotAt(pos);
  if (s >= 0) choose(model_.radial[static_cast<size_t>(s)].command);
  else if (const int r = rowAt(pos); r >= 0) choose(model_.overflow[static_cast<size_t>(r)].command);
  else if (std::hypot(pos.x() - size_ / 2.0, pos.y() - size_ / 2.0) < 18) hide();
}

void MarkingMenu::keyPressEvent(QKeyEvent* e) {
  if (e->key() == Qt::Key_Escape) hide();
  else QWidget::keyPressEvent(e);
}

void MarkingMenu::choose(const std::string& cmd) {
  hide();
  if (!cmd.empty()) emit commandChosen(cmd);
}

}  // namespace fabricad::app
