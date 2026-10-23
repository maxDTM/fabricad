// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/standards/Fasteners.h"

#include "part/standards/Clearance.h"
#include "part/standards/Heads.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <gp_Ax2.hxx>

#include <algorithm>
#include <cmath>
#include <format>

namespace fabricad::part::standards {

namespace {

// ---- tables (metric mm, inch rows in inches) ----------------------------------------------

struct NutRow {
  const char* size;
  bool inch;
  double s, m;        ///< across flats, height (nyloc: total height h)
};
constexpr NutRow kHexNuts[] = {  // ISO 4032 / ASME B18.2.2
    {"M3", false, 5.5, 2.4},  {"M4", false, 7, 3.2},     {"M5", false, 8, 4.7},     {"M6", false, 10, 5.2},
    {"M8", false, 13, 6.8},   {"M10", false, 16, 8.4},   {"M12", false, 18, 10.8},  {"M14", false, 21, 12.8},
    {"M16", false, 24, 14.8}, {"M20", false, 30, 18},    {"M24", false, 36, 21.5},  {"M30", false, 46, 25.6},
    {"M36", false, 55, 31},   {"#10", true, 0.375, 0.125}, {"1/4", true, 0.4375, 0.2188}, {"5/16", true, 0.5, 0.2656},
    {"3/8", true, 0.5625, 0.3281}, {"1/2", true, 0.75, 0.4375}, {"5/8", true, 0.9375, 0.5469}, {"3/4", true, 1.125, 0.6406},
};
constexpr NutRow kNylocNuts[] = {  // ISO 7040 (h total)
    {"M3", false, 5.5, 4},  {"M4", false, 7, 5},     {"M5", false, 8, 5},     {"M6", false, 10, 6},   {"M8", false, 13, 8},
    {"M10", false, 16, 10}, {"M12", false, 18, 12},  {"M16", false, 24, 16},  {"M20", false, 30, 20}, {"M24", false, 36, 24},
    {"1/4", true, 0.4375, 0.3125}, {"5/16", true, 0.5, 0.3594}, {"3/8", true, 0.5625, 0.4219}, {"1/2", true, 0.75, 0.5938},
};
struct WasherRow {
  const char* size;
  bool inch;
  double d1, d2, h;
};
constexpr WasherRow kWashers[] = {  // ISO 7089 / ASME B18.22.1 type A narrow
    {"M2", false, 2.2, 5, 0.3},     {"M2.5", false, 2.7, 6, 0.5},  {"M3", false, 3.2, 7, 0.5},   {"M4", false, 4.3, 9, 0.8},
    {"M5", false, 5.3, 10, 1},      {"M6", false, 6.4, 12, 1.6},   {"M8", false, 8.4, 16, 1.6},  {"M10", false, 10.5, 20, 2},
    {"M12", false, 13, 24, 2.5},    {"M14", false, 15, 28, 2.5},   {"M16", false, 17, 30, 3},    {"M20", false, 21, 37, 3},
    {"M24", false, 25, 44, 4},      {"M30", false, 31, 56, 4},     {"M36", false, 37, 66, 5},
    {"#10", true, 0.219, 0.5, 0.049}, {"1/4", true, 0.281, 0.625, 0.065}, {"5/16", true, 0.344, 0.688, 0.065},
    {"3/8", true, 0.406, 0.812, 0.065}, {"1/2", true, 0.531, 1.062, 0.095}, {"5/8", true, 0.656, 1.312, 0.095},
    {"3/4", true, 0.812, 1.469, 0.134},
};
constexpr WasherRow kSmallWashers[] = {  // ISO 7092
    {"M2", false, 2.2, 4.5, 0.3}, {"M2.5", false, 2.7, 5, 0.5}, {"M3", false, 3.2, 6, 0.5},   {"M4", false, 4.3, 8, 0.5},
    {"M5", false, 5.3, 9, 1},     {"M6", false, 6.4, 11, 1.6},  {"M8", false, 8.4, 15, 1.6},  {"M10", false, 10.5, 18, 1.6},
    {"M12", false, 13, 20, 2},    {"M16", false, 17, 28, 2.5},  {"M20", false, 21, 34, 3},
};
struct InsertRow {
  const char* size;
  double outer, length, shortLength, hole;
};
constexpr InsertRow kInserts[] = {  // tapered heat-set inserts for thermoplastics (common sizes)
    {"M2", 3.6, 4.0, 3.0, 3.2}, {"M2.5", 4.6, 5.7, 4.0, 4.0}, {"M3", 4.6, 5.7, 4.0, 4.0}, {"M4", 6.3, 8.1, 4.0, 5.6},
    {"M5", 7.1, 9.5, 6.0, 6.4}, {"M6", 8.7, 12.7, 7.0, 8.0}, {"M8", 10.1, 12.7, 8.0, 9.7},
};
struct PinRow {
  const char* size;
  bool inch;
  double d;
};
constexpr PinRow kPins[] = {  // ISO 2338 m6 / ASME B18.8.2
    {"2", false, 2},   {"2.5", false, 2.5}, {"3", false, 3},   {"4", false, 4},     {"5", false, 5},
    {"6", false, 6},   {"8", false, 8},     {"10", false, 10}, {"12", false, 12},   {"16", false, 16},
    {"20", false, 20}, {"1/8", true, 0.125}, {"3/16", true, 0.1875}, {"1/4", true, 0.25}, {"5/16", true, 0.3125},
    {"3/8", true, 0.375}, {"1/2", true, 0.5},
};
struct SetRow {
  const char* size;
  double s, t;  ///< socket, socket depth
};
constexpr SetRow kSetScrews[] = {  // ISO 4026
    {"M3", 1.5, 1.2}, {"M4", 2, 1.5},  {"M5", 2.5, 2},   {"M6", 3, 2},     {"M8", 4, 3},
    {"M10", 5, 4},    {"M12", 6, 4.8}, {"M16", 8, 6.4},  {"M20", 10, 8},
};

constexpr double kMetricLengths[] = {2,  2.5, 3,  4,  5,  6,  8,  10, 12, 14, 16, 18, 20,  22,  25,  28,  30,  35,  40, 45,
                                     50, 55,  60, 65, 70, 75, 80, 90, 100, 110, 120, 130, 140, 150, 160, 180, 200};
constexpr double kInchLengths[] = {0.125, 0.1875, 0.25, 0.3125, 0.375, 0.5, 0.625, 0.75, 0.875, 1, 1.25, 1.5,
                                   1.75,  2,      2.25, 2.5,    2.75,  3,   3.5,   4,    4.5,   5, 6,    8};

bool isInchSize(const std::string& size) { return !size.empty() && size[0] != 'M' && (size[0] == '#' || size.find('/') != std::string::npos || size == "1"); }

double nominalOf(const std::string& size, bool* ok) {
  *ok = true;
  if (const ThreadSpec* t = findThread(threadDesignationFor(size))) return t->nominal;
  *ok = false;
  return 0;
}

template <class Row>
const Row* findRow(const Row* begin, const Row* end, const std::string& size) {
  for (const Row* r = begin; r != end; ++r)
    if (size == r->size) return r;
  return nullptr;
}

TopoDS_Shape hexPrism(double acrossFlats, double z0, double h) {
  const double rc = acrossFlats / std::sqrt(3.0);  // circumradius
  BRepBuilderAPI_MakePolygon poly;
  for (int i = 0; i < 6; ++i) {
    const double a = M_PI / 6.0 + i * M_PI / 3.0;  // flats parallel to X
    poly.Add(gp_Pnt(rc * std::cos(a), rc * std::sin(a), z0));
  }
  poly.Close();
  BRepBuilderAPI_MakeFace f(poly.Wire(), Standard_True);
  return BRepPrimAPI_MakePrism(f.Face(), gp_Vec(0, 0, h)).Shape();
}

TopoDS_Shape cylinder(double dia, double z0, double h) {
  return BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, z0), gp_Dir(0, 0, 1)), dia / 2.0, h).Shape();
}

TopoDS_Shape fuse(const TopoDS_Shape& a, const TopoDS_Shape& b) {
  BRepAlgoAPI_Fuse op(a, b);
  op.Build();
  if (!op.IsDone()) return {};
  ShapeUpgrade_UnifySameDomain u(op.Shape(), Standard_True, Standard_True, Standard_False);
  u.Build();
  return u.Shape();
}

TopoDS_Shape cut(const TopoDS_Shape& a, const TopoDS_Shape& b) {
  BRepAlgoAPI_Cut op(a, b);
  op.Build();
  return op.IsDone() ? op.Shape() : TopoDS_Shape();
}

double hexArea(double s) { return std::sqrt(3.0) / 2.0 * s * s; }
double circleArea(double d) { return M_PI * d * d / 4.0; }

}  // namespace

const std::vector<std::string>& familyNames() {
  static const std::vector<std::string> k{"Socket head cap screw (ISO 4762 / B18.3)",
                                          "Hex bolt (ISO 4017 / B18.2.1)",
                                          "Hex nut (ISO 4032 / B18.2.2)",
                                          "Nyloc nut (ISO 7040)",
                                          "Washer (ISO 7089 / B18.22.1)",
                                          "Small washer (ISO 7092)",
                                          "Heat-set insert",
                                          "Dowel pin (ISO 2338 / B18.8.2)",
                                          "Set screw (ISO 4026)"};
  return k;
}

const std::vector<std::string>& familyKeys() {
  static const std::vector<std::string> k{"shcs", "hexbolt", "hexnut", "nyloc", "washer", "smallwasher", "insert", "dowel", "setscrew"};
  return k;
}

std::optional<Family> familyFromKey(const std::string& keyIn) {
  std::string key;
  for (char c : keyIn) key += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  const auto& k = familyKeys();
  for (size_t i = 0; i < k.size(); ++i)
    if (k[i] == key) return static_cast<Family>(i);
  if (key == "hex" || key == "bolt") return Family::HexBolt;
  if (key == "nut") return Family::HexNut;
  if (key == "pin") return Family::DowelPin;
  return std::nullopt;
}

bool familyHasLength(Family f) {
  return f == Family::SocketCap || f == Family::HexBolt || f == Family::DowelPin || f == Family::SetScrew;
}
bool familyIsScrew(Family f) { return f == Family::SocketCap || f == Family::HexBolt || f == Family::SetScrew; }

const std::vector<Grade>& grades() {
  static const std::vector<Grade> k{{"8.8", "Steel", 7.85},          {"10.9", "Steel", 7.85}, {"12.9", "Steel", 7.85},
                                    {"A2", "Stainless Steel", 7.90}, {"A4", "Stainless Steel", 8.00},
                                    {"Brass", "Brass", 8.50},        {"Nylon", "Nylon 6/6", 1.14}};
  return k;
}

const Grade* findGrade(const std::string& key) {
  for (const Grade& g : grades())
    if (key == g.key) return &g;
  std::string l;
  for (char c : key) l += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  for (const Grade& g : grades()) {
    std::string gk;
    for (const char* p = g.key; *p; ++p) gk += static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
    if (gk == l) return &g;
  }
  return nullptr;
}

std::string threadDesignationFor(const std::string& size) {
  if (const ThreadSpec* t = findThread(size)) return t->designation;
  return size;
}

std::vector<std::string> familySizes(Family f) {
  std::vector<std::string> out;
  auto heads = [&](HeadType h) {
    for (const ThreadSpec& t : isoMetricThreads())
      if (t.coarse && headFor(h, t)) out.push_back(sizeOf(t.designation));
    for (const ThreadSpec& t : inchThreads())
      if (t.coarse && headFor(h, t)) out.push_back(sizeOf(t.designation));
  };
  switch (f) {
    case Family::SocketCap: heads(HeadType::SocketCap); break;
    case Family::HexBolt: heads(HeadType::HexBolt); break;
    case Family::HexNut: for (const auto& r : kHexNuts) out.push_back(r.size); break;
    case Family::NylocNut: for (const auto& r : kNylocNuts) out.push_back(r.size); break;
    case Family::Washer: for (const auto& r : kWashers) out.push_back(r.size); break;
    case Family::SmallWasher: for (const auto& r : kSmallWashers) out.push_back(r.size); break;
    case Family::HeatSetInsert: for (const auto& r : kInserts) out.push_back(r.size); break;
    case Family::DowelPin: for (const auto& r : kPins) out.push_back(r.size); break;
    case Family::SetScrew: for (const auto& r : kSetScrews) out.push_back(r.size); break;
  }
  return out;
}

std::vector<double> familyLengths(Family f, const std::string& size) {
  if (!familyHasLength(f)) return {};
  const bool inch = isInchSize(size);
  double d = 0;
  if (f == Family::DowelPin) {
    for (const auto& r : kPins)
      if (size == r.size) d = r.inch ? r.d * kInch : r.d;
  } else {
    bool ok = false;
    d = nominalOf(size, &ok);
  }
  if (d <= 0) return {};
  double lo = 0, hi = 0;
  switch (f) {
    case Family::SocketCap: lo = 1.5 * d; hi = 12 * d; break;
    case Family::HexBolt: lo = 2 * d; hi = 15 * d; break;
    case Family::DowelPin: lo = 2 * d; hi = 10 * d; break;
    default: lo = d; hi = 6 * d; break;  // set screws
  }
  std::vector<double> out;
  if (inch) {
    for (double l : kInchLengths)
      if (l * kInch >= lo - 1e-6 && l * kInch <= hi + 1e-6) out.push_back(l * kInch);
  } else {
    for (double l : kMetricLengths)
      if (l >= lo - 1e-6 && l <= hi + 1e-6) out.push_back(l);
  }
  return out;
}

std::optional<FastenerDims> fastenerDims(const FastenerSpec& s, std::string* error) {
  auto fail = [&](std::string msg) -> std::optional<FastenerDims> {
    if (error) *error = std::move(msg);
    return std::nullopt;
  };
  FastenerDims d;
  const bool inch = isInchSize(s.size);
  const double u = inch ? kInch : 1.0;
  const ThreadSpec* thread = s.family == Family::DowelPin ? nullptr : findThread(threadDesignationFor(s.size));
  if (s.family != Family::DowelPin && !thread) return fail(std::format("unknown size {}", s.size));
  if (thread) {
    d.d = thread->nominal;
    d.bore = thread->minorDiameter();
  }
  if (familyHasLength(s.family)) {
    if (s.length <= 0) return fail("length must be positive");
    d.length = s.length;
  }
  switch (s.family) {
    case Family::SocketCap:
    case Family::HexBolt: {
      auto h = headFor(s.family == Family::SocketCap ? HeadType::SocketCap : HeadType::HexBolt, *thread);
      if (!h) return fail(std::format("no {} in size {}", familyDescription(s.family), s.size));
      d.headDia = h->dk;
      d.headHeight = h->k;
      d.wrench = h->socket;
      if (s.family == Family::SocketCap) d.socketDepth = std::min(0.5 * d.d, 0.6 * h->k);
      return d;
    }
    case Family::HexNut:
    case Family::NylocNut: {
      const NutRow* r = s.family == Family::HexNut ? findRow(std::begin(kHexNuts), std::end(kHexNuts), s.size)
                                                  : findRow(std::begin(kNylocNuts), std::end(kNylocNuts), s.size);
      if (!r) return fail(std::format("no {} in size {}", familyDescription(s.family), s.size));
      d.wrench = r->s * u;
      d.headDia = d.wrench * 2.0 / std::sqrt(3.0);
      d.headHeight = r->m * u;
      if (s.family == Family::NylocNut) {
        const NutRow* plain = findRow(std::begin(kHexNuts), std::end(kHexNuts), s.size);
        d.hexHeight = std::min(plain ? plain->m * u : 0.8 * d.headHeight, 0.8 * d.headHeight);
      }
      return d;
    }
    case Family::Washer:
    case Family::SmallWasher: {
      const WasherRow* r = s.family == Family::Washer ? findRow(std::begin(kWashers), std::end(kWashers), s.size)
                                                     : findRow(std::begin(kSmallWashers), std::end(kSmallWashers), s.size);
      if (!r) return fail(std::format("no {} in size {}", familyDescription(s.family), s.size));
      d.bore = r->d1 * u;
      d.headDia = r->d2 * u;
      d.headHeight = r->h * u;
      return d;
    }
    case Family::HeatSetInsert: {
      const InsertRow* r = findRow(std::begin(kInserts), std::end(kInserts), s.size);
      if (!r) return fail(std::format("no heat-set insert in size {}", s.size));
      d.headDia = r->outer;
      d.length = s.shortInsert ? r->shortLength : r->length;
      d.recommendedHole = r->hole;
      return d;
    }
    case Family::DowelPin: {
      const PinRow* r = findRow(std::begin(kPins), std::end(kPins), s.size);
      if (!r) return fail(std::format("no dowel pin of diameter {}", s.size));
      d.d = r->d * (r->inch ? kInch : 1.0);
      return d;
    }
    case Family::SetScrew: {
      const SetRow* r = findRow(std::begin(kSetScrews), std::end(kSetScrews), s.size);
      if (!r) return fail(std::format("no set screw in size {}", s.size));
      d.wrench = r->s;
      d.socketDepth = std::min(r->t, 0.8 * s.length);
      return d;
    }
  }
  return fail("unknown family");
}

TopoDS_Shape makeFastener(const FastenerSpec& s, std::string* error) {
  auto dims = fastenerDims(s, error);
  if (!dims) return {};
  const FastenerDims& d = *dims;
  try {
    TopoDS_Shape out;
    switch (s.family) {
      case Family::SocketCap: {
        TopoDS_Shape head = cut(cylinder(d.headDia, 0, d.headHeight), hexPrism(d.wrench, d.headHeight - d.socketDepth, d.socketDepth + 1.0));
        out = fuse(head, cylinder(d.d, -d.length, d.length));
        break;
      }
      case Family::HexBolt: out = fuse(hexPrism(d.wrench, 0, d.headHeight), cylinder(d.d, -d.length, d.length)); break;
      case Family::HexNut: out = cut(hexPrism(d.wrench, 0, d.headHeight), cylinder(d.bore, -1, d.headHeight + 2)); break;
      case Family::NylocNut: {
        TopoDS_Shape body = fuse(hexPrism(d.wrench, 0, d.hexHeight), cylinder(0.95 * d.wrench, d.hexHeight, d.headHeight - d.hexHeight));
        out = cut(body, cylinder(d.bore, -1, d.headHeight + 2));
        break;
      }
      case Family::Washer:
      case Family::SmallWasher: out = cut(cylinder(d.headDia, 0, d.headHeight), cylinder(d.bore, -1, d.headHeight + 2)); break;
      case Family::HeatSetInsert: out = cut(cylinder(d.headDia, -d.length, d.length), cylinder(d.bore, -d.length - 1, d.length + 2)); break;
      case Family::DowelPin: out = cylinder(d.d, -d.length / 2.0, d.length); break;
      case Family::SetScrew:
        out = cut(cylinder(d.d, -d.length, d.length), hexPrism(d.wrench, -d.socketDepth, d.socketDepth + 1.0));
        break;
    }
    if (out.IsNull() && error) *error = "fastener construction failed";
    return out;
  } catch (const Standard_Failure& e) {
    if (error) *error = e.GetMessageString() ? e.GetMessageString() : "fastener construction failed";
    return {};
  }
}

double nominalVolume(const FastenerSpec& s) {
  auto dims = fastenerDims(s);
  if (!dims) return 0;
  const FastenerDims& d = *dims;
  switch (s.family) {
    case Family::SocketCap:
      return circleArea(d.headDia) * d.headHeight - hexArea(d.wrench) * d.socketDepth + circleArea(d.d) * d.length;
    case Family::HexBolt: return hexArea(d.wrench) * d.headHeight + circleArea(d.d) * d.length;
    case Family::HexNut: return (hexArea(d.wrench) - circleArea(d.bore)) * d.headHeight;
    case Family::NylocNut:
      return hexArea(d.wrench) * d.hexHeight + circleArea(0.95 * d.wrench) * (d.headHeight - d.hexHeight) -
             circleArea(d.bore) * d.headHeight;
    case Family::Washer:
    case Family::SmallWasher: return (circleArea(d.headDia) - circleArea(d.bore)) * d.headHeight;
    case Family::HeatSetInsert: return (circleArea(d.headDia) - circleArea(d.bore)) * d.length;
    case Family::DowelPin: return circleArea(d.d) * d.length;
    case Family::SetScrew: return circleArea(d.d) * d.length - hexArea(d.wrench) * d.socketDepth;
  }
  return 0;
}

double stackHeight(const FastenerSpec& s) {
  auto dims = fastenerDims(s);
  if (!dims) return 0;
  switch (s.family) {
    case Family::SocketCap:
    case Family::HexBolt:
    case Family::HexNut:
    case Family::NylocNut:
    case Family::Washer:
    case Family::SmallWasher: return dims->headHeight;
    default: return 0;
  }
}

std::string standardName(Family f, bool inch) {
  switch (f) {
    case Family::SocketCap: return inch ? "ASME B18.3" : "ISO 4762";
    case Family::HexBolt: return inch ? "ASME B18.2.1" : "ISO 4017";
    case Family::HexNut: return inch ? "ASME B18.2.2" : "ISO 4032";
    case Family::NylocNut: return inch ? "ASME B18.16.6" : "ISO 7040";
    case Family::Washer: return inch ? "ASME B18.22.1" : "ISO 7089";
    case Family::SmallWasher: return "ISO 7092";
    case Family::HeatSetInsert: return "Heat-set insert";
    case Family::DowelPin: return inch ? "ASME B18.8.2" : "ISO 2338";
    case Family::SetScrew: return "ISO 4026";
  }
  return {};
}

std::string familyDescription(Family f) {
  switch (f) {
    case Family::SocketCap: return "Socket head cap screw";
    case Family::HexBolt: return "Hex bolt";
    case Family::HexNut: return "Hex nut";
    case Family::NylocNut: return "Nyloc nut";
    case Family::Washer: return "Plain washer";
    case Family::SmallWasher: return "Small washer";
    case Family::HeatSetInsert: return "Heat-set insert";
    case Family::DowelPin: return "Dowel pin";
    case Family::SetScrew: return "Set screw, flat point";
  }
  return {};
}

std::string partNumber(const FastenerSpec& s) {
  const bool inch = isInchSize(s.size);
  const std::string stdName = standardName(s.family, inch);
  auto len = [&] {
    if (inch) return inchText(s.length / kInch);
    return std::format("{:g}", std::round(s.length * 100) / 100);
  };
  std::string grade = s.grade;
  if (inch && (grade == "8.8" || grade == "10.9" || grade == "12.9") && s.family == Family::SocketCap) grade = "ASTM A574";
  std::string size = s.family == Family::DowelPin ? (inch ? s.size + "\"" : s.size + " m6") : s.size;
  if (inch && familyIsScrew(s.family)) size = threadDesignationFor(s.size);
  switch (s.family) {
    case Family::SocketCap:
    case Family::HexBolt:
    case Family::SetScrew: return std::format("{} {}{}{} {}", stdName, size, inch ? " x " : "x", len(), grade);
    case Family::DowelPin: return std::format("{} {} x {} {}", stdName, size, len(), grade);
    case Family::HeatSetInsert: return std::format("Heat-set insert {} {} {}", s.size, s.shortInsert ? "short" : "standard", grade);
    default: return std::format("{} {} {}", stdName, size, grade);
  }
}

std::string sizeForHoleDiameter(double holeDia, bool inch) {
  const auto& table = inch ? inchThreads() : isoMetricThreads();
  for (const ThreadSpec& t : table) {
    if (!t.coarse) continue;
    for (Fit f : {Fit::Normal, Fit::Close, Fit::Loose})
      if (std::abs(clearanceDiameter(t, f) - holeDia) < 0.05) return sizeOf(t.designation);
  }
  for (const ThreadSpec& t : table)
    if (t.coarse && std::abs(t.tapDrill() - holeDia) < 0.05) return sizeOf(t.designation);
  return {};
}

double nextStandardLength(Family f, const std::string& size, double minLength) {
  const std::vector<double> ls = familyLengths(f, size);
  for (double l : ls)
    if (l >= minLength - 1e-6) return l;
  return ls.empty() ? minLength : ls.back();
}

double threadLength(const FastenerSpec& s) {
  auto dims = fastenerDims(s);
  if (!dims) return 0;
  if (s.family == Family::SetScrew || s.family == Family::HexBolt) return s.length;  // ISO 4017 / 4026: full thread
  if (s.family != Family::SocketCap) return 0;
  const double b = 2 * dims->d + 12;
  return s.length <= b + 2 * dims->d ? s.length : b;
}

}  // namespace fabricad::part::standards
