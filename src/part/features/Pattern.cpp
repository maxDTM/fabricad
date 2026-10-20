// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/features/Pattern.h"

#include "core/Document.h"
#include "core/Feature.h"
#include "core/Json.h"
#include "core/ShapeUtils.h"
#include "part/PartUtils.h"

#include <QJsonArray>

#include <gp_Ax2.hxx>

#include <algorithm>
#include <cmath>
#include <format>

namespace fabricad::part {

namespace {
constexpr auto kPlaneKinds = core::kPlaneKinds | core::ShapeKind::Sketch;
}

// ---------------------------------------------------------------------------
// TransformFeature
// ---------------------------------------------------------------------------

void TransformFeature::addObjectProperties() {
  addEnum("patternType", {"Features", "Bodies"}, Features).setLabel("Pattern type");
  addRefs("objects", core::ShapeKind::Feature | core::ShapeKind::Body).setLabel("Objects");
  core::Property suppressed("suppressed", core::PropertyType::Json);
  suppressed.setJson(QJsonArray());
  suppressed.setLabel("Suppressed instances");
  suppressed.flags().hidden = true;
  addProperty(std::move(suppressed));
}

std::vector<int> TransformFeature::suppressedInstances() const {
  std::vector<int> out;
  const core::Property* p = property("suppressed");
  if (!p) return out;
  for (const auto& v : p->json().toArray()) out.push_back(v.toInt());
  return out;
}

void TransformFeature::applyTransforms(core::RecomputeContext& ctx, const std::vector<gp_Trsf>& transforms, Outcome& out,
                                       bool joinBodies) {
  if (transforms.empty()) {
    ctx.setError("the pattern has no instances");
    return;
  }
  std::vector<core::ShapeRef>& objects = prop("objects").refs();
  if (objects.empty()) {
    ctx.setError("select features or bodies to pattern");
    return;
  }
  const bool bodiesMode = enumIndex("patternType") == Bodies;
  const std::string base = name().empty() ? type() : name();

  // Working copy of the body shapes: several features may hit the same body in sequence.
  std::map<core::ObjectId, TopoDS_Shape> working;
  std::map<core::ObjectId, NameMap> workingNames;
  auto shapeOf = [&](core::ObjectId id) -> TopoDS_Shape& {
    auto it = working.find(id);
    if (it == working.end()) {
      it = working.emplace(id, ctx.bodyShape(id)).first;
      workingNames[id] = upstreamNames(ctx, id);
    }
    return it->second;
  };

  int handled = 0;
  for (auto& ref : objects) {
    if (ref.kind == core::ShapeKind::Body || (bodiesMode && ref.isBrep())) {
      TopoDS_Shape s = ctx.resolve(ref);
      if (s.IsNull()) continue;
      ++handled;
      const core::Body* b = ctx.document().body(ref.owner);
      std::vector<TopoDS_Shape> copies;
      int k = 0;
      for (const auto& t : transforms) {
        ++k;
        TopoDS_Shape c = copyTransformed(s, t);
        out.addPreview(c, Operation::NewBody);
        if (joinBodies) {
          copies.push_back(c);
        } else {
          out.addNew(c, b ? std::format("{} ({})", b->name, k + 1) : std::string(), b ? b->component : core::kInvalidId);
        }
      }
      if (joinBodies && !copies.empty()) {
        TopoDS_Shape& cur = shapeOf(ref.owner);
        bool touching = false;
        for (const auto& c : copies)
          if (shapesTouch(cur, c)) touching = true;
        if (touching) {
          BoolResult r = booleanOp(Operation::Join, cur, copies);
          if (!r) {
            ctx.setError("mirror join failed: " + r.error);
            return;
          }
          workingNames[ref.owner] = mapNames(workingNames[ref.owner], History::of(r.history), r.shape);
          cur = r.shape;
        } else {
          int j = 0;
          for (const auto& c : copies) out.addNew(c, b ? std::format("{} ({})", b->name, ++j + 1) : std::string(), b ? b->component : core::kInvalidId);
        }
      }
      continue;
    }
    if (ref.kind != core::ShapeKind::Feature) continue;
    const PartFeature* src = ctx.featureAs<PartFeature>(ref.owner);
    if (!src) {
      ctx.setWarning(std::format("feature {} cannot be patterned", ref.owner));
      continue;
    }
    TopoDS_Shape tool = src->toolShape();
    if (tool.IsNull()) {
      ctx.setWarning(std::format("'{}' has no tool shape to pattern", src->name()));
      continue;
    }
    ++handled;
    Operation op = src->operation();
    std::vector<core::ObjectId> targets = src->touchedBodies();
    if (op == Operation::NewBody) {
      op = Operation::Join;
      targets = src->createdBodies();
    }
    std::vector<TopoDS_Shape> copies;
    for (const auto& t : transforms) {
      TopoDS_Shape c = copyTransformed(tool, t);
      copies.push_back(c);
      out.addPreview(c, op);
    }
    out.tool = compoundOf(copies);
    out.operation = op;
    NameMap copyNames;
    int k = 0;
    for (const auto& c : copies) {
      ++k;
      int fi = 0;
      for (const auto& f : facesOf(c)) copyNames[std::format("{}/{}/I{}/F{}", base, src->name(), k, ++fi)] = f;
    }
    for (core::ObjectId bodyId : targets) {
      TopoDS_Shape& cur = shapeOf(bodyId);
      if (cur.IsNull()) continue;
      BoolResult r = booleanOp(op == Operation::Intersect ? Operation::Join : op, cur, copies);
      if (!r.error.empty()) {
        ctx.setError(std::format("pattern of '{}' failed: {}", src->name(), r.error));
        return;
      }
      if (r.shape.IsNull()) {
        ctx.setError(std::format("pattern of '{}' removes the whole body", src->name()));
        return;
      }
      NameMap names = workingNames[bodyId];
      names.insert(copyNames.begin(), copyNames.end());
      workingNames[bodyId] = mapNames(names, History::of(r.history), r.shape);
      cur = r.shape;
    }
  }
  if (handled == 0) {
    ctx.setError("nothing to pattern");
    return;
  }
  for (auto& [id, shape] : working) {
    if (shape.IsEqual(ctx.bodyShape(id))) continue;
    NameMap& names = workingNames[id];
    addEdgeNames(names, shape);
    out.addModified(id, shape);
    out.names.insert(names.begin(), names.end());
  }
}

// ---------------------------------------------------------------------------
// RectangularPattern
// ---------------------------------------------------------------------------

RectangularPatternFeature::RectangularPatternFeature() : TransformFeature(kType) {
  addObjectProperties();
  addRef("direction1", core::kAxisKinds).setLabel("Direction 1");
  addInt("quantity1", 3).setLabel("Quantity 1");
  addLength("distance1", 20.0).setLabel("Distance 1");
  addBool("symmetric1", false).setLabel("Symmetric 1");
  addRef("direction2", core::kAxisKinds).setLabel("Direction 2");
  addInt("quantity2", 1).setLabel("Quantity 2");
  addLength("distance2", 20.0).setLabel("Distance 2");
  addBool("symmetric2", false).setLabel("Symmetric 2");
  addEnum("distribution", {"Extent", "Spacing"}, Extent).setLabel("Distribution")
      .setTooltip("Extent: distance is the total length; Spacing: distance between instances");
  updatePropertyFlags();
}

void RectangularPatternFeature::updatePropertyFlags() {
  bool d2 = ref("direction2").isValid();
  prop("quantity2").flags().hidden = !d2;
  prop("distance2").flags().hidden = !d2;
  prop("symmetric2").flags().hidden = !d2;
}

namespace {

/// Offsets along one direction (0 excluded unless symmetric adds negatives).
std::vector<double> linearOffsets(int quantity, double distance, bool extent, bool symmetric, std::string* err) {
  std::vector<double> out{0.0};
  if (quantity < 1) {
    *err = "quantity must be at least 1";
    return {};
  }
  if (quantity == 1) return out;
  double step = extent ? distance / (quantity - 1) : distance;
  if (std::abs(step) < 1e-9) {
    *err = "distance must not be zero";
    return {};
  }
  for (int i = 1; i < quantity; ++i) {
    out.push_back(i * step);
    if (symmetric) out.push_back(-i * step);
  }
  return out;
}

}  // namespace

void RectangularPatternFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  if (ref("direction1").isBrep()) resolveRefProperty(ctx, prop("direction1"));
  auto ax1 = ctx.axis(ref("direction1"));
  if (!ax1) {
    ctx.setError("select the first direction (edge or axis)");
    return;
  }
  std::string err;
  bool extent = enumIndex("distribution") == Extent;
  auto off1 = linearOffsets(static_cast<int>(prop("quantity1").toInt()), ctx.length("distance1"), extent, flag("symmetric1"), &err);
  if (off1.empty()) {
    ctx.setError(err);
    return;
  }
  std::vector<double> off2{0.0};
  std::optional<gp_Ax1> ax2;
  if (ref("direction2").isValid()) {
    if (ref("direction2").isBrep()) resolveRefProperty(ctx, prop("direction2"));
    ax2 = ctx.axis(ref("direction2"));
    if (!ax2) {
      ctx.setError("second direction is not a line");
      return;
    }
    off2 = linearOffsets(static_cast<int>(prop("quantity2").toInt()), ctx.length("distance2"), extent, flag("symmetric2"), &err);
    if (off2.empty()) {
      ctx.setError(err);
      return;
    }
  }
  std::vector<int> suppressed = suppressedInstances();
  std::vector<gp_Trsf> transforms;
  int index = 0;
  for (double b : off2) {
    for (double a : off1) {
      int idx = index++;
      if (std::abs(a) < 1e-12 && std::abs(b) < 1e-12) continue;  // the original
      if (std::find(suppressed.begin(), suppressed.end(), idx) != suppressed.end()) continue;
      gp_Vec v = gp_Vec(ax1->Direction()) * a;
      if (ax2) v += gp_Vec(ax2->Direction()) * b;
      gp_Trsf t;
      t.SetTranslation(v);
      transforms.push_back(t);
    }
  }
  applyTransforms(ctx, transforms, out, false);
}

// ---------------------------------------------------------------------------
// CircularPattern
// ---------------------------------------------------------------------------

CircularPatternFeature::CircularPatternFeature() : TransformFeature(kType) {
  addObjectProperties();
  addRef("axis", core::kAxisKinds | core::ShapeKind::Face).setLabel("Axis");
  addInt("quantity", 6).setLabel("Quantity");
  addEnum("type", {"Full", "Angle", "Symmetric"}, Full).setLabel("Type");
  addAngle("angle", 360.0).setLabel("Total angle");
  updatePropertyFlags();
}

void CircularPatternFeature::updatePropertyFlags() { prop("angle").flags().hidden = enumIndex("type") == Full; }

void CircularPatternFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  if (ref("axis").isBrep()) resolveRefProperty(ctx, prop("axis"));
  auto axis = ctx.axis(ref("axis"));
  if (!axis) {
    ctx.setError("select an axis (edge, cylindrical face, construction or origin axis)");
    return;
  }
  int n = static_cast<int>(prop("quantity").toInt());
  if (n < 2) {
    ctx.setError("quantity must be at least 2");
    return;
  }
  int type = enumIndex("type");
  double total = type == Full ? 360.0 : ctx.angle("angle");
  if (std::abs(total) < 1e-9) {
    ctx.setError("angle must not be zero");
    return;
  }
  std::vector<int> suppressed = suppressedInstances();
  std::vector<gp_Trsf> transforms;
  auto add = [&](int idx, double deg) {
    if (std::find(suppressed.begin(), suppressed.end(), idx) != suppressed.end()) return;
    gp_Trsf t;
    t.SetRotation(*axis, deg * M_PI / 180.0);
    transforms.push_back(t);
  };
  if (type == Full) {
    double step = 360.0 / n;
    for (int i = 1; i < n; ++i) add(i, i * step);
  } else if (type == Angle) {
    double step = total / (n - 1);
    for (int i = 1; i < n; ++i) add(i, i * step);
  } else {
    double step = total / (n - 1);
    for (int i = 1; i < n; ++i) {
      add(i, i * step);
      add(n - 1 + i, -i * step);
    }
  }
  applyTransforms(ctx, transforms, out, false);
}

// ---------------------------------------------------------------------------
// Mirror
// ---------------------------------------------------------------------------

MirrorFeature::MirrorFeature() : TransformFeature(kType) {
  addObjectProperties();
  addRef("mirrorPlane", kPlaneKinds).setLabel("Mirror plane");
  addEnum("operation", {"Join", "New Body"}, 0).setLabel("Operation").setTooltip("For bodies: join the mirrored copy or keep it separate");
}

void MirrorFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  std::optional<gp_Ax3> plane;
  if (ref("mirrorPlane").kind == core::ShapeKind::Face) plane = core::facePlane(resolveRefProperty(ctx, prop("mirrorPlane")));
  else plane = ctx.plane(ref("mirrorPlane"));
  if (!plane) {
    ctx.setError("select a mirror plane");
    return;
  }
  gp_Trsf t;
  t.SetMirror(gp_Ax2(plane->Location(), plane->Direction()));
  applyTransforms(ctx, {t}, out, enumIndex("operation") == 0);
}

}  // namespace fabricad::part
