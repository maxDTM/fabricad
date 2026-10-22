// FabriCAD sketch: inference implementation.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sketch/SketchInference.h"

#include <cmath>

namespace fabricad::sketch {

const char* inferenceGlyphText(InferenceGlyph g) {
  switch (g) {
    case InferenceGlyph::None: return "";
    case InferenceGlyph::Coincident: return "■";  // small square
    case InferenceGlyph::Midpoint: return "△";    // triangle
    case InferenceGlyph::Center: return "⊕";      // circled plus
    case InferenceGlyph::OnCurve: return "◇";     // diamond
    case InferenceGlyph::Horizontal: return "H";
    case InferenceGlyph::Vertical: return "V";
    case InferenceGlyph::Parallel: return "∥";
    case InferenceGlyph::Perpendicular: return "⊥";
    case InferenceGlyph::Tangent: return "T";
  }
  return "";
}

GlyphShape glyphShapeFor(InferenceGlyph g) {
  switch (g) {
    case InferenceGlyph::None: return GlyphShape::None;
    case InferenceGlyph::Coincident: return GlyphShape::Coincident;
    case InferenceGlyph::Midpoint: return GlyphShape::Midpoint;
    case InferenceGlyph::Center: return GlyphShape::Center;
    case InferenceGlyph::OnCurve: return GlyphShape::OnCurve;
    case InferenceGlyph::Horizontal: return GlyphShape::Horizontal;
    case InferenceGlyph::Vertical: return GlyphShape::Vertical;
    case InferenceGlyph::Parallel: return GlyphShape::Parallel;
    case InferenceGlyph::Perpendicular: return GlyphShape::Perpendicular;
    case InferenceGlyph::Tangent: return GlyphShape::Tangent;
  }
  return GlyphShape::None;
}

std::vector<Pt2> glyphSegments(GlyphShape g, bool frame) {
  std::vector<Pt2> s;
  auto seg = [&](Pt2 a, Pt2 b) {
    s.push_back(a);
    s.push_back(b);
  };
  auto circle = [&](Pt2 c, double r, double a0 = 0, double a1 = 2 * kPi, int n = 12) {
    for (int i = 0; i < n; ++i) seg(c + Pt2::polar(r, a0 + (a1 - a0) * i / n), c + Pt2::polar(r, a0 + (a1 - a0) * (i + 1) / n));
  };
  auto rect = [&](Pt2 mn, Pt2 mx) {
    seg(mn, {mx.x, mn.y});
    seg({mx.x, mn.y}, mx);
    seg(mx, {mn.x, mx.y});
    seg({mn.x, mx.y}, mn);
  };
  const double k = 0.6;  // icon extent inside the frame
  switch (g) {
    case GlyphShape::None: return s;
    case GlyphShape::Horizontal: seg({-k, 0}, {k, 0}); break;
    case GlyphShape::Vertical: seg({0, -k}, {0, k}); break;
    case GlyphShape::Parallel:
      seg({-k, -k * 0.5}, {k * 0.2, k});
      seg({-k * 0.2, -k}, {k, k * 0.5});
      break;
    case GlyphShape::Perpendicular:
      seg({-k, -k}, {k, -k});
      seg({0, -k}, {0, k});
      break;
    case GlyphShape::Tangent:
      circle({0, 0.15}, k * 0.65);
      seg({-k, 0.15 - k * 0.65}, {k, 0.15 - k * 0.65});
      break;
    case GlyphShape::Equal:
      seg({-k, k * 0.35}, {k, k * 0.35});
      seg({-k, -k * 0.35}, {k, -k * 0.35});
      break;
    case GlyphShape::Concentric:
      circle({0, 0}, k);
      circle({0, 0}, k * 0.45, 0, 2 * kPi, 8);
      break;
    case GlyphShape::Collinear:
      seg({-k, -k}, {k, k});
      seg({-k * 0.25, -k * 0.75}, {-k * 0.75, -k * 0.25});
      seg({k * 0.75, k * 0.25}, {k * 0.25, k * 0.75});
      break;
    case GlyphShape::Midpoint:
      seg({-k, -k * 0.6}, {k, -k * 0.6});
      seg({k, -k * 0.6}, {0, k * 0.8});
      seg({0, k * 0.8}, {-k, -k * 0.6});
      break;
    case GlyphShape::Fix:
      // padlock: body + shackle
      rect({-k * 0.8, -k}, {k * 0.8, k * 0.1});
      circle({0, k * 0.1}, k * 0.5, 0, kPi, 6);
      seg({0, -k * 0.6}, {0, -k * 0.3});
      break;
    case GlyphShape::Symmetric:
      seg({0, -k}, {0, k});
      seg({-k, k * 0.45}, {-k * 0.35, 0});
      seg({-k * 0.35, 0}, {-k, -k * 0.45});
      seg({k, k * 0.45}, {k * 0.35, 0});
      seg({k * 0.35, 0}, {k, -k * 0.45});
      break;
    case GlyphShape::Coincident: rect({-k * 0.5, -k * 0.5}, {k * 0.5, k * 0.5}); break;
    case GlyphShape::Center:
      circle({0, 0}, k);
      seg({-k, 0}, {k, 0});
      seg({0, -k}, {0, k});
      break;
    case GlyphShape::OnCurve:
      seg({0, k}, {k, 0});
      seg({k, 0}, {0, -k});
      seg({0, -k}, {-k, 0});
      seg({-k, 0}, {0, k});
      break;
  }
  if (frame) rect({-1, -1}, {1, 1});
  return s;
}

namespace {
constexpr double kAngleSnapDeg = 3.0;

bool nearAngle(double a, double target, double tolDeg) {
  double d = std::abs(angleDelta(a, target));
  return d <= tolDeg * kPi / 180.0;
}
}  // namespace

InferenceResult infer(const Sketch& sketch, const InferenceInput& in) {
  InferenceResult r;
  r.point = in.cursor;
  if (in.suppress) return r;
  const double tol = std::max(in.tolerance, 1e-9);

  // 1. existing points (endpoints, centres, midpoints), then the sketch origin
  if (in.allowPoints) {
    auto hit = sketch.hitPoint(in.cursor, tol, in.exclude);
    const Pt2 origin{0, 0};
    const double dOrigin = in.cursor.distance(origin);
    if (dOrigin <= tol && (!hit || dOrigin < hit->point.distance(in.cursor) - 1e-12) && !(in.from && in.from->near(origin, 1e-9))) {
      r.point = origin;
      r.onOrigin = true;
      r.glyph = InferenceGlyph::Coincident;
      if (in.from && in.allowDirections) {
        Pt2 d = r.point - *in.from;
        if (d.length() > tol) {
          if (std::abs(d.y) < 1e-9) r.horizontal = true;
          else if (std::abs(d.x) < 1e-9) r.vertical = true;
        }
      }
      return r;
    }
    if (hit) {
      r.point = hit->point;
      r.onPoint = hit->ref;
      const Geometry* g = sketch.geometry(hit->ref.geo);
      if (hit->ref.pos == PointPos::Mid && g && g->type != GeomType::Point) r.glyph = InferenceGlyph::Midpoint;
      else if (hit->ref.pos == PointPos::Center && g && g->isCircular()) r.glyph = InferenceGlyph::Center;
      else r.glyph = InferenceGlyph::Coincident;
      // Fusion keeps H/V inference when the snapped point also happens to be aligned.
      if (in.from && in.allowDirections) {
        Pt2 d = r.point - *in.from;
        if (d.length() > tol) {
          if (std::abs(d.y) < 1e-9) r.horizontal = true;
          else if (std::abs(d.x) < 1e-9) r.vertical = true;
        }
      }
      return r;
    }
  }

  // 2. direction snapping relative to the previous point
  bool dirSnapped = false;
  if (in.from && in.allowDirections) {
    Pt2 d = in.cursor - *in.from;
    double len = d.length();
    if (len > tol) {
      double ang = d.angle();
      if (nearAngle(ang, 0, kAngleSnapDeg) || nearAngle(ang, kPi, kAngleSnapDeg)) {
        r.point = {in.cursor.x, in.from->y};
        r.horizontal = true;
        r.glyph = InferenceGlyph::Horizontal;
        dirSnapped = true;
      } else if (nearAngle(ang, kPi / 2, kAngleSnapDeg) || nearAngle(ang, -kPi / 2, kAngleSnapDeg)) {
        r.point = {in.from->x, in.cursor.y};
        r.vertical = true;
        r.glyph = InferenceGlyph::Vertical;
        dirSnapped = true;
      } else if (in.referenceDirection && in.referenceCurve) {
        Pt2 ref = in.referenceDirection->normalized();
        double refAng = ref.angle();
        auto snapAlong = [&](Pt2 dir) {
          double t = d.dot(dir);
          r.point = *in.from + dir * t;
        };
        if (nearAngle(ang, refAng, kAngleSnapDeg) || nearAngle(ang, refAng + kPi, kAngleSnapDeg)) {
          snapAlong(ref);
          r.parallelTo = in.referenceCurve;
          r.glyph = InferenceGlyph::Parallel;
          dirSnapped = true;
        } else if (nearAngle(ang, refAng + kPi / 2, kAngleSnapDeg) || nearAngle(ang, refAng - kPi / 2, kAngleSnapDeg)) {
          snapAlong(ref.perp());
          r.perpendicularTo = in.referenceCurve;
          r.glyph = InferenceGlyph::Perpendicular;
          dirSnapped = true;
        }
      }
    }
  }

  // 3. on-curve snapping (only when no direction snap, or combine: point on curve along the snapped direction)
  if (in.allowCurves) {
    if (auto hit = sketch.hitCurve(dirSnapped ? r.point : in.cursor, tol, in.exclude)) {
      if (dirSnapped) {
        // intersect the snapped direction ray with the curve for an exact point
        const Geometry* g = sketch.geometry(hit->ref.geo);
        Pt2 dir = (r.point - *in.from).normalized();
        std::vector<Pt2> cand;
        if (g->type == GeomType::Line) {
          if (auto p = intersectLines(*in.from, dir, g->start(), g->direction())) cand.push_back(*p);
        } else {
          cand = intersectLineCircle(*in.from, dir, g->center(), g->radius());
        }
        for (const Pt2& p : cand)
          if (p.distance(r.point) <= tol && g->distanceTo(p) <= 1e-6) {
            r.point = p;
            r.onCurve = hit->ref;
            break;
          }
      } else {
        r.point = hit->point;
        r.onCurve = hit->ref;
        r.glyph = InferenceGlyph::OnCurve;
      }
      if (r.onCurve) return r;
    }
  }

  // 4. alignment with existing points (tracking lines)
  if (in.allowAlignment) {
    std::optional<GeoRef> bx, by;
    double bestX = tol, bestY = tol;
    Pt2 px, py;
    for (const Geometry& g : sketch.geometries()) {
      if (in.exclude.count(g.id)) continue;
      for (PointPos pos : g.pointPositions()) {
        Pt2 p = g.point(pos);
        if (in.from && p.near(*in.from, 1e-9)) continue;
        double dx = std::abs(p.x - r.point.x), dy = std::abs(p.y - r.point.y);
        if (!r.vertical && !r.parallelTo && !r.perpendicularTo && dx < bestX) {
          bestX = dx;
          bx = GeoRef{g.id, pos};
          px = p;
        }
        if (!r.horizontal && !r.parallelTo && !r.perpendicularTo && dy < bestY) {
          bestY = dy;
          by = GeoRef{g.id, pos};
          py = p;
        }
      }
    }
    if (bx) {
      r.point.x = px.x;
      r.alignedXWith = bx;
      r.guides.push_back({px, r.point});
    }
    if (by) {
      r.point.y = py.y;
      r.alignedYWith = by;
      r.guides.push_back({py, r.point});
    }
    // re-apply direction snaps that the alignment may have broken
    if (in.from) {
      if (r.horizontal) r.point.y = in.from->y;
      if (r.vertical) r.point.x = in.from->x;
    }
  }
  return r;
}

void applyInference(Sketch& sketch, const InferenceResult& r, const GeoRef& newPoint, int newLine) {
  if (r.onPoint && newPoint.isValid()) {
    sketch.addConstraint(Constraint{.type = ConstraintType::Coincident, .a = newPoint, .b = *r.onPoint});
  } else if (r.onOrigin && newPoint.isValid()) {
    // the origin is not sketch geometry: pin the point there with a Fix shown as a coincident dot
    sketch.addConstraint(Constraint{.type = ConstraintType::Fix, .a = newPoint, .name = kOriginFixName});
  } else if (r.onCurve && newPoint.isValid()) {
    sketch.addConstraint(Constraint{.type = ConstraintType::PointOnCurve, .a = newPoint, .b = *r.onCurve});
  }
  if (newLine >= 0) {
    if (r.horizontal) sketch.addConstraint(Constraint{.type = ConstraintType::Horizontal, .a = {newLine}});
    else if (r.vertical) sketch.addConstraint(Constraint{.type = ConstraintType::Vertical, .a = {newLine}});
    else if (r.parallelTo && *r.parallelTo != newLine)
      sketch.addConstraint(Constraint{.type = ConstraintType::Parallel, .a = {newLine}, .b = {*r.parallelTo}});
    else if (r.perpendicularTo && *r.perpendicularTo != newLine)
      sketch.addConstraint(Constraint{.type = ConstraintType::Perpendicular, .a = {newLine}, .b = {*r.perpendicularTo}});
  }
  // Tracking-line alignment (alignedXWith / alignedYWith) only snaps the position, like Fusion:
  // it adds no constraint, so separate shapes are never tied together by hidden H/V constraints.
}

}  // namespace fabricad::sketch
