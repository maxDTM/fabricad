// FabriCAD drawing: minimal DXF R12 (AC1009) ASCII writer. Layers per LineClass, LINE /
// CIRCLE / ARC / POLYLINE / TEXT / SOLID entities. Y axis is flipped (sheet Y down ->
// DXF Y up) and numbers are formatted in the C locale.
//
// Model-space mode (P3, the 1:1 flat DXF of a face or sketch): DxfWriter::model(version) writes
// without the Y flip, with free layers (addLayer) and the entity calls line / arc / circle /
// lwpolyline (bulges) / spline, as R12 (POLYLINE, splines given as fit polylines) or R2000
// (AC1015: handles, subclass markers, LWPOLYLINE, SPLINE, $INSUNITS, BLOCK_RECORD table and the
// root dictionary). The sheet export (the constructors above) is unchanged: R12 byte for byte.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "drawing/DrawingTypes.h"
#include "drawing/SheetRenderer.h"

#include <string>
#include <utility>
#include <vector>

namespace fabricad::drawing {

class DxfWriter {
 public:
  enum class Version { R12, R2000 };
  /// $INSUNITS values (R2000).
  enum class Units { Unitless = 0, Inches = 1, Millimeters = 4 };
  struct PolyVertex {
    double x = 0, y = 0;
    double bulge = 0;  ///< tan(sweep / 4) of the arc to the next vertex; 0 = straight
  };
  /// A model-space writer (no Y flip, no sheet layers).
  static DxfWriter model(Version version);
  void setUnits(Units u) { units_ = u; }
  Version version() const { return version_; }
  /// Model space: declare a layer (name, ACI colour, line type CONTINUOUS / HIDDEN / CENTER).
  void addLayer(const std::string& name, int color, const std::string& ltype = "CONTINUOUS");
  void line(const std::string& layer, double x1, double y1, double x2, double y2);
  void circle(const std::string& layer, double cx, double cy, double r);
  /// CCW arc from startDeg to endDeg.
  void arc(const std::string& layer, double cx, double cy, double r, double startDeg, double endDeg);
  /// LWPOLYLINE (R2000) or POLYLINE/VERTEX (R12) with bulges.
  void lwpolyline(const std::string& layer, const std::vector<PolyVertex>& pts, bool closed);
  /// SPLINE (R2000) from control points (x, y), knots and weights (empty = non-rational); R12
  /// gets the `fit` polyline instead.
  void spline(const std::string& layer, int degree, const std::vector<std::pair<double, double>>& poles,
              const std::vector<double>& knots, const std::vector<double>& weights, bool closed,
              const std::vector<std::pair<double, double>>& fit);

  /// sheetHeight is used to flip Y (DXF y = sheetHeight - sheet y).
  explicit DxfWriter(double sheetHeight) : height_(sheetHeight) {}
  /// Sheet size for $EXTMAX / $LIMMAX (DRW-ASM-OUTPUT-9); 0 = unknown (not written).
  DxfWriter(double sheetWidth, double sheetHeight) : height_(sheetHeight), width_(sheetWidth) {}

  void addDisplayList(const DisplayList& list);
  void addCurve(const Curve2D& c);
  void addText(const DisplayText& t);
  void addFill(const DisplayFill& f);
  /// Complete DXF document text.
  std::string finish() const;
  /// Layer name of a class.
  static const char* layerName(LineClass cls);
  /// Text with Ø / ° / ± replaced by the AutoCAD %%c / %%d / %%p control codes.
  static std::string dxfEncodeText(const std::string& text);
  /// AutoCAD colour index for a class.
  static int layerColor(LineClass cls);

 private:
  void group(int code, const std::string& value);
  void group(int code, double value);
  void group(int code, int value);
  void entityHeader(const char* type, LineClass cls);
  double fy(double y) const { return height_ - y; }

  void modelEntity(const char* type, const std::string& layer, const char* subclass);
  std::string finishModel() const;
  std::string handle();

  double height_;
  double width_ = 0;
  std::string entities_;
  bool model_ = false;
  Version version_ = Version::R12;
  Units units_ = Units::Millimeters;
  int nextHandle_ = 0x100;  ///< entity handles; table / block handles are below
  struct Layer {
    std::string name, ltype;
    int color;
  };
  std::vector<Layer> layers_;
  double minX_ = 0, minY_ = 0, maxX_ = 0, maxY_ = 0;
  bool hasExtents_ = false;
  void extend(double x, double y);
};

}  // namespace fabricad::drawing
