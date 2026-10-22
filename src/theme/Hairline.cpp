// FabriCAD theme: device-pixel-aligned hairlines (see Hairline.h).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "theme/Hairline.h"

#include <QPainter>
#include <QTransform>

#include <cmath>

namespace fabricad::theme {

namespace {

constexpr qreal kEps = 1e-3;

/// Paints in device pixels: the painter's world transform is replaced so that one unit is one device
/// pixel (the painter keeps any redirection offset and the device pixel ratio in its device transform).
class DeviceSpace {
 public:
  explicit DeviceSpace(QPainter& p) : p_(p) {
    toDevice_ = p.deviceTransform();
    const QTransform world = p.worldTransform();
    base_ = world.inverted() * toDevice_;  // logical-without-world -> device
    p_.save();
    p_.setWorldTransform(base_.inverted());
    p_.setRenderHint(QPainter::Antialiasing, false);
  }
  ~DeviceSpace() { p_.restore(); }
  QRectF map(const QRectF& r) const { return toDevice_.mapRect(r); }
  void fill(const QRect& d, const QColor& c) { p_.fillRect(d, c); }

 private:
  QPainter& p_;
  QTransform toDevice_, base_;
};

QRect inwards(const QRectF& d) {
  const int l = static_cast<int>(std::ceil(d.left() - kEps));
  const int t = static_cast<int>(std::ceil(d.top() - kEps));
  const int r = static_cast<int>(std::floor(d.right() + kEps));
  const int b = static_cast<int>(std::floor(d.bottom() + kEps));
  return QRect(QPoint(l, t), QPoint(r - 1, b - 1));  // r, b exclusive
}

}  // namespace

QRectF snapInside(const QPainter& p, const QRectF& r) {
  const QTransform t = p.deviceTransform();
  const QRect d = inwards(t.mapRect(r));
  return t.inverted().mapRect(QRectF(d.left(), d.top(), d.width(), d.height()));
}

void hairlineFrame(QPainter& p, const QRectF& r, const QColor& c, int devicePx) {
  DeviceSpace ds(p);
  const QRect d = inwards(ds.map(r));
  if (d.width() <= 0 || d.height() <= 0) return;
  const int w = std::min(devicePx, std::max(1, std::min(d.width(), d.height()) / 2));
  ds.fill(QRect(d.left(), d.top(), d.width(), w), c);
  ds.fill(QRect(d.left(), d.bottom() - w + 1, d.width(), w), c);
  ds.fill(QRect(d.left(), d.top() + w, w, d.height() - 2 * w), c);
  ds.fill(QRect(d.right() - w + 1, d.top() + w, w, d.height() - 2 * w), c);
}

void hairlineV(QPainter& p, qreal x, qreal y0, qreal y1, const QColor& c, int devicePx) {
  DeviceSpace ds(p);
  const QRectF d = ds.map(QRectF(QPointF(x, y0), QPointF(x, y1)).normalized());
  const int dx = static_cast<int>(std::ceil(d.left() - kEps));
  const QRect span = inwards(QRectF(d.left(), d.top(), 1, d.height()));
  ds.fill(QRect(dx, span.top(), devicePx, span.height()), c);
}

void hairlineH(QPainter& p, qreal y, qreal x0, qreal x1, const QColor& c, int devicePx) {
  DeviceSpace ds(p);
  const QRectF d = ds.map(QRectF(QPointF(x0, y), QPointF(x1, y)).normalized());
  const int dy = static_cast<int>(std::ceil(d.top() - kEps));
  const QRect span = inwards(QRectF(d.left(), d.top(), d.width(), 1));
  ds.fill(QRect(span.left(), dy, span.width(), devicePx), c);
}

}  // namespace fabricad::theme
