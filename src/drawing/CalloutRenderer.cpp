// FabriCAD drawing: layout of hole callouts, GD&T frames, datum symbols, surface texture
// symbols, ordinate dimensions and revision tables (sheet mm, Y down).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "drawing/CalloutRenderer.h"

#include "drawing/DrawingView.h"
#include "drawing/Sheet.h"
#include "drawing/Tolerance.h"

#include <algorithm>
#include <cmath>

namespace fabricad::drawing {

void CalloutRenderer::leaderArrow(DisplayList& out, const Point2& from, const Point2& tip) const {
  if (from.distance(tip) < 0.3) return;
  out.curves.push_back(Curve2D::segment(from, tip, LineClass::Dimension));
  auto a = arrowHead(tip, (tip - from).normalized(), style_);
  out.fills.push_back({{a[0], a[1], a[2]}, LineClass::Dimension});
}

// ---------------------------------------------------------------------------------------
// Hole callout
// ---------------------------------------------------------------------------------------

void CalloutRenderer::addHoleCallout(DisplayList& out, const Sheet& sheet, const HoleCallout& c) const {
  const DrawingView* view = sheet.view(c.viewId);
  const double h = style_.dimTextHeight;
  DimTarget t;
  if (view) t = resolveDimRef(c.ref, *view);
  if (!view || !t.ok || t.isVertex || t.curve.type != CurveType::Arc) {
    DisplayText e{c.position, "?", h * 1.4, 0, HAlign::Center, VAlign::Middle, LineClass::Dimension, true};
    e.error = true;
    out.texts.push_back(e);
    return;
  }
  Point2 center = view->toSheet(t.curve.center);
  double r = t.curve.radius * view->scale;
  Point2 dir = (c.position - center).normalized();
  if (dir.length() < 0.5) dir = Point2{1, -1}.normalized();
  Point2 tip = center + dir * r;
  const double side = c.position.x >= center.x ? 1.0 : -1.0;
  Point2 shoulderEnd = c.position + Point2{side * 2.5, 0};
  leaderArrow(out, c.position, tip);
  out.curves.push_back(Curve2D::segment(c.position, shoulderEnd, LineClass::Dimension));
  std::vector<RichLine> lines = c.lines;
  if (lines.empty()) lines = {{RichRun::t("\xC3\x98" + formatValue(2 * t.curve.radius, units_.precision))}};
  for (size_t i = 0; i < lines.size(); ++i) {
    double w = richLineWidth(lines[i], h, style_.fontFamily);
    double x = side > 0 ? shoulderEnd.x + 0.8 : shoulderEnd.x - 0.8 - w;
    double y = c.position.y + h * 1.6 * static_cast<double>(i);
    addRichLine(out, lines[i], {x, y}, h, LineClass::Annotation, style_.fontFamily);
  }
}

// ---------------------------------------------------------------------------------------
// Feature control frame
// ---------------------------------------------------------------------------------------

namespace {
RichLine toleranceRun(const FcfRow& row, const core::UnitSystem& units) {
  RichLine l;
  std::string v = row.toleranceText;
  if (v.empty()) v = formatValue(core::toDisplayValue(core::Quantity::mm(row.tolerance), units), 4);
  l.push_back(RichRun::t((row.diameterZone ? "\xC3\x98" : "") + v));
  if (row.modifier != GdtModifier::None) l.push_back(RichRun::g(glyphFor(row.modifier)));
  return l;
}
RichLine datumRun(const GdtDatumRef& d) {
  RichLine l{RichRun::t(d.letter)};
  if (d.modifier != GdtModifier::None) l.push_back(RichRun::g(glyphFor(d.modifier)));
  return l;
}
void rect(DisplayList& out, double x0, double y0, double x1, double y1, LineClass cls) {
  out.curves.push_back(Curve2D::polyline({{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}, {x0, y0}}, cls));
}
}  // namespace

double CalloutRenderer::frameRowWidth(const FcfRow& row) const {
  const double h = style_.dimTextHeight, H = 2 * h;
  double w = H + richLineWidth(toleranceRun(row, units_), h, style_.fontFamily) + h;
  for (const auto& d : row.datums) w += std::max(H, richLineWidth(datumRun(d), h, style_.fontFamily) + h);
  return w;
}

void CalloutRenderer::addFeatureFrame(DisplayList& out, const Sheet& sheet, const FeatureControlFrame& f) const {
  const double h = style_.dimTextHeight, H = 2 * h;
  const Point2 o = f.position;
  const LineClass cls = LineClass::Annotation;
  double maxW = 0;
  const size_t n = f.rows.size();
  for (size_t r = 0; r < n; ++r) {
    const FcfRow& row = f.rows[r];
    double y0 = o.y + H * static_cast<double>(r), y1 = y0 + H;
    // composite frames share the characteristic cell
    bool sameAsPrev = r > 0 && f.rows[r - 1].characteristic == row.characteristic;
    if (!sameAsPrev) {
      size_t span = 1;
      while (r + span < n && f.rows[r + span].characteristic == row.characteristic) ++span;
      double ye = y0 + H * static_cast<double>(span);
      rect(out, o.x, y0, o.x + H, ye, cls);
      addGlyph(out, glyphFor(row.characteristic), {o.x + H * 0.5, 0.5 * (y0 + ye)}, h * 1.25, cls);
    }
    double x = o.x + H;
    RichLine tol = toleranceRun(row, units_);
    double tw = richLineWidth(tol, h, style_.fontFamily) + h;
    rect(out, x, y0, x + tw, y1, cls);
    addRichLine(out, tol, {x + h * 0.5, 0.5 * (y0 + y1)}, h, cls, style_.fontFamily);
    x += tw;
    for (const auto& d : row.datums) {
      RichLine dl = datumRun(d);
      double lw = richLineWidth(dl, h, style_.fontFamily);
      double dw = std::max(H, lw + h);
      rect(out, x, y0, x + dw, y1, cls);
      addRichLine(out, dl, {x + (dw - lw) * 0.5, 0.5 * (y0 + y1)}, h, cls, style_.fontFamily);
      x += dw;
    }
    maxW = std::max(maxW, x - o.x);
  }
  if (!f.above.empty()) out.texts.push_back({{o.x, o.y - 0.6 * h}, f.above, h, 0, HAlign::Left, VAlign::Bottom, cls});
  if (!f.below.empty())
    out.texts.push_back({{o.x, o.y + H * static_cast<double>(n) + 0.6 * h}, f.below, h, 0, HAlign::Left, VAlign::Top, cls});
  if (f.leader) {
    Point2 tip = f.anchor.resolve(sheet);
    bool left = tip.x < o.x + maxW * 0.5;
    Point2 start{left ? o.x : o.x + maxW, o.y + H * 0.5};
    Point2 elbow = start + Point2{left ? -3.0 : 3.0, 0};
    out.curves.push_back(Curve2D::segment(start, elbow, LineClass::Dimension));
    leaderArrow(out, elbow, tip);
  }
}

// ---------------------------------------------------------------------------------------
// Datum feature symbol
// ---------------------------------------------------------------------------------------

void CalloutRenderer::addDatum(DisplayList& out, const Sheet& sheet, const DatumFeature& d) const {
  const double h = style_.dimTextHeight, S = 2 * h;
  const Point2 c = d.position;
  const Point2 a = d.anchor.resolve(sheet);
  rect(out, c.x - S / 2, c.y - S / 2, c.x + S / 2, c.y + S / 2, LineClass::Annotation);
  out.texts.push_back({c, d.letter, h, 0, HAlign::Center, VAlign::Middle, LineClass::Annotation});
  Point2 u = (c - a).normalized();  // from the feature towards the box
  if (u.length() < 0.5) u = {0, -1};
  // filled datum triangle: base on the feature, apex towards the box
  const double th = 0.9 * h, half = th / std::sqrt(3.0);
  Point2 n = u.perp();
  Point2 apex = a + u * th;
  out.fills.push_back({{a + n * half, a - n * half, apex}, LineClass::Annotation});
  // leader from the apex to the box edge
  double m = std::max(std::abs(u.x), std::abs(u.y));
  Point2 edge = c - u * (S / 2 / std::max(m, 1e-9));
  if (apex.distance(c) > S / 2) out.curves.push_back(Curve2D::segment(apex, edge, LineClass::Annotation));
}

// ---------------------------------------------------------------------------------------
// Surface texture
// ---------------------------------------------------------------------------------------

void CalloutRenderer::addSurfaceFinish(DisplayList& out, const Sheet& sheet, const SurfaceFinish& s) const {
  const double h = style_.dimTextHeight;
  const LineClass cls = LineClass::Annotation;
  const Point2 anchor = s.anchor.resolve(sheet);
  const Point2 T = s.leader ? s.position : anchor;
  if (s.leader) leaderArrow(out, T, anchor);
  const double H1 = 1.4 * h, H2 = 3.0 * h, k = 1.0 / std::tan(deg2rad(60.0));
  const Point2 shortEnd = T + Point2{-H1 * k, -H1};
  const Point2 longEnd = T + Point2{H2 * k, -H2};
  out.curves.push_back(Curve2D::polyline({shortEnd, T, longEnd}, cls));
  if (s.process == SurfaceProcess::RemovalRequired)
    out.curves.push_back(Curve2D::segment(shortEnd, T + Point2{H1 * k, -H1}, cls));
  else if (s.process == SurfaceProcess::RemovalProhibited)
    out.curves.push_back(Curve2D::circle(T + Point2{0, -2.0 * H1 / 3.0}, H1 / 3.0, cls));
  const std::string& font = style_.fontFamily;
  bool line = !s.roughness.empty() || !s.secondary.empty() || !s.method.empty();
  if (line) {
    double w = std::max({measureText(s.roughness, h, font), measureText(s.secondary, h, font), measureText(s.method, h, font)});
    out.curves.push_back(Curve2D::segment(longEnd, longEnd + Point2{w + 0.8 * h, 0}, cls));
    Point2 tx = longEnd + Point2{0.4 * h, 0};
    if (!s.method.empty()) out.texts.push_back({tx + Point2{0, -0.8 * h}, s.method, h, 0, HAlign::Left, VAlign::Middle, cls});
    if (!s.roughness.empty()) out.texts.push_back({tx + Point2{0, 0.9 * h}, s.roughness, h, 0, HAlign::Left, VAlign::Middle, cls});
    if (!s.secondary.empty()) out.texts.push_back({tx + Point2{0, 2.4 * h}, s.secondary, h, 0, HAlign::Left, VAlign::Middle, cls});
  }
  if (s.allAround) out.curves.push_back(Curve2D::circle(longEnd, 0.4 * h, cls));
  if (!s.lay.empty()) {
    Point2 lp = T + Point2{H1 * k + 0.5 * h, -0.45 * H1};
    if (s.lay == "\xE2\x8A\xA5" || s.lay == "perp" || s.lay == "PERP") addGlyph(out, Glyph::Perpendicularity, lp + Point2{0.5 * h, 0}, h, cls);
    else out.texts.push_back({lp, s.lay, h, 0, HAlign::Left, VAlign::Middle, cls});
  }
  if (!s.allowance.empty())
    out.texts.push_back({{shortEnd.x - 0.3 * h, T.y - 0.5 * H1}, s.allowance, h, 0, HAlign::Right, VAlign::Middle, cls});
}

// ---------------------------------------------------------------------------------------
// Ordinate dimensions
// ---------------------------------------------------------------------------------------

std::string CalloutRenderer::ordinateText(double value, int precision) const {
  int prec = precision >= 0 ? precision : units_.precision;
  return formatValue(core::toDisplayValue(core::Quantity::mm(value), units_), prec);
}

void CalloutRenderer::addOrdinate(DisplayList& out, const Sheet& sheet, const OrdinateDimension& o) const {
  const DrawingView* view = sheet.view(o.viewId);
  if (!view) return;
  const double h = style_.dimTextHeight;
  auto member = [&](const DimRef& ref, const Point2& textPos, const std::string& text, bool valid) {
    DimTarget t = resolveDimRef(ref, *view);
    if (!t.ok || !valid) {
      DisplayText e{textPos, "?", h * 1.4, 0, HAlign::Center, VAlign::Middle, LineClass::Dimension, true};
      e.error = true;
      out.texts.push_back(e);
      return;
    }
    Point2 local = t.isVertex ? t.point : (t.curve.type == CurveType::Arc ? t.curve.center : t.curve.midpoint());
    Point2 P = view->toSheet(local);
    const bool xAxis = o.axis == OrdinateAxis::X;
    // leader runs perpendicular to the measured axis
    double span = xAxis ? textPos.y - P.y : textPos.x - P.x;
    double s = span >= 0 ? 1.0 : -1.0;
    Point2 T = xAxis ? Point2{textPos.x, textPos.y} : Point2{textPos.x, textPos.y};
    Point2 start = xAxis ? P + Point2{0, s * style_.extensionGap} : P + Point2{s * style_.extensionGap, 0};
    double offAcross = xAxis ? T.x - P.x : T.y - P.y;
    std::vector<Point2> pts{start};
    if (std::abs(offAcross) > 0.05 && std::abs(span) > 8.0) {
      // jog near the text end so staggered ordinates stay readable
      if (xAxis) {
        pts.push_back({P.x, T.y - s * 6.0});
        pts.push_back({T.x, T.y - s * 3.0});
      } else {
        pts.push_back({T.x - s * 6.0, P.y});
        pts.push_back({T.x - s * 3.0, T.y});
      }
      pts.push_back(T);
    } else {
      pts.push_back(xAxis ? Point2{P.x, T.y} : Point2{T.x, P.y});
    }
    out.curves.push_back(Curve2D::polyline(pts, LineClass::Dimension));
    Point2 end = pts.back();
    double w = measureText(text, h, style_.fontFamily);
    if (xAxis) out.texts.push_back({end + Point2{0, s * (w * 0.5 + 1.0)}, text, h, 90.0, HAlign::Center, VAlign::Middle, LineClass::Dimension});
    else out.texts.push_back({end + Point2{s * (w * 0.5 + 1.0), 0}, text, h, 0.0, HAlign::Center, VAlign::Middle, LineClass::Dimension});
  };
  member(o.origin, o.originText, "0", o.valid);
  for (const auto& m : o.members) member(m.ref, m.textPos, ordinateText(m.value, o.precision), m.valid);
}

// ---------------------------------------------------------------------------------------
// Revision table
// ---------------------------------------------------------------------------------------

void CalloutRenderer::addRevisionTable(DisplayList& out, const RevisionTable& t) const {
  const double w = t.width(), hgt = t.height();
  const Point2 o = t.position;
  auto seg = [&](Point2 a, Point2 b) { out.curves.push_back(Curve2D::segment(a, b, LineClass::Border)); };
  for (size_t r = 0; r <= t.rows.size() + 1; ++r) {
    double y = o.y + t.rowHeight * static_cast<double>(r);
    seg({o.x, y}, {o.x + w, y});
  }
  double x = o.x;
  seg({x, o.y}, {x, o.y + hgt});
  for (double cw : t.columnWidths) {
    x += cw;
    seg({x, o.y}, {x, o.y + hgt});
  }
  x = o.x;
  for (size_t c = 0; c < t.columns.size() && c < t.columnWidths.size(); ++c) {
    double cw = t.columnWidths[c];
    double th = t.textHeight;
    double est = measureText(t.columns[c], th, style_.fontFamily) * 1.1;  // bold is wider
    if (est > cw - 2.0 && est > 0) th = std::max(1.5, th * (cw - 2.0) / est);
    out.texts.push_back({{x + cw * 0.5, o.y + t.rowHeight * 0.5}, t.columns[c], th, 0, HAlign::Center, VAlign::Middle,
                         LineClass::Annotation, true});
    x += cw;
  }
  for (size_t r = 0; r < t.rows.size(); ++r) {
    const RevisionRow& row = t.rows[r];
    const std::string cells[4] = {row.rev, row.description, row.date, row.approved};
    double y = o.y + t.rowHeight * (static_cast<double>(r) + 1.5);
    x = o.x;
    for (size_t c = 0; c < t.columns.size() && c < t.columnWidths.size() && c < 4; ++c) {
      double cw = t.columnWidths[c];
      const std::string& v = cells[c];
      bool centre = c != 1;
      double th = t.textHeight;
      double est = measureText(v, th, style_.fontFamily);
      if (est > cw - 3.0 && est > 0) th = std::max(1.5, th * (cw - 3.0) / est);
      out.texts.push_back({{centre ? x + cw * 0.5 : x + 1.5, y}, v, th, 0, centre ? HAlign::Center : HAlign::Left, VAlign::Middle,
                           LineClass::Annotation});
      x += cw;
    }
  }
}

// ---------------------------------------------------------------------------------------
// All
// ---------------------------------------------------------------------------------------

std::vector<std::pair<core::ObjectId, DisplayList>> CalloutRenderer::entityLists(const Sheet& sheet) const {
  std::vector<std::pair<core::ObjectId, DisplayList>> out;
  const CalloutSet& cs = sheet.callouts;
  auto add = [&](core::ObjectId id, auto&& fn) {
    DisplayList l;
    fn(l);
    out.emplace_back(id, std::move(l));
  };
  for (const auto& e : cs.holeCallouts) add(e.id, [&](DisplayList& l) { addHoleCallout(l, sheet, e); });
  for (const auto& e : cs.featureFrames) add(e.id, [&](DisplayList& l) { addFeatureFrame(l, sheet, e); });
  for (const auto& e : cs.datums) add(e.id, [&](DisplayList& l) { addDatum(l, sheet, e); });
  for (const auto& e : cs.surfaceFinishes) add(e.id, [&](DisplayList& l) { addSurfaceFinish(l, sheet, e); });
  for (const auto& e : cs.ordinates) add(e.id, [&](DisplayList& l) { addOrdinate(l, sheet, e); });
  for (const auto& e : cs.revisionTables) add(e.id, [&](DisplayList& l) { addRevisionTable(l, e); });
  return out;
}

void CalloutRenderer::addAll(DisplayList& out, const Sheet& sheet) const {
  for (auto& [id, list] : entityLists(sheet)) {
    (void)id;
    out.append(list);
  }
}

}  // namespace fabricad::drawing
