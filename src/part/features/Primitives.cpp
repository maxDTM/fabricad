// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/features/Primitives.h"

#include "core/Feature.h"
#include "core/ShapeUtils.h"
#include "part/ExtraUtils.h"
#include "part/PartUtils.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <BRepPrim_Cylinder.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>

#include <cmath>
#include <format>

namespace fabricad::part {

namespace {
constexpr auto kPlaneInputKinds = core::kPlaneKinds | core::ShapeKind::Sketch | core::ShapeKind::Face;
}

// ---------------------------------------------------------------------------
// PrimitiveFeature
// ---------------------------------------------------------------------------

void PrimitiveFeature::addPlacementProperties() {
  addRef("plane", kPlaneInputKinds)
      .setLabel("Plane")
      .setTooltip("Plane or planar face the primitive sits on")
      .setRef(core::ShapeRef::originPlane("XY"));
  addLength("x", 0.0).setLabel("Position X").setTooltip("Centre position along the plane's X direction");
  addLength("y", 0.0).setLabel("Position Y").setTooltip("Centre position along the plane's Y direction");
}

void PrimitiveFeature::addTail() { addOperationProperties(*this, Operation::NewBody); }

void PrimitiveFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  std::optional<gp_Ax3> plane = resolvePlaneInput(ctx, prop("plane"));
  if (!plane) {
    ctx.setError("select a plane or planar face");
    return;
  }
  const double x = ctx.length("x"), y = ctx.length("y");
  gp_Ax3 frame = *plane;
  frame.Translate(gp_Vec(plane->XDirection()) * x + gp_Vec(plane->YDirection()) * y);

  NameMap local;
  TopoDS_Shape s;
  try {
    s = makeLocal(ctx, local);
  } catch (const Standard_Failure& e) {
    ctx.setError(std::format("{} failed: {}", type(), e.GetMessageString() ? e.GetMessageString() : "OCCT error"));
    return;
  }
  if (ctx.hasError()) return;
  if (s.IsNull()) {
    ctx.setError(std::format("{} could not be built", type()));
    return;
  }
  // One builder for the solid and its named faces: transforming them separately would give
  // the faces different locations than the ones inside the placed solid.
  BRepBuilderAPI_Transform tr(s, frameTransform(frame), true);
  TopoDS_Shape placed = tr.Shape();
  const std::string base = name().empty() ? type() : name();
  NameMap names;
  for (const auto& [n, sub] : local) {
    TopoDS_Shape m = tr.ModifiedShape(sub);
    if (!m.IsNull()) names[base + "/" + n] = m;
  }
  applyTool(ctx, placed, operation(), prop("targets").refs(), names, out);
}

// ---------------------------------------------------------------------------
// Box
// ---------------------------------------------------------------------------

BoxFeature::BoxFeature() : PrimitiveFeature(kType) {
  addPlacementProperties();
  addLength("length", 20.0).setLabel("Length").setTooltip("Size along the plane's X direction");
  addLength("width", 20.0).setLabel("Width").setTooltip("Size along the plane's Y direction");
  addLength("height", 10.0).setLabel("Height").setTooltip("Size along the plane normal (negative = other side)");
  addEnum("direction", {"One Side", "Symmetric"}, OneSide).setLabel("Direction");
  addTail();
}

TopoDS_Shape BoxFeature::makeLocal(core::RecomputeContext& ctx, NameMap& names) {
  const double l = ctx.length("length"), w = ctx.length("width"), h = ctx.length("height");
  if (l <= 1e-9 || w <= 1e-9 || std::abs(h) <= 1e-9) {
    ctx.setError("length and width must be positive and height must not be zero");
    return {};
  }
  double z0 = enumIndex("direction") == Symmetric ? -std::abs(h) / 2 : std::min(0.0, h);
  BRepPrimAPI_MakeBox mk(gp_Ax2(gp_Pnt(-l / 2, -w / 2, z0), gp::DZ(), gp::DX()), l, w, std::abs(h));
  mk.Build();
  names["Bottom"] = mk.BottomFace();
  names["Top"] = mk.TopFace();
  names["Front"] = mk.FrontFace();
  names["Back"] = mk.BackFace();
  names["Left"] = mk.LeftFace();
  names["Right"] = mk.RightFace();
  return mk.Shape();
}

// ---------------------------------------------------------------------------
// Cylinder
// ---------------------------------------------------------------------------

CylinderFeature::CylinderFeature() : PrimitiveFeature(kType) {
  addPlacementProperties();
  addLength("diameter", 20.0).setLabel("Diameter");
  addLength("height", 20.0).setLabel("Height").setTooltip("Along the plane normal (negative = other side)");
  addEnum("direction", {"One Side", "Symmetric"}, OneSide).setLabel("Direction");
  addTail();
}

TopoDS_Shape CylinderFeature::makeLocal(core::RecomputeContext& ctx, NameMap& names) {
  const double d = ctx.length("diameter"), h = ctx.length("height");
  if (d <= 1e-9 || std::abs(h) <= 1e-9) {
    ctx.setError("diameter must be positive and height must not be zero");
    return {};
  }
  double z0 = enumIndex("direction") == Symmetric ? -std::abs(h) / 2 : std::min(0.0, h);
  BRepPrimAPI_MakeCylinder mk(gp_Ax2(gp_Pnt(0, 0, z0), gp::DZ(), gp::DX()), d / 2, std::abs(h));
  mk.Build();
  BRepPrim_Cylinder& prim = mk.Cylinder();
  names["Bottom"] = prim.BottomFace();
  names["Top"] = prim.TopFace();
  names["Side"] = prim.LateralFace();
  return mk.Shape();
}

// ---------------------------------------------------------------------------
// Sphere
// ---------------------------------------------------------------------------

SphereFeature::SphereFeature() : PrimitiveFeature(kType) {
  addPlacementProperties();
  addLength("diameter", 20.0).setLabel("Diameter");
  addTail();
}

TopoDS_Shape SphereFeature::makeLocal(core::RecomputeContext& ctx, NameMap& names) {
  const double d = ctx.length("diameter");
  if (d <= 1e-9) {
    ctx.setError("diameter must be positive");
    return {};
  }
  TopoDS_Shape s = BRepPrimAPI_MakeSphere(gp_Ax2(gp::Origin(), gp::DZ(), gp::DX()), d / 2).Shape();
  for (const auto& f : facesOf(s)) names["Surface"] = f;
  return s;
}

// ---------------------------------------------------------------------------
// Torus
// ---------------------------------------------------------------------------

TorusFeature::TorusFeature() : PrimitiveFeature(kType) {
  addPlacementProperties();
  addLength("majorDiameter", 40.0).setLabel("Diameter").setTooltip("Diameter of the centre line of the ring");
  addLength("minorDiameter", 10.0).setLabel("Torus diameter").setTooltip("Diameter of the ring's cross-section");
  addTail();
}

void TorusFeature::applyDefaultsForNewFeature(const core::Document& doc) {
  PrimitiveFeature::applyDefaultsForNewFeature(doc);
  core::Property& major = prop("majorDiameter");
  core::Property& minor = prop("minorDiameter");
  if (major.toDouble() <= 0) major.setDouble(40.0);
  if (minor.toDouble() <= 0 || minor.toDouble() >= major.toDouble()) minor.setDouble(major.toDouble() / 4);
}

TopoDS_Shape TorusFeature::makeLocal(core::RecomputeContext& ctx, NameMap& names) {
  const double big = ctx.length("majorDiameter") / 2, small = ctx.length("minorDiameter") / 2;
  if (big <= 1e-9 || small <= 1e-9) {
    ctx.setError("diameters must be positive");
    return {};
  }
  if (small >= big - 1e-9) {
    ctx.setError("the torus diameter must be smaller than the ring diameter");
    return {};
  }
  TopoDS_Shape s = BRepPrimAPI_MakeTorus(gp_Ax2(gp::Origin(), gp::DZ(), gp::DX()), big, small).Shape();
  for (const auto& f : facesOf(s)) names["Surface"] = f;
  return s;
}

// ---------------------------------------------------------------------------
// Coil
// ---------------------------------------------------------------------------

CoilFeature::CoilFeature() : PrimitiveFeature(kType) {
  addPlacementProperties();
  addLength("diameter", 40.0).setLabel("Diameter").setTooltip("Coil diameter (where the section sits depends on Section Position)");
  addEnum("coilType", {"Revolution and Height", "Revolution and Pitch", "Height and Pitch"}, RevolutionHeight).setLabel("Type");
  addDouble("revolutions", 4.0).setLabel("Revolutions");
  addLength("height", 40.0).setLabel("Height");
  addLength("pitch", 10.0).setLabel("Pitch");
  addAngle("angle", 0.0).setLabel("Angle").setTooltip("Taper angle (positive narrows the coil upwards)");
  addEnum("section", {"Circular", "Square", "Triangular (External)", "Triangular (Internal)"}, Circular).setLabel("Section");
  addEnum("sectionPosition", {"Inside", "On Center", "Outside"}, OnCenter).setLabel("Section position");
  addLength("sectionSize", 4.0).setLabel("Section size");
  addBool("clockwise", false).setLabel("Clockwise").setTooltip("Left-handed coil (clockwise seen from the plane normal)");
  addTail();
  updatePropertyFlags();
}

void CoilFeature::updatePropertyFlags() {
  int t = enumIndex("coilType");
  prop("revolutions").flags().hidden = t == HeightPitch;
  prop("height").flags().hidden = t == RevolutionPitch;
  prop("pitch").flags().hidden = t == RevolutionHeight;
}

std::optional<CoilFeature::Helix> CoilFeature::helixFor(int type, double revolutions, double height, double pitch,
                                                        std::string* error) {
  auto fail = [&](const char* m) -> std::optional<Helix> {
    if (error) *error = m;
    return std::nullopt;
  };
  Helix h;
  switch (type) {
    case RevolutionHeight:
      if (revolutions <= 1e-9 || height <= 1e-9) return fail("revolutions and height must be positive");
      h = {revolutions, height / revolutions, height};
      break;
    case RevolutionPitch:
      if (revolutions <= 1e-9 || pitch <= 1e-9) return fail("revolutions and pitch must be positive");
      h = {revolutions, pitch, pitch * revolutions};
      break;
    default:
      if (height <= 1e-9 || pitch <= 1e-9) return fail("height and pitch must be positive");
      h = {height / pitch, pitch, height};
      break;
  }
  return h;
}

TopoDS_Shape CoilFeature::makeLocal(core::RecomputeContext& ctx, NameMap& names) {
  std::string err;
  auto hx = helixFor(enumIndex("coilType"), ctx.number("revolutions"), ctx.length("height"), ctx.length("pitch"), &err);
  if (!hx) {
    ctx.setError(err);
    return {};
  }
  const double d = ctx.length("diameter"), s = ctx.length("sectionSize"), angle = ctx.angle("angle");
  if (d <= 1e-9 || s <= 1e-9) {
    ctx.setError("diameter and section size must be positive");
    return {};
  }
  if (std::abs(angle) >= 89) {
    ctx.setError("taper angle must be between -89 and 89 degrees");
    return {};
  }
  if (hx->pitch <= s + 1e-6) {
    ctx.setError("pitch must be larger than the section size (the turns would overlap)");
    return {};
  }
  const int section = enumIndex("section");
  const double tri = s * std::sqrt(3.0) / 2;  // triangle height
  // radial half extents of the section about its centre (inner, outer)
  double inner = s / 2, outer = s / 2;
  if (section == TriangularExternal) inner = tri / 3, outer = 2 * tri / 3;
  if (section == TriangularInternal) inner = 2 * tri / 3, outer = tri / 3;
  double rc = d / 2;
  switch (enumIndex("sectionPosition")) {
    case Inside: rc = d / 2 - outer; break;
    case Outside: rc = d / 2 + inner; break;
    default: break;
  }
  if (rc - inner <= 1e-6) {
    ctx.setError("the section reaches the coil axis (increase the diameter)");
    return {};
  }
  if (std::abs(angle) > 1e-9) {
    double rEnd = rc - hx->height * std::tan(angle * M_PI / 180.0);
    if (rEnd - inner <= 1e-6) {
      ctx.setError("the taper makes the coil reach its axis");
      return {};
    }
  }
  TopoDS_Wire prof;
  if (section == Circular) {
    gp_Circ c(gp_Ax2(gp_Pnt(rc, 0, 0), gp::DY(), gp::DX()), s / 2);
    prof = BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(c).Edge()).Wire();
  } else if (section == Square) {
    prof = polygonWire({{rc - s / 2, 0, -s / 2}, {rc + s / 2, 0, -s / 2}, {rc + s / 2, 0, s / 2}, {rc - s / 2, 0, s / 2}});
  } else if (section == TriangularExternal) {
    prof = polygonWire({{rc - inner, 0, -s / 2}, {rc + outer, 0, 0}, {rc - inner, 0, s / 2}});
  } else {
    prof = polygonWire({{rc + outer, 0, -s / 2}, {rc + outer, 0, s / 2}, {rc - inner, 0, 0}});
  }
  TopoDS_Wire helix = makeHelix(rc, hx->pitch, hx->height, angle, flag("clockwise"));
  if (helix.IsNull()) {
    ctx.setError("could not build the coil helix");
    return {};
  }
  TopoDS_Shape solid = sweepAlongHelix(prof, helix, &err);
  if (solid.IsNull()) {
    ctx.setError(err);
    return {};
  }
  std::vector<TopoDS_Shape> caps, sides;
  for (const auto& f : facesOf(solid)) {
    BRepAdaptor_Surface a(TopoDS::Face(f), false);
    (a.GetType() == GeomAbs_Plane ? caps : sides).push_back(f);
  }
  std::sort(caps.begin(), caps.end(), [](const TopoDS_Shape& a, const TopoDS_Shape& b) {
    return core::centroid(a).z < core::centroid(b).z;  // start cap (z ~ 0) first
  });
  std::sort(sides.begin(), sides.end(), shapeLess);
  if (caps.size() >= 1) names["Start"] = caps.front();
  if (caps.size() >= 2) names["End"] = caps.back();
  for (size_t i = 0; i < sides.size(); ++i) names[std::format("Side/{}", i + 1)] = sides[i];
  return solid;
}

}  // namespace fabricad::part
