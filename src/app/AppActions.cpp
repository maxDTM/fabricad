// FabriCAD app: document-level actions.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/AppActions.h"

#include "assembly/ComponentOps.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/Materials.h"
#include "core/Feature.h"
#include "core/Expression.h"
#include "core/Tool.h"
#include "sketch/SketchFeature.h"
#include "part/tools/FeatureTool.h"
#include "viewer/ViewerWidget.h"

#include <algorithm>
#include <format>
#include <optional>
#include <set>

namespace fabricad::app {

using core::ObjectId;
using core::ShapeKind;
using core::ShapeRef;

core::ShapeRef featureRef(const core::Feature& f) {
  ShapeKind k = ShapeKind::Feature;
  if (f.profileSource() || f.type() == "Sketch") k = ShapeKind::Sketch;
  else if (f.browserFolder() == "Joints") k = ShapeKind::Joint;
  else if (f.browserFolder() == "Construction") {
    if (f.asPlane()) k = ShapeKind::ConstructionPlane;
    else if (f.asAxis()) k = ShapeKind::ConstructionAxis;
    else if (f.asPoint()) k = ShapeKind::ConstructionPoint;
  }
  return ShapeRef::feature(f.id(), k);
}

ObjectId featureOfRef(const ShapeRef& r) {
  switch (r.kind) {
    case ShapeKind::Sketch:
    case ShapeKind::ConstructionPlane:
    case ShapeKind::ConstructionAxis:
    case ShapeKind::ConstructionPoint:
    case ShapeKind::Joint:
    case ShapeKind::Feature: return r.owner;
    default: return core::kInvalidId;
  }
}

// --- delete ------------------------------------------------------------------------------

std::string DeletePlan::summary(const core::Document& doc) const {
  std::vector<std::string> names;
  for (ObjectId id : features)
    if (const core::Feature* f = doc.feature(id)) names.push_back(f->name());
  for (ObjectId id : components)
    if (const core::Component* c = doc.component(id)) names.push_back(c->name);
  std::string s;
  for (size_t i = 0; i < names.size(); ++i) {
    if (i == 4 && names.size() > 5) {
      s += std::format(" and {} more", names.size() - 4);
      break;
    }
    if (i) s += ", ";
    s += names[i];
  }
  std::set<ObjectId> doomed(features.begin(), features.end());
  int dependents = 0;
  for (ObjectId id : features)
    for (ObjectId d : doc.dependentsOf(id, true))
      if (!doomed.count(d)) ++dependents;
  if (dependents > 0) s += std::format(" ({} dependent feature{} may fail)", dependents, dependents == 1 ? "" : "s");
  return s;
}

DeletePlan planDelete(const core::Document& doc, const std::vector<ShapeRef>& items) {
  DeletePlan plan;
  std::set<ObjectId> feats, comps;
  for (const ShapeRef& r : items) {
    if (ObjectId fid = featureOfRef(r); fid != core::kInvalidId) {
      if (doc.feature(fid)) feats.insert(fid);
    } else if (r.kind == ShapeKind::Body) {
      const core::Body* b = doc.body(r.owner);
      if (b && b->creator != core::kInvalidId && doc.feature(b->creator)) {
        feats.insert(b->creator);
        plan.bodyNotes.push_back(std::format("{} (created by {})", b->name, doc.feature(b->creator)->name()));
      }
    } else if (r.kind == ShapeKind::Component && r.owner != core::kRootComponentId && doc.component(r.owner)) {
      comps.insert(r.owner);
    }
  }
  for (ObjectId id : feats) plan.features.push_back(id);
  std::sort(plan.features.begin(), plan.features.end(),
            [&](ObjectId a, ObjectId b) { return doc.indexOf(a) < doc.indexOf(b); });
  plan.components.assign(comps.begin(), comps.end());
  // features that will fail once these are gone (the Delete prompt lists them, DOC-09)
  std::set<ObjectId> listed;
  for (ObjectId id : plan.features)
    for (ObjectId d : doc.dependentsOf(id, true))
      if (!feats.count(d) && doc.feature(d)) listed.insert(d);
  std::vector<ObjectId> deps(listed.begin(), listed.end());
  std::sort(deps.begin(), deps.end(), [&](ObjectId a, ObjectId b) { return doc.indexOf(a) < doc.indexOf(b); });
  for (ObjectId d : deps) plan.dependents.push_back(doc.feature(d)->name());  // timeline order
  return plan;
}

std::unique_ptr<core::UndoCommand> makeDeleteCommand(core::Document& doc, const DeletePlan& plan) {
  if (plan.isEmpty()) return nullptr;
  auto macro = std::make_unique<core::MacroCommand>("Delete");
  // last first, so every RemoveFeatureCommand records a stable index for its undo
  for (auto it = plan.features.rbegin(); it != plan.features.rend(); ++it)
    macro->add(std::make_unique<core::RemoveFeatureCommand>(doc, *it));
  // components go through the assembly delete (ASM-02): joints / rigid groups referencing them
  // or their bodies are removed with them, children and bodies move to the parent
  for (ObjectId c : plan.components)
    if (auto cmd = assembly::makeDeleteComponentCommand(doc, c)) macro->add(std::move(cmd));
  return macro;
}

// --- visibility ----------------------------------------------------------------------------

std::unique_ptr<core::UndoCommand> toggleVisibility(core::Document& doc, viewer::ViewerWidget* viewer,
                                                    const std::vector<ShapeRef>& items) {
  auto macro = std::make_unique<core::MacroCommand>("Show/Hide");
  std::set<ObjectId> done;
  for (const ShapeRef& r : items) {
    if (!done.insert(r.owner).second) continue;
    if (r.isBrep()) {
      if (const core::Body* b = doc.body(r.owner)) {
        const bool v = !b->visible;
        macro->add(core::makeBodyEditCommand(doc, r.owner, [v](core::Body& body) { body.visible = v; },
                                             v ? "Show body" : "Hide body"));
      }
    } else if (r.kind == ShapeKind::Component) {
      if (const core::Component* c = doc.component(r.owner)) {
        const bool v = !c->visible;
        macro->add(core::makeComponentEditCommand(doc, r.owner, [v](core::Component& comp) { comp.visible = v; },
                                                  v ? "Show component" : "Hide component"));
      }
    } else if (ObjectId fid = featureOfRef(r); fid != core::kInvalidId && viewer) {
      viewer->setFeatureHidden(fid, !viewer->isFeatureHidden(fid));
    } else if (core::hasKind(ShapeKind::OriginPlane | ShapeKind::OriginAxis | ShapeKind::OriginPoint, r.kind) &&
               viewer) {
      viewer->setOriginVisible(!viewer->isOriginVisible());
    }
  }
  if (macro->empty()) return nullptr;
  return macro;
}

std::unique_ptr<core::UndoCommand> isolate(core::Document& doc, const std::vector<ShapeRef>& items) {
  std::set<ObjectId> keepBodies, keepComps, pathComps;
  auto addPath = [&](ObjectId comp) {
    for (ObjectId p = comp; p != core::kInvalidId;) {
      pathComps.insert(p);
      const core::Component* c = doc.component(p);
      p = c ? c->parent : core::kInvalidId;
    }
  };
  for (const ShapeRef& r : items) {
    if (r.kind == ShapeKind::Body) {
      if (const core::Body* b = doc.body(r.owner)) {
        keepBodies.insert(b->id);
        addPath(b->component);
      }
    } else if (r.kind == ShapeKind::Component && doc.component(r.owner)) {
      keepComps.insert(r.owner);
      for (ObjectId d : assembly::descendants(doc, r.owner)) keepComps.insert(d);
      addPath(r.owner);
    }
  }
  if (keepBodies.empty() && keepComps.empty()) return nullptr;
  auto macro = std::make_unique<core::MacroCommand>("Isolate");
  for (const core::Body* b : std::as_const(doc).bodies()) {
    const bool keep = keepBodies.count(b->id) || keepComps.count(b->component);
    if (keep && !b->visible && keepBodies.count(b->id))
      macro->add(core::makeBodyEditCommand(doc, b->id, [](core::Body& body) { body.visible = true; }));
    else if (!keep && b->visible)
      macro->add(core::makeBodyEditCommand(doc, b->id, [](core::Body& body) { body.visible = false; }));
  }
  for (const core::Component* c : std::as_const(doc).components()) {
    if (c->isRoot()) continue;
    const bool keep = keepComps.count(c->id) || pathComps.count(c->id);
    if (keep != c->visible)
      macro->add(core::makeComponentEditCommand(doc, c->id, [keep](core::Component& comp) { comp.visible = keep; }));
  }
  if (macro->empty()) return nullptr;
  return macro;
}

std::unique_ptr<core::UndoCommand> showAll(core::Document& doc, viewer::ViewerWidget* viewer) {
  auto macro = std::make_unique<core::MacroCommand>("Show all");
  for (const core::Body* b : std::as_const(doc).bodies())
    if (!b->visible)
      macro->add(core::makeBodyEditCommand(doc, b->id, [](core::Body& body) { body.visible = true; }));
  for (const core::Component* c : std::as_const(doc).components())
    if (!c->visible)
      macro->add(core::makeComponentEditCommand(doc, c->id, [](core::Component& comp) { comp.visible = true; }));
  if (viewer) {
    viewer->setHiddenFeatures({});
    for (const char* folder : {"Sketches", "Construction", "Joints"}) viewer->setFolderVisible(folder, true);
  }
  if (macro->empty()) return nullptr;
  return macro;
}

// --- rename / suppress ------------------------------------------------------------------------

std::unique_ptr<core::UndoCommand> makeRenameFeature(core::Document& doc, ObjectId id, const std::string& name) {
  const core::Feature* f = doc.feature(id);
  if (!f || name.empty() || f->name() == name) return nullptr;
  return std::make_unique<core::ModifyFeatureCommand>(
      doc, id, [name](core::Feature& feat) { feat.setName(name); }, "Rename " + f->name());
}

std::unique_ptr<core::UndoCommand> makeRenameBody(core::Document& doc, ObjectId id, const std::string& name) {
  const core::Body* b = doc.body(id);
  if (!b || name.empty() || b->name == name) return nullptr;
  return core::makeBodyEditCommand(doc, id, [name](core::Body& body) { body.name = name; }, "Rename " + b->name);
}

std::unique_ptr<core::UndoCommand> makeRenameComponent(core::Document& doc, ObjectId id, const std::string& name) {
  const core::Component* c = doc.component(id);
  if (!c || name.empty() || c->name == name) return nullptr;
  if (c->isRoot()) {
    // the root component is the document: keep both names in sync
    std::string old = c->name;
    return std::make_unique<core::LambdaCommand>(
        "Rename " + old,
        [&doc, name] {
          doc.rootComponent().name = name;
          doc.setName(name);
          doc.notifyComponentChanged(core::kRootComponentId);
        },
        [&doc, old] {
          doc.rootComponent().name = old;
          doc.setName(old);
          doc.notifyComponentChanged(core::kRootComponentId);
        });
  }
  return core::makeComponentEditCommand(doc, id, [name](core::Component& comp) { comp.name = name; },
                                        "Rename " + c->name);
}

std::unique_ptr<core::UndoCommand> makeSuppress(core::Document& doc, ObjectId id, bool suppressed) {
  const core::Feature* f = doc.feature(id);
  if (!f || f->isSuppressed() == suppressed) return nullptr;
  return std::make_unique<core::ModifyFeatureCommand>(
      doc, id, [suppressed](core::Feature& feat) { feat.setSuppressed(suppressed); },
      (suppressed ? "Suppress " : "Unsuppress ") + f->name());
}

// --- physical material / appearance / component properties ------------------------------------

std::unique_ptr<core::UndoCommand> makeSetBodyMaterial(core::Document& doc, ObjectId bodyId, const std::string& material,
                                                       double density, bool applyColor) {
  const core::Body* b = doc.body(bodyId);
  if (!b) return nullptr;
  const core::Material* lib = core::findMaterial(material);
  std::string name = lib ? lib->name : material;
  double rho = lib ? lib->density : density;
  std::optional<std::array<float, 4>> color;
  if (applyColor && lib) color = lib->color;
  if (b->material == name && b->density == rho && (!color || b->color == *color)) return nullptr;
  return core::makeBodyEditCommand(
      doc, bodyId,
      [name, rho, color](core::Body& body) {
        body.material = name;
        body.density = rho;
        if (color) body.color = *color;
      },
      name.empty() ? "Clear material of " + b->name : std::format("Material {} for {}", name, b->name));
}

std::unique_ptr<core::UndoCommand> makeSetBodyColor(core::Document& doc, ObjectId bodyId, std::array<float, 4> rgba) {
  const core::Body* b = doc.body(bodyId);
  if (!b || b->color == rgba) return nullptr;
  return core::makeBodyEditCommand(doc, bodyId, [rgba](core::Body& body) { body.color = rgba; }, "Appearance of " + b->name);
}

std::unique_ptr<core::UndoCommand> makeSetComponentProperties(core::Document& doc, ObjectId componentId, const std::string& partNumber,
                                                              const std::string& description, const std::string& material) {
  const core::Component* c = doc.component(componentId);
  if (!c) return nullptr;
  if (c->partNumber == partNumber && c->description == description && c->material == material) return nullptr;
  return core::makeComponentEditCommand(
      doc, componentId,
      [partNumber, description, material](core::Component& comp) {
        comp.partNumber = partNumber;
        comp.description = description;
        comp.material = material;
      },
      "Properties of " + c->name);
}

// --- edit dispatch ----------------------------------------------------------------------------

EditCommand editCommandFor(const core::ToolRegistry& reg, const core::Feature& f) {
  const std::string id = std::to_string(f.id());
  if (const std::string own = f.editToolId(); !own.empty() && reg.has(own)) return {own, {"edit", id}};
  if (f.type() == "Sketch" || f.profileSource()) {
    if (reg.has("sketch.edit")) return {"sketch.edit", {id}};
    if (reg.has("sketch.create")) return {"sketch.create", {"edit", id}};
  }
  if (std::string t = part::toolIdForFeatureType(f.type()); !t.empty() && reg.has(t)) return {t, {"edit", id}};
  if (f.browserFolder() == "Joints") {
    for (const char* t : {"assembly.edit-joint", "assembly.joint"})
      if (reg.has(t)) return {t, {"edit", id}};
  }
  if (reg.has("app.edit-properties")) return {"app.edit-properties", {id}};
  return {};
}

// --- appearance -------------------------------------------------------------------------------

std::vector<core::ObjectId> appearanceTargets(const core::Document& doc, const std::vector<core::ShapeRef>& items) {
  std::vector<core::ObjectId> out;
  auto add = [&](core::ObjectId id) {
    if (doc.body(id) && std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
  };
  for (const core::ShapeRef& r : items) {
    if (r.kind == core::ShapeKind::Component) {
      for (core::ObjectId b : doc.displayedBodies(r.owner)) add(b);
    } else if (r.isBrep()) {
      add(r.owner);  // body, or the body owning the face/edge/vertex
    }
  }
  return out;
}

std::unique_ptr<core::UndoCommand> makeSetBodiesColor(core::Document& doc, const std::vector<core::ObjectId>& bodyIds,
                                                      std::array<float, 4> rgba) {
  auto macro = std::make_unique<core::MacroCommand>(bodyIds.size() == 1 ? "Appearance" : "Appearance of bodies");
  int n = 0;
  for (core::ObjectId id : bodyIds)
    if (auto cmd = makeSetBodyColor(doc, id, rgba)) {
      macro->add(std::move(cmd));
      ++n;
    }
  if (n == 0) return nullptr;
  return macro;
}

// --- parameters / units -----------------------------------------------------------------------------

namespace {
bool references(const std::string& expr, const std::string& name) {
  if (expr.empty()) return false;
  const std::vector<std::string> refs = core::referencedParameters(expr);
  return std::find(refs.begin(), refs.end(), name) != refs.end();
}
}  // namespace

std::vector<std::string> parameterUsers(const core::Document& doc, const std::string& name) {
  std::vector<std::string> out;
  auto add = [&](const std::string& who) {
    if (std::find(out.begin(), out.end(), who) == out.end()) out.push_back(who);
  };
  for (const core::Document::Parameter& p : doc.parameters())
    if (p.name != name && references(p.expression, name)) add(p.name);
  for (const auto& f : doc.features()) {
    bool used = false;
    for (const core::Property& prop : f->properties())
      if (prop.hasExpression() && references(prop.expression(), name)) used = true;
    if (auto* sk = dynamic_cast<const sketch::SketchFeature*>(f.get()))
      for (const sketch::Constraint& c : sk->sketch().constraints())
        if (references(c.expression, name)) used = true;
    if (used) add(f->name());
  }
  return out;
}

std::string checkParameterEdit(const core::Document& doc, const std::string& oldName, const std::string& newName) {
  if (!newName.empty() && newName != oldName) {
    if (!core::isValidParameterName(newName))
      return std::format("Invalid name '{}': use letters, digits and _, not starting with a digit, and not a unit or "
                         "function name",
                         newName);
    if (doc.parameter(newName)) return std::format("A parameter named '{}' already exists", newName);
  }
  if (oldName.empty() || newName == oldName) return {};
  const std::vector<std::string> users = parameterUsers(doc, oldName);
  if (users.empty()) return {};
  std::string list;
  for (const std::string& u : users) list += (list.empty() ? "" : ", ") + u;
  return std::format("'{}' is used by {}: {} it would break them", oldName, list, newName.empty() ? "removing" : "renaming");
}

std::unique_ptr<core::UndoCommand> makeSetUnits(core::Document& doc, const core::UnitSystem& units) {
  if (units == doc.units()) return nullptr;
  std::vector<std::string> lengths, angles;
  for (const auto& f : doc.features())
    if (auto* sk = dynamic_cast<const sketch::SketchFeature*>(f.get()))
      for (const sketch::Constraint& c : sk->sketch().constraints()) {
        if (c.expression.empty()) continue;
        for (const std::string& n : core::referencedParameters(c.expression))
          (c.type == sketch::ConstraintType::Angle ? angles : lengths).push_back(n);
      }
  return std::make_unique<core::SetUnitsCommand>(doc, units, std::move(lengths), std::move(angles));
}

}  // namespace fabricad::app
