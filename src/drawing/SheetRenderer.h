// FabriCAD drawing: turns sheet content into a display list (curves/texts/fills in sheet
// millimetres) and paints display lists with QPainter. One code path serves the canvas,
// PDF/SVG export (QPainter) and DXF export (display list walked by DxfWriter).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Units.h"
#include "drawing/Annotations.h"
#include "drawing/Dimension.h"
#include "drawing/DrawingTypes.h"
#include "drawing/Geometry2D.h"
#include "drawing/Sheet.h"

#include <QColor>
#include <QPen>

#include <array>
#include <map>
#include <string>
#include <vector>

class QPainter;

namespace fabricad::drawing {

class DrawingModel;

enum class HAlign { Left, Center, Right };
enum class VAlign { Top, Middle, Bottom };

struct DisplayText {
  Point2 pos;            ///< anchor (sheet mm)
  std::string text;
  double height = 3.5;   ///< cap height in mm
  double angle = 0;      ///< degrees CCW (visual)
  HAlign halign = HAlign::Center;
  VAlign valign = VAlign::Middle;
  LineClass cls = LineClass::Annotation;
  bool bold = false;
  bool error = false;    ///< drawn in the error colour (orphaned dimension)
};

struct DisplayFill {
  std::vector<Point2> polygon;
  LineClass cls = LineClass::Dimension;
};

struct DisplayList {
  std::vector<Curve2D> curves;   ///< sheet coordinates
  std::vector<DisplayText> texts;
  std::vector<DisplayFill> fills;
  void append(const DisplayList& o);
  BBox2 bbox() const;
  bool empty() const { return curves.empty() && texts.empty() && fills.empty(); }
};

class SheetRenderer {
 public:
  SheetRenderer(const DrawingStyle& style, const core::UnitSystem& units) : style_(style), units_(units) {}

  // --- builders ------------------------------------------------------------------------
  void addFrame(DisplayList& out, const Sheet& sheet) const;
  void addTitleBlock(DisplayList& out, const Sheet& sheet, const std::map<std::string, std::string>& values) const;
  /// Curves of the view, its label, plus the section lines / detail circles of its children.
  void addView(DisplayList& out, const Sheet& sheet, const DrawingView& view) const;
  void addViewCurves(DisplayList& out, const DrawingView& view) const;
  void addViewDecorations(DisplayList& out, const Sheet& sheet, const DrawingView& view) const;
  void addDimension(DisplayList& out, const DrawingView& view, const Dimension& dim) const;
  void addNote(DisplayList& out, const TextNote& note) const;
  void addCenterMark(DisplayList& out, const DrawingView& view, const CenterMark& mark) const;
  void addCenterLine(DisplayList& out, const DrawingView& view, const CenterLine& line) const;
  void addBalloon(DisplayList& out, const DrawingView& view, const Balloon& balloon) const;
  void addPartsList(DisplayList& out, const PartsList& list) const;
  /// Everything on a sheet (views, annotations, frame, title block).
  void addSheet(DisplayList& out, const Sheet& sheet, const std::map<std::string, std::string>& titleValues,
                bool frame = true) const;

  // --- painting ---------------------------------------------------------------------------
  /// Drawing ink (token drawing.ink): black in both themes, on white paper (drawing.paper).
  static QColor ink();
  /// Error text on the sheet (status.error, light value: it sits on the white paper).
  static QColor errorInk();
  /// Pen for a line class: widths in mm (non-cosmetic), ISO dash patterns.
  static QPen pen(LineClass cls, const QColor& color = ink(), double widthScale = 1.0);
  static void paint(QPainter& p, const DisplayList& list, const QColor& color = ink(),
                    const std::string& fontFamily = "DejaVu Sans");
  static void paintCurve(QPainter& p, const Curve2D& c);
  static void paintText(QPainter& p, const DisplayText& t, const std::string& fontFamily);
  /// Rough text width estimate in mm (0.62 * height per character).
  static double textWidth(const std::string& text, double height);

  const DrawingStyle& style() const { return style_; }

 private:
  void addSectionLine(DisplayList& out, const DrawingView& parent, const DrawingView& section) const;
  void addDetailCircle(DisplayList& out, const DrawingView& parent, const DrawingView& detail) const;
  void addArrow(DisplayList& out, const Point2& tip, const Point2& dir, LineClass cls) const;

  DrawingStyle style_;
  core::UnitSystem units_;
};

/// Title block values with auto-filled defaults (Title, Date, Scale, Sheet, Size, Mass...).
std::map<std::string, std::string> titleBlockValues(const DrawingModel& model, const Sheet& sheet);
/// Label text of a view ("A-A (1:2)", "DETAIL B (2:1)", "VIEW1").
std::string viewLabelText(const DrawingView& view);

}  // namespace fabricad::drawing
