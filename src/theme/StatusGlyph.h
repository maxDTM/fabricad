// FabriCAD theme: shape-coded status glyphs (docs/plans/ui-redesign.md 4.3, 4.7, decision 9).
// An error is never colour alone: it always carries the octagon, a warning the triangle, info the
// circle, so the level reads without hue (colour-vision gate G, P5-CVD-1/2).
//
//   paintStatusGlyph()     paint the glyph into a rect (custom painters)
//   setStatusGlyph(label)  put the glyph in front of a QLabel's text (a small child widget plus a
//                          text indent); Level::None removes it. Colours are read from the tokens
//                          at paint time, so a theme switch needs no re-call.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QRectF>

class QLabel;
class QPainter;

namespace fabricad::theme {

enum class StatusLevel { None, Info, Warning, Error };

/// Error: octagon in status.error with a light bar; Warning: triangle in status.warning with "!";
/// Info: circle in status.info with "i".
void paintStatusGlyph(QPainter& p, const QRectF& r, StatusLevel level);
/// Glyph before `label`'s text (12 px, vertically centred on the first line).
void setStatusGlyph(QLabel* label, StatusLevel level);
/// The level a label shows (None without a glyph).
StatusLevel statusGlyphOf(const QLabel* label);

}  // namespace fabricad::theme
