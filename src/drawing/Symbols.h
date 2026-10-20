// FabriCAD drawing: drafting symbols drawn as geometry (so PDF, SVG and DXF all show them
// identically, independent of fonts): ISO 1101 / ASME Y14.5 geometric characteristics and
// modifiers, hole callout symbols (depth, counterbore, countersink), and "rich" text lines that
// mix text runs and symbols. Also the toleranced dimension text block used by SheetRenderer.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "drawing/DrawingTypes.h"
#include "drawing/Geometry2D.h"
#include "drawing/Tolerance.h"

#include <string>
#include <vector>

namespace fabricad::drawing {

struct Dimension;
struct DisplayList;

/// Geometric characteristics (ISO 1101 table 2).
enum class GdtCharacteristic {
  Straightness, Flatness, Circularity, Cylindricity, ProfileLine, ProfileSurface,
  Angularity, Perpendicularity, Parallelism, Position, Concentricity, Symmetry,
  CircularRunout, TotalRunout,
};
const char* gdtCharacteristicName(GdtCharacteristic c);
bool gdtCharacteristicFromName(const std::string& s, GdtCharacteristic& out);
std::vector<GdtCharacteristic> gdtCharacteristics();
/// Whether the characteristic needs a datum reference (orientation, location, run-out).
bool gdtNeedsDatum(GdtCharacteristic c);

/// Material condition / zone modifiers shown as a letter in a circle.
enum class GdtModifier { None, MMC, LMC, FreeState, TangentPlane, ProjectedZone };
const char* gdtModifierName(GdtModifier m);
GdtModifier gdtModifierFromName(const std::string& s);
/// Letter of a modifier ("M", "L", "F", "T", "P"; empty for None).
const char* gdtModifierLetter(GdtModifier m);

/// Glyphs drawn as curves inside a square of side `size` centred at `center` (sheet mm).
enum class Glyph {
  None,
  // characteristics (same order as GdtCharacteristic)
  Straightness, Flatness, Circularity, Cylindricity, ProfileLine, ProfileSurface,
  Angularity, Perpendicularity, Parallelism, Position, Concentricity, Symmetry,
  CircularRunout, TotalRunout,
  // modifiers (circled letters)
  ModifierM, ModifierL, ModifierF, ModifierT, ModifierP,
  // hole callouts
  Depth, Counterbore, Countersink,
};
Glyph glyphFor(GdtCharacteristic c);
Glyph glyphFor(GdtModifier m);
/// Width a glyph occupies in a rich line of text height h.
double glyphWidth(Glyph g, double h);
/// Draw a glyph (curves, fills, and the letter of circled modifiers).
void addGlyph(DisplayList& out, Glyph g, const Point2& center, double size, LineClass cls = LineClass::Annotation);

/// A run of a rich text line: text or a glyph.
struct RichRun {
  std::string text;
  Glyph glyph = Glyph::None;
  static RichRun t(std::string s) { return {std::move(s), Glyph::None}; }
  static RichRun g(Glyph gl) { return {{}, gl}; }
};
using RichLine = std::vector<RichRun>;
/// Plain text of a rich line (glyphs spelled out: "DEEP", "CBORE", "CSK", "(M)"...), e.g. for
/// clipboards, tests and DXF-style plain exports.
std::string richLinePlainText(const RichLine& line);

/// Measured width (mm) of text at cap height `height`, using the real font metrics when a
/// QGuiApplication exists (the same metrics SheetRenderer::paintText uses), else an estimate.
double measureText(const std::string& text, double height, const std::string& fontFamily = "DejaVu Sans");
double richLineWidth(const RichLine& line, double height, const std::string& fontFamily = "DejaVu Sans");
/// Lay out a rich line horizontally with its left end at `leftMid` (vertical middle). Returns
/// the width.
double addRichLine(DisplayList& out, const RichLine& line, const Point2& leftMid, double height,
                   LineClass cls = LineClass::Annotation, const std::string& fontFamily = "DejaVu Sans");

/// Text parts of a dimension value: prefix + symbol ("2x Ø"), number, suffix ("°", " TYP").
struct DimTextParts {
  std::string prefix, number, suffix;
  bool angle = false;
  std::string joined() const { return prefix + number + suffix; }
};
DimTextParts dimensionTextParts(const Dimension& dim, double value, const core::UnitSystem& units);

/// Dimension text block with its tolerance at `center`, rotated by `angleDeg` (visual CCW).
/// Without a tolerance this is a single centred text (identical to the old renderer output).
void addToleranceText(DisplayList& out, const Dimension& dim, double value, const Point2& center, double angleDeg,
                      const DrawingStyle& style, const core::UnitSystem& units);

}  // namespace fabricad::drawing
