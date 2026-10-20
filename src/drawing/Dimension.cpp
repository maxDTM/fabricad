// FabriCAD drawing: dimension resolution, measurement, layout and formatting.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "drawing/Dimension.h"

#include "core/Json.h"
#include "drawing/DrawingView.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fabricad::drawing {

using core::idFromJson;
using core::idToJson;
using core::qs;
using core::ss;

// ---------------------------------------------------------------------------------------
// DimRef
// ---------------------------------------------------------------------------------------

DimRef DimRef::toCurve(const Curve2D& c) {
  DimRef r;
  r.isVertex = false;
  r.body = c.sourceBody;
  r.edge = c.sourceEdge;
  r.name = c.sourceName;
  r.sig = c.sourceSig;
  r.fallback = c.midpoint();
  return r;
}

DimRef DimRef::toVertex(const Vertex2D& v) {
  DimRef r;
  r.isVertex = true;
  r.vertexRole = v.role;
  r.body = v.sourceBody;
  r.edge = v.sourceEdge;
  r.name = v.sourceName;
  r.sig = v.sourceSig;
  r.fallback = v.p;
  return r;
}

QJsonObject DimRef::toJson() const {
  QJsonObject o;
  o["vertex"] = isVertex;
  if (isVertex) o["role"] = vertexRole;
  if (body != core::kInvalidId) o["body"] = idToJson(body);
  if (edge) o["edge"] = edge;
  if (!name.empty()) o["name"] = qs(name);
  if (sig.isValid()) o["sig"] = sig.toJson();
  o["at"] = QJsonArray{fallback.x, fallback.y};
  return o;
}

DimRef DimRef::fromJson(const QJsonObject& o) {
  DimRef r;
  r.isVertex = o["vertex"].toBool(false);
  r.vertexRole = o["role"].toInt(0);
  r.body = idFromJson(o["body"]);
  r.edge = o["edge"].toInt(0);
  r.name = ss(o["name"].toString());
  if (o.contains("sig")) r.sig = core::GeomSignature::fromJson(o["sig"].toObject());
  auto a = o["at"].toArray();
  if (a.size() >= 2) r.fallback = {a[0].toDouble(), a[1].toDouble()};
  return r;
}

namespace {

// Signature distance below which two body-local edge signatures denote the same edge.
constexpr double kSameEdgeSignature = 0.02;

/// Curves of `body` in the view that match the ref by name (method 1) or signature (method 2).
std::vector<const Curve2D*> curvesByIdentity(const DimRef& ref, const DrawingView& view, int& method) {
  std::vector<const Curve2D*> out;
  method = 0;
  if (ref.body == core::kInvalidId) return out;
  if (!ref.name.empty()) {
    for (const auto& c : view.curves)
      if (c.sourceBody == ref.body && c.sourceName == ref.name) out.push_back(&c);
    if (!out.empty()) {
      method = 1;
      return out;
    }
  }
  if (ref.sig.isValid()) {
    double modelSize = std::max(1.0, (ref.sig.bboxMax - ref.sig.bboxMin).length() * 4.0);
    for (const auto& c : view.curves) {
      if (c.sourceBody != ref.body || !c.sourceSig.isValid()) continue;
      if (core::signatureDistance(ref.sig, c.sourceSig, modelSize) < kSameEdgeSignature) out.push_back(&c);
    }
    if (!out.empty()) method = 2;
  }
  return out;
}

/// Largest view-local distance between the index-resolved curve and the stored fallback that
/// is still believable: the picked position on an edge may drift with a model edit, but an
/// edge on the far side of the view is a renumbered stranger.
double indexAcceptDistance(const DrawingView& view, double tol) {
  double diag = (view.bbox.max - view.bbox.min).length();
  return std::max(tol * 4.0, diag * 0.15);
}

}  // namespace

DimTarget resolveDimRef(const DimRef& ref, const DrawingView& view, double tol) {
  DimTarget t;
  t.isVertex = ref.isVertex;
  if (!view.geometryValid) return t;
  int method = 0;
  std::vector<const Curve2D*> identified = curvesByIdentity(ref, view, method);
  if (ref.isVertex) {
    // 1/2: a vertex of a curve identified by name or signature, same role preferred
    if (!identified.empty()) {
      const Vertex2D* best = nullptr;
      double bestD = 1e300;
      for (const auto& v : view.vertices) {
        if (v.sourceBody != ref.body) continue;
        bool match = false;
        for (const Curve2D* c : identified)
          if (method == 1 ? c->sourceName == v.sourceName : c->sourceEdge == v.sourceEdge) match = true;
        if (!match) continue;
        double d = v.p.distance(ref.fallback);
        if (v.role == ref.vertexRole) d *= 0.5;
        if (d < bestD) {
          bestD = d;
          best = &v;
        }
      }
      if (best) {
        t.ok = true;
        t.point = best->p;
        t.method = method;
        return t;
      }
    }
    // 3: by index, only when the vertex is still near where it was picked
    if (ref.body != core::kInvalidId && ref.edge > 0) {
      const Vertex2D* best = nullptr;
      double bestD = 1e300;
      for (const auto& v : view.vertices) {
        if (v.sourceBody != ref.body || v.sourceEdge != ref.edge) continue;
        if (!v.sourceName.empty() && !ref.name.empty() && v.sourceName != ref.name) continue;  // renumbered
        double d = v.p.distance(ref.fallback);
        if (v.role == ref.vertexRole) d *= 0.5;
        if (d < bestD) {
          bestD = d;
          best = &v;
        }
      }
      if (best && best->p.distance(ref.fallback) <= indexAcceptDistance(view, tol)) {
        t.ok = true;
        t.point = best->p;
        t.method = 3;
        return t;
      }
    }
    // 4: whatever vertex sits where the pick was
    if (const Vertex2D* v = view.pickVertex(ref.fallback, tol)) {
      t.ok = true;
      t.point = v->p;
      t.method = 4;
      return t;
    }
    return t;
  }
  // curves ------------------------------------------------------------------------------
  if (!identified.empty()) {
    // Several HLR pieces may come from the same edge: pick the one nearest the fallback.
    const Curve2D* best = identified.front();
    double bestD = best->distanceTo(ref.fallback);
    for (const Curve2D* k : identified) {
      double d = k->distanceTo(ref.fallback);
      if (d < bestD) {
        bestD = d;
        best = k;
      }
    }
    t.ok = true;
    t.curve = *best;
    t.method = method;
    return t;
  }
  if (const Curve2D* c = view.curveForSource(ref.body, ref.edge)) {
    const Curve2D* best = c;
    double bestD = c->distanceTo(ref.fallback);
    for (const auto& k : view.curves)
      if (k.sourceBody == ref.body && k.sourceEdge == ref.edge) {
        double d = k.distanceTo(ref.fallback);
        if (d < bestD) {
          bestD = d;
          best = &k;
        }
      }
    // The index is only trusted when nothing better is known about the edge (no names on
    // either side) and the curve is still near the original pick.
    bool renamed = !best->sourceName.empty() && !ref.name.empty() && best->sourceName != ref.name;
    if (!renamed && bestD <= indexAcceptDistance(view, tol)) {
      t.ok = true;
      t.curve = *best;
      t.method = 3;
      return t;
    }
  }
  if (const Curve2D* c = view.pickCurve(ref.fallback, tol)) {
    t.ok = true;
    t.curve = *c;
    t.method = 4;
  }
  return t;
}

// ---------------------------------------------------------------------------------------
// Dimension
// ---------------------------------------------------------------------------------------

QJsonObject Dimension::toJson() const {
  QJsonObject o;
  o["id"] = idToJson(id);
  o["view"] = idToJson(viewId);
  o["type"] = dimensionTypeName(type);
  QJsonArray r;
  for (const auto& ref : refs) r.push_back(ref.toJson());
  o["refs"] = r;
  o["text"] = QJsonArray{textPos.x, textPos.y};
  if (!overrideText.empty()) o["override"] = qs(overrideText);
  if (!prefix.empty()) o["prefix"] = qs(prefix);
  if (!suffix.empty()) o["suffix"] = qs(suffix);
  if (precision >= 0) o["precision"] = precision;
  if (tolerance.isSet()) o["tolerance"] = tolerance.toJson();
  return o;
}

Dimension Dimension::fromJson(const QJsonObject& o) {
  Dimension d;
  d.id = idFromJson(o["id"]);
  d.viewId = idFromJson(o["view"]);
  d.type = dimensionTypeFromName(ss(o["type"].toString()));
  for (const auto& r : o["refs"].toArray()) d.refs.push_back(DimRef::fromJson(r.toObject()));
  auto t = o["text"].toArray();
  if (t.size() >= 2) d.textPos = {t[0].toDouble(), t[1].toDouble()};
  d.overrideText = ss(o["override"].toString());
  d.prefix = ss(o["prefix"].toString());
  d.suffix = ss(o["suffix"].toString());
  d.precision = o["precision"].toInt(-1);
  d.tolerance = DimTolerance::fromJson(o["tolerance"].toObject());
  return d;
}

bool inferDimension(const std::vector<DimPick>& picks, Dimension& out) {
  out.refs.clear();
  if (picks.empty() || picks.size() > 2) return false;
  if (picks.size() == 1) {
    const DimPick& p = picks[0];
    if (p.isVertex) return false;
    if (p.curve.type == CurveType::Arc) {
      out.type = p.curve.isCircle() ? DimensionType::Diameter : DimensionType::Radius;
      out.refs.push_back(DimRef::toCurve(p.curve));
      return true;
    }
    if (p.curve.type == CurveType::Segment) {
      out.type = DimensionType::Linear;
      out.refs.push_back(DimRef::toCurve(p.curve));
      return true;
    }
    return false;
  }
  const DimPick& a = picks[0];
  const DimPick& b = picks[1];
  bool aLine = !a.isVertex && a.curve.type == CurveType::Segment;
  bool bLine = !b.isVertex && b.curve.type == CurveType::Segment;
  if (aLine && bLine) {
    Point2 da = a.curve.direction(), db = b.curve.direction();
    bool parallel = std::abs(da.cross(db)) < 1e-3;
    out.type = parallel ? DimensionType::Linear : DimensionType::Angle;
    out.refs = {DimRef::toCurve(a.curve), DimRef::toCurve(b.curve)};
    return true;
  }
  // points, or point + circle centre, or two circles (centre distance)
  auto asRef = [](const DimPick& p) {
    if (p.isVertex) return DimRef::toVertex(p.vertex);
    if (p.curve.type == CurveType::Arc) {
      return DimRef::toVertex(Vertex2D::of(p.curve, p.curve.center, 2));
    }
    return DimRef::toCurve(p.curve);
  };
  out.type = DimensionType::Linear;
  out.refs = {asRef(a), asRef(b)};
  return true;
}

namespace {
// Project point p onto the infinite line through a with direction d.
Point2 projectOnLine(const Point2& a, const Point2& d, const Point2& p) { return a + d * (p - a).dot(d); }
}  // namespace

bool measureDimension(const Dimension& dim, const DrawingView& view, DimGeometry& g) {
  if (dim.refs.empty()) return false;
  std::vector<DimTarget> t;
  for (const auto& r : dim.refs) {
    t.push_back(resolveDimRef(r, view));
    if (!t.back().ok) return false;
  }
  switch (dim.type) {
    case DimensionType::Radius:
    case DimensionType::Diameter: {
      if (t[0].isVertex || t[0].curve.type != CurveType::Arc) return false;
      const Curve2D& c = t[0].curve;
      g.p1 = c.center;
      g.radius = c.radius;
      // point on arc nearest to the text position (view-local)
      Point2 textLocal = view.fromSheet(dim.textPos);
      Point2 dir = (textLocal - c.center).normalized();
      if (dir.length() < 0.5) dir = {1, 0};
      if (!c.isCircle() && !angleInArc(dir.angleDeg(), c.startAngle, c.sweep)) {
        // keep the dimension on the arc: snap to the nearer end
        Point2 s = c.start(), e = c.end();
        dir = ((textLocal.distance(s) < textLocal.distance(e)) ? s - c.center : e - c.center).normalized();
      }
      g.p2 = c.center + dir * c.radius;
      g.value = dim.type == DimensionType::Radius ? c.radius : 2.0 * c.radius;
      return true;
    }
    case DimensionType::Angle: {
      if (t.size() != 2 || t[0].isVertex || t[1].isVertex) return false;
      const Curve2D& a = t[0].curve;
      const Curve2D& b = t[1].curve;
      if (a.type != CurveType::Segment || b.type != CurveType::Segment) return false;
      Point2 da = a.direction(), db = b.direction();
      auto x = lineIntersection(a.a, da, b.a, db);
      if (!x) return false;
      g.apex = *x;
      // Choose the ray directions towards the text position's quadrant.
      Point2 textLocal = view.fromSheet(dim.textPos);
      Point2 toText = textLocal - g.apex;
      if (toText.dot(da) < 0) da = -da;
      if (toText.dot(db) < 0) db = -db;
      g.d1 = da;
      g.d2 = db;
      double ang = rad2deg(std::acos(std::clamp(da.dot(db), -1.0, 1.0)));
      g.value = ang;
      return true;
    }
    default: break;
  }
  // Linear family
  Point2 p1, p2;
  if (t.size() == 1) {
    if (t[0].isVertex || t[0].curve.type != CurveType::Segment) return false;
    p1 = t[0].curve.a;
    p2 = t[0].curve.b;
  } else if (t.size() == 2) {
    auto pointOf = [&](const DimTarget& tt, const DimTarget& other, Point2& out) {
      if (tt.isVertex) {
        out = tt.point;
        return true;
      }
      if (tt.curve.type == CurveType::Arc) {
        out = tt.curve.center;
        return true;
      }
      if (tt.curve.type == CurveType::Segment) {
        // distance between parallel lines or line-to-point: foot of the perpendicular
        Point2 ref = other.isVertex ? other.point : (other.curve.type == CurveType::Arc ? other.curve.center : other.curve.a);
        out = projectOnLine(tt.curve.a, tt.curve.direction(), ref);
        return true;
      }
      return false;
    };
    if (!pointOf(t[0], t[1], p1) || !pointOf(t[1], t[0], p2)) return false;
    // two parallel lines: measure between the lines along the normal
    if (!t[0].isVertex && !t[1].isVertex && t[0].curve.type == CurveType::Segment &&
        t[1].curve.type == CurveType::Segment) {
      p1 = t[0].curve.midpoint();
      p2 = projectOnLine(t[1].curve.a, t[1].curve.direction(), p1);
    }
  } else {
    return false;
  }
  g.p1 = p1;
  g.p2 = p2;
  Point2 d = p2 - p1;
  DimensionType eff = dim.type;
  if (eff == DimensionType::Linear) {
    // Auto: horizontal/vertical from where the text is placed (Fusion behaviour); aligned
    // when both offsets are small relative to the span.
    Point2 textLocal = view.fromSheet(dim.textPos);
    Point2 mid = (p1 + p2) * 0.5;
    Point2 off = textLocal - mid;
    if (std::abs(d.x) < 1e-9) eff = DimensionType::Vertical;
    else if (std::abs(d.y) < 1e-9) eff = DimensionType::Horizontal;
    else eff = std::abs(off.y) >= std::abs(off.x) ? DimensionType::Horizontal : DimensionType::Vertical;
  }
  switch (eff) {
    case DimensionType::Horizontal: g.value = std::abs(d.x); break;
    case DimensionType::Vertical: g.value = std::abs(d.y); break;
    default: g.value = d.length(); break;
  }
  return true;
}

bool updateDimension(Dimension& dim, const DrawingView& view) {
  DimGeometry g;
  dim.valid = measureDimension(dim, view, g);
  if (dim.valid) dim.value = g.value;
  dim.orphaned = false;
  if (!dim.valid && view.geometryValid)
    for (const auto& r : dim.refs)
      if (!resolveDimRef(r, view).ok) dim.orphaned = true;
  return dim.valid;
}

std::string formatDimensionText(const Dimension& dim, const core::UnitSystem& units) {
  if (!dim.overrideText.empty()) return dim.overrideText;
  if (!dim.valid) return "?";
  int prec = dim.precision >= 0 ? dim.precision : units.precision;
  char buf[64];
  std::string body;
  if (dim.type == DimensionType::Angle) {
    std::snprintf(buf, sizeof buf, "%.*f", prec, dim.value);
    body = buf;
    body += "\xC2\xB0";  // degree sign
  } else {
    double v = core::toDisplayValue(core::Quantity::mm(dim.value), units);
    std::snprintf(buf, sizeof buf, "%.*f", prec, v);
    body = buf;
    // trim trailing zeros but keep at least one decimal when precision > 0
    if (body.find('.') != std::string::npos) {
      while (body.size() > 1 && body.back() == '0') body.pop_back();
      if (body.back() == '.') body.pop_back();
    }
    if (dim.type == DimensionType::Diameter) body = "\xC3\x98" + body;  // Ø
    else if (dim.type == DimensionType::Radius) body = "R" + body;
  }
  return dim.prefix + body + dim.suffix;
}

std::array<Point2, 3> arrowHead(const Point2& tip, const Point2& dir, const DrawingStyle& style) {
  Point2 d = dir.normalized();
  Point2 n = d.perp();
  Point2 base = tip - d * style.arrowLength;
  return {tip, base + n * (style.arrowWidth * 0.5), base - n * (style.arrowWidth * 0.5)};
}

DimLayout layoutDimension(const Dimension& dim, const DrawingView& view, const DrawingStyle& style,
                          const core::UnitSystem& units) {
  DimLayout L;
  DimGeometry g;
  if (!measureDimension(dim, view, g)) return L;
  Dimension measured = dim;
  measured.valid = true;
  measured.value = g.value;
  L.text = formatDimensionText(measured, units);
  const double textW = 0.7 * style.dimTextHeight * static_cast<double>(L.text.size());

  auto S = [&](const Point2& p) { return view.toSheet(p); };
  auto line = [&](const Point2& a, const Point2& b) { L.lines.push_back(Curve2D::segment(a, b, LineClass::Dimension)); };

  switch (dim.type) {
    case DimensionType::Radius:
    case DimensionType::Diameter: {
      Point2 c = S(g.p1), onArc = S(g.p2);
      Point2 dir = (onArc - c).normalized();
      Point2 text = dim.textPos;
      // leader from the arc point outwards to the text, horizontal shoulder
      Point2 outer = onArc + dir * std::max(2.0, text.distance(onArc) * 0.6);
      if ((text - c).dot(dir) < (onArc - c).dot(dir)) outer = onArc;  // text inside: keep it short
      if (dim.type == DimensionType::Diameter) {
        Point2 opposite = c - (onArc - c);
        line(opposite, onArc);
        L.arrows.push_back(arrowHead(onArc, dir, style));
        L.arrows.push_back(arrowHead(opposite, -dir, style));
        line(onArc, outer);
      } else {
        line(c, onArc);
        L.arrows.push_back(arrowHead(onArc, dir, style));
        line(onArc, outer);
      }
      double shoulder = (text.x >= outer.x ? 1.0 : -1.0) * (textW * 0.5 + 1.0);
      Point2 shoulderEnd = {outer.x + shoulder, outer.y};
      line(outer, shoulderEnd);
      L.textPos = {outer.x + shoulder * 0.5 + (shoulder > 0 ? 0 : 0), outer.y - style.dimTextHeight * 0.75};
      L.textPos.x = (outer.x + shoulderEnd.x) * 0.5;
      L.ok = true;
      return L;
    }
    case DimensionType::Angle: {
      Point2 apex = S(g.apex);
      // directions in sheet space (Y flipped)
      Point2 d1 = {g.d1.x, -g.d1.y}, d2 = {g.d2.x, -g.d2.y};
      double r = std::max(5.0, dim.textPos.distance(apex));
      Point2 a1 = apex + d1 * r, a2 = apex + d2 * r;
      // extension lines from the apex region to the arc (thin)
      line(apex + d1 * 1.0, a1 + d1 * style.extensionOvershoot);
      line(apex + d2 * 1.0, a2 + d2 * style.extensionOvershoot);
      // arc between them: sheet-space angles (Y down => arc goes clockwise visually; store CCW in math coords)
      double ang1 = d1.angleDeg(), ang2 = d2.angleDeg();
      double sweep = normalizeAngle(ang2 - ang1);
      if (sweep > 180.0) {
        std::swap(ang1, ang2);
        sweep = 360.0 - sweep;
      }
      L.lines.push_back(Curve2D::arc(apex, r, ang1, sweep, LineClass::Dimension));
      // arrows at both ends, tangent to the arc
      Point2 e1 = apex + Point2::polar(ang1, r), e2 = apex + Point2::polar(ang1 + sweep, r);
      Point2 t1 = Point2::polar(ang1, 1.0).perp(), t2 = Point2::polar(ang1 + sweep, 1.0).perp();
      L.arrows.push_back(arrowHead(e1, -t1, style));
      L.arrows.push_back(arrowHead(e2, t2, style));
      Point2 mid = apex + Point2::polar(ang1 + sweep * 0.5, r + style.dimTextHeight * 0.9);
      L.textPos = mid;
      L.ok = true;
      return L;
    }
    default: break;
  }

  // Linear family in sheet coordinates.
  Point2 p1 = S(g.p1), p2 = S(g.p2);
  DimensionType eff = dim.type;
  if (eff == DimensionType::Linear) {
    Point2 d = p2 - p1;
    Point2 mid = (p1 + p2) * 0.5;
    Point2 off = dim.textPos - mid;
    if (std::abs(d.x) < 1e-9) eff = DimensionType::Vertical;
    else if (std::abs(d.y) < 1e-9) eff = DimensionType::Horizontal;
    else eff = std::abs(off.y) >= std::abs(off.x) ? DimensionType::Horizontal : DimensionType::Vertical;
  }
  Point2 dir, nrm;  // dir along the dimension line, nrm = offset direction
  if (eff == DimensionType::Horizontal) {
    dir = {1, 0};
    nrm = {0, 1};
  } else if (eff == DimensionType::Vertical) {
    dir = {0, 1};
    nrm = {1, 0};
  } else {
    dir = (p2 - p1).normalized();
    if (dir.length() < 0.5) dir = {1, 0};
    nrm = dir.perp();
  }
  // dimension line passes through the text position's projection on the normal
  Point2 mid = (p1 + p2) * 0.5;
  double offset = (dim.textPos - mid).dot(nrm);
  if (std::abs(offset) < 1e-9) offset = 10.0;
  Point2 lineOrigin = mid + nrm * offset;
  Point2 q1 = lineOrigin + dir * (p1 - lineOrigin).dot(dir);
  Point2 q2 = lineOrigin + dir * (p2 - lineOrigin).dot(dir);
  if (q1.distance(q2) < 1e-9) return L;
  // extension lines from geometry (with gap) to beyond the dimension line
  Point2 sign = offset >= 0 ? nrm : -nrm;
  auto ext = [&](const Point2& p, const Point2& q) {
    Point2 toQ = (q - p);
    double len = toQ.length();
    if (len < style.extensionGap) return;
    Point2 u = toQ / len;
    line(p + u * style.extensionGap, q + u * style.extensionOvershoot);
  };
  ext(p1, q1);
  ext(p2, q2);
  (void)sign;
  // dimension line and arrows (inside when there is room, outside otherwise)
  Point2 along = (q2 - q1).normalized();
  double span = q1.distance(q2);
  bool inside = span > 2.0 * style.arrowLength + textW * 0.3;
  if (inside) {
    line(q1, q2);
    L.arrows.push_back(arrowHead(q1, -along, style));
    L.arrows.push_back(arrowHead(q2, along, style));
  } else {
    line(q1 - along * (style.arrowLength * 2.0), q2 + along * (style.arrowLength * 2.0));
    L.arrows.push_back(arrowHead(q1, along, style));
    L.arrows.push_back(arrowHead(q2, -along, style));
  }
  // text above the line (towards the offset side), centred, clamped to the user's position
  // along the line
  double t = std::clamp((dim.textPos - q1).dot(along), 0.0, span);
  Point2 textOnLine = q1 + along * t;
  Point2 up = along.perp();
  if (up.y > 0) up = -up;  // "above" in sheet space is -y; for vertical lines prefer left
  if (std::abs(along.y) > 0.9) up = {-std::abs(up.x) , 0};
  if (eff == DimensionType::Vertical) up = {-1, 0};
  L.textPos = textOnLine + up * (style.dimTextHeight * 0.8);
  L.textAngle = eff == DimensionType::Vertical ? 90.0 : (eff == DimensionType::Horizontal ? 0.0 : -along.angleDeg());
  if (eff == DimensionType::Aligned) {
    double a = along.angleDeg();  // sheet-space (Y down) angle
    // keep text readable (never upside down)
    double ccw = -a;
    if (ccw > 90 || ccw < -90) ccw += 180;
    L.textAngle = ccw;
  }
  L.ok = true;
  return L;
}

}  // namespace fabricad::drawing
