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

  // 1. existing points (endpoints, centres, midpoints)
  if (in.allowPoints) {
    if (auto hit = sketch.hitPoint(in.cursor, tol, in.exclude)) {
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
  if (!r.onPoint && !r.onCurve && newPoint.isValid()) {
    if (r.alignedXWith && !(newLine >= 0 && r.vertical))
      sketch.addConstraint(Constraint{.type = ConstraintType::Vertical, .a = newPoint, .b = *r.alignedXWith, .internal = true});
    if (r.alignedYWith && !(newLine >= 0 && r.horizontal))
      sketch.addConstraint(Constraint{.type = ConstraintType::Horizontal, .a = newPoint, .b = *r.alignedYWith, .internal = true});
  }
}

}  // namespace fabricad::sketch
