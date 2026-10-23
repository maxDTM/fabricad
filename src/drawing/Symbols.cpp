// FabriCAD drawing: drafting symbols as geometry, rich text lines, toleranced dimension text.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "drawing/Symbols.h"

#include "drawing/Dimension.h"
#include "drawing/SheetRenderer.h"

#include <QCoreApplication>
#include <QFont>
#include <QFontMetricsF>
#include <QGuiApplication>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>

namespace fabricad::drawing {

// ---------------------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------------------

namespace {
struct CharName {
  GdtCharacteristic c;
  const char* name;
};
const CharName kCharNames[] = {
    {GdtCharacteristic::Straightness, "Straightness"},   {GdtCharacteristic::Flatness, "Flatness"},
    {GdtCharacteristic::Circularity, "Circularity"},     {GdtCharacteristic::Cylindricity, "Cylindricity"},
    {GdtCharacteristic::ProfileLine, "ProfileLine"},     {GdtCharacteristic::ProfileSurface, "ProfileSurface"},
    {GdtCharacteristic::Angularity, "Angularity"},       {GdtCharacteristic::Perpendicularity, "Perpendicularity"},
    {GdtCharacteristic::Parallelism, "Parallelism"},     {GdtCharacteristic::Position, "Position"},
    {GdtCharacteristic::Concentricity, "Concentricity"}, {GdtCharacteristic::Symmetry, "Symmetry"},
    {GdtCharacteristic::CircularRunout, "CircularRunout"}, {GdtCharacteristic::TotalRunout, "TotalRunout"},
};
std::string lowerAlnum(const std::string& s) {
  std::string o;
  for (char c : s)
    if (std::isalnum(static_cast<unsigned char>(c))) o += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return o;
}
}  // namespace

const char* gdtCharacteristicName(GdtCharacteristic c) {
  for (const auto& n : kCharNames)
    if (n.c == c) return n.name;
  return "Position";
}

bool gdtCharacteristicFromName(const std::string& s, GdtCharacteristic& out) {
  std::string l = lowerAlnum(s);
  static const std::map<std::string, GdtCharacteristic> aliases = {
      {"roundness", GdtCharacteristic::Circularity}, {"perp", GdtCharacteristic::Perpendicularity},
      {"parallel", GdtCharacteristic::Parallelism},  {"pos", GdtCharacteristic::Position},
      {"truepos", GdtCharacteristic::Position},      {"runout", GdtCharacteristic::CircularRunout},
      {"profile", GdtCharacteristic::ProfileSurface}, {"coaxiality", GdtCharacteristic::Concentricity},
      {"angle", GdtCharacteristic::Angularity},      {"flat", GdtCharacteristic::Flatness},
  };
  for (const auto& n : kCharNames)
    if (lowerAlnum(n.name) == l) {
      out = n.c;
      return true;
    }
  auto it = aliases.find(l);
  if (it == aliases.end()) return false;
  out = it->second;
  return true;
}

std::vector<GdtCharacteristic> gdtCharacteristics() {
  std::vector<GdtCharacteristic> v;
  for (const auto& n : kCharNames) v.push_back(n.c);
  return v;
}

bool gdtNeedsDatum(GdtCharacteristic c) {
  switch (c) {
    case GdtCharacteristic::Straightness:
    case GdtCharacteristic::Flatness:
    case GdtCharacteristic::Circularity:
    case GdtCharacteristic::Cylindricity:
    case GdtCharacteristic::ProfileLine:
    case GdtCharacteristic::ProfileSurface: return false;
    default: return true;
  }
}

const char* gdtModifierName(GdtModifier m) {
  switch (m) {
    case GdtModifier::None: return "None";
    case GdtModifier::MMC: return "MMC";
    case GdtModifier::LMC: return "LMC";
    case GdtModifier::FreeState: return "FreeState";
    case GdtModifier::TangentPlane: return "TangentPlane";
    case GdtModifier::ProjectedZone: return "ProjectedZone";
  }
  return "None";
}

GdtModifier gdtModifierFromName(const std::string& s) {
  std::string l = lowerAlnum(s);
  if (l == "mmc" || l == "m") return GdtModifier::MMC;
  if (l == "lmc" || l == "l") return GdtModifier::LMC;
  if (l == "freestate" || l == "f") return GdtModifier::FreeState;
  if (l == "tangentplane" || l == "t") return GdtModifier::TangentPlane;
  if (l == "projectedzone" || l == "p") return GdtModifier::ProjectedZone;
  return GdtModifier::None;
}

const char* gdtModifierLetter(GdtModifier m) {
  switch (m) {
    case GdtModifier::MMC: return "M";
    case GdtModifier::LMC: return "L";
    case GdtModifier::FreeState: return "F";
    case GdtModifier::TangentPlane: return "T";
    case GdtModifier::ProjectedZone: return "P";
    default: return "";
  }
}

Glyph glyphFor(GdtCharacteristic c) {
  return static_cast<Glyph>(static_cast<int>(Glyph::Straightness) + static_cast<int>(c));
}

Glyph glyphFor(GdtModifier m) {
  switch (m) {
    case GdtModifier::MMC: return Glyph::ModifierM;
    case GdtModifier::LMC: return Glyph::ModifierL;
    case GdtModifier::FreeState: return Glyph::ModifierF;
    case GdtModifier::TangentPlane: return Glyph::ModifierT;
    case GdtModifier::ProjectedZone: return Glyph::ModifierP;
    default: return Glyph::None;
  }
}

// ---------------------------------------------------------------------------------------
// Glyph geometry
// ---------------------------------------------------------------------------------------

namespace {
bool isModifierGlyph(Glyph g) { return g >= Glyph::ModifierM && g <= Glyph::ModifierP; }
const char* modifierLetter(Glyph g) {
  switch (g) {
    case Glyph::ModifierM: return "M";
    case Glyph::ModifierL: return "L";
    case Glyph::ModifierF: return "F";
    case Glyph::ModifierT: return "T";
    case Glyph::ModifierP: return "P";
    default: return "";
  }
}
}  // namespace

double glyphWidth(Glyph g, double h) {
  if (g == Glyph::None) return 0;
  if (isModifierGlyph(g)) return 1.6 * h;
  if (g == Glyph::Depth) return 1.0 * h;
  return 1.45 * h;
}

void addGlyph(DisplayList& out, Glyph g, const Point2& c, double s, LineClass cls) {
  // unit coordinates in [-0.5, 0.5], sheet orientation (y down)
  auto P = [&](double x, double y) { return Point2{c.x + x * s, c.y + y * s}; };
  auto seg = [&](double x1, double y1, double x2, double y2) { out.curves.push_back(Curve2D::segment(P(x1, y1), P(x2, y2), cls)); };
  auto circ = [&](double x, double y, double r) { out.curves.push_back(Curve2D::circle(P(x, y), r * s, cls)); };
  auto arrowTip = [&](Point2 tip, Point2 dir) {
    Point2 d = dir.normalized(), n = d.perp();
    Point2 base = tip - d * (0.3 * s);
    out.fills.push_back({{tip, base + n * (0.11 * s), base - n * (0.11 * s)}, cls});
  };
  switch (g) {
    case Glyph::None: break;
    case Glyph::Straightness: seg(-0.5, 0, 0.5, 0); break;
    case Glyph::Flatness:
      seg(-0.5, 0.25, 0.2, 0.25);
      seg(0.2, 0.25, 0.5, -0.25);
      seg(0.5, -0.25, -0.2, -0.25);
      seg(-0.2, -0.25, -0.5, 0.25);
      break;
    case Glyph::Circularity: circ(0, 0, 0.4); break;
    case Glyph::Cylindricity: {
      circ(0, 0, 0.3);
      // two tangents at 60 degrees
      Point2 d = Point2{std::cos(deg2rad(60)), -std::sin(deg2rad(60))};
      Point2 n = d.perp();
      for (double sgn : {-1.0, 1.0}) {
        Point2 m = n * (0.3 * sgn);
        seg(m.x - d.x * 0.55, m.y - d.y * 0.55, m.x + d.x * 0.55, m.y + d.y * 0.55);
      }
      break;
    }
    case Glyph::ProfileLine: out.curves.push_back(Curve2D::arc(P(0, 0.2), 0.45 * s, 180, 180, cls)); break;
    case Glyph::ProfileSurface:
      out.curves.push_back(Curve2D::arc(P(0, 0.2), 0.45 * s, 180, 180, cls));
      seg(-0.45, 0.2, 0.45, 0.2);
      break;
    case Glyph::Angularity:
      seg(-0.45, 0.4, 0.5, 0.4);
      seg(-0.45, 0.4, 0.4, -0.4);
      break;
    case Glyph::Perpendicularity:
      seg(-0.45, 0.4, 0.45, 0.4);
      seg(0, 0.4, 0, -0.45);
      break;
    case Glyph::Parallelism:
      seg(-0.4, 0.45, -0.05, -0.45);
      seg(0.05, 0.45, 0.4, -0.45);
      break;
    case Glyph::Position:
      circ(0, 0, 0.3);
      seg(-0.5, 0, 0.5, 0);
      seg(0, -0.5, 0, 0.5);
      break;
    case Glyph::Concentricity:
      circ(0, 0, 0.2);
      circ(0, 0, 0.42);
      break;
    case Glyph::Symmetry:
      seg(-0.5, 0, 0.5, 0);
      seg(-0.28, -0.25, 0.28, -0.25);
      seg(-0.28, 0.25, 0.28, 0.25);
      break;
    case Glyph::CircularRunout:
      seg(-0.25, 0.45, 0.2, -0.3);
      arrowTip(P(0.3, -0.47), Point2{0.45, -0.75});
      break;
    case Glyph::TotalRunout:
      seg(-0.5, 0.45, 0.25, 0.45);
      seg(-0.5, 0.45, -0.18, -0.15);
      seg(-0.05, 0.45, 0.27, -0.15);
      arrowTip(P(-0.08, -0.45), Point2{0.32, -0.6});
      arrowTip(P(0.37, -0.45), Point2{0.32, -0.6});
      break;
    case Glyph::ModifierM:
    case Glyph::ModifierL:
    case Glyph::ModifierF:
    case Glyph::ModifierT:
    case Glyph::ModifierP:
      circ(0, 0, 0.5);
      out.texts.push_back({c, modifierLetter(g), s * 0.55, 0, HAlign::Center, VAlign::Middle, cls});
      break;
    case Glyph::Depth:
      // vertical stroke ending in a V on a horizontal bar (ASME Y14.5 depth symbol)
      seg(0, -0.5, 0, 0.45);
      seg(-0.18, 0.18, 0, 0.45);
      seg(0.18, 0.18, 0, 0.45);
      seg(-0.3, 0.45, 0.3, 0.45);
      break;
    case Glyph::Counterbore:
      seg(-0.42, -0.4, -0.42, 0.4);
      seg(-0.42, 0.4, 0.42, 0.4);
      seg(0.42, 0.4, 0.42, -0.4);
      break;
    case Glyph::Countersink:
      seg(-0.45, -0.4, 0, 0.4);
      seg(0, 0.4, 0.45, -0.4);
      break;
  }
}

std::string richLinePlainText(const RichLine& line) {
  std::string s;
  for (const auto& r : line) {
    if (r.glyph == Glyph::None) {
      s += r.text;
      continue;
    }
    switch (r.glyph) {
      case Glyph::Depth: s += "DEEP "; break;
      case Glyph::Counterbore: s += "CBORE "; break;
      case Glyph::Countersink: s += "CSK "; break;
      default:
        if (isModifierGlyph(r.glyph)) s += std::string("(") + modifierLetter(r.glyph) + ")";
        else s += gdtCharacteristicName(static_cast<GdtCharacteristic>(static_cast<int>(r.glyph) - static_cast<int>(Glyph::Straightness)));
        break;
    }
  }
  return s;
}

// ---------------------------------------------------------------------------------------
// Text measuring and rich lines
// ---------------------------------------------------------------------------------------

double measureText(const std::string& text, double height, const std::string& fontFamily) {
  if (text.empty() || height <= 0) return 0;
  if (!qobject_cast<QGuiApplication*>(QCoreApplication::instance())) return SheetRenderer::textWidth(text, height);
  // same font setup as SheetRenderer::paintText (pixel size 100, scaled by the cap height)
  static std::mutex mutex;
  std::lock_guard<std::mutex> lock(mutex);
  QFont font(QString::fromStdString(fontFamily));
  font.setCapitalization(QFont::MixedCase);  // typed case on every device (DRW2-ASM-OUTPUT-5, DRW2-VIEWS-ANNOT-5)
  font.setPixelSize(100);
  QFontMetricsF fm(font);
  double cap = fm.capHeight() > 0 ? fm.capHeight() : fm.ascent() * 0.72;
  return fm.horizontalAdvance(QString::fromUtf8(text.c_str())) * height / cap;
}

double richLineWidth(const RichLine& line, double height, const std::string& fontFamily) {
  double w = 0;
  for (const auto& r : line) w += r.glyph == Glyph::None ? measureText(r.text, height, fontFamily) : glyphWidth(r.glyph, height);
  return w;
}

double addRichLine(DisplayList& out, const RichLine& line, const Point2& leftMid, double height, LineClass cls,
                   const std::string& fontFamily) {
  double x = leftMid.x;
  for (const auto& r : line) {
    if (r.glyph == Glyph::None) {
      if (!r.text.empty()) out.texts.push_back({{x, leftMid.y}, r.text, height, 0, HAlign::Left, VAlign::Middle, cls});
      x += measureText(r.text, height, fontFamily);
    } else {
      double w = glyphWidth(r.glyph, height);
      double size = isModifierGlyph(r.glyph) ? 1.3 * height : 1.05 * height;
      addGlyph(out, r.glyph, {x + w * 0.5, leftMid.y}, size, cls);
      x += w;
    }
  }
  return x - leftMid.x;
}

// ---------------------------------------------------------------------------------------
// Toleranced dimension text
// ---------------------------------------------------------------------------------------

DimTextParts dimensionTextParts(const Dimension& dim, double value, const core::UnitSystem& units) {
  DimTextParts p;
  int prec = dim.precision >= 0 ? dim.precision : units.precision;
  p.prefix = dim.prefix;
  if (dim.type == DimensionType::Angle) {
    p.angle = true;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", prec, value);
    p.number = buf;
    p.suffix = "\xC2\xB0" + dim.suffix;
    return p;
  }
  p.number = formatValue(core::toDisplayValue(core::Quantity::mm(value), units), prec);
  if (dim.type == DimensionType::Diameter) p.prefix += "\xC3\x98";
  else if (dim.type == DimensionType::Radius) p.prefix += "R";
  p.suffix = dim.suffix;
  return p;
}

void addToleranceText(DisplayList& out, const Dimension& dim, double value, const Point2& center, double angleDeg,
                      const DrawingStyle& style, const core::UnitSystem& units) {
  const double h = style.dimTextHeight;
  std::string mainText = formatDimensionText(dim, units);
  if (!dim.tolerance.isSet()) {
    out.texts.push_back({center, mainText, h, angleDeg, HAlign::Center, VAlign::Middle, LineClass::Dimension});
    return;
  }
  DimTextParts parts = dimensionTextParts(dim, value, units);
  int prec = dim.precision >= 0 ? dim.precision : units.precision;
  ToleranceText tt = composeTolerance(dim.tolerance, value, dim.overrideText.empty() ? parts.joined() : dim.overrideText,
                                      parts.prefix, parts.suffix, !parts.angle, prec, units);
  const std::string& font = style.fontFamily;
  const double th = h * std::clamp(dim.tolerance.textScale, 0.3, 1.0);
  // block: [main][after][stacked]; limits: [stacked full size]
  double wMain = tt.stackedMain ? 0.0 : measureText(tt.main + tt.after, h, font);
  double wMainOnly = tt.stackedMain ? 0.0 : measureText(tt.main, h, font);
  double stackH = tt.stackedMain ? h : th;
  double wStack = tt.stacked() ? std::max(measureText(tt.upper, stackH, font), measureText(tt.lower, stackH, font)) : 0.0;
  double gap = tt.stacked() && !tt.stackedMain ? 0.4 * h : 0.0;
  double frame = tt.framed ? 0.5 * h : 0.0;
  double W = wMain + gap + wStack + 2 * frame;
  // local frame: along = reading direction, up = visual up (sheet y is down)
  Point2 along = Point2::polar(-angleDeg, 1.0);
  Point2 up{along.y, -along.x};
  Point2 left = center - along * (W * 0.5);
  auto at = [&](double x, double y) { return left + along * x + up * y; };
  if (!tt.stackedMain) {
    out.texts.push_back({at(frame, 0), tt.main + tt.after, h, angleDeg, HAlign::Left, VAlign::Middle, LineClass::Dimension});
    if (tt.framed) {
      double fw = wMainOnly + 2 * frame, fh = h * 1.8;
      Point2 a = at(0, fh / 2), b = at(fw, fh / 2), c2 = at(fw, -fh / 2), d = at(0, -fh / 2);
      out.curves.push_back(Curve2D::polyline({a, b, c2, d, a}, LineClass::Dimension));
    }
  }
  if (tt.stacked()) {
    double x = frame + wMain + gap;
    double dy = tt.stackedMain ? 0.75 * h : 0.55 * h;
    if (!tt.upper.empty()) out.texts.push_back({at(x, dy), tt.upper, stackH, angleDeg, HAlign::Left, VAlign::Middle, LineClass::Dimension});
    if (!tt.lower.empty()) out.texts.push_back({at(x, -dy), tt.lower, stackH, angleDeg, HAlign::Left, VAlign::Middle, LineClass::Dimension});
  }
}

}  // namespace fabricad::drawing
