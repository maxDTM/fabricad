// FabriCAD drawing: DXF R12 writer implementation (and the R12 / R2000 model-space mode).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "drawing/DxfWriter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>

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

std::string DxfWriter::dxfEncodeText(const std::string& text) {
  // AutoCAD control codes for the drafting characters (R12 TEXT has no Unicode): %%c Ø,
  // %%d degree, %%p plus/minus; a literal '%%' is escaped as %%%.
  std::string out;
  for (size_t i = 0; i < text.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(text[i]);
    if (c == 0xC3 && i + 1 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0x98) {
      out += "%%c";
      ++i;
    } else if (c == 0xC2 && i + 1 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0xB0) {
      out += "%%d";
      ++i;
    } else if (c == 0xC2 && i + 1 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0xB1) {
      out += "%%p";
      ++i;
    } else if (c == '%' && i + 1 < text.size() && text[i + 1] == '%') {
      out += "%%%";
      ++i;
    } else {
      out += text[i];
    }
  }
  return out;
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
  group(1, dxfEncodeText(t.text));
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
  if (model_) return finishModel();
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
  // R12 has no $INSUNITS (AC1012+); $MEASUREMENT is R14+ too. Paper millimetres are implied
  // by the extents/limits of the sheet (DRW-ASM-OUTPUT-9).
  g(9, "$EXTMIN"); g(10, fmt(0)); g(20, fmt(0)); g(30, fmt(0));
  if (width_ > 0) {
    g(9, "$EXTMAX"); g(10, fmt(width_)); g(20, fmt(height_)); g(30, fmt(0));
    g(9, "$LIMMIN"); g(10, fmt(0)); g(20, fmt(0));
    g(9, "$LIMMAX"); g(10, fmt(width_)); g(20, fmt(height_));
  }
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

// ---------------------------------------------------------------------------
// model space (P3: flat 1:1 DXF of a face or sketch)
// ---------------------------------------------------------------------------

DxfWriter DxfWriter::model(Version version) {
  DxfWriter w(0.0);
  w.model_ = true;
  w.version_ = version;
  return w;
}

void DxfWriter::addLayer(const std::string& name, int color, const std::string& ltype) {
  for (const auto& l : layers_)
    if (l.name == name) return;
  layers_.push_back({name, ltype, color});
}

std::string DxfWriter::handle() { return std::format("{:X}", nextHandle_++); }

void DxfWriter::extend(double x, double y) {
  if (!hasExtents_) {
    minX_ = maxX_ = x;
    minY_ = maxY_ = y;
    hasExtents_ = true;
    return;
  }
  minX_ = std::min(minX_, x);
  minY_ = std::min(minY_, y);
  maxX_ = std::max(maxX_, x);
  maxY_ = std::max(maxY_, y);
}

void DxfWriter::modelEntity(const char* type, const std::string& layer, const char* subclass) {
  group(0, std::string(type));
  if (version_ == Version::R2000) {
    group(5, handle());
    group(330, std::string("1F"));  // owner: *Model_Space block record
    group(100, std::string("AcDbEntity"));
  }
  group(8, layer);
  if (version_ == Version::R2000 && subclass) group(100, std::string(subclass));
}

void DxfWriter::line(const std::string& layer, double x1, double y1, double x2, double y2) {
  modelEntity("LINE", layer, "AcDbLine");
  group(10, x1); group(20, y1); group(30, 0.0);
  group(11, x2); group(21, y2); group(31, 0.0);
  extend(x1, y1);
  extend(x2, y2);
}

void DxfWriter::circle(const std::string& layer, double cx, double cy, double r) {
  modelEntity("CIRCLE", layer, "AcDbCircle");
  group(10, cx); group(20, cy); group(30, 0.0);
  group(40, r);
  extend(cx - r, cy - r);
  extend(cx + r, cy + r);
}

void DxfWriter::arc(const std::string& layer, double cx, double cy, double r, double startDeg, double endDeg) {
  modelEntity("ARC", layer, "AcDbCircle");
  group(10, cx); group(20, cy); group(30, 0.0);
  group(40, r);
  if (version_ == Version::R2000) group(100, std::string("AcDbArc"));
  group(50, normalizeAngle(startDeg));
  group(51, normalizeAngle(endDeg));
  extend(cx - r, cy - r);  // conservative
  extend(cx + r, cy + r);
}

void DxfWriter::lwpolyline(const std::string& layer, const std::vector<PolyVertex>& pts, bool closed) {
  if (pts.size() < 2) return;
  for (const auto& p : pts) extend(p.x, p.y);
  if (version_ == Version::R2000) {
    modelEntity("LWPOLYLINE", layer, "AcDbPolyline");
    group(90, static_cast<int>(pts.size()));
    group(70, closed ? 1 : 0);
    group(43, 0.0);
    for (const auto& p : pts) {
      group(10, p.x);
      group(20, p.y);
      if (p.bulge != 0) group(42, p.bulge);
    }
    return;
  }
  modelEntity("POLYLINE", layer, nullptr);
  group(66, 1);
  group(70, closed ? 1 : 0);
  group(10, 0.0); group(20, 0.0); group(30, 0.0);
  for (const auto& p : pts) {
    modelEntity("VERTEX", layer, nullptr);
    group(10, p.x); group(20, p.y); group(30, 0.0);
    if (p.bulge != 0) group(42, p.bulge);
  }
  modelEntity("SEQEND", layer, nullptr);
}

void DxfWriter::spline(const std::string& layer, int degree, const std::vector<std::pair<double, double>>& poles,
                       const std::vector<double>& knots, const std::vector<double>& weights, bool closed,
                       const std::vector<std::pair<double, double>>& fit) {
  if (version_ == Version::R12 || poles.size() < 2 || knots.size() != poles.size() + static_cast<size_t>(degree) + 1) {
    std::vector<PolyVertex> v;
    for (const auto& [x, y] : fit) v.push_back({x, y, 0});
    lwpolyline(layer, v, closed);
    return;
  }
  const bool rational = !weights.empty();
  modelEntity("SPLINE", layer, "AcDbSpline");
  group(210, 0.0); group(220, 0.0); group(230, 1.0);
  group(70, 8 | (closed ? 1 : 0) | (rational ? 4 : 0));
  group(71, degree);
  group(72, static_cast<int>(knots.size()));
  group(73, static_cast<int>(poles.size()));
  group(74, 0);
  group(42, 1e-10);
  group(43, 1e-10);
  for (double k : knots) group(40, k);
  if (rational)
    for (double w : weights) group(41, w);
  for (const auto& [x, y] : poles) {
    group(10, x); group(20, y); group(30, 0.0);
    extend(x, y);
  }
}

std::string DxfWriter::finishModel() const {
  std::string out;
  auto g = [&](int code, const std::string& v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%3d\n", code);
    out += buf;
    out += v;
    out += '\n';
  };
  const bool r2k = version_ == Version::R2000;
  const double x0 = hasExtents_ ? minX_ : 0, y0 = hasExtents_ ? minY_ : 0, x1 = hasExtents_ ? maxX_ : 0, y1 = hasExtents_ ? maxY_ : 0;
  g(0, "SECTION"); g(2, "HEADER");
  g(9, "$ACADVER"); g(1, r2k ? "AC1015" : "AC1009");
  if (r2k) {
    g(9, "$HANDSEED"); g(5, std::format("{:X}", nextHandle_ + 1));
    g(9, "$INSUNITS"); g(70, std::to_string(static_cast<int>(units_)));
    g(9, "$MEASUREMENT"); g(70, units_ == Units::Inches ? "0" : "1");
  }
  g(9, "$EXTMIN"); g(10, fmt(x0)); g(20, fmt(y0)); g(30, fmt(0));
  g(9, "$EXTMAX"); g(10, fmt(x1)); g(20, fmt(y1)); g(30, fmt(0));
  g(0, "ENDSEC");
  if (r2k) {
    g(0, "SECTION"); g(2, "CLASSES"); g(0, "ENDSEC");
  }
  g(0, "SECTION"); g(2, "TABLES");
  auto table = [&](const char* name, const char* h, int count) {
    g(0, "TABLE"); g(2, name);
    if (r2k) {
      g(5, h); g(330, "0"); g(100, "AcDbSymbolTable");
    }
    g(70, std::to_string(count));
  };
  auto record = [&](const char* type, const std::string& h, const char* owner, const char* sub) {
    g(0, type);
    if (r2k) {
      g(5, h); g(330, owner); g(100, "AcDbSymbolTableRecord"); g(100, sub);
    }
  };
  if (r2k) {
    table("VPORT", "8", 0); g(0, "ENDTAB");
  }
  // line types
  struct LT {
    const char* name;
    const char* desc;
    std::vector<double> dashes;
  };
  const LT lts[] = {{"CONTINUOUS", "Solid line", {}}, {"HIDDEN", "Hidden __ __ __", {3.0, -1.5}}, {"CENTER", "Center ____ _ ____ _", {8.0, -1.5, 1.0, -1.5}}};
  table("LTYPE", "5", r2k ? 5 : 3);
  if (r2k) {
    record("LTYPE", "14", "5", "AcDbLinetypeTableRecord"); g(2, "ByBlock"); g(70, "0"); g(3, ""); g(72, "65"); g(73, "0"); g(40, fmt(0));
    record("LTYPE", "15", "5", "AcDbLinetypeTableRecord"); g(2, "ByLayer"); g(70, "0"); g(3, ""); g(72, "65"); g(73, "0"); g(40, fmt(0));
  }
  int lh = 0x16;
  for (const LT& lt : lts) {
    double total = 0;
    for (double d : lt.dashes) total += std::abs(d);
    record("LTYPE", std::format("{:X}", lh++), "5", "AcDbLinetypeTableRecord");
    g(2, lt.name); g(70, "0"); g(3, lt.desc); g(72, "65"); g(73, std::to_string(lt.dashes.size())); g(40, fmt(total));
    for (double d : lt.dashes) {
      g(49, fmt(d));
      if (r2k) g(74, "0");
    }
  }
  g(0, "ENDTAB");
  table("LAYER", "2", static_cast<int>(layers_.size()) + 1);
  int layerHandle = 0x30;
  record("LAYER", std::format("{:X}", layerHandle++), "2", "AcDbLayerTableRecord");
  g(2, "0"); g(70, "0"); g(62, "7"); g(6, "CONTINUOUS");
  for (const auto& l : layers_) {
    record("LAYER", std::format("{:X}", layerHandle++), "2", "AcDbLayerTableRecord");
    g(2, l.name); g(70, "0"); g(62, std::to_string(l.color)); g(6, l.ltype);
  }
  g(0, "ENDTAB");
  table("STYLE", "3", 1);
  record("STYLE", "11", "3", "AcDbTextStyleTableRecord");
  g(2, "STANDARD"); g(70, "0"); g(40, fmt(0)); g(41, fmt(1)); g(50, fmt(0)); g(71, "0"); g(42, fmt(2.5)); g(3, "txt"); g(4, "");
  g(0, "ENDTAB");
  if (r2k) {
    table("VIEW", "6", 0); g(0, "ENDTAB");
    table("UCS", "7", 0); g(0, "ENDTAB");
    table("APPID", "9", 1);
    record("APPID", "12", "9", "AcDbRegAppTableRecord"); g(2, "ACAD"); g(70, "0");
    g(0, "ENDTAB");
    table("DIMSTYLE", "A", 0); g(100, "AcDbDimStyleTable"); g(0, "ENDTAB");
    table("BLOCK_RECORD", "1", 2);
    record("BLOCK_RECORD", "1F", "1", "AcDbBlockTableRecord"); g(2, "*Model_Space");
    record("BLOCK_RECORD", "1B", "1", "AcDbBlockTableRecord"); g(2, "*Paper_Space");
    g(0, "ENDTAB");
  }
  g(0, "ENDSEC");
  g(0, "SECTION"); g(2, "BLOCKS");
  if (r2k) {
    auto block = [&](const char* name, const char* begin, const char* end, const char* owner, bool paper) {
      g(0, "BLOCK"); g(5, begin); g(330, owner); g(100, "AcDbEntity");
      if (paper) g(67, "1");
      g(8, "0"); g(100, "AcDbBlockBegin"); g(2, name); g(70, "0"); g(10, fmt(0)); g(20, fmt(0)); g(30, fmt(0)); g(3, name); g(1, "");
      g(0, "ENDBLK"); g(5, end); g(330, owner); g(100, "AcDbEntity");
      if (paper) g(67, "1");
      g(8, "0"); g(100, "AcDbBlockEnd");
    };
    block("*Model_Space", "20", "21", "1F", false);
    block("*Paper_Space", "1C", "1D", "1B", true);
  }
  g(0, "ENDSEC");
  g(0, "SECTION"); g(2, "ENTITIES");
  out += entities_;
  g(0, "ENDSEC");
  if (r2k) {
    g(0, "SECTION"); g(2, "OBJECTS");
    g(0, "DICTIONARY"); g(5, "C"); g(330, "0"); g(100, "AcDbDictionary"); g(281, "1"); g(3, "ACAD_GROUP"); g(350, "D");
    g(0, "DICTIONARY"); g(5, "D"); g(330, "C"); g(100, "AcDbDictionary"); g(281, "1");
    g(0, "ENDSEC");
  }
  g(0, "EOF");
  return out;
}

}  // namespace fabricad::drawing
