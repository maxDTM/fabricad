// FabriCAD part: manipulator bindings per feature type.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/tools/FeatureManipulators.h"

#include "core/Feature.h"
#include "core/ShapeUtils.h"
#include "part/PartFeature.h"
#include "part/ExtraUtils.h"
#include "part/PartUtils.h"
#include "part/features/BodyOps.h"
#include "part/features/Construction.h"
#include "part/features/Extrude.h"
#include "part/features/Fillet.h"
#include "part/features/Hole.h"
#include "part/features/Primitives.h"
#include "part/features/Revolve.h"
#include "part/features/Shell.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepLProp_SLProps.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <ShapeAnalysis_Surface.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Vec.hxx>

#include <cmath>
#include <map>
#include <mutex>

namespace fabricad::part {

using core::ManipulatorKind;
using core::ManipulatorSpec;

namespace {

/// Screen size of the secondary rings (taper) in pixels; arrows use the automatic size.
constexpr double kRingPixels = 32.0;

std::map<std::string, ManipulatorProvider>& providers() {
  static std::map<std::string, ManipulatorProvider> m;
  return m;
}

double px(const PixelSizeFn& fn, const gp_Pnt& at) { return fn ? fn(at) : 0.0; }

ManipulatorSpec linear(const gp_Pnt& origin, const gp_Dir& dir, double value, const std::string& label) {
  ManipulatorSpec s;
  s.kind = ManipulatorKind::Linear;
  s.frame = gp_Ax3(origin, dir, anyPerpendicular(dir));
  s.value = value;
  s.label = label;
  return s;
}

ManipulatorSpec angular(const gp_Pnt& origin, const gp_Dir& axis, const gp_Dir& zero, double value, double size,
                        const std::string& label) {
  ManipulatorSpec s;
  s.kind = ManipulatorKind::Angular;
  gp_Vec x = gp_Vec(zero) - gp_Vec(axis) * zero.Dot(axis);
  s.frame = gp_Ax3(origin, axis, x.Magnitude() > 1e-9 ? gp_Dir(x) : anyPerpendicular(axis));
  s.value = value;
  s.size = size;
  s.label = label;
  return s;
}

/// Midpoint of an edge and the direction into the material at that point (average of the
/// inward normals of the faces sharing the edge). False when the edge is not part of the body.
bool edgeMidAndInward(const TopoDS_Shape& body, const TopoDS_Shape& edgeShape, gp_Pnt& mid, gp_Dir& inward) {
  if (edgeShape.IsNull() || edgeShape.ShapeType() != TopAbs_EDGE) return false;
  const TopoDS_Edge edge = TopoDS::Edge(edgeShape);
  BRepAdaptor_Curve c(edge);
  const double t = 0.5 * (c.FirstParameter() + c.LastParameter());
  mid = c.Value(t);
  TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
  TopExp::MapShapesAndAncestors(body, TopAbs_EDGE, TopAbs_FACE, edgeFaces);
  gp_Vec sum(0, 0, 0);
  if (edgeFaces.Contains(edge)) {
    for (const TopoDS_Shape& fs : edgeFaces.FindFromKey(edge)) {
      const TopoDS_Face face = TopoDS::Face(fs);
      BRepAdaptor_Surface surf(face);
      ShapeAnalysis_Surface sas(BRep_Tool::Surface(face));
      gp_Pnt2d uv = sas.ValueOfUV(mid, 1e-6);
      BRepLProp_SLProps props(surf, uv.X(), uv.Y(), 1, 1e-9);
      if (!props.IsNormalDefined()) continue;
      gp_Dir n = props.Normal();
      if (face.Orientation() == TopAbs_REVERSED) n.Reverse();
      sum -= gp_Vec(n);  // outward normal -> into the material
    }
  }
  if (sum.Magnitude() < 1e-9) {
    // fall back to a direction perpendicular to the edge
    gp_Vec tangent;
    gp_Pnt p;
    c.D1(t, p, tangent);
    inward = tangent.Magnitude() > 1e-12 ? anyPerpendicular(gp_Dir(tangent)) : gp_Dir(0, 0, 1);
    return true;
  }
  inward = gp_Dir(sum);
  return true;
}

// --- Extrude ---------------------------------------------------------------------------------------

std::vector<ManipulatorBinding> extrudeManipulators(PartFeature& f, core::RecomputeContext& ctx, const PixelSizeFn& pxFn) {
  std::vector<ManipulatorBinding> out;
  std::string err;
  std::vector<TopoDS_Face> faces = profileFaces(ctx, f.prop("profiles").refs(), &err);
  if (faces.empty()) return out;
  auto n = faceNormal(faces.front());
  if (!n) return out;
  gp_Dir dir = *n;
  if (f.flag("flip")) dir.Reverse();
  gp_Pnt origin = core::centroid(faces.front()).toPnt();
  origin.Translate(gp_Vec(dir) * f.length("startOffset"));
  const int direction = f.enumIndex("direction");
  const int ext1 = f.enumIndex("extentType");
  const int ext2 = f.enumIndex("extentType2");
  const double d1 = f.length("distance");
  const double d2 = f.length("distance2");

  if (direction == ExtrudeFeature::Symmetric) {
    const double scale = f.enumIndex("symmetricMeasure") == 1 ? 0.5 : 1.0;
    if (ext1 == ExtrudeFeature::Distance) out.push_back({"distance", linear(origin, dir, d1 * scale, "Distance"), scale});
    return out;
  }
  if (ext1 == ExtrudeFeature::Distance) {
    out.push_back({"distance", linear(origin, dir, d1, "Distance"), 1.0});
    // taper ring on the end face: axis in the end plane, zero along the extrusion direction
    const gp_Pnt end = origin.Translated(gp_Vec(dir) * d1);
    const double size = kRingPixels * px(pxFn, end);
    if (size > 0) out.push_back({"taperAngle", angular(end, anyPerpendicular(dir), dir, f.angle("taperAngle"), size, "Taper"), 1.0});
  }
  if (direction == ExtrudeFeature::TwoSides && ext2 == ExtrudeFeature::Distance) {
    const gp_Dir back = dir.Reversed();
    out.push_back({"distance2", linear(origin, back, d2, "Distance 2"), 1.0});
    const gp_Pnt end = origin.Translated(gp_Vec(back) * d2);
    const double size = kRingPixels * px(pxFn, end);
    if (size > 0) out.push_back({"taperAngle2", angular(end, anyPerpendicular(back), back, f.angle("taperAngle2"), size, "Taper 2"), 1.0});
  }
  return out;
}

// --- Revolve -------------------------------------------------------------------------------------------

std::vector<ManipulatorBinding> revolveManipulators(PartFeature& f, core::RecomputeContext& ctx, const PixelSizeFn& pxFn) {
  std::vector<ManipulatorBinding> out;
  if (f.enumIndex("extentType") == RevolveFeature::Full) return out;
  std::string err;
  std::vector<TopoDS_Face> faces = profileFaces(ctx, f.prop("profiles").refs(), &err);
  if (faces.empty() || !f.ref("axis").isValid()) return out;
  std::optional<gp_Ax1> axis = ctx.axis(f.ref("axis"));
  if (!axis) return out;
  gp_Ax1 ax = *axis;
  if (f.flag("flip")) ax.Reverse();
  const gp_Pnt c = core::centroid(faces.front()).toPnt();
  // foot of the centroid on the axis and the radial direction: the ring passes through the profile
  gp_Vec along(ax.Location(), c);
  const gp_Pnt foot = ax.Location().Translated(gp_Vec(ax.Direction()) * along.Dot(gp_Vec(ax.Direction())));
  gp_Vec radial(foot, c);
  const double R = radial.Magnitude();
  const gp_Dir zero = R > 1e-9 ? gp_Dir(radial) : anyPerpendicular(ax.Direction());
  const double size = std::max(R, 40.0 * px(pxFn, foot));
  const int direction = f.enumIndex("direction");
  const double scale = direction == RevolveFeature::Symmetric ? 0.5 : 1.0;
  out.push_back({"angle", angular(foot, ax.Direction(), zero, f.angle("angle") * scale, size, "Angle"), scale});
  if (direction == RevolveFeature::TwoSides)
    out.push_back({"angle2", angular(foot, ax.Direction().Reversed(), zero, f.angle("angle2"), size, "Angle 2"), 1.0});
  return out;
}

// --- Fillet / Chamfer ---------------------------------------------------------------------------------

std::vector<ManipulatorBinding> edgeDressupManipulators(PartFeature& f, core::RecomputeContext& ctx, const std::string& property,
                                                        const std::string& label) {
  std::vector<ManipulatorBinding> out;
  std::vector<ResolvedGroup> groups = resolveByBody(ctx, f.prop("edges").refs(), core::ShapeKind::Edge, true);
  for (const ResolvedGroup& g : groups) {
    if (g.shapes.empty()) continue;
    gp_Pnt mid;
    gp_Dir inward;
    if (!edgeMidAndInward(g.bodyShape, g.shapes.front(), mid, inward)) continue;
    ManipulatorSpec s = linear(mid, inward, f.length(property), label);
    s.minValue = 0.0;
    out.push_back({property, s, 1.0});
    break;
  }
  return out;
}

std::vector<ManipulatorBinding> filletManipulators(PartFeature& f, core::RecomputeContext& ctx, const PixelSizeFn&) {
  return edgeDressupManipulators(f, ctx, "radius", "Radius");
}

std::vector<ManipulatorBinding> chamferManipulators(PartFeature& f, core::RecomputeContext& ctx, const PixelSizeFn&) {
  return edgeDressupManipulators(f, ctx, "distance", "Distance");
}

// --- Shell -----------------------------------------------------------------------------------------------

std::vector<ManipulatorBinding> shellManipulators(PartFeature& f, core::RecomputeContext& ctx, const PixelSizeFn&) {
  std::vector<ManipulatorBinding> out;
  const bool inside = f.enumIndex("direction") == ShellFeature::Inside;
  std::vector<ResolvedGroup> groups = resolveByBody(ctx, f.prop("faces").refs(), core::ShapeKind::Face);
  for (const ResolvedGroup& g : groups) {
    if (g.shapes.empty()) continue;
    auto n = faceNormal(g.shapes.front());
    gp_Pnt at = core::centroid(g.shapes.front()).toPnt();
    gp_Dir dir = n ? *n : gp_Dir(0, 0, 1);
    if (inside) dir.Reverse();
    ManipulatorSpec s = linear(at, dir, f.length("thickness"), "Thickness");
    s.minValue = 0.0;
    out.push_back({"thickness", s, 1.0});
    return out;
  }
  // whole bodies: arrow above the first body
  for (auto& bref : f.prop("bodies").refs()) {
    TopoDS_Shape body = ctx.resolve(bref);
    if (body.IsNull()) continue;
    core::Vec3 lo, hi;
    if (!core::bbox(body, lo, hi)) continue;
    const gp_Pnt top(0.5 * (lo.x + hi.x), 0.5 * (lo.y + hi.y), hi.z);
    ManipulatorSpec s = linear(top, inside ? gp_Dir(0, 0, -1) : gp_Dir(0, 0, 1), f.length("thickness"), "Thickness");
    s.minValue = 0.0;
    out.push_back({"thickness", s, 1.0});
    break;
  }
  return out;
}

// --- Offset Face (Press Pull) ---------------------------------------------------------------------------

std::vector<ManipulatorBinding> offsetFaceManipulators(PartFeature& f, core::RecomputeContext& ctx, const PixelSizeFn&) {
  std::vector<ManipulatorBinding> out;
  std::vector<ResolvedGroup> groups = resolveByBody(ctx, f.prop("faces").refs(), core::ShapeKind::Face);
  for (const ResolvedGroup& g : groups) {
    if (g.shapes.empty()) continue;
    auto n = faceNormal(g.shapes.front());
    if (!n) continue;
    out.push_back({"distance", linear(core::centroid(g.shapes.front()).toPnt(), *n, f.length("distance"), "Distance"), 1.0});
    break;
  }
  return out;
}

// --- Offset Plane ----------------------------------------------------------------------------------------

std::vector<ManipulatorBinding> offsetPlaneManipulators(PartFeature& f, core::RecomputeContext& ctx, const PixelSizeFn&) {
  std::vector<ManipulatorBinding> out;
  if (!f.ref("plane").isValid()) return out;
  std::optional<gp_Ax3> base = ctx.plane(f.ref("plane"));
  if (!base) return out;
  gp_Dir dir = base->Direction();
  if (f.flag("flip")) dir.Reverse();
  out.push_back({"distance", linear(base->Location(), dir, f.length("distance"), "Distance"), 1.0});
  return out;
}

// --- Hole ---------------------------------------------------------------------------------------------------

std::vector<ManipulatorBinding> holeManipulators(PartFeature& f, core::RecomputeContext& ctx, const PixelSizeFn&) {
  std::vector<ManipulatorBinding> out;
  if (f.enumIndex("extentType") == HoleFeature::All) return out;
  std::optional<gp_Dir> normal;
  if (f.ref("face").isValid()) {
    core::ShapeRef faceRef = f.ref("face");
    TopoDS_Shape face = ctx.resolve(faceRef);
    if (!face.IsNull()) normal = faceNormal(face);
  }
  std::optional<gp_Pnt> centre;
  for (auto& r : f.prop("points").refs()) {
    if (!normal && r.kind == core::ShapeKind::SketchPoint) {
      if (const core::IProfileSource* ps = ctx.profileSource(r)) normal = ps->sketchPlane().Direction();
    }
    if (!centre) centre = ctx.point(r);
    if (centre && normal) break;
  }
  if (!centre) return out;
  if (!normal) normal = gp_Dir(0, 0, 1);
  gp_Dir axis = normal->Reversed();  // into the material
  if (f.flag("flip")) axis.Reverse();
  ManipulatorSpec s = linear(*centre, axis, f.length("depth"), "Depth");
  s.minValue = 0.0;
  out.push_back({"depth", s, 1.0});
  return out;
}

// --- Primitives (round 2): placed on a plane at x/y; height along the normal, diameters radial ---

std::optional<gp_Ax3> primitiveFrame(PartFeature& f, core::RecomputeContext& ctx) {
  std::optional<gp_Ax3> plane = resolvePlaneInput(ctx, f.prop("plane"));
  if (!plane) return std::nullopt;
  gp_Ax3 frame = *plane;
  frame.Translate(gp_Vec(plane->XDirection()) * f.length("x") + gp_Vec(plane->YDirection()) * f.length("y"));
  return frame;
}

/// Height arrow of a Box / Cylinder / Coil: from the placement point along the normal (negative
/// heights go to the other side); symmetric primitives show half the height, like Extrude.
void addHeightArrow(std::vector<ManipulatorBinding>& out, PartFeature& f, const gp_Ax3& frame, bool symmetric) {
  const double h = f.length("height");
  ManipulatorSpec s = linear(frame.Location(), frame.Direction(), symmetric ? std::abs(h) / 2 : h, "Height");
  if (symmetric) s.minValue = 0.0;
  out.push_back({"height", s, symmetric ? 0.5 : 1.0});
}

/// Diameter arrow: radial along the frame X from the centre; the handle sits on the surface (radius).
void addDiameterArrow(std::vector<ManipulatorBinding>& out, PartFeature& f, const gp_Ax3& frame, const std::string& prop,
                      const std::string& label, double alongNormal = 0.0) {
  gp_Pnt origin = frame.Location().Translated(gp_Vec(frame.Direction()) * alongNormal);
  ManipulatorSpec s = linear(origin, frame.XDirection(), f.length(prop) / 2, label);
  s.minValue = 0.0;
  out.push_back({prop, s, 0.5});
}

std::vector<ManipulatorBinding> boxManipulators(PartFeature& f, core::RecomputeContext& ctx, const PixelSizeFn&) {
  std::vector<ManipulatorBinding> out;
  if (auto frame = primitiveFrame(f, ctx)) addHeightArrow(out, f, *frame, f.enumIndex("direction") == PrimitiveFeature::Symmetric);
  return out;
}

std::vector<ManipulatorBinding> cylinderManipulators(PartFeature& f, core::RecomputeContext& ctx, const PixelSizeFn&) {
  std::vector<ManipulatorBinding> out;
  if (auto frame = primitiveFrame(f, ctx)) {
    const bool sym = f.enumIndex("direction") == PrimitiveFeature::Symmetric;
    addHeightArrow(out, f, *frame, sym);
    const double h = f.length("height");
    // diameter arrow at the base of the cylinder (or the mid plane when symmetric)
    addDiameterArrow(out, f, *frame, "diameter", "Diameter", sym ? 0.0 : std::min(0.0, h));
  }
  return out;
}

std::vector<ManipulatorBinding> sphereManipulators(PartFeature& f, core::RecomputeContext& ctx, const PixelSizeFn&) {
  std::vector<ManipulatorBinding> out;
  if (auto frame = primitiveFrame(f, ctx)) addDiameterArrow(out, f, *frame, "diameter", "Diameter");
  return out;
}

std::vector<ManipulatorBinding> torusManipulators(PartFeature& f, core::RecomputeContext& ctx, const PixelSizeFn&) {
  std::vector<ManipulatorBinding> out;
  if (auto frame = primitiveFrame(f, ctx)) {
    addDiameterArrow(out, f, *frame, "majorDiameter", "Diameter");
    // the ring's own diameter: an arrow from the centre line of the ring outwards along X
    gp_Pnt ring = frame->Location().Translated(gp_Vec(frame->XDirection()) * (f.length("majorDiameter") / 2));
    ManipulatorSpec s = linear(ring, frame->XDirection(), f.length("minorDiameter") / 2, "Torus diameter");
    s.minValue = 0.0;
    out.push_back({"minorDiameter", s, 0.5});
  }
  return out;
}

std::vector<ManipulatorBinding> coilManipulators(PartFeature& f, core::RecomputeContext& ctx, const PixelSizeFn&) {
  std::vector<ManipulatorBinding> out;
  if (auto frame = primitiveFrame(f, ctx)) {
    addDiameterArrow(out, f, *frame, "diameter", "Diameter");
    if (f.enumIndex("coilType") != CoilFeature::RevolutionPitch) {
      ManipulatorSpec s = linear(frame->Location(), frame->Direction(), f.length("height"), "Height");
      s.minValue = 0.0;
      out.push_back({"height", s, 1.0});
    }
  }
  return out;
}

// --- Thicken / Rib (round 2) -------------------------------------------------------------------------

std::vector<ManipulatorBinding> thickenManipulators(PartFeature& f, core::RecomputeContext& ctx, const PixelSizeFn&) {
  std::vector<ManipulatorBinding> out;
  const auto& refs = f.prop("faces").refs();
  if (refs.empty()) return out;
  core::ShapeRef ref = refs.front();
  TopoDS_Shape face = ctx.resolve(ref);
  if (face.IsNull()) return out;
  auto n = faceNormal(face);
  if (!n) return out;
  GProp_GProps props;
  BRepGProp::SurfaceProperties(face, props);
  const bool sym = f.enumIndex("direction") == ThickenFeature::Symmetric;
  const double t = f.length("thickness");
  ManipulatorSpec s = linear(props.CentreOfMass(), *n, sym ? std::abs(t) / 2 : t, "Thickness");
  if (sym) s.minValue = 0.0;
  out.push_back({"thickness", s, sym ? 0.5 : 1.0});
  return out;
}

std::vector<ManipulatorBinding> ribManipulators(PartFeature& f, core::RecomputeContext& ctx, const PixelSizeFn&) {
  std::vector<ManipulatorBinding> out;
  const auto& refs = f.prop("curves").refs();
  if (refs.empty()) return out;
  core::ShapeRef ref = refs.front();
  std::optional<gp_Dir> normal;
  if (f.ref("plane").isValid())
    if (auto pl = resolvePlaneInput(ctx, f.prop("plane"))) normal = pl->Direction();
  if (!normal && ref.kind == core::ShapeKind::SketchCurve)
    if (const core::IProfileSource* ps = ctx.profileSource(ref)) normal = ps->sketchPlane().Direction();
  if (!normal) return out;
  TopoDS_Shape edge;
  if (ref.kind == core::ShapeKind::SketchCurve) {
    if (const core::IProfileSource* ps = ctx.profileSource(ref)) edge = ps->curveEdge(ref.index);
  } else {
    edge = ctx.resolve(ref);
  }
  if (edge.IsNull() || edge.ShapeType() != TopAbs_EDGE) return out;
  BRepAdaptor_Curve c(TopoDS::Edge(edge));
  const gp_Pnt mid = c.Value(0.5 * (c.FirstParameter() + c.LastParameter()));
  const int tt = f.enumIndex("thicknessType");
  const double t = f.length("thickness");
  gp_Dir dir = *normal;
  if (tt == RibFeature::OtherSide) dir.Reverse();
  const bool sym = tt == RibFeature::Symmetric;
  ManipulatorSpec s = linear(mid, dir, sym ? t / 2 : t, "Thickness");
  s.minValue = 0.0;
  out.push_back({"thickness", s, sym ? 0.5 : 1.0});
  return out;
}

void installBuiltins() {
  static std::once_flag once;
  std::call_once(once, [] {
    auto& m = providers();
    m.emplace(BoxFeature::kType, boxManipulators);
    m.emplace(CylinderFeature::kType, cylinderManipulators);
    m.emplace(SphereFeature::kType, sphereManipulators);
    m.emplace(TorusFeature::kType, torusManipulators);
    m.emplace(CoilFeature::kType, coilManipulators);
    m.emplace(ThickenFeature::kType, thickenManipulators);
    m.emplace(RibFeature::kType, ribManipulators);
    m.emplace(ExtrudeFeature::kType, extrudeManipulators);
    m.emplace(RevolveFeature::kType, revolveManipulators);
    m.emplace(FilletFeature::kType, filletManipulators);
    m.emplace(ChamferFeature::kType, chamferManipulators);
    m.emplace(ShellFeature::kType, shellManipulators);
    m.emplace(OffsetFaceFeature::kType, offsetFaceManipulators);
    m.emplace(OffsetPlaneFeature::kType, offsetPlaneManipulators);
    m.emplace(HoleFeature::kType, holeManipulators);
  });
}

}  // namespace

std::vector<ManipulatorBinding> manipulatorsFor(PartFeature& feature, core::RecomputeContext& ctx, const PixelSizeFn& pixelSize) {
  installBuiltins();
  auto it = providers().find(feature.type());
  if (it == providers().end()) return {};
  std::vector<ManipulatorBinding> out;
  try {
    out = it->second(feature, ctx, pixelSize);
  } catch (...) {
    return {};
  }
  // drop bindings whose property is hidden or missing
  std::vector<ManipulatorBinding> kept;
  for (auto& b : out) {
    const core::Property* p = feature.property(b.property);
    if (!p || !p->isNumeric() || p->flags().hidden) continue;
    kept.push_back(std::move(b));
  }
  return kept;
}

void registerManipulatorProvider(const std::string& featureType, ManipulatorProvider provider) {
  installBuiltins();
  providers()[featureType] = std::move(provider);
}

bool hasManipulatorProvider(const std::string& featureType) {
  installBuiltins();
  return providers().count(featureType) > 0;
}

}  // namespace fabricad::part
