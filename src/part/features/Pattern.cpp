// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/features/Pattern.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/UndoStack.h"
#include "core/Feature.h"
#include "core/Json.h"
#include "core/ShapeUtils.h"
#include "part/PartUtils.h"

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
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
  addEnum("patternType", {"Features", "Bodies", "Components"}, Features).setLabel("Pattern type");
  addRefs("objects", core::ShapeKind::Feature | core::ShapeKind::Body | core::ShapeKind::Component).setLabel("Objects");
  core::Property suppressed("suppressed", core::PropertyType::Json);
  suppressed.setJson(QJsonArray());
  suppressed.setLabel("Suppressed instances");
  suppressed.flags().hidden = true;
  addProperty(std::move(suppressed));
  core::Property instances("instances", core::PropertyType::Json);
  instances.setJson(QJsonArray());
  instances.setLabel("Component instances");
  instances.flags().hidden = true;
  addProperty(std::move(instances));
}

std::vector<core::ObjectId> TransformFeature::instanceComponents() const {
  std::vector<core::ObjectId> out;
  if (const core::Property* p = property("instances"))
    for (const auto& v : p->json().toArray()) out.push_back(core::idFromJson(v.toObject().value("component")));
  return out;
}

bool TransformFeature::wantsCommitPreparation() const {
  if (enumIndex("patternType") == Components) return true;
  return !instanceComponents().empty();  // switched away from Components: remove the occurrences
}

namespace {

/// Insert a component with a fixed id (allocated on the first redo); undo removes it.
class AddComponentCommand : public core::UndoCommand {
 public:
  AddComponentCommand(core::Document& doc, core::Component c) : UndoCommand("Add pattern instance"), doc_(doc), c_(std::move(c)) {}
  core::ObjectId id() const { return c_.id; }
  void redo() override {
    if (!doc_.component(c_.parent)) c_.parent = core::kRootComponentId;
    c_.children.clear();
    c_.id = doc_.addComponent(c_).id;
    doc_.notifyComponentChanged(c_.id);
  }
  void undo() override { doc_.removeComponent(c_.id); }

 private:
  core::Document& doc_;
  core::Component c_;
};

/// Remove a (leaf) instance component; undo puts it back with the same id.
class RemoveComponentCommand : public core::UndoCommand {
 public:
  RemoveComponentCommand(core::Document& doc, core::ObjectId id) : UndoCommand("Remove pattern instance"), doc_(doc), id_(id) {}
  void redo() override {
    if (const core::Component* c = doc_.component(id_)) {
      saved_ = *c;
      saved_.children.clear();
      doc_.removeComponent(id_);
    }
  }
  void undo() override {
    if (saved_.id == core::kInvalidId || doc_.component(saved_.id)) return;
    if (!doc_.component(saved_.parent)) saved_.parent = core::kRootComponentId;
    doc_.addComponent(saved_);
    doc_.notifyComponentChanged(saved_.id);
  }

 private:
  core::Document& doc_;
  core::ObjectId id_;
  core::Component saved_;
};

void runOrPush(core::UndoStack* undo, std::unique_ptr<core::UndoCommand> cmd) {
  if (undo) undo->push(std::move(cmd));
  else cmd->redo();
}

gp_Trsf parentWorld(const core::Document& doc, core::ObjectId comp) {
  const core::Component* c = doc.component(comp);
  return c && c->parent != core::kInvalidId ? doc.worldPlacement(c->parent) : gp_Trsf();
}

}  // namespace

void TransformFeature::prepareCommit(core::Document& doc, core::UndoStack* undo) {
  const bool comps = enumIndex("patternType") == Components;
  QJsonArray old = prop("instances").json().toArray();
  QJsonArray next;
  std::vector<bool> used(static_cast<size_t>(old.size()), false);
  if (comps) {
    for (const ComponentInstance& need : needed_) {
      core::ObjectId found = core::kInvalidId;
      for (int i = 0; i < old.size(); ++i) {
        QJsonObject o = old[i].toObject();
        if (used[static_cast<size_t>(i)] || core::idFromJson(o.value("source")) != need.source || o.value("index").toInt() != need.index)
          continue;
        core::ObjectId id = core::idFromJson(o.value("component"));
        if (!doc.component(id)) continue;
        used[static_cast<size_t>(i)] = true;
        found = id;
        break;
      }
      if (found == core::kInvalidId) {
        const core::Component* src = doc.component(need.source);
        if (!src) continue;
        core::Component c;
        c.name = src->name;
        c.parent = src->parent == core::kInvalidId ? core::kRootComponentId : src->parent;
        c.definition = doc.definitionOf(need.source);
        c.placement = parentWorld(doc, need.source).Inverted() * need.world;
        c.partNumber = src->partNumber;
        c.description = src->description;
        c.material = src->material;
        c.visible = src->visible;
        auto cmd = std::make_unique<AddComponentCommand>(doc, c);
        AddComponentCommand* raw = cmd.get();
        runOrPush(undo, std::move(cmd));
        found = raw->id();
      }
      QJsonObject o;
      o["source"] = core::idToJson(need.source);
      o["index"] = need.index;
      o["component"] = core::idToJson(found);
      next.append(o);
    }
  }
  for (int i = 0; i < old.size(); ++i) {
    if (used[static_cast<size_t>(i)]) continue;
    core::ObjectId id = core::idFromJson(old[i].toObject().value("component"));
    if (doc.component(id)) runOrPush(undo, std::make_unique<RemoveComponentCommand>(doc, id));
  }
  prop("instances").setJson(next);
}

void TransformFeature::applyComponentTransforms(core::RecomputeContext& ctx, const std::vector<gp_Trsf>& transforms,
                                                Outcome& out) {
  const core::Document& doc = ctx.document();
  needed_.clear();
  // sources: component refs, or the component of a picked body
  std::vector<core::ObjectId> sources;
  for (const auto& ref : prop("objects").refs()) {
    core::ObjectId comp = core::kInvalidId;
    if (ref.kind == core::ShapeKind::Component) comp = ref.owner;
    else if (ref.kind == core::ShapeKind::Body)
      if (const core::Body* b = doc.body(ref.owner)) comp = b->component;
    if (comp == core::kInvalidId || comp == core::kRootComponentId || !doc.component(comp)) continue;
    if (std::find(sources.begin(), sources.end(), comp) == sources.end()) sources.push_back(comp);
  }
  if (sources.empty()) {
    ctx.setError("select components to pattern");
    return;
  }
  // The pattern's directions live in the frame of the component the feature belongs to.
  const core::ObjectId owner = component() != core::kInvalidId && doc.component(component()) ? component() : core::kRootComponentId;
  const gp_Trsf frame = doc.worldPlacement(owner);
  const QJsonArray inst = prop("instances").json().toArray();
  int missing = 0;
  for (core::ObjectId src : sources) {
    const gp_Trsf srcWorld = doc.worldPlacement(src);
    // geometry for the preview: the bodies the source displays, in world coordinates
    std::vector<TopoDS_Shape> local;
    for (core::ObjectId b : doc.displayedBodies(src))
      if (!ctx.bodyShape(b).IsNull()) local.push_back(ctx.bodyShape(b));
    TopoDS_Shape srcShape = compoundOf(local);
    for (size_t k = 0; k < transforms.size(); ++k) {
      const int idx = k < transformIndices_.size() ? transformIndices_[k] : static_cast<int>(k) + 1;
      gp_Trsf world = frame * transforms[k] * frame.Inverted() * srcWorld;
      needed_.push_back({src, idx, world});
      if (!srcShape.IsNull()) out.addPreview(core::transformed(srcShape, world), Operation::NewBody);
      core::ObjectId target = core::kInvalidId;
      for (const auto& v : inst) {
        QJsonObject o = v.toObject();
        if (core::idFromJson(o.value("source")) == src && o.value("index").toInt() == idx) target = core::idFromJson(o.value("component"));
      }
      if (target == core::kInvalidId || !doc.component(target)) {
        ++missing;
        continue;
      }
      out.placements.emplace_back(target, parentWorld(doc, target).Inverted() * world);
    }
  }
  if (missing > 0 && !instanceComponents().empty())
    ctx.setWarning(std::format("{} pattern instance(s) have no component yet; edit the pattern and press OK", missing));
}

std::vector<int> TransformFeature::suppressedInstances() const {
  std::vector<int> out;
  const core::Property* p = property("suppressed");
  if (!p) return out;
  for (const auto& v : p->json().toArray()) out.push_back(v.toInt());
  return out;
}

bool TransformFeature::checkInstanceSpacing(core::RecomputeContext& ctx, const std::vector<gp_Trsf>& transforms,
                                            bool newBodies) {
  // G3 PART-10: bounding box of what is patterned (bodies, or the features' tool shapes)
  Bnd_Box box;
  const bool bodiesMode = enumIndex("patternType") == Bodies;
  for (core::ShapeRef ref : prop("objects").refs()) {  // a copy: resolve() may annotate it
    TopoDS_Shape s;
    if (ref.kind == core::ShapeKind::Body || (bodiesMode && ref.isBrep())) s = ctx.resolve(ref);
    else if (ref.kind == core::ShapeKind::Feature)
      if (const PartFeature* src = ctx.featureAs<PartFeature>(ref.owner)) s = src->toolShape();
    if (!s.IsNull()) BRepBndLib::Add(s, box);
  }
  if (box.IsVoid() || transforms.empty()) return true;
  double x0, y0, z0, x1, y1, z1;
  box.Get(x0, y0, z0, x1, y1, z1);
  std::vector<gp_Pnt> corners;
  for (double x : {x0, x1})
    for (double y : {y0, y1})
      for (double z : {z0, z1}) corners.emplace_back(x, y, z);
  const gp_Pnt centre((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2);
  // instances: identity (the original) + transforms
  std::vector<gp_Trsf> all{gp_Trsf()};
  all.insert(all.end(), transforms.begin(), transforms.end());
  // max corner displacement between two instances: 0 = the very same place
  auto apart = [&](const gp_Trsf& a, const gp_Trsf& b) {
    double d = 0;
    for (const gp_Pnt& c : corners) d = std::max(d, c.Transformed(a).Distance(c.Transformed(b)));
    return d;
  };
  double minApart = 1e100;
  double overlapStep = 1e100;  // centre step of the closest pair
  gp_Vec overlapDir;
  const size_t n = all.size();
  const size_t maxPairs = 200000;  // O(n^2) is fine up to the instance limit; cap anyway
  size_t pairs = 0;
  for (size_t i = 0; i < n && pairs < maxPairs; ++i)
    for (size_t j = i + 1; j < n && pairs < maxPairs; ++j, ++pairs) {
      const double d = apart(all[i], all[j]);
      if (d < minApart) {
        minApart = d;
        gp_Vec v(centre.Transformed(all[i]), centre.Transformed(all[j]));
        overlapStep = v.Magnitude();
        overlapDir = v;
      }
    }
  if (minApart < 1e-3) {
    ctx.setError(std::format("pattern instances coincide (spacing {:.4g} mm)", minApart));
    return false;
  }
  if (newBodies) {
    // extent of the patterned shape along the step between the closest instances
    double extent = 0;
    if (overlapStep > 1e-12) {
      const gp_Dir d(overlapDir);
      double lo = 1e100, hi = -1e100;
      for (const gp_Pnt& c : corners) {
        const double t = gp_Vec(c.XYZ()).Dot(gp_Vec(d));
        lo = std::min(lo, t);
        hi = std::max(hi, t);
      }
      extent = hi - lo;
    }
    if (overlapStep < extent - 1e-6)
      ctx.setWarning(std::format("pattern instances overlap (spacing {:.4g} mm, size {:.4g} mm)", overlapStep, extent));
  }
  return true;
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
  if (enumIndex("patternType") == Components) {
    applyComponentTransforms(ctx, transforms, out);
    return;
  }
  const bool bodiesMode = enumIndex("patternType") == Bodies;
  const std::string base = name().empty() ? type() : name();
  if (!checkInstanceSpacing(ctx, transforms, bodiesMode && !joinBodies)) return;

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

namespace {
/// Upper bound on pattern instances (incl. the original). The recompute is synchronous,
/// so an unbounded quantity (e.g. 100000) froze the UI and used gigabytes of RAM.
constexpr int kMaxPatternInstances = 1000;
std::string tooLargeMessage() {
  return "pattern too large (max " + std::to_string(kMaxPatternInstances) + " instances)";
}
void limitQuantity(core::Property& p, int min) {
  p.flags().allowNegative = false;
  p.flags().intMin = min;
  p.flags().intMax = kMaxPatternInstances;
}
}  // namespace

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
  limitQuantity(prop("quantity1"), 1);
  limitQuantity(prop("quantity2"), 1);
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
  if (quantity > kMaxPatternInstances) {
    *err = tooLargeMessage();
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
  if (off1.size() * off2.size() > static_cast<std::size_t>(kMaxPatternInstances)) {
    ctx.setError(tooLargeMessage());
    return;
  }
  std::vector<int> suppressed = suppressedInstances();
  std::vector<gp_Trsf> transforms;
  transformIndices_.clear();
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
      transformIndices_.push_back(idx);
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
  limitQuantity(prop("quantity"), 2);
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
  if (n > kMaxPatternInstances) {
    ctx.setError(tooLargeMessage());
    return;
  }
  int type = enumIndex("type");
  if (type == Symmetric && 2 * n - 1 > kMaxPatternInstances) {
    ctx.setError(tooLargeMessage());
    return;
  }
  double total = type == Full ? 360.0 : ctx.angle("angle");
  if (std::abs(total) < 1e-9) {
    ctx.setError("angle must not be zero");
    return;
  }
  std::vector<int> suppressed = suppressedInstances();
  std::vector<gp_Trsf> transforms;
  transformIndices_.clear();
  auto add = [&](int idx, double deg) {
    if (std::find(suppressed.begin(), suppressed.end(), idx) != suppressed.end()) return;
    gp_Trsf t;
    t.SetRotation(*axis, deg * M_PI / 180.0);
    transforms.push_back(t);
    transformIndices_.push_back(idx);
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
  if (enumIndex("patternType") == Components) {
    ctx.setError("Mirror works on features and bodies (a mirrored component is not an occurrence)");
    return;
  }
  gp_Trsf t;
  t.SetMirror(gp_Ax2(plane->Location(), plane->Direction()));
  transformIndices_ = {1};
  applyTransforms(ctx, {t}, out, enumIndex("operation") == 0);
}

}  // namespace fabricad::part
