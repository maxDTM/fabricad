// FabriCAD theme: device-pixel-aligned hairlines for custom painters (docs/plans/ui-redesign.md 7.4).
// At a fractional scale (QT_SCALE_FACTOR 1.25, 1.5) a widget can start half-way into a device pixel; a
// 1 px QPen at its edge then lands outside the widget's clip and vanishes, or smears over two pixels.
// These helpers map the logical rectangle to device pixels, round it inwards and fill whole device
// pixels, so a frame is always `devicePx` device pixels wide and always inside the widget.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QColor>
#include <QRectF>

class QPainter;

namespace fabricad::theme {

/// Frame of `devicePx` device pixels along the inside of `r` (logical coordinates of `p`).
void hairlineFrame(QPainter& p, const QRectF& r, const QColor& c, int devicePx = 1);
/// Vertical hairline at logical x (snapped to the device pixel at or right of x), from y0 to y1.
void hairlineV(QPainter& p, qreal x, qreal y0, qreal y1, const QColor& c, int devicePx = 1);
/// Horizontal hairline at logical y (snapped to the device pixel at or below y), from x0 to x1.
void hairlineH(QPainter& p, qreal y, qreal x0, qreal x1, const QColor& c, int devicePx = 1);
/// `r` (logical) rounded inwards to whole device pixels and returned in logical coordinates.
QRectF snapInside(const QPainter& p, const QRectF& r);

}  // namespace fabricad::theme
