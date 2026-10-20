// FabriCAD assembly: component helpers implementation.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "assembly/ComponentOps.h"

#include "assembly/JointFeature.h"
#include "core/Commands.h"
#include "core/Json.h"
#include "core/ShapeUtils.h"

#include <QJsonArray>
#include <QJsonObject>

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>

#include <algorithm>
#include <format>
#include <functional>

namespace fabricad::assembly {

using core::Document;
using core::kInvalidId;
using core::kRootComponentId;
using core::ObjectId;

namespace {

const core::BodyStateMap& stateOf(const Document& doc, const core::BodyStateMap* state) {
  return state ? *state : doc.bodyStates();
}

}  // namespace

ObjectId definitionOf(const Document& doc, ObjectId comp) {
  int guard = 0;
  const core::Component* c = doc.component(comp);
  while (c && c->definition != kInvalidId && guard++ < 64) {
    const core::Component* d = doc.component(c->definition);
    if (!d) break;
    comp = c->definition;
    c = d;
  }
  return comp;
}

std::vector<ObjectId> displayedBodies(const Document& doc, ObjectId comp, const core::BodyStateMap* state) {
  std::vector<ObjectId> out;
  ObjectId def = definitionOf(doc, comp);
  const auto& st = stateOf(doc, state);
  for (ObjectId b : doc.bodiesOf(def)) {
    auto it = st.find(b);
    if (it != st.end() && !it->second.IsNull()) out.push_back(b);
  }
  return out;
}

std::vector<ObjectId> descendants(const Document& doc, ObjectId comp) {
  std::vector<ObjectId> out;
  std::function<void(ObjectId)> walk = [&](ObjectId id) {
    const core::Component* c = doc.component(id);
    if (!c) return;
    for (ObjectId child : c->children) {
      out.push_back(child);
      walk(child);
    }
  };
  walk(comp);
  return out;
}

bool isAncestorOf(const Document& doc, ObjectId ancestor, ObjectId comp) {
  const core::Component* c = doc.component(comp);
  int guard = 0;
  while (c && c->parent != kInvalidId && guard++ < 1000) {
    if (c->parent == ancestor) return true;
    c = doc.component(c->parent);
  }
  return false;
}

std::vector<ObjectId> leafComponents(const Document& doc, ObjectId root) {
  std::vector<ObjectId> out;
  const core::Component* r = doc.component(root);
  if (!r) return out;
  if (r->children.empty()) return {root};
  for (ObjectId d : descendants(doc, root))
    if (const core::Component* c = doc.component(d); c && c->children.empty()) out.push_back(d);
  return out;
}

std::vector<BodyInstance> bodyInstances(const Document& doc, const std::vector<ObjectId>& components, bool includeChildren,
                                        const core::BodyStateMap* state) {
  std::vector<ObjectId> comps = components;
  if (comps.empty()) {
    comps.push_back(kRootComponentId);
    includeChildren = true;
  }
  std::vector<ObjectId> all;
  for (ObjectId c : comps) {
    if (std::find(all.begin(), all.end(), c) == all.end()) all.push_back(c);
    if (includeChildren)
      for (ObjectId d : descendants(doc, c))
        if (std::find(all.begin(), all.end(), d) == all.end()) all.push_back(d);
  }
  const auto& st = stateOf(doc, state);
  std::vector<BodyInstance> out;
  for (ObjectId c : all) {
    const core::Component* comp = doc.component(c);
    if (!comp || !comp->visible) continue;
    gp_Trsf world = doc.worldPlacement(c);
    for (ObjectId b : displayedBodies(doc, c, state)) {
      const core::Body* body = doc.body(b);
      if (!body || !body->visible) continue;
      BodyInstance bi;
      bi.body = b;
      bi.component = c;
      bi.worldShape = world.Form() == gp_Identity ? st.at(b) : core::transformed(st.at(b), world);
      out.push_back(std::move(bi));
    }
  }
  return out;
}

TopoDS_Shape componentLocalShape(const Document& doc, ObjectId comp, const core::BodyStateMap* state) {
  std::vector<TopoDS_Shape> parts;
  const auto& st = stateOf(doc, state);
  for (ObjectId b : displayedBodies(doc, comp, state)) parts.push_back(st.at(b));
  return core::makeCompound(parts);
}

TopoDS_Shape componentWorldShape(const Document& doc, ObjectId comp, bool includeChildren, const core::BodyStateMap* state) {
  std::vector<TopoDS_Shape> parts;
  for (const BodyInstance& bi : bodyInstances(doc, {comp}, includeChildren, state)) parts.push_back(bi.worldShape);
  return core::makeCompound(parts);
}

std::optional<gp_Pnt> componentCenter(const Document& doc, ObjectId comp) {
  TopoDS_Shape s = componentWorldShape(doc, comp, true);
  if (s.IsNull()) return std::nullopt;
  Bnd_Box box;
  BRepBndLib::Add(s, box, false);
  if (box.IsVoid()) return std::nullopt;
  double xmin, ymin, zmin, xmax, ymax, zmax;
  box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
  return gp_Pnt((xmin + xmax) / 2, (ymin + ymax) / 2, (zmin + zmax) / 2);
}

ObjectId componentOfRef(const Document& doc, const core::ShapeRef& ref) {
  switch (ref.kind) {
    case core::ShapeKind::Body:
    case core::ShapeKind::Face:
    case core::ShapeKind::Edge:
    case core::ShapeKind::Vertex: {
      const core::Body* b = doc.body(ref.owner);
      return b ? b->component : kInvalidId;
    }
    case core::ShapeKind::Component: return doc.component(ref.owner) ? ref.owner : kInvalidId;
    case core::ShapeKind::OriginPlane:
    case core::ShapeKind::OriginAxis:
    case core::ShapeKind::OriginPoint: return kRootComponentId;
    case core::ShapeKind::ConstructionPlane:
    case core::ShapeKind::ConstructionAxis:
    case core::ShapeKind::ConstructionPoint:
    case core::ShapeKind::Sketch:
    case core::ShapeKind::SketchCurve:
    case core::ShapeKind::SketchPoint:
    case core::ShapeKind::Profile:
    case core::ShapeKind::Feature: {
      // features do not record their component in core; bodies they created tell us
      const core::Feature* f = doc.feature(ref.owner);
      if (f && !f->createdBodies().empty())
        if (const core::Body* b = doc.body(f->createdBodies().front())) return b->component;
      return doc.activeComponent();
    }
    default: return kInvalidId;
  }
}

std::string componentLabel(const Document& doc, ObjectId comp) {
  const core::Component* c = doc.component(comp);
  if (!c) return {};
  if (c->definition == kInvalidId) return c->name;
  const core::Component* def = doc.component(c->definition);
  std::string base = def ? def->name : c->name;
  // occurrence number = 1-based index among siblings sharing the definition (definition itself = 1)
  int n = 1;
  for (const core::Component* o : doc.components()) {
    if (o->id == comp) break;
    if (o->definition == c->definition && o->id != c->definition) ++n;
  }
  return std::format("{}:{}", base, n + 1);
}

bool isJointed(const Document& doc, ObjectId comp) {
  for (const auto& f : doc.features()) {
    if (!doc.isFeatureActive(f->id()) || f->isSuppressed()) continue;
    if (const auto* jp = dynamic_cast<const IJointProvider*>(f.get()))
      if (jp->involvesComponent(comp)) return true;
  }
  return false;
}

bool setWorldPlacement(Document& doc, ObjectId comp, const gp_Trsf& world, double tol) {
  core::Component* c = doc.component(comp);
  if (!c) return false;
  gp_Trsf parentWorld = c->parent == kInvalidId ? gp_Trsf() : doc.worldPlacement(c->parent);
  gp_Trsf local = parentWorld.Inverted().Multiplied(world);
  const gp_Trsf& cur = c->placement;
  bool same = (local.TranslationPart() - cur.TranslationPart()).Modulus() <= tol;
  if (same) {
    gp_Mat a = local.VectorialPart(), b = cur.VectorialPart();
    for (int i = 1; i <= 3 && same; ++i)
      for (int j = 1; j <= 3; ++j)
        if (std::abs(a.Value(i, j) - b.Value(i, j)) > tol) {
          same = false;
          break;
        }
  }
  if (same) return false;
  c->placement = local;
  doc.notifyComponentChanged(comp);
  return true;
}

std::map<ObjectId, gp_Trsf> snapshotPlacements(const Document& doc) {
  std::map<ObjectId, gp_Trsf> out;
  for (const core::Component* c : doc.components()) out[c->id] = c->placement;
  return out;
}

std::unique_ptr<core::UndoCommand> makePlacementsCommand(Document& doc, std::map<ObjectId, gp_Trsf> before,
                                                         std::map<ObjectId, gp_Trsf> after, std::string text) {
  auto apply = [&doc](const std::map<ObjectId, gp_Trsf>& placements) {
    for (const auto& [id, p] : placements)
      if (core::Component* c = doc.component(id)) {
        c->placement = p;
        doc.notifyComponentChanged(id);
      }
  };
  return std::make_unique<core::LambdaCommand>(std::move(text), [apply, after] { apply(after); },
                                               [apply, before] { apply(before); });
}

std::unique_ptr<core::UndoCommand> makeGroundCommand(Document& doc, ObjectId comp, bool grounded) {
  return core::makeComponentEditCommand(doc, comp, [grounded](core::Component& c) { c.grounded = grounded; },
                                        grounded ? "Ground" : "Unground");
}

namespace {

/// Does a feature reference any of the given components (Component refs in its properties, or joints)?
bool referencesComponents(const core::Feature& f, const std::vector<ObjectId>& ids) {
  auto hit = [&](ObjectId id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); };
  for (const core::Property& p : f.properties()) {
    if (p.type() == core::PropertyType::Ref) {
      if (p.ref().kind == core::ShapeKind::Component && hit(p.ref().owner)) return true;
    } else if (p.type() == core::PropertyType::RefList) {
      for (const core::ShapeRef& r : p.refs())
        if (r.kind == core::ShapeKind::Component && hit(r.owner)) return true;
    }
  }
  if (const auto* jp = dynamic_cast<const IJointProvider*>(&f))
    for (ObjectId id : ids)
      if (jp->involvesComponent(id)) return true;
  return false;
}

/// Rewrite component ids inside feature JSON: ShapeRef objects of kind Component and JointOrigin "component".
QJsonValue remapComponentIds(const QJsonValue& v, const std::map<ObjectId, ObjectId>& map) {
  if (v.isArray()) {
    QJsonArray out;
    for (const auto& e : v.toArray()) out.append(remapComponentIds(e, map));
    return out;
  }
  if (!v.isObject()) return v;
  QJsonObject o = v.toObject();
  for (const QString& k : o.keys()) o[k] = remapComponentIds(o[k], map);
  QJsonValue kind = o.value("kind");
  bool isComponentRef = kind.isString() ? kind.toString() == core::shapeKindName(core::ShapeKind::Component)
                                        : kind.isDouble() && static_cast<core::ShapeKindMask>(kind.toDouble()) == core::mask(core::ShapeKind::Component);
  if (isComponentRef) {
    auto f = map.find(core::idFromJson(o.value("owner")));
    if (f != map.end()) o["owner"] = core::idToJson(f->second);
  }
  if (o.value("component").isDouble()) {
    auto f = map.find(core::idFromJson(o.value("component")));
    if (f != map.end()) o["component"] = core::idToJson(f->second);
  }
  return o;
}

void copyComponentFields(core::Component& dst, const core::Component& src) {
  dst.placement = src.placement;
  dst.grounded = src.grounded;
  dst.visible = src.visible;
  dst.externalPath = src.externalPath;
  dst.partNumber = src.partNumber;
  dst.description = src.description;
  dst.material = src.material;
}

/// New component (optionally with bodies and a timeline marker). Core cannot insert a component with a
/// given id, so a redo after an undo allocates a fresh id (see docs/integration-notes/assembly.md).
class CreateComponentCommand : public core::UndoCommand {
 public:
  CreateComponentCommand(Document& doc, NewComponentSpec spec, std::string text)
      : UndoCommand(std::move(text)), doc_(doc), spec_(std::move(spec)) {
    parent_ = spec_.parent == kInvalidId ? doc.activeComponent() : spec_.parent;
    activeBefore_ = doc.activeComponent();
  }
  ObjectId created() const { return created_; }

  void redo() override {
    core::Component& c = doc_.addComponent(spec_.name, parent_);
    created_ = c.id;
    c.grounded = spec_.ground;
    c.externalPath = spec_.externalPath;
    bodiesBefore_.clear();
    for (ObjectId b : spec_.bodies)
      if (core::Body* body = doc_.body(b)) {
        bodiesBefore_[b] = body->component;
        body->component = created_;
        doc_.notifyBodyChanged(b);
      }
    if (spec_.addTimelineMarker) {
      if (!marker_) {
        auto m = std::make_unique<NewComponentFeature>();
        m->setName(c.name);
        marker_ = std::move(m);
      }
      static_cast<NewComponentFeature*>(marker_.get())->setComponent(created_);
      markerId_ = doc_.addFeature(std::move(marker_), markerIndex_)->id();
      doc_.recompute();
    }
    if (spec_.activate) doc_.setActiveComponent(created_);
    doc_.notifyComponentChanged(created_);
  }

  void undo() override {
    if (markerId_ != kInvalidId) {
      markerIndex_ = doc_.indexOf(markerId_);
      marker_ = doc_.takeFeature(markerId_);
      markerId_ = kInvalidId;
    }
    for (const auto& [b, comp] : bodiesBefore_)
      if (core::Body* body = doc_.body(b)) {
        body->component = doc_.component(comp) ? comp : parent_;
        doc_.notifyBodyChanged(b);
      }
    doc_.removeComponent(created_);
    if (doc_.component(activeBefore_)) doc_.setActiveComponent(activeBefore_);
    if (marker_) doc_.recompute();
  }

 private:
  Document& doc_;
  NewComponentSpec spec_;
  ObjectId parent_ = kInvalidId;
  ObjectId activeBefore_ = kInvalidId;
  ObjectId created_ = kInvalidId;
  std::map<ObjectId, ObjectId> bodiesBefore_;
  std::unique_ptr<core::Feature> marker_;  ///< held while undone
  ObjectId markerId_ = kInvalidId;
  int markerIndex_ = -1;
};

class CreateOccurrenceCommand : public core::UndoCommand {
 public:
  CreateOccurrenceCommand(Document& doc, ObjectId definition, ObjectId parent, const gp_Trsf& placement)
      : UndoCommand("Insert occurrence"), doc_(doc), definition_(definition), parent_(parent), placement_(placement) {}
  ObjectId created() const { return created_; }
  void redo() override {
    const core::Component* def = doc_.component(definition_);
    if (!def) return;
    core::Component& c = doc_.addComponent(def->name, parent_ == kInvalidId ? kRootComponentId : parent_);
    created_ = c.id;
    c.definition = definition_;
    c.placement = placement_;
    c.partNumber = def->partNumber;
    c.description = def->description;
    c.material = def->material;
    doc_.notifyComponentChanged(created_);
  }
  void undo() override {
    if (created_ != kInvalidId) doc_.removeComponent(created_);
    created_ = kInvalidId;
  }

 private:
  Document& doc_;
  ObjectId definition_, parent_;
  gp_Trsf placement_;
  ObjectId created_ = kInvalidId;
};

/// Delete one component: its bodies and child components move to its parent (the children keep
/// their world placement), the features referencing it (joints, its timeline marker) are removed
/// and the features made inside it are re-homed to the parent. Undo re-creates the component
/// (fresh id) and puts everything back.
class DeleteComponentCommand : public core::UndoCommand {
 public:
  DeleteComponentCommand(Document& doc, ObjectId comp) : UndoCommand("Delete component"), doc_(doc), comp_(comp) {}
  ObjectId component() const { return comp_; }
  int movedChildren() const { return static_cast<int>(children_.size()); }
  int movedBodies() const { return static_cast<int>(node_.bodies.size()); }

  void redo() override {
    features_.clear();
    children_.clear();
    rehomed_.clear();
    activeBefore_ = doc_.activeComponent();
    const core::Component* c = doc_.component(comp_);
    if (!c) return;
    node_ = {*c, doc_.bodiesOf(comp_)};
    const ObjectId parent = c->parent != kInvalidId && doc_.component(c->parent) ? c->parent : kRootComponentId;
    // children: same world placement under the grandparent
    for (ObjectId child : std::vector<ObjectId>(c->children))
      if (const core::Component* cc = doc_.component(child)) {
        children_.push_back({child, cc->placement});
        doc_.reparentComponent(child, parent, node_.comp.placement.Multiplied(cc->placement));
      }
    const std::vector<ObjectId> gone{comp_};
    std::vector<int> doomed;
    for (int i = 0; i < doc_.featureCount(); ++i) {
      core::Feature* f = doc_.featureAt(i);
      if (referencesComponents(*f, gone)) doomed.push_back(i);
      else if (f->component() == comp_) {
        rehomed_.push_back(f->id());
        f->setComponent(parent);
      }
    }
    for (auto it = doomed.rbegin(); it != doomed.rend(); ++it)  // highest index first: indices stay valid
      features_.emplace_back(*it, doc_.takeFeature(doc_.featureAt(*it)->id()));
    doc_.removeComponent(comp_);  // bodies move to the parent
    doc_.recompute();
  }

  void undo() override {
    const core::Component& n = node_.comp;
    ObjectId parent = doc_.component(n.parent) ? n.parent : kRootComponentId;
    core::Component& c = doc_.addComponent(n.name, parent);
    copyComponentFields(c, n);
    c.definition = doc_.component(n.definition) ? n.definition : kInvalidId;
    std::map<ObjectId, ObjectId> map{{comp_, c.id}};
    comp_ = c.id;
    for (ObjectId b : node_.bodies)
      if (core::Body* body = doc_.body(b)) {
        body->component = c.id;
        doc_.notifyBodyChanged(b);
      }
    for (const auto& [child, placement] : children_) doc_.reparentComponent(child, c.id, placement);
    for (ObjectId fid : rehomed_)
      if (core::Feature* f = doc_.feature(fid)) f->setComponent(c.id);
    doc_.notifyComponentChanged(c.id);
    std::sort(features_.begin(), features_.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& [index, f] : features_) {
      QJsonObject json = remapComponentIds(f->toJson(), map).toObject();
      f->fromJson(json);
      doc_.addFeature(std::move(f), std::min(index, doc_.featureCount()));
    }
    features_.clear();
    if (doc_.component(activeBefore_)) doc_.setActiveComponent(activeBefore_);
    else if (map.count(activeBefore_)) doc_.setActiveComponent(map.at(activeBefore_));
    doc_.recompute();
  }

 private:
  struct Node {
    core::Component comp;
    std::vector<ObjectId> bodies;
  };
  Document& doc_;
  ObjectId comp_;
  Node node_;
  std::vector<std::pair<ObjectId, gp_Trsf>> children_;  ///< re-parented children and their old local placements
  std::vector<ObjectId> rehomed_;                       ///< features whose component() was the deleted one
  std::vector<std::pair<int, std::unique_ptr<core::Feature>>> features_;
  ObjectId activeBefore_ = kInvalidId;
};

template <class Cmd, class... Args>
Cmd* runCommand(core::UndoStack* undo, std::unique_ptr<Cmd>& keep, Args&&... args) {
  keep = std::make_unique<Cmd>(std::forward<Args>(args)...);
  Cmd* raw = keep.get();
  if (undo) undo->push(std::move(keep));  // the stack (or the open macro) owns it now
  else raw->redo();
  return raw;
}

}  // namespace

ObjectId createComponent(Document& doc, core::UndoStack* undo, const NewComponentSpec& spec) {
  std::unique_ptr<CreateComponentCommand> keep;
  return runCommand(undo, keep, doc, spec, std::string("New component"))->created();
}

ObjectId createOccurrence(Document& doc, core::UndoStack* undo, ObjectId definition, ObjectId parent,
                          const gp_Trsf& placement) {
  if (!doc.component(definition)) return kInvalidId;
  std::unique_ptr<CreateOccurrenceCommand> keep;
  return runCommand(undo, keep, doc, definition, parent, placement)->created();
}

bool deleteComponent(Document& doc, core::UndoStack* undo, ObjectId comp, DeleteComponentResult* result) {
  if (comp == kRootComponentId || !doc.component(comp)) return false;
  std::unique_ptr<DeleteComponentCommand> keep;
  DeleteComponentCommand* cmd = runCommand(undo, keep, doc, comp);
  if (result) {
    result->movedChildren = cmd->movedChildren();
    result->movedBodies = cmd->movedBodies();
  }
  return true;
}

int recomputeJoints(Document& doc) {
  // Placements are not inputs of body features (bodies live in component-local coordinates), so a
  // placement or ground change only needs the joint features re-solved: replay from the first one.
  for (int i = 0; i < doc.featureCount(); ++i) {
    const core::Feature* f = doc.featureAt(i);
    if (!dynamic_cast<const IJointProvider*>(f) || f->isSuppressed() || !doc.isFeatureActive(f->id())) continue;
    doc.markDirty(f->id());
    doc.recompute();
    return i;
  }
  return -1;
}

}  // namespace fabricad::assembly
