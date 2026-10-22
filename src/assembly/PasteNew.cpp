// SPDX-License-Identifier: LGPL-2.1-or-later
#include "assembly/PasteNew.h"

#include "assembly/BaseBodyFeature.h"
#include "assembly/ComponentOps.h"
#include "assembly/tools/ComponentTools.h"
#include "core/Commands.h"
#include "core/Json.h"
#include "core/Registry.h"
#include "core/Selection.h"
#include "core/ShapeRef.h"
#include "core/UndoStack.h"

#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <cctype>
#include <memory>
#include <format>
#include <set>

namespace fabricad::assembly {

using core::Document;
using core::kInvalidId;
using core::ObjectId;

namespace {

/// Feature types that describe the assembly rather than a component's own geometry.
bool isAssemblyFeature(const core::Feature& f) {
  return f.browserFolder() == "Joints" || f.type() == "NewComponent" || f.type() == "InsertDesign" ||
         f.type() == "RigidGroup";
}

std::string copyName(const Document& doc, const std::string& base) {
  std::set<std::string> used;
  for (const core::Component* c : doc.components()) used.insert(c->name);
  for (int k = 1;; ++k) {
    std::string n = std::format("{} ({})", base, k);
    if (!used.count(n)) return n;
  }
}

struct Maps {
  std::map<ObjectId, ObjectId> ids;  ///< features, bodies and components
  std::map<ObjectId, ObjectId> components;
  std::map<std::string, std::string> names;
};

QJsonValue remap(const QJsonValue& v, const Maps& m) {
  if (v.isArray()) {
    QJsonArray out;
    for (const auto& e : v.toArray()) out.append(remap(e, m));
    return out;
  }
  if (!v.isObject()) return v;
  QJsonObject o = v.toObject();
  for (const QString& k : o.keys()) o[k] = remap(o[k], m);
  if (o.contains("owner") && o.contains("kind")) {  // a ShapeRef
    auto f = m.ids.find(core::idFromJson(o.value("owner")));
    if (f != m.ids.end()) o["owner"] = core::idToJson(f->second);
    if (o.value("name").isString())
      o["name"] = core::qs(renameHistoryName(core::ss(o.value("name").toString()), m.names));
  }
  if (o.value("component").isDouble()) {
    auto f = m.components.find(core::idFromJson(o.value("component")));
    if (f != m.components.end()) o["component"] = core::idToJson(f->second);
  }
  return o;
}

}  // namespace

std::string renameHistoryName(const std::string& historyName, const std::map<std::string, std::string>& names) {
  if (names.empty() || historyName.empty()) return historyName;
  std::string out;
  size_t i = 0;
  while (i < historyName.size()) {
    // a token starts at the beginning or after '{' / '|' and runs to the next '/'
    bool tokenStart = i == 0 || historyName[i - 1] == '{' || historyName[i - 1] == '|';
    if (tokenStart) {
      size_t slash = historyName.find('/', i);
      size_t stop = historyName.find_first_of("{|}", i);
      if (slash != std::string::npos && (stop == std::string::npos || slash < stop)) {
        auto it = names.find(historyName.substr(i, slash - i));
        if (it != names.end()) {
          out += it->second;
          i = slash;
          continue;
        }
      }
    }
    out += historyName[i++];
  }
  return out;
}

DeepCopyResult deepCopyComponent(Document& doc, ObjectId source, ObjectId parent) {
  DeepCopyResult res;
  const core::Component* src = doc.component(source);
  if (!src || source == core::kRootComponentId) return res;
  if (!doc.component(parent)) parent = core::kRootComponentId;
  const gp_Trsf world = doc.worldPlacement(source);

  // 1. component sub-tree (definitions for occurrences), parents first
  Maps m;
  struct Pending {
    ObjectId from;      ///< component whose features/bodies are copied (definition)
    ObjectId instance;  ///< component in the source tree (for name/children)
    ObjectId newParent;
  };
  std::vector<Pending> queue{{doc.definitionOf(source), source, parent}};
  for (size_t qi = 0; qi < queue.size(); ++qi) {
    Pending p = queue[qi];
    const core::Component* inst = doc.component(p.instance);
    const core::Component* def = doc.component(p.from);
    if (!inst || !def) continue;
    core::Component c;
    c.name = qi == 0 ? copyName(doc, def->name) : def->name;
    c.parent = p.newParent;
    c.placement = qi == 0 ? doc.worldPlacement(p.newParent).Inverted() * world : inst->placement;
    c.visible = inst->visible;
    c.partNumber = def->partNumber;
    c.description = def->description;
    c.material = def->material;
    ObjectId nid = doc.addComponent(c).id;
    if (qi == 0) res.component = nid;
    m.components[p.from] = nid;
    m.components[p.instance] = nid;
    m.ids[p.from] = nid;
    m.ids[p.instance] = nid;
    res.components[p.instance] = nid;
    for (ObjectId child : inst->children) queue.push_back({doc.definitionOf(child), child, nid});
  }

  // 2. features of the copied definitions, in timeline order (only the active part)
  std::vector<const core::Feature*> feats;
  for (int i = 0; i < doc.rollbackIndex(); ++i) {
    const core::Feature* f = doc.featureAt(i);
    if (!f || isAssemblyFeature(*f)) continue;
    bool mine = false;
    for (const Pending& p : queue)
      if (f->component() == p.from) mine = true;
    if (mine) feats.push_back(f);
  }
  // Bodies of the copied components whose creating feature is not copied (it lives outside the
  // tree, e.g. "Create Components from Bodies" moved a root body into the component): those
  // become frozen BaseBody copies of their current shape (ASM-03), else the copy is empty.
  std::set<ObjectId> copiedFeatures;
  for (const core::Feature* f : feats) copiedFeatures.insert(f->id());
  struct Frozen {
    ObjectId body;       ///< source body
    ObjectId component;  ///< new owning component
    ObjectId feature = kInvalidId;
  };
  std::vector<Frozen> frozen;
  std::set<ObjectId> seenDefs;
  for (const Pending& p : queue) {
    if (!seenDefs.insert(p.from).second || !m.components.count(p.from)) continue;
    for (ObjectId b : doc.bodiesOf(p.from)) {
      const core::Body* body = doc.body(b);
      if (!body || copiedFeatures.count(body->creator) || doc.bodyShape(b).IsNull()) continue;
      frozen.push_back({b, m.components.at(p.from)});
    }
  }
  for (Frozen& fz : frozen) {
    fz.feature = doc.allocateId();
    ObjectId nb = doc.allocateId();
    m.ids[fz.body] = nb;
    res.bodies[fz.body] = nb;
  }
  // ids first (bodies may be referenced by any later feature), then one feature at a time so
  // uniqueFeatureName sees the copies already added and history names map to the new names.
  for (const core::Feature* f : feats) {
    ObjectId nid = doc.allocateId();
    m.ids[f->id()] = nid;
    res.features[f->id()] = nid;
    for (ObjectId b : f->createdBodies())
      if (!m.ids.count(b)) {
        ObjectId nb = doc.allocateId();
        m.ids[b] = nb;
        res.bodies[b] = nb;
      }
  }
  std::vector<std::pair<ObjectId, ObjectId>> bodyPairs(res.bodies.begin(), res.bodies.end());
  for (const Frozen& fz : frozen) {
    const core::Body* body = doc.body(fz.body);
    auto nf = std::make_unique<BaseBodyFeature>();
    nf->setShape(doc.bodyShape(fz.body), body ? body->name : std::string());
    QJsonObject j = nf->toJson();
    j["id"] = core::idToJson(fz.feature);
    j["name"] = core::qs(doc.uniqueFeatureName("BaseBody"));
    j["component"] = core::idToJson(fz.component);
    j["createdBodies"] = QJsonArray{core::idToJson(m.ids.at(fz.body))};
    nf->fromJson(j);
    doc.addFeature(std::move(nf), -1);
  }
  for (const core::Feature* f : feats) {
    std::string base = f->name();
    while (!base.empty() && std::isdigit(static_cast<unsigned char>(base.back()))) base.pop_back();
    if (base.empty()) base = f->type();
    m.names[f->name()] = doc.uniqueFeatureName(base);
    QJsonObject j = remap(f->toJson(), m).toObject();
    j["id"] = core::idToJson(m.ids.at(f->id()));
    j["name"] = core::qs(m.names.at(f->name()));
    if (f->component() != kInvalidId && m.components.count(f->component()))
      j["component"] = core::idToJson(m.components.at(f->component()));
    QJsonArray created;
    for (ObjectId b : f->createdBodies()) created.append(core::idToJson(m.ids.count(b) ? m.ids.at(b) : b));
    if (!created.isEmpty()) j["createdBodies"] = created;
    std::unique_ptr<core::Feature> nf = core::FeatureFactory::instance().create(f->type());
    if (!nf) continue;
    nf->fromJson(j);
    doc.addFeature(std::move(nf), -1);
  }
  doc.recompute();

  // 3. body attributes follow the source bodies
  for (const auto& [oldId, newId] : bodyPairs) {
    const core::Body* ob = doc.body(oldId);
    core::Body* nb = doc.body(newId);
    if (!ob || !nb) continue;
    nb->name = ob->name;
    nb->color = ob->color;
    nb->material = ob->material;
    nb->visible = ob->visible;
    if (m.components.count(ob->component)) nb->component = m.components.at(ob->component);
    doc.notifyBodyChanged(newId);
  }
  for (const auto& [o, n] : res.components) doc.notifyComponentChanged(n);
  return res;
}

std::vector<ObjectId> pasteNewComponents(core::ToolContext& ctx, bool startMove) {
  std::vector<ObjectId> created;
  if (!ctx.document) return created;
  Document& doc = *ctx.document;
  std::vector<ObjectId> sources;
  for (ObjectId id : componentClipboard())
    if (doc.component(id) && id != core::kRootComponentId) sources.push_back(id);
  if (sources.empty()) {
    ctx.showMessage("Nothing to paste: copy a component first (Ctrl+C)", true);
    return created;
  }
  auto out = std::make_shared<std::vector<ObjectId>>();
  auto edit = [&doc, sources, out] {
    std::vector<ObjectId>& created = *out;
    created.clear();
    for (ObjectId src : sources) {
      const core::Component* c = doc.component(src);
      if (!c) continue;
      ObjectId parent = doc.activeComponent();
      ObjectId def = doc.definitionOf(src);
      if (parent == kInvalidId || !doc.component(parent) || parent == src || parent == def ||
          isAncestorOf(doc, src, parent) || isAncestorOf(doc, def, parent))
        parent = c->parent != kInvalidId ? c->parent : core::kRootComponentId;
      DeepCopyResult r = deepCopyComponent(doc, src, parent);
      if (r.component != kInvalidId) created.push_back(r.component);
    }
  };
  if (ctx.undo) ctx.undo->push(std::make_unique<core::DocumentSnapshotCommand>(doc, "Paste New", edit));
  else edit();
  created = *out;
  if (created.empty()) return created;
  std::vector<core::ShapeRef> refs;
  for (ObjectId id : created) refs.push_back(core::ShapeRef::component(id));
  if (ctx.selection) ctx.selection->set(refs);
  auto showsNothing = [&doc](ObjectId c) {
    if (!displayedBodies(doc, c).empty()) return false;
    for (ObjectId d : descendants(doc, c))
      if (!displayedBodies(doc, d).empty()) return false;
    return true;
  };
  std::vector<std::string> empty;
  for (ObjectId id : created)
    if (showsNothing(id)) empty.push_back(componentLabel(doc, id));
  if (!empty.empty()) {
    std::string names;
    for (const std::string& n : empty) names += (names.empty() ? "" : ", ") + n;
    ctx.showMessage(std::format("Pasted {}, but it has no bodies (the copied component was empty)", names), true);
  } else {
    ctx.showMessage(created.size() == 1 ? std::format("Pasted new component {}", componentLabel(doc, created.front()))
                                        : std::format("Pasted {} new components", created.size()),
                    false);
  }
  if (startMove && ctx.manager && ctx.manager->registry().has("assembly.move")) ctx.manager->run("assembly.move");
  return created;
}

}  // namespace fabricad::assembly
