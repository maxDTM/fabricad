// FabriCAD drawing: minimal DXF R12 (AC1009) ASCII writer. Layers per LineClass, LINE /
// CIRCLE / ARC / POLYLINE / TEXT / SOLID entities. Y axis is flipped (sheet Y down ->
// DXF Y up) and numbers are formatted in the C locale.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "drawing/DrawingTypes.h"
#include "drawing/SheetRenderer.h"

#include <string>

namespace fabricad::drawing {

class DxfWriter {
 public:
  /// sheetHeight is used to flip Y (DXF y = sheetHeight - sheet y).
  explicit DxfWriter(double sheetHeight) : height_(sheetHeight) {}

  void addDisplayList(const DisplayList& list);
  void addCurve(const Curve2D& c);
  void addText(const DisplayText& t);
  void addFill(const DisplayFill& f);
  /// Complete DXF document text.
  std::string finish() const;
  /// Layer name of a class.
  static const char* layerName(LineClass cls);
  /// AutoCAD colour index for a class.
  static int layerColor(LineClass cls);

 private:
  void group(int code, const std::string& value);
  void group(int code, double value);
  void group(int code, int value);
  void entityHeader(const char* type, LineClass cls);
  double fy(double y) const { return height_ - y; }

  double height_;
  std::string entities_;
};

}  // namespace fabricad::drawing
