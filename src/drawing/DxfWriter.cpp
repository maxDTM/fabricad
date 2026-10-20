// FabriCAD drawing: DXF R12 writer implementation.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "drawing/DxfWriter.h"

#include <cmath>
#include <cstdio>

namespace fabricad::drawing {

namespace {
const struct {
  LineClass cls;
  const char* name;
  int color;
  const char* ltype;
} kLayers[] = {
    {LineClass::Visible, "VISIBLE", 7, "CONTINUOUS"},     {LineClass::Hidden, "HIDDEN", 8, "HIDDEN"},
    {LineClass::Tangent, "TANGENT", 9, "CONTINUOUS"},     {LineClass::Section, "VISIBLE", 7, "CONTINUOUS"},
    {LineClass::Hatch, "HATCH", 3, "CONTINUOUS"},         {LineClass::Center, "CENTER", 1, "CENTER"},
    {LineClass::Dimension, "DIM", 5, "CONTINUOUS"},       {LineClass::Annotation, "TEXT", 7, "CONTINUOUS"},
    {LineClass::Border, "BORDER", 7, "CONTINUOUS"},       {LineClass::Detail, "DIM", 5, "CONTINUOUS"},
};

std::string fmt(double v) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.6f", v);
  return buf;
}
}  // namespace

const char* DxfWriter::layerName(LineClass cls) {
  for (const auto& l : kLayers)
    if (l.cls == cls) return l.name;
  return "0";
}

int DxfWriter::layerColor(LineClass cls) {
  for (const auto& l : kLayers)
    if (l.cls == cls) return l.color;
  return 7;
}

void DxfWriter::group(int code, const std::string& value) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "%3d\n", code);
  entities_ += buf;
  entities_ += value;
  entities_ += '\n';
}

void DxfWriter::group(int code, double value) { group(code, fmt(value)); }
void DxfWriter::group(int code, int value) { group(code, std::to_string(value)); }

void DxfWriter::entityHeader(const char* type, LineClass cls) {
  group(0, std::string(type));
  group(8, std::string(layerName(cls)));
}

void DxfWriter::addCurve(const Curve2D& c) {
  switch (c.type) {
    case CurveType::Segment:
      entityHeader("LINE", c.cls);
      group(10, c.a.x);
      group(20, fy(c.a.y));
      group(30, 0.0);
      group(11, c.b.x);
      group(21, fy(c.b.y));
      group(31, 0.0);
      break;
    case CurveType::Arc: {
      if (c.isCircle()) {
        entityHeader("CIRCLE", c.cls);
        group(10, c.center.x);
        group(20, fy(c.center.y));
        group(30, 0.0);
        group(40, c.radius);
        break;
      }
      // Flipping Y negates angles: the arc from s (sweep w) becomes CCW from -(s+w) to -s for
      // w > 0, and from -s to -(s+w) for w < 0.
      double s = c.startAngle, e = c.startAngle + c.sweep;
      double start = c.sweep > 0 ? -e : -s;
      double end = c.sweep > 0 ? -s : -e;
      entityHeader("ARC", c.cls);
      group(10, c.center.x);
      group(20, fy(c.center.y));
      group(30, 0.0);
      group(40, c.radius);
      group(50, normalizeAngle(start));
      group(51, normalizeAngle(end));
      break;
    }
    case CurveType::Polyline: {
      if (c.pts.size() < 2) return;
      entityHeader("POLYLINE", c.cls);
      group(66, 1);
      group(70, 0);
      group(10, 0.0);
      group(20, 0.0);
      group(30, 0.0);
      for (const auto& p : c.pts) {
        entityHeader("VERTEX", c.cls);
        group(10, p.x);
        group(20, fy(p.y));
        group(30, 0.0);
      }
      entityHeader("SEQEND", c.cls);
      break;
    }
  }
}

void DxfWriter::addText(const DisplayText& t) {
  if (t.text.empty()) return;
  // DXF TEXT with alignment: group 72 (horizontal: 0 left, 1 centre, 2 right), 73 (vertical:
  // 0 baseline, 1 bottom, 2 middle, 3 top); 11/21 hold the alignment point when 72/73 != 0.
  int h = t.halign == HAlign::Left ? 0 : (t.halign == HAlign::Center ? 1 : 2);
  int v = t.valign == VAlign::Bottom ? 1 : (t.valign == VAlign::Middle ? 2 : 3);
  entityHeader("TEXT", t.cls);
  group(10, t.pos.x);
  group(20, fy(t.pos.y));
  group(30, 0.0);
  group(40, t.height);
  group(1, t.text);
  group(50, t.angle);
  group(72, h);
  group(11, t.pos.x);
  group(21, fy(t.pos.y));
  group(31, 0.0);
  group(73, v);
}

void DxfWriter::addFill(const DisplayFill& f) {
  if (f.polygon.size() < 3) return;
  // SOLID takes 3 or 4 corners (4th = 3rd for triangles).
  entityHeader("SOLID", f.cls);
  const Point2& a = f.polygon[0];
  const Point2& b = f.polygon[1];
  const Point2& c = f.polygon[2];
  const Point2& d = f.polygon.size() > 3 ? f.polygon[3] : f.polygon[2];
  group(10, a.x); group(20, fy(a.y)); group(30, 0.0);
  group(11, b.x); group(21, fy(b.y)); group(31, 0.0);
  group(12, d.x); group(22, fy(d.y)); group(32, 0.0);
  group(13, c.x); group(23, fy(c.y)); group(33, 0.0);
}

void DxfWriter::addDisplayList(const DisplayList& list) {
  for (const auto& c : list.curves) addCurve(c);
  for (const auto& f : list.fills) addFill(f);
  for (const auto& t : list.texts) addText(t);
}

std::string DxfWriter::finish() const {
  std::string out;
  auto g = [&](int code, const std::string& v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%3d\n", code);
    out += buf;
    out += v;
    out += '\n';
  };
  // HEADER
  g(0, "SECTION"); g(2, "HEADER");
  g(9, "$ACADVER"); g(1, "AC1009");
  g(9, "$INSUNITS"); g(70, "4");  // millimetres
  g(9, "$EXTMIN"); g(10, fmt(0)); g(20, fmt(0)); g(30, fmt(0));
  g(0, "ENDSEC");
  // TABLES: line types and layers
  g(0, "SECTION"); g(2, "TABLES");
  g(0, "TABLE"); g(2, "LTYPE"); g(70, "3");
  g(0, "LTYPE"); g(2, "CONTINUOUS"); g(70, "0"); g(3, "Solid line"); g(72, "65"); g(73, "0"); g(40, fmt(0));
  g(0, "LTYPE"); g(2, "HIDDEN"); g(70, "0"); g(3, "Hidden __ __ __"); g(72, "65"); g(73, "2"); g(40, fmt(4.5));
  g(49, fmt(3.0)); g(49, fmt(-1.5));
  g(0, "LTYPE"); g(2, "CENTER"); g(70, "0"); g(3, "Center ____ _ ____ _"); g(72, "65"); g(73, "4"); g(40, fmt(12.0));
  g(49, fmt(8.0)); g(49, fmt(-1.5)); g(49, fmt(1.0)); g(49, fmt(-1.5));
  g(0, "ENDTAB");
  g(0, "TABLE"); g(2, "LAYER"); g(70, std::to_string(std::size(kLayers)));
  // de-duplicate layer names
  std::string written;
  for (const auto& l : kLayers) {
    std::string key = std::string("|") + l.name + "|";
    if (written.find(key) != std::string::npos) continue;
    written += key;
    g(0, "LAYER"); g(2, l.name); g(70, "0"); g(62, std::to_string(l.color)); g(6, l.ltype);
  }
  g(0, "ENDTAB");
  g(0, "TABLE"); g(2, "STYLE"); g(70, "1");
  g(0, "STYLE"); g(2, "STANDARD"); g(70, "0"); g(40, fmt(0)); g(41, fmt(1)); g(50, fmt(0)); g(71, "0");
  g(42, fmt(2.5)); g(3, "txt"); g(4, "");
  g(0, "ENDTAB");
  g(0, "ENDSEC");
  // BLOCKS (empty) and ENTITIES
  g(0, "SECTION"); g(2, "BLOCKS"); g(0, "ENDSEC");
  g(0, "SECTION"); g(2, "ENTITIES");
  out += entities_;
  g(0, "ENDSEC");
  g(0, "EOF");
  return out;
}

}  // namespace fabricad::drawing
