// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/features/Extrude.h"

#include "core/Feature.h"
#include "core/ShapeUtils.h"
#include "part/PartUtils.h"

#include <BRepAlgoAPI_Common.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <TopoDS.hxx>
#include <gp_Pln.hxx>

#include <cmath>
#include <format>

namespace fabricad::part {

ExtrudeFeature::ExtrudeFeature() : PartFeature(kType) {
  addRefs("profiles", core::ShapeKind::Profile | core::ShapeKind::Face | core::ShapeKind::Sketch).setLabel("Profiles");
  addLength("startOffset", 0.0).setLabel("Start offset").setTooltip("Offset of the start plane along the direction");
  addEnum("direction", {"One Side", "Two Sides", "Symmetric"}, OneSide).setLabel("Direction");
  addEnum("extentType", {"Distance", "To Object", "All"}, Distance).setLabel("Extent");
  addLength("distance", 10.0).setLabel("Distance");
  addRef("toObject", core::ShapeKind::Face | core::ShapeKind::ConstructionPlane | core::ShapeKind::OriginPlane).setLabel("To object");
  addAngle("taperAngle", 0.0).setLabel("Taper angle");
  addEnum("extentType2", {"Distance", "To Object", "All"}, Distance).setLabel("Extent (side 2)");
  addLength("distance2", 10.0).setLabel("Distance (side 2)");
  addRef("toObject2", core::ShapeKind::Face | core::ShapeKind::ConstructionPlane | core::ShapeKind::OriginPlane).setLabel("To object (side 2)");
  addAngle("taperAngle2", 0.0).setLabel("Taper angle (side 2)");
  addEnum("symmetricMeasure", {"Half Length", "Whole Length"}, 0).setLabel("Measurement");
  addBool("flip", false).setLabel("Flip direction");
  addOperationProperties(*this, Operation::Join);
  updatePropertyFlags();
}

void ExtrudeFeature::updatePropertyFlags() {
  int dir = enumIndex("direction");
  int ext = enumIndex("extentType");
  int ext2 = enumIndex("extentType2");
  bool two = dir == TwoSides;
  prop("distance").flags().hidden = ext != Distance;
  prop("toObject").flags().hidden = ext != ToObject;
  prop("extentType2").flags().hidden = !two;
  prop("distance2").flags().hidden = !two || ext2 != Distance;
  prop("toObject2").flags().hidden = !two || ext2 != ToObject;
  prop("taperAngle2").flags().hidden = !two;
  prop("symmetricMeasure").flags().hidden = dir != Symmetric;
}

namespace {

struct Side {
  int extent = ExtrudeFeature::Distance;
  double distance = 0;
  core::ShapeRef* toObject = nullptr;
  double taper = 0;
};

/// Build one side of the extrusion for one profile face. `dir` is the side's direction.
TopoDS_Shape buildSide(core::RecomputeContext& ctx, const TopoDS_Face& face, gp_Dir dir, const Side& side,
                       const std::string& prefix, NameMap& names, std::string& err) {
  double length = 0;
  switch (side.extent) {
    case ExtrudeFeature::Distance:
      length = side.distance;
      if (std::abs(length) < 1e-9) {
        err = "distance must not be zero";
        return {};
      }
      if (length < 0) {
        dir.Reverse();
        length = -length;
      }
      return prismFace(face, dir, length, side.taper, prefix, names, &err);
    case ExtrudeFeature::All:
      length = throughAllLength(ctx, {face});
      return prismFace(face, dir, length, side.taper, prefix, names, &err);
    case ExtrudeFeature::ToObject: {
      if (!side.toObject || !side.toObject->isValid()) {
        err = "select an object to extrude to";
        return {};
      }
      gp_Pnt start = core::centroid(face).toPnt();
      // planar target: exact length
      std::optional<gp_Ax3> plane;
      TopoDS_Shape targetShape;
      if (side.toObject->kind == core::ShapeKind::Face) {
        targetShape = resolveRef(ctx, *side.toObject);
        if (targetShape.IsNull()) {
          err = "target face not found";
          return {};
        }
        plane = core::facePlane(targetShape);
      } else {
        plane = ctx.plane(*side.toObject);
        if (!plane) {
          err = "target plane not found";
          return {};
        }
      }
      if (plane) {
        auto t = distanceToPlane(start, dir, gp_Pln(*plane));
        if (!t) {
          err = "target plane is parallel to the extrusion direction";
          return {};
        }
        if (*t < 1e-9) {
          err = "the target object is behind the profile";
          return {};
        }
        return prismFace(face, dir, *t, side.taper, prefix, names, &err);
      }
      // non-planar face: long prism clipped by the half-space of the target surface
      double L = throughAllLength(ctx, {face, targetShape});
      TopoDS_Shape prism = prismFace(face, dir, L, side.taper, prefix, names, &err);
      if (prism.IsNull()) return {};
      TopoDS_Shape half = halfSpaceOf(TopoDS::Face(targetShape), start, L);
      if (half.IsNull()) {
        err = "cannot build the extent from the target face";
        return {};
      }
      BRepAlgoAPI_Common common(prism, half);
      if (!common.IsDone() || common.Shape().IsNull() || !hasSolid(common.Shape())) {
        err = "the extrusion does not reach the target face";
        return {};
      }
      TopoDS_Shape r = solidsToShape(solidsOf(common.Shape()));
      NameMap mapped = mapNames(names, History::of(common), r);
      names = mapped;
      // the clipped end face is whatever the target generated
      for (const auto& f : facesOf(r)) {
        if (nameOf(names, f).empty()) {
          names[prefix + "End"] = f;
          break;
        }
      }
      return r;
    }
  }
  err = "unknown extent type";
  return {};
}

}  // namespace

void ExtrudeFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  std::string err;
  std::vector<TopoDS_Face> faces = profileFaces(ctx, prop("profiles").refs(), &err);
  if (faces.empty()) {
    ctx.setError(err.empty() ? "select a profile to extrude" : err);
    return;
  }
  const int direction = enumIndex("direction");
  const bool flip = flag("flip");
  const double startOffset = ctx.length("startOffset");
  core::ShapeRef to1 = ref("toObject"), to2 = ref("toObject2");
  Side s1{enumIndex("extentType"), ctx.length("distance"), &to1, ctx.angle("taperAngle")};
  Side s2{enumIndex("extentType2"), ctx.length("distance2"), &to2, ctx.angle("taperAngle2")};

  std::vector<TopoDS_Shape> solids;
  NameMap names;
  const std::string base = name().empty() ? std::string(kType) : name();
  int pi = 0;
  for (TopoDS_Face face : faces) {
    ++pi;
    std::string prefix = faces.size() > 1 ? std::format("{}/P{}/", base, pi) : base + "/";
    auto n = faceNormal(face);
    if (!n) {
      ctx.setError("profile is not planar");
      return;
    }
    gp_Dir dir = *n;
    if (flip) dir.Reverse();
    if (std::abs(startOffset) > 1e-9) {
      gp_Trsf t;
      t.SetTranslation(gp_Vec(dir) * startOffset);
      face = TopoDS::Face(BRepBuilderAPI_Transform(face, t, Standard_True).Shape());
    }

    TopoDS_Shape solid;
    if (direction == OneSide) {
      solid = buildSide(ctx, face, dir, s1, prefix, names, err);
    } else if (direction == Symmetric) {
      double d = s1.extent == All ? throughAllLength(ctx, {face}) : std::abs(s1.distance);
      if (s1.extent == ToObject) {
        ctx.setError("symmetric extrusion does not support 'To Object'");
        return;
      }
      if (enumIndex("symmetricMeasure") == 1) d *= 0.5;  // whole length -> half each side
      if (d < 1e-9) {
        ctx.setError("distance must not be zero");
        return;
      }
      if (std::abs(s1.taper) > 1e-9) {
        Side a = s1, b = s1;
        a.extent = b.extent = Distance;
        a.distance = b.distance = d;
        TopoDS_Shape sa = buildSide(ctx, face, dir, a, prefix, names, err);
        if (sa.IsNull()) break;
        TopoDS_Shape sb = buildSide(ctx, face, dir.Reversed(), b, prefix + "2/", names, err);
        if (sb.IsNull()) break;
        BoolResult r = fuseAll({sa, sb});
        if (!r) {
          err = "could not join the two sides: " + r.error;
          break;
        }
        names = mapNames(names, History::of(r.history), r.shape);
        solid = r.shape;
      } else {
        gp_Trsf t;
        t.SetTranslation(gp_Vec(dir) * -d);
        TopoDS_Face moved = TopoDS::Face(BRepBuilderAPI_Transform(face, t, Standard_True).Shape());
        solid = prismFace(moved, dir, 2 * d, 0.0, prefix, names, &err);
      }
    } else {  // two sides
      TopoDS_Shape sa = buildSide(ctx, face, dir, s1, prefix, names, err);
      if (sa.IsNull()) break;
      TopoDS_Shape sb = buildSide(ctx, face, dir.Reversed(), s2, prefix + "2/", names, err);
      if (sb.IsNull()) break;
      BoolResult r = fuseAll({sa, sb});
      if (!r) {
        err = "could not join the two sides: " + r.error;
        break;
      }
      names = mapNames(names, History::of(r.history), r.shape);
      solid = r.shape;
    }
    if (solid.IsNull()) break;
    // stable side names: by sketch curve id instead of edge order
    if (refs("profiles").size() == faces.size()) stabilizeSideNames(ctx, refs("profiles")[static_cast<size_t>(pi - 1)], prefix, names);
    solids.push_back(solid);
  }
  // write back re-matched indices of the "to object" refs
  if (to1 != ref("toObject") || to1.index != ref("toObject").index) prop("toObject").setRef(to1);
  if (to2 != ref("toObject2") || to2.index != ref("toObject2").index) prop("toObject2").setRef(to2);
  if (solids.size() != faces.size()) {
    ctx.setError(err.empty() ? "extrusion failed" : err);
    return;
  }
  TopoDS_Shape tool;
  if (solids.size() == 1) {
    tool = solids.front();
  } else {
    BoolResult r = fuseAll(solids);
    if (!r) {
      ctx.setError("could not combine the profiles: " + r.error);
      return;
    }
    tool = r.shape;
    names = mapNames(names, History::of(r.history), tool);
  }
  applyTool(ctx, tool, operation(), prop("targets").refs(), names, out);
}

}  // namespace fabricad::part
