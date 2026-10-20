// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/features/Construction.h"

#include "core/Feature.h"
#include "core/ShapeUtils.h"
#include "part/PartUtils.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <IntAna_IntConicQuad.hxx>
#include <IntAna_QuadQuadGeo.hxx>
#include <TopoDS.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>

#include <cmath>

namespace fabricad::part {

namespace {
constexpr auto kPlaneKinds = core::kPlaneKinds | core::ShapeKind::Sketch;

std::optional<gp_Ax3> planeFromRef(core::RecomputeContext& ctx, core::Property& p) {
  const core::ShapeRef& r = p.ref();
  if (!r.isValid()) return std::nullopt;
  if (r.kind == core::ShapeKind::Face) {
    TopoDS_Shape s = resolveRefProperty(ctx, p);
    return core::facePlane(s);
  }
  return ctx.plane(r);
}

std::optional<gp_Ax1> axisFromRef(core::RecomputeContext& ctx, core::Property& p) {
  const core::ShapeRef& r = p.ref();
  if (!r.isValid()) return std::nullopt;
  if (r.kind == core::ShapeKind::Edge || r.kind == core::ShapeKind::Face) resolveRefProperty(ctx, p);
  return ctx.axis(p.ref());
}

std::optional<gp_Pnt> pointFromRef(core::RecomputeContext& ctx, core::Property& p) {
  const core::ShapeRef& r = p.ref();
  if (!r.isValid()) return std::nullopt;
  if (r.isBrep() && r.kind != core::ShapeKind::Body) resolveRefProperty(ctx, p);
  return ctx.point(p.ref());
}

/// Plane with an x-direction chosen for a nice display orientation.
gp_Ax3 makePlane(const gp_Pnt& origin, const gp_Dir& normal, std::optional<gp_Dir> xHint = std::nullopt) {
  if (xHint && std::abs(gp_Vec(normal).Dot(gp_Vec(*xHint))) < 0.999) {
    gp_Vec x = gp_Vec(*xHint) - gp_Vec(normal) * gp_Vec(normal).Dot(gp_Vec(*xHint));
    if (x.Magnitude() > 1e-9) return gp_Ax3(origin, normal, gp_Dir(x));
  }
  return gp_Ax3(origin, normal, anyPerpendicular(normal));
}
}  // namespace

// ---------------------------------------------------------------------------
// ConstructionFeature
// ---------------------------------------------------------------------------

double ConstructionFeature::displaySize(core::RecomputeContext& ctx) const {
  if (const core::Property* p = property("size"); p && p->toDouble() > 0) return p->toDouble();
  return std::max(10.0, 0.6 * sceneSize(ctx));
}

void ConstructionFeature::setPlaneResult(core::RecomputeContext& ctx, const gp_Ax3& plane, Outcome& out) {
  plane_ = plane;
  axis_.reset();
  point_.reset();
  out.display = planeDisplayFace(plane, displaySize(ctx));
  out.addPreview(out.display, Operation::NewBody);
}

void ConstructionFeature::setAxisResult(core::RecomputeContext& ctx, const gp_Ax1& axis, Outcome& out) {
  axis_ = axis;
  plane_.reset();
  point_.reset();
  out.display = axisDisplayEdge(axis, displaySize(ctx));
  out.addPreview(out.display, Operation::NewBody);
}

void ConstructionFeature::setPointResult(const gp_Pnt& p, Outcome& out) {
  point_ = p;
  plane_.reset();
  axis_.reset();
  out.display = pointDisplayVertex(p);
  out.addPreview(out.display, Operation::NewBody);
}

// ---------------------------------------------------------------------------
// OffsetPlane
// ---------------------------------------------------------------------------

OffsetPlaneFeature::OffsetPlaneFeature() : ConstructionFeature(kType) {
  addRef("plane", kPlaneKinds).setLabel("Plane");
  addLength("distance", 10.0).setLabel("Distance");
  addBool("flip", false).setLabel("Flip");
  addLength("size", 0.0).setLabel("Display size").setTooltip("0 = automatic");
}

void OffsetPlaneFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  auto base = planeFromRef(ctx, prop("plane"));
  if (!base) {
    ctx.setError("select a plane or planar face");
    return;
  }
  double d = ctx.length("distance");
  if (flag("flip")) d = -d;
  gp_Ax3 p = *base;
  p.Translate(gp_Vec(p.Direction()) * d);
  setPlaneResult(ctx, p, out);
}

// ---------------------------------------------------------------------------
// PlaneAtAngle
// ---------------------------------------------------------------------------

PlaneAtAngleFeature::PlaneAtAngleFeature() : ConstructionFeature(kType) {
  addRef("axis", core::kAxisKinds).setLabel("Line");
  addAngle("angle", 45.0).setLabel("Angle");
  addRef("reference", kPlaneKinds).setLabel("Reference plane").setTooltip("Plane the angle is measured from (optional)");
  addLength("size", 0.0).setLabel("Display size").setTooltip("0 = automatic");
}

void PlaneAtAngleFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  auto axis = axisFromRef(ctx, prop("axis"));
  if (!axis) {
    ctx.setError("select a linear edge or axis");
    return;
  }
  gp_Dir a = axis->Direction();
  gp_Dir n0;
  if (auto refPlane = planeFromRef(ctx, prop("reference"))) {
    // start from the reference plane's normal projected perpendicular to the axis
    gp_Vec n = gp_Vec(refPlane->Direction());
    n -= gp_Vec(a) * n.Dot(gp_Vec(a));
    if (n.Magnitude() < 1e-9) {
      ctx.setError("reference plane is perpendicular to the line");
      return;
    }
    n0 = gp_Dir(n);
  } else {
    // default: the plane containing the axis that is closest to a world plane
    gp_Dir cands[3] = {gp_Dir(0, 0, 1), gp_Dir(0, 1, 0), gp_Dir(1, 0, 0)};
    double best = -1;
    for (const auto& c : cands) {
      gp_Vec n = gp_Vec(c) - gp_Vec(a) * gp_Vec(c).Dot(gp_Vec(a));
      if (n.Magnitude() > best && n.Magnitude() > 1e-9) {
        best = n.Magnitude();
        n0 = gp_Dir(n);
      }
    }
  }
  gp_Dir n = n0.Rotated(*axis, ctx.angle("angle") * M_PI / 180.0);
  setPlaneResult(ctx, gp_Ax3(axis->Location(), n, a), out);
}

// ---------------------------------------------------------------------------
// Midplane
// ---------------------------------------------------------------------------

MidplaneFeature::MidplaneFeature() : ConstructionFeature(kType) {
  addRef("planeA", kPlaneKinds).setLabel("First plane");
  addRef("planeB", kPlaneKinds).setLabel("Second plane");
  addLength("size", 0.0).setLabel("Display size").setTooltip("0 = automatic");
}

void MidplaneFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  auto a = planeFromRef(ctx, prop("planeA"));
  auto b = planeFromRef(ctx, prop("planeB"));
  if (!a || !b) {
    ctx.setError("select two planes or planar faces");
    return;
  }
  gp_Vec na(a->Direction()), nb(b->Direction());
  if (na.Dot(nb) < 0) nb.Reverse();  // same hemisphere
  if (na.IsParallel(nb, 1e-6)) {
    gp_Pnt o = a->Location();
    gp_Pln pb(*b);
    double d = pb.Distance(o);
    gp_Vec toB(o, b->Location());
    double sgn = toB.Dot(nb) >= 0 ? 1.0 : -1.0;
    gp_Pnt mid = o.Translated(nb * (sgn * d * 0.5));
    setPlaneResult(ctx, makePlane(mid, a->Direction(), a->XDirection()), out);
    return;
  }
  // bisector: contains the intersection line, normal = na + nb (normalised)
  IntAna_QuadQuadGeo inter(gp_Pln(*a), gp_Pln(*b), 1e-9, 1e-9);
  if (!inter.IsDone() || inter.NbSolutions() < 1) {
    ctx.setError("planes do not intersect");
    return;
  }
  gp_Lin line = inter.Line(1);
  gp_Vec n = na + nb;
  gp_Pnt o = line.Location();
  setPlaneResult(ctx, gp_Ax3(o, gp_Dir(n), line.Direction()), out);
}

// ---------------------------------------------------------------------------
// PlaneThreePoints
// ---------------------------------------------------------------------------

PlaneThreePointsFeature::PlaneThreePointsFeature() : ConstructionFeature(kType) {
  addRef("pointA", core::kPointKinds).setLabel("First point");
  addRef("pointB", core::kPointKinds).setLabel("Second point");
  addRef("pointC", core::kPointKinds).setLabel("Third point");
  addLength("size", 0.0).setLabel("Display size").setTooltip("0 = automatic");
}

void PlaneThreePointsFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  auto a = pointFromRef(ctx, prop("pointA"));
  auto b = pointFromRef(ctx, prop("pointB"));
  auto c = pointFromRef(ctx, prop("pointC"));
  if (!a || !b || !c) {
    ctx.setError("select three points");
    return;
  }
  gp_Vec ab(*a, *b), ac(*a, *c);
  gp_Vec n = ab.Crossed(ac);
  if (n.Magnitude() < 1e-9) {
    ctx.setError("points are collinear");
    return;
  }
  gp_Pnt centre((a->X() + b->X() + c->X()) / 3, (a->Y() + b->Y() + c->Y()) / 3, (a->Z() + b->Z() + c->Z()) / 3);
  setPlaneResult(ctx, gp_Ax3(centre, gp_Dir(n), gp_Dir(ab)), out);
}

// ---------------------------------------------------------------------------
// TangentPlane
// ---------------------------------------------------------------------------

TangentPlaneFeature::TangentPlaneFeature() : ConstructionFeature(kType) {
  addRef("face", core::mask(core::ShapeKind::Face)).setLabel("Cylindrical face");
  addAngle("angle", 0.0).setLabel("Angle");
  addRef("reference", kPlaneKinds).setLabel("Reference plane").setTooltip("optional: the tangent plane parallel to it");
  addLength("size", 0.0).setLabel("Display size").setTooltip("0 = automatic");
}

void TangentPlaneFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  TopoDS_Shape s = resolveRefProperty(ctx, prop("face"));
  if (s.IsNull() || s.ShapeType() != TopAbs_FACE) {
    ctx.setError("select a cylindrical or conical face");
    return;
  }
  BRepAdaptor_Surface surf(TopoDS::Face(s), false);
  gp_Ax1 axis;
  double radius = 0;
  if (surf.GetType() == GeomAbs_Cylinder) {
    axis = surf.Cylinder().Axis();
    radius = surf.Cylinder().Radius();
  } else {
    ctx.setError("tangent plane needs a cylindrical face");
    return;
  }
  gp_Dir a = axis.Direction();
  gp_Dir radial;
  if (auto refPlane = planeFromRef(ctx, prop("reference"))) {
    gp_Vec n = gp_Vec(refPlane->Direction());
    n -= gp_Vec(a) * n.Dot(gp_Vec(a));
    if (n.Magnitude() < 1e-9) {
      ctx.setError("reference plane is perpendicular to the cylinder axis");
      return;
    }
    radial = gp_Dir(n);
  } else {
    // radial direction towards the face centroid
    gp_Pnt c = core::centroid(s).toPnt();
    gp_Vec v(axis.Location(), c);
    v -= gp_Vec(a) * v.Dot(gp_Vec(a));
    radial = v.Magnitude() > 1e-9 ? gp_Dir(v) : anyPerpendicular(a);
  }
  radial = radial.Rotated(axis, ctx.angle("angle") * M_PI / 180.0);
  gp_Pnt c = core::centroid(s).toPnt();
  double h = gp_Vec(axis.Location(), c).Dot(gp_Vec(a));
  gp_Pnt origin = axis.Location().Translated(gp_Vec(a) * h).Translated(gp_Vec(radial) * radius);
  setPlaneResult(ctx, gp_Ax3(origin, radial, a), out);
}

// ---------------------------------------------------------------------------
// Axis
// ---------------------------------------------------------------------------

AxisFeature::AxisFeature() : ConstructionFeature(kType) {
  addEnum("mode", {"Through Edge / Cylinder", "Through Two Points", "Perpendicular to Face at Point", "Through Two Planes"},
          ThroughEdgeOrCylinder)
      .setLabel("Type");
  addRef("reference", core::kAxisKinds | core::ShapeKind::Face).setLabel("Edge / face");
  addRef("pointA", core::kPointKinds).setLabel("First point");
  addRef("pointB", core::kPointKinds).setLabel("Second point");
  addRef("planeA", kPlaneKinds).setLabel("First plane");
  addRef("planeB", kPlaneKinds).setLabel("Second plane");
  addLength("size", 0.0).setLabel("Display size").setTooltip("0 = automatic");
  updatePropertyFlags();
}

void AxisFeature::updatePropertyFlags() {
  int m = enumIndex("mode");
  prop("reference").flags().hidden = !(m == ThroughEdgeOrCylinder || m == PerpendicularAtPoint);
  prop("pointA").flags().hidden = !(m == TwoPoints || m == PerpendicularAtPoint);
  prop("pointB").flags().hidden = m != TwoPoints;
  prop("planeA").flags().hidden = m != TwoPlanes;
  prop("planeB").flags().hidden = m != TwoPlanes;
}

void AxisFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  switch (enumIndex("mode")) {
    case ThroughEdgeOrCylinder: {
      auto ax = axisFromRef(ctx, prop("reference"));
      if (!ax) {
        ctx.setError("select a linear edge, axis or cylindrical face");
        return;
      }
      setAxisResult(ctx, *ax, out);
      return;
    }
    case TwoPoints: {
      auto a = pointFromRef(ctx, prop("pointA"));
      auto b = pointFromRef(ctx, prop("pointB"));
      if (!a || !b) {
        ctx.setError("select two points");
        return;
      }
      gp_Vec v(*a, *b);
      if (v.Magnitude() < 1e-9) {
        ctx.setError("points coincide");
        return;
      }
      gp_Pnt mid((a->X() + b->X()) / 2, (a->Y() + b->Y()) / 2, (a->Z() + b->Z()) / 2);
      setAxisResult(ctx, gp_Ax1(mid, gp_Dir(v)), out);
      return;
    }
    case PerpendicularAtPoint: {
      auto pl = planeFromRef(ctx, prop("reference"));
      auto p = pointFromRef(ctx, prop("pointA"));
      if (!pl || !p) {
        ctx.setError("select a planar face and a point");
        return;
      }
      setAxisResult(ctx, gp_Ax1(*p, pl->Direction()), out);
      return;
    }
    case TwoPlanes: {
      auto a = planeFromRef(ctx, prop("planeA"));
      auto b = planeFromRef(ctx, prop("planeB"));
      if (!a || !b) {
        ctx.setError("select two planes");
        return;
      }
      IntAna_QuadQuadGeo inter(gp_Pln(*a), gp_Pln(*b), 1e-9, 1e-9);
      if (!inter.IsDone() || inter.NbSolutions() < 1) {
        ctx.setError("planes are parallel");
        return;
      }
      gp_Lin l = inter.Line(1);
      setAxisResult(ctx, gp_Ax1(l.Location(), l.Direction()), out);
      return;
    }
  }
  ctx.setError("unknown axis type");
}

// ---------------------------------------------------------------------------
// Point
// ---------------------------------------------------------------------------

PointFeature::PointFeature() : ConstructionFeature(kType) {
  addEnum("mode", {"At Point / Circle Centre", "Edge and Plane", "Three Planes", "Two Edges"}, AtPoint).setLabel("Type");
  addRef("point", core::kPointKinds | core::ShapeKind::Edge).setLabel("Point / circular edge");
  addRef("edge", core::kAxisKinds).setLabel("Edge");
  addRef("edgeB", core::kAxisKinds).setLabel("Second edge");
  addRef("planeA", kPlaneKinds).setLabel("Plane");
  addRef("planeB", kPlaneKinds).setLabel("Second plane");
  addRef("planeC", kPlaneKinds).setLabel("Third plane");
  updatePropertyFlags();
}

void PointFeature::updatePropertyFlags() {
  int m = enumIndex("mode");
  prop("point").flags().hidden = m != AtPoint;
  prop("edge").flags().hidden = !(m == EdgeAndPlane || m == TwoEdges);
  prop("edgeB").flags().hidden = m != TwoEdges;
  prop("planeA").flags().hidden = !(m == EdgeAndPlane || m == ThreePlanes);
  prop("planeB").flags().hidden = m != ThreePlanes;
  prop("planeC").flags().hidden = m != ThreePlanes;
}

void PointFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  switch (enumIndex("mode")) {
    case AtPoint: {
      auto p = pointFromRef(ctx, prop("point"));
      if (!p) {
        ctx.setError("select a point, vertex or circular edge");
        return;
      }
      setPointResult(*p, out);
      return;
    }
    case EdgeAndPlane: {
      auto ax = axisFromRef(ctx, prop("edge"));
      auto pl = planeFromRef(ctx, prop("planeA"));
      if (!ax || !pl) {
        ctx.setError("select an edge and a plane");
        return;
      }
      auto t = distanceToPlane(ax->Location(), ax->Direction(), gp_Pln(*pl));
      if (!t) {
        ctx.setError("edge is parallel to the plane");
        return;
      }
      setPointResult(ax->Location().Translated(gp_Vec(ax->Direction()) * *t), out);
      return;
    }
    case ThreePlanes: {
      auto a = planeFromRef(ctx, prop("planeA"));
      auto b = planeFromRef(ctx, prop("planeB"));
      auto c = planeFromRef(ctx, prop("planeC"));
      if (!a || !b || !c) {
        ctx.setError("select three planes");
        return;
      }
      IntAna_QuadQuadGeo inter(gp_Pln(*a), gp_Pln(*b), 1e-9, 1e-9);
      if (!inter.IsDone() || inter.NbSolutions() < 1) {
        ctx.setError("the first two planes are parallel");
        return;
      }
      gp_Lin l = inter.Line(1);
      auto t = distanceToPlane(l.Location(), l.Direction(), gp_Pln(*c));
      if (!t) {
        ctx.setError("third plane is parallel to the intersection line");
        return;
      }
      setPointResult(l.Location().Translated(gp_Vec(l.Direction()) * *t), out);
      return;
    }
    case TwoEdges: {
      auto a = axisFromRef(ctx, prop("edge"));
      auto b = axisFromRef(ctx, prop("edgeB"));
      if (!a || !b) {
        ctx.setError("select two linear edges");
        return;
      }
      // closest points of two lines
      gp_Vec d1(a->Direction()), d2(b->Direction()), r(b->Location(), a->Location());
      double aa = d1.Dot(d1), bb = d1.Dot(d2), cc = d2.Dot(d2), dd = d1.Dot(r), ee = d2.Dot(r);
      double den = aa * cc - bb * bb;
      if (std::abs(den) < 1e-12) {
        ctx.setError("edges are parallel");
        return;
      }
      double s = (bb * ee - cc * dd) / den;
      double t = (aa * ee - bb * dd) / den;
      gp_Pnt p1 = a->Location().Translated(d1 * s);
      gp_Pnt p2 = b->Location().Translated(d2 * t);
      if (p1.Distance(p2) > 1e-3) ctx.setWarning("edges do not intersect; using the closest point");
      setPointResult(gp_Pnt((p1.X() + p2.X()) / 2, (p1.Y() + p2.Y()) / 2, (p1.Z() + p2.Z()) / 2), out);
      return;
    }
  }
  ctx.setError("unknown point type");
}

}  // namespace fabricad::part
