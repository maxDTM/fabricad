// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/Document.h"

#include "core/Json.h"
#include "core/Log.h"
#include "core/Registry.h"
#include "core/Selection.h"
#include "core/ShapeUtils.h"
#include "core/UndoStack.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <Standard_Failure.hxx>

#include <algorithm>
#include <cctype>
#include <format>
#include <set>

namespace fabricad::core {

// ---------------------------------------------------------------------------
// construction / reset
// ---------------------------------------------------------------------------

Document::Document(QObject* parent)
    : QObject(parent), undo_(std::make_unique<UndoStack>()), selection_(std::make_unique<SelectionModel>()) {
  resetToEmpty();
}

Document::~Document() = default;

void Document::resetToEmpty() {
  features_.clear();
  rollback_ = 0;
  firstDirty_ = 0;
  cancelRequested_ = false;
  bodies_.clear();
  bodyOrder_.clear();
  currentState_.clear();
  components_.clear();
  parameters_.clear();
  extensions_.clear();
  nextId_ = kFirstFreeId;
  Component root;
  root.id = kRootComponentId;
  root.name = name_;
  root.grounded = true;
  components_[kRootComponentId] = root;
  activeComponent_ = kRootComponentId;
  selection_->clear();
}

void Document::clear() {
  resetToEmpty();
  filePath_.clear();
  name_ = "Untitled";
  components_[kRootComponentId].name = name_;
  undo_->clear();
  modified_ = false;
  emit documentReset();
  emit modifiedChanged(false);
}

void Document::setName(std::string n) {
  if (n == name_) return;
  name_ = std::move(n);
  if (auto it = components_.find(kRootComponentId); it != components_.end()) it->second.name = name_;
  touchModified();
  emit nameChanged();
}

void Document::setModified(bool m) {
  if (m == modified_) return;
  modified_ = m;
  emit modifiedChanged(m);
}

void Document::touchModified() { setModified(true); }

void Document::setUnits(const UnitSystem& u) {
  if (u == units_) return;
  units_ = u;
  markAllDirty();
  touchModified();
  emit unitsChanged();
}

ObjectId Document::allocateId() { return nextId_++; }

// ---------------------------------------------------------------------------
// features
// ---------------------------------------------------------------------------

Feature* Document::addFeature(std::unique_ptr<Feature> f, int index) {
  if (!f) return nullptr;
  if (f->id_ == kInvalidId) f->id_ = allocateId();
  else nextId_ = std::max(nextId_, f->id_ + 1);
  if (f->name_.empty()) f->name_ = uniqueFeatureName(f->type_);
  if (f->component_ == kInvalidId || !components_.count(f->component_)) f->component_ = activeComponent_;
  int count = static_cast<int>(features_.size());
  if (index < 0 || index > count) index = rollback_;
  index = std::clamp(index, 0, count);
  Feature* raw = f.get();
  features_.insert(features_.begin() + index, std::move(f));
  if (index <= rollback_) ++rollback_;
  firstDirty_ = std::min(firstDirty_, index);
  raw->setStatus(FeatureStatus::Stale);
  touchModified();
  emit featureAdded(raw->id(), index);
  return raw;
}

std::unique_ptr<Feature> Document::takeFeature(ObjectId id) {
  int idx = indexOf(id);
  if (idx < 0) return nullptr;
  std::unique_ptr<Feature> f = std::move(features_[static_cast<size_t>(idx)]);
  features_.erase(features_.begin() + idx);
  if (idx < rollback_) --rollback_;
  firstDirty_ = std::min(firstDirty_, idx);
  for (ObjectId b : f->createdBodies_) removeBody(b);
  selection_->removeOwner(id);
  touchModified();
  emit featureRemoved(id, idx);
  return f;
}

bool Document::canMoveFeature(ObjectId id, int newIndex, std::string* why) const {
  int oldIndex = indexOf(id);
  if (oldIndex < 0) {
    if (why) *why = "unknown feature";
    return false;
  }
  int count = static_cast<int>(features_.size());
  if (newIndex < 0 || newIndex >= count) {
    if (why) *why = "index out of range";
    return false;
  }
  if (newIndex == oldIndex) return true;
  // simulate the new order
  std::vector<ObjectId> order;
  for (const auto& f : features_) order.push_back(f->id());
  order.erase(order.begin() + oldIndex);
  order.insert(order.begin() + newIndex, id);
  auto posOf = [&](ObjectId x) {
    return static_cast<int>(std::find(order.begin(), order.end(), x) - order.begin());
  };
  for (ObjectId dep : dependenciesOf(id)) {
    if (posOf(dep) > newIndex) {
      if (why) *why = std::format("would move before its input '{}'", feature(dep)->name());
      return false;
    }
  }
  for (ObjectId dependent : dependentsOf(id)) {
    if (posOf(dependent) < newIndex) {
      if (why) *why = std::format("would move after dependent '{}'", feature(dependent)->name());
      return false;
    }
  }
  return true;
}

bool Document::moveFeature(ObjectId id, int newIndex) {
  if (!canMoveFeature(id, newIndex)) return false;
  int oldIndex = indexOf(id);
  if (oldIndex == newIndex) return true;
  std::unique_ptr<Feature> f = std::move(features_[static_cast<size_t>(oldIndex)]);
  features_.erase(features_.begin() + oldIndex);
  features_.insert(features_.begin() + newIndex, std::move(f));
  bool wasActive = oldIndex < rollback_;
  bool isActive = newIndex < rollback_;
  if (wasActive && !isActive) --rollback_;
  else if (!wasActive && isActive) ++rollback_;
  firstDirty_ = std::min({firstDirty_, oldIndex, newIndex});
  touchModified();
  emit featureMoved(id, oldIndex, newIndex);
  return true;
}

Feature* Document::feature(ObjectId id) {
  for (auto& f : features_)
    if (f->id() == id) return f.get();
  return nullptr;
}

const Feature* Document::feature(ObjectId id) const {
  for (const auto& f : features_)
    if (f->id() == id) return f.get();
  return nullptr;
}

Feature* Document::featureAt(int index) {
  if (index < 0 || index >= static_cast<int>(features_.size())) return nullptr;
  return features_[static_cast<size_t>(index)].get();
}

const Feature* Document::featureAt(int index) const {
  if (index < 0 || index >= static_cast<int>(features_.size())) return nullptr;
  return features_[static_cast<size_t>(index)].get();
}

int Document::indexOf(ObjectId featureId) const {
  for (size_t i = 0; i < features_.size(); ++i)
    if (features_[i]->id() == featureId) return static_cast<int>(i);
  return -1;
}

std::string Document::uniqueFeatureName(const std::string& base) const {
  std::set<std::string> used;
  for (const auto& f : features_) used.insert(f->name());
  for (int n = 1;; ++n) {
    std::string candidate = base + std::to_string(n);
    if (!used.count(candidate)) return candidate;
  }
}

void Document::notifyFeatureChanged(ObjectId id) {
  int idx = indexOf(id);
  if (idx < 0) return;
  firstDirty_ = std::min(firstDirty_, idx);
  touchModified();
  emit featureChanged(id);
}

void Document::setRollbackIndex(int index) {
  int count = static_cast<int>(features_.size());
  index = std::clamp(index, 0, count);
  if (index == rollback_) return;
  if (index > rollback_) {
    firstDirty_ = std::min(firstDirty_, rollback_);
    rollback_ = index;
    touchModified();
    emit rollbackChanged(rollback_);
    recompute();
    return;
  }
  rollback_ = index;
  for (int i = rollback_; i < count; ++i) {
    Feature* f = features_[static_cast<size_t>(i)].get();
    if (f->status() != FeatureStatus::Stale) {
      f->setStatus(FeatureStatus::Stale);
      emit featureStatusChanged(f->id());
    }
  }
  applyState(bodyStatesAfter(rollback_ - 1));
  touchModified();
  emit rollbackChanged(rollback_);
}

bool Document::isFeatureActive(ObjectId id) const {
  int idx = indexOf(id);
  return idx >= 0 && idx < rollback_;
}

// ---------------------------------------------------------------------------
// dependencies
// ---------------------------------------------------------------------------

std::vector<ObjectId> Document::featuresTouchingBody(ObjectId bodyId, int beforeIndex) const {
  std::vector<ObjectId> out;
  if (beforeIndex < 0) beforeIndex = static_cast<int>(features_.size());
  for (int i = 0; i < beforeIndex && i < static_cast<int>(features_.size()); ++i) {
    const Feature* f = features_[static_cast<size_t>(i)].get();
    bool touches = std::find(f->touchedBodies_.begin(), f->touchedBodies_.end(), bodyId) != f->touchedBodies_.end() ||
                   std::find(f->createdBodies_.begin(), f->createdBodies_.end(), bodyId) != f->createdBodies_.end();
    if (touches) out.push_back(f->id());
  }
  return out;
}

TopoDS_Shape Document::namedSubShape(ObjectId bodyId, const std::string& name, int beforeIndex) const {
  if (name.empty()) return {};
  if (beforeIndex < 0) beforeIndex = static_cast<int>(features_.size());
  for (int i = std::min(beforeIndex, static_cast<int>(features_.size())) - 1; i >= 0; --i) {
    const Feature* f = features_[static_cast<size_t>(i)].get();
    bool touches = std::find(f->touchedBodies_.begin(), f->touchedBodies_.end(), bodyId) != f->touchedBodies_.end() ||
                   std::find(f->createdBodies_.begin(), f->createdBodies_.end(), bodyId) != f->createdBodies_.end();
    if (!touches) continue;
    auto it = f->namedShapes_.find(name);
    return it == f->namedShapes_.end() ? TopoDS_Shape() : it->second;
  }
  return {};
}

std::string Document::subShapeName(ObjectId bodyId, const TopoDS_Shape& sub, int beforeIndex) const {
  if (sub.IsNull()) return {};
  if (beforeIndex < 0) beforeIndex = static_cast<int>(features_.size());
  for (int i = std::min(beforeIndex, static_cast<int>(features_.size())) - 1; i >= 0; --i) {
    const Feature* f = features_[static_cast<size_t>(i)].get();
    bool touches = std::find(f->touchedBodies_.begin(), f->touchedBodies_.end(), bodyId) != f->touchedBodies_.end() ||
                   std::find(f->createdBodies_.begin(), f->createdBodies_.end(), bodyId) != f->createdBodies_.end();
    if (!touches) continue;
    for (const auto& [name, shape] : f->namedShapes_)
      if (shape.IsSame(sub)) return name;
    return {};
  }
  return {};
}

std::vector<ObjectId> Document::dependenciesOf(ObjectId featureId) const {
  std::vector<ObjectId> out;
  const Feature* f = feature(featureId);
  if (!f) return out;
  int idx = indexOf(featureId);
  auto push = [&](ObjectId id) {
    if (id != featureId && std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
  };
  for (ObjectId dep : f->dependencies()) {
    if (feature(dep)) push(dep);
    else if (bodies_.count(dep))
      for (ObjectId t : featuresTouchingBody(dep, idx)) push(t);
  }
  return out;
}

std::vector<ObjectId> Document::dependentsOf(ObjectId featureId, bool transitive) const {
  std::vector<ObjectId> out;
  std::vector<ObjectId> frontier{featureId};
  while (!frontier.empty()) {
    ObjectId cur = frontier.back();
    frontier.pop_back();
    int idx = indexOf(cur);
    for (int i = idx + 1; i < static_cast<int>(features_.size()); ++i) {
      ObjectId other = features_[static_cast<size_t>(i)]->id();
      std::vector<ObjectId> deps = dependenciesOf(other);
      if (std::find(deps.begin(), deps.end(), cur) == deps.end()) continue;
      if (std::find(out.begin(), out.end(), other) != out.end()) continue;
      out.push_back(other);
      if (transitive) frontier.push_back(other);
    }
  }
  return out;
}

// ---------------------------------------------------------------------------
// recompute
// ---------------------------------------------------------------------------

void Document::markDirty(ObjectId featureId) {
  int idx = indexOf(featureId);
  if (idx >= 0) firstDirty_ = std::min(firstDirty_, idx);
}

void Document::markAllDirty() { firstDirty_ = 0; }

void Document::recomputeAll() {
  markAllDirty();
  recompute();
}

void Document::recompute() {
  emit recomputeStarted();
  evaluateParameters();
  int start = std::clamp(firstDirty_, 0, rollback_);
  std::map<ObjectId, TopoDS_Shape> state = bodyStatesAfter(start - 1);

  for (int i = start; i < rollback_; ++i) {
    Feature& f = *features_[static_cast<size_t>(i)];
    f.touchedBodies_.clear();
    if (f.isSuppressed()) {
      f.bodyStatesAfter_ = state;
      f.setStatus(FeatureStatus::Ok, "suppressed");
      emit featureStatusChanged(f.id());
      continue;
    }
    RecomputeContext ctx(*this, f, state);
    try {
      f.recompute(ctx);
    } catch (const Standard_Failure& e) {
      ctx.setError(std::format("OCCT: {}", e.GetMessageString() ? e.GetMessageString() : "failure"));
    } catch (const std::exception& e) {
      ctx.setError(e.what());
    } catch (...) {
      ctx.setError("unknown exception");
    }
    if (ctx.hasError()) {
      f.bodyStatesAfter_ = state;  // effect skipped
      f.setStatus(FeatureStatus::Error, ctx.errorMessage());
    } else {
      state = ctx.state();
      f.bodyStatesAfter_ = state;
      f.touchedBodies_ = ctx.touched_;
      std::string msg;
      FeatureStatus st = FeatureStatus::Ok;
      if (ctx.hasWarning()) {
        st = FeatureStatus::Warning;
        msg = ctx.warnings().front();
        if (ctx.warnings().size() > 1) msg += std::format(" (+{} more)", ctx.warnings().size() - 1);
      }
      // upstream problems propagate as a warning
      for (ObjectId dep : dependenciesOf(f.id())) {
        const Feature* d = feature(dep);
        if (d && d->status() == FeatureStatus::Error) {
          st = FeatureStatus::Warning;
          if (msg.empty()) msg = std::format("input '{}' has errors", d->name());
          break;
        }
      }
      f.setStatus(st, msg);
    }
    emit featureStatusChanged(f.id());
  }
  // Features after the marker are stale.
  for (int i = rollback_; i < static_cast<int>(features_.size()); ++i) {
    Feature& f = *features_[static_cast<size_t>(i)];
    if (f.status() != FeatureStatus::Stale) {
      f.setStatus(FeatureStatus::Stale);
      emit featureStatusChanged(f.id());
    }
  }
  firstDirty_ = rollback_;
  applyState(state);
  emit recomputeFinished();
}

void Document::applyState(const std::map<ObjectId, TopoDS_Shape>& newState) {
  std::vector<ObjectId> changed;
  for (const auto& [id, shape] : newState) {
    auto it = currentState_.find(id);
    if (it == currentState_.end() || !it->second.IsEqual(shape)) changed.push_back(id);
  }
  for (const auto& [id, shape] : currentState_)
    if (!newState.count(id)) changed.push_back(id);
  currentState_ = newState;
  for (ObjectId id : changed)
    if (bodies_.count(id)) emit bodyChanged(id);
}

// ---------------------------------------------------------------------------
// bodies
// ---------------------------------------------------------------------------

Body* Document::body(ObjectId id) {
  auto it = bodies_.find(id);
  return it == bodies_.end() ? nullptr : &it->second;
}

const Body* Document::body(ObjectId id) const {
  auto it = bodies_.find(id);
  return it == bodies_.end() ? nullptr : &it->second;
}

std::vector<Body*> Document::bodies() {
  std::vector<Body*> out;
  for (ObjectId id : bodyOrder_)
    if (Body* b = body(id)) out.push_back(b);
  return out;
}

std::vector<const Body*> Document::bodies() const {
  std::vector<const Body*> out;
  for (ObjectId id : bodyOrder_)
    if (const Body* b = body(id)) out.push_back(b);
  return out;
}

Body& Document::addBody(Body b) {
  if (b.id == kInvalidId) b.id = allocateId();
  else nextId_ = std::max(nextId_, b.id + 1);
  if (b.name.empty()) b.name = uniqueBodyName();
  if (!components_.count(b.component)) b.component = kRootComponentId;
  ObjectId id = b.id;
  bodies_[id] = std::move(b);
  if (std::find(bodyOrder_.begin(), bodyOrder_.end(), id) == bodyOrder_.end()) bodyOrder_.push_back(id);
  touchModified();
  emit bodyAdded(id);
  return bodies_[id];
}

bool Document::removeBody(ObjectId id) {
  if (!bodies_.erase(id)) return false;
  bodyOrder_.erase(std::remove(bodyOrder_.begin(), bodyOrder_.end(), id), bodyOrder_.end());
  currentState_.erase(id);
  selection_->removeOwner(id);
  touchModified();
  emit bodyRemoved(id);
  return true;
}

const TopoDS_Shape& Document::bodyShape(ObjectId id) const {
  static const TopoDS_Shape kNull;
  auto it = currentState_.find(id);
  return it == currentState_.end() ? kNull : it->second;
}

const std::map<ObjectId, TopoDS_Shape>& Document::bodyStatesAfter(int featureIndex) const {
  if (featureIndex < 0 || featureIndex >= static_cast<int>(features_.size())) return emptyState_;
  return features_[static_cast<size_t>(featureIndex)]->bodyStatesAfter_;
}

std::vector<ObjectId> Document::liveBodies(bool visibleOnly) const {
  std::vector<ObjectId> out;
  for (ObjectId id : bodyOrder_) {
    auto it = currentState_.find(id);
    if (it == currentState_.end() || it->second.IsNull()) continue;
    if (visibleOnly) {
      const Body* b = body(id);
      if (!b || !b->visible) continue;
      const Component* c = component(b->component);
      if (c && !c->visible) continue;
    }
    out.push_back(id);
  }
  return out;
}

void Document::notifyBodyChanged(ObjectId id) {
  if (!bodies_.count(id)) return;
  touchModified();
  emit bodyChanged(id);
}

std::string Document::uniqueBodyName(const std::string& base) const {
  std::set<std::string> used;
  for (const auto& [id, b] : bodies_) used.insert(b.name);
  for (int n = 1;; ++n) {
    std::string c = base + std::to_string(n);
    if (!used.count(c)) return c;
  }
}

// ---------------------------------------------------------------------------
// components
// ---------------------------------------------------------------------------

Component& Document::rootComponent() { return components_[kRootComponentId]; }
const Component& Document::rootComponent() const { return components_.at(kRootComponentId); }

Component* Document::component(ObjectId id) {
  auto it = components_.find(id);
  return it == components_.end() ? nullptr : &it->second;
}

const Component* Document::component(ObjectId id) const {
  auto it = components_.find(id);
  return it == components_.end() ? nullptr : &it->second;
}

std::vector<Component*> Document::components() {
  std::vector<Component*> out;
  for (auto& [id, c] : components_) out.push_back(&c);
  return out;
}

std::vector<const Component*> Document::components() const {
  std::vector<const Component*> out;
  for (const auto& [id, c] : components_) out.push_back(&c);
  return out;
}

Component& Document::addComponent(const std::string& name, ObjectId parent) {
  Component c;
  c.name = name;
  c.parent = parent;
  return addComponent(std::move(c));
}

Component& Document::addComponent(Component c) {
  if (c.parent == kInvalidId || !components_.count(c.parent)) c.parent = kRootComponentId;
  if (c.id == kInvalidId || components_.count(c.id)) c.id = allocateId();
  else nextId_ = std::max(nextId_, c.id + 1);
  if (c.name.empty()) c.name = std::format("Component{}", components_.size());
  c.children.clear();
  const ObjectId parent = c.parent;
  const ObjectId id = c.id;
  components_[id] = std::move(c);
  components_[parent].children.push_back(id);
  touchModified();
  emit componentAdded(id);
  return components_[id];
}

ObjectId Document::definitionOf(ObjectId componentId) const {
  const Component* c = component(componentId);
  if (!c) return kInvalidId;
  int guard = 0;
  while (c->definition != kInvalidId && guard++ < 64) {
    const Component* d = component(c->definition);
    if (!d) break;
    c = d;
  }
  return c->id;
}

std::vector<ObjectId> Document::displayedBodies(ObjectId componentId) const {
  const ObjectId def = definitionOf(componentId);
  return def == kInvalidId ? std::vector<ObjectId>{} : bodiesOf(def);
}

bool Document::removeComponent(ObjectId id) {
  if (id == kRootComponentId) return false;
  auto it = components_.find(id);
  if (it == components_.end()) return false;
  Component c = it->second;
  for (ObjectId child : std::vector<ObjectId>(c.children)) removeComponent(child);
  for (auto& [bid, b] : bodies_)
    if (b.component == id) {
      b.component = c.parent;
      emit bodyChanged(bid);
    }
  if (Component* p = component(c.parent))
    p->children.erase(std::remove(p->children.begin(), p->children.end(), id), p->children.end());
  components_.erase(id);
  if (activeComponent_ == id) setActiveComponent(c.parent);
  selection_->removeOwner(id);
  touchModified();
  emit componentRemoved(id);
  return true;
}

void Document::setActiveComponent(ObjectId id) {
  if (!components_.count(id)) id = kRootComponentId;
  if (id == activeComponent_) return;
  activeComponent_ = id;
  emit activeComponentChanged(id);
}

gp_Trsf Document::worldPlacement(ObjectId componentId) const {
  gp_Trsf world;
  std::vector<const Component*> chain;
  const Component* c = component(componentId);
  int guard = 0;
  while (c && guard++ < 1000) {
    chain.push_back(c);
    c = c->parent == kInvalidId ? nullptr : component(c->parent);
  }
  for (auto it = chain.rbegin(); it != chain.rend(); ++it) world = world.Multiplied((*it)->placement);
  return world;
}

std::vector<ObjectId> Document::bodiesOf(ObjectId componentId) const {
  std::vector<ObjectId> out;
  for (ObjectId id : bodyOrder_)
    if (const Body* b = body(id); b && b->component == componentId) out.push_back(id);
  return out;
}

void Document::notifyComponentChanged(ObjectId id) {
  if (!components_.count(id)) return;
  touchModified();
  emit componentChanged(id);
}

// ---------------------------------------------------------------------------
// parameters / expressions
// ---------------------------------------------------------------------------

const Document::Parameter* Document::parameter(const std::string& name) const {
  for (const auto& p : parameters_)
    if (p.name == name) return &p;
  return nullptr;
}

bool Document::setParameter(const std::string& name, const std::string& expression, const std::string& comment) {
  if (!isValidParameterName(name)) return false;
  bool found = false;
  for (auto& p : parameters_) {
    if (p.name == name) {
      p.expression = expression;
      p.comment = comment;
      found = true;
    }
  }
  if (!found) {
    Parameter np;
    np.name = name;
    np.expression = expression;
    np.comment = comment;
    parameters_.push_back(std::move(np));
  }
  evaluateParameters();
  markAllDirty();
  touchModified();
  emit parametersChanged();
  return true;
}

bool Document::removeParameter(const std::string& name) {
  auto it = std::remove_if(parameters_.begin(), parameters_.end(), [&](const Parameter& p) { return p.name == name; });
  if (it == parameters_.end()) return false;
  parameters_.erase(it, parameters_.end());
  evaluateParameters();
  markAllDirty();
  touchModified();
  emit parametersChanged();
  return true;
}

void Document::evaluateParameters() {
  for (auto& p : parameters_) {
    p.ok = false;
    p.error.clear();
  }
  ExpressionContext ctx;
  ctx.units = units_;
  ctx.lookup = [this](const std::string& n) -> std::optional<Quantity> {
    const Parameter* p = parameter(n);
    if (p && p->ok) return p->value;
    return std::nullopt;
  };
  for (size_t pass = 0; pass <= parameters_.size(); ++pass) {
    bool progress = false;
    for (auto& p : parameters_) {
      if (p.ok) continue;
      EvalResult r = evaluateExpression(p.expression, ctx);
      if (r.ok) {
        p.value = r.value;
        p.ok = true;
        progress = true;
      } else {
        p.error = r.error;
      }
    }
    if (!progress) break;
  }
}

ExpressionContext Document::expressionContext() const {
  ExpressionContext ctx;
  ctx.units = units_;
  ctx.lookup = [this](const std::string& n) -> std::optional<Quantity> {
    const Parameter* p = parameter(n);
    if (p && p->ok) return p->value;
    return std::nullopt;
  };
  return ctx;
}

EvalResult Document::evaluate(const std::string& expr, std::optional<Dimension> expected) const {
  return evaluateExpression(expr, expressionContext(), expected);
}

// ---------------------------------------------------------------------------
// extension data
// ---------------------------------------------------------------------------

QJsonObject Document::extensionData(const std::string& key) const {
  auto it = extensions_.find(key);
  return it == extensions_.end() ? QJsonObject() : it->second;
}

void Document::setExtensionData(const std::string& key, const QJsonObject& data) {
  extensions_[key] = data;
  touchModified();
}

// ---------------------------------------------------------------------------
// serialisation
// ---------------------------------------------------------------------------

QJsonObject Document::toJson() const {
  QJsonObject root;
  root["format"] = "fabricad";
  root["version"] = kFormatVersion;
  root["name"] = qs(name_);
  QJsonObject units;
  units["length"] = lengthUnitSymbol(units_.length);
  units["angle"] = angleUnitSymbol(units_.angle);
  units["precision"] = units_.precision;
  root["units"] = units;
  root["nextId"] = idToJson(nextId_);

  QJsonArray params;
  for (const auto& p : parameters_) {
    QJsonObject o;
    o["name"] = qs(p.name);
    o["expression"] = qs(p.expression);
    if (!p.comment.empty()) o["comment"] = qs(p.comment);
    params.append(o);
  }
  root["parameters"] = params;

  QJsonArray comps;
  for (const auto& [id, c] : components_) comps.append(c.toJson());
  root["components"] = comps;
  root["activeComponent"] = idToJson(activeComponent_);

  QJsonArray bodies;
  for (ObjectId id : bodyOrder_)
    if (const Body* b = body(id)) bodies.append(b->toJson());
  root["bodies"] = bodies;

  QJsonArray feats;
  for (const auto& f : features_) feats.append(f->toJson());
  root["features"] = feats;
  root["rollbackIndex"] = rollback_;

  QJsonObject ext;
  for (const auto& [k, v] : extensions_) ext[qs(k)] = v;
  root["extensions"] = ext;
  return root;
}

bool Document::fromJson(const QJsonObject& root, std::string* error) {
  if (root["format"].toString() != "fabricad") {
    if (error) *error = "not a FabriCAD document";
    return false;
  }
  int version = root["version"].toInt(0);
  if (version > kFormatVersion) {
    if (error) *error = std::format("file format version {} is newer than supported ({})", version, kFormatVersion);
    return false;
  }
  std::string keepPath = filePath_;
  resetToEmpty();
  filePath_ = keepPath;
  name_ = ss(root["name"].toString("Untitled"));

  QJsonObject units = root["units"].toObject();
  if (auto lu = lengthUnitFromSymbol(ss(units["length"].toString("mm")))) units_.length = *lu;
  if (auto au = angleUnitFromSymbol(ss(units["angle"].toString("deg")))) units_.angle = *au;
  units_.precision = units["precision"].toInt(3);

  for (const auto& v : root["parameters"].toArray()) {
    QJsonObject o = v.toObject();
    Parameter np;
    np.name = ss(o["name"].toString());
    np.expression = ss(o["expression"].toString());
    np.comment = ss(o["comment"].toString());
    parameters_.push_back(std::move(np));
  }

  components_.clear();
  for (const auto& v : root["components"].toArray()) {
    Component c = Component::fromJson(v.toObject());
    if (c.id == kInvalidId) continue;
    components_[c.id] = c;
    nextId_ = std::max(nextId_, c.id + 1);
  }
  if (!components_.count(kRootComponentId)) {
    Component rootComp;
    rootComp.id = kRootComponentId;
    rootComp.name = name_;
    rootComp.grounded = true;
    components_[kRootComponentId] = rootComp;
  }
  activeComponent_ = idFromJson(root["activeComponent"], kRootComponentId);
  if (!components_.count(activeComponent_)) activeComponent_ = kRootComponentId;

  for (const auto& v : root["bodies"].toArray()) {
    Body b = Body::fromJson(v.toObject());
    if (b.id == kInvalidId) continue;
    nextId_ = std::max(nextId_, b.id + 1);
    bodies_[b.id] = b;
    bodyOrder_.push_back(b.id);
  }

  for (const auto& v : root["features"].toArray()) {
    QJsonObject o = v.toObject();
    std::string type = ss(o["type"].toString());
    std::unique_ptr<Feature> f = FeatureFactory::instance().create(type);
    if (!f) {
      FC_WARN("unknown feature type '{}', keeping placeholder", type);
      f = std::make_unique<PlaceholderFeature>(type);
    }
    f->fromJson(o);
    if (f->id_ == kInvalidId) f->id_ = allocateId();
    nextId_ = std::max(nextId_, f->id_ + 1);
    if (f->name_.empty()) f->name_ = uniqueFeatureName(f->type_);
    features_.push_back(std::move(f));
  }
  rollback_ = std::clamp(root["rollbackIndex"].toInt(static_cast<int>(features_.size())), 0,
                         static_cast<int>(features_.size()));
  nextId_ = std::max(nextId_, idFromJson(root["nextId"], nextId_));

  QJsonObject ext = root["extensions"].toObject();
  for (auto it = ext.begin(); it != ext.end(); ++it) extensions_[ss(it.key())] = it.value().toObject();

  firstDirty_ = 0;
  // Note: the undo stack is deliberately kept (DocumentSnapshotCommand restores through
  // fromJson while it is being executed by that very stack); load()/clear() reset it.
  emit documentReset();
  recompute();
  emit loaded();
  setModified(false);
  return true;
}

bool Document::save(const std::string& path, std::string* error) {
  emit aboutToSave();
  QJsonDocument doc(toJson());
  QSaveFile file(qs(path));
  if (!file.open(QIODevice::WriteOnly)) {
    if (error) *error = ss(file.errorString());
    return false;
  }
  file.write(doc.toJson(QJsonDocument::Indented));
  if (!file.commit()) {
    if (error) *error = ss(file.errorString());
    return false;
  }
  filePath_ = path;
  undo_->setClean();
  setModified(false);
  return true;
}

bool Document::load(const std::string& path, std::string* error) {
  QFile file(qs(path));
  if (!file.open(QIODevice::ReadOnly)) {
    if (error) *error = ss(file.errorString());
    return false;
  }
  QJsonParseError perr;
  QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &perr);
  if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
    if (error) *error = ss(perr.errorString());
    return false;
  }
  filePath_ = path;
  if (!fromJson(doc.object(), error)) return false;
  undo_->clear();
  return true;
}

// ---------------------------------------------------------------------------
// PlaceholderFeature
// ---------------------------------------------------------------------------

PlaceholderFeature::PlaceholderFeature(std::string originalType) : Feature("Placeholder"), originalType_(std::move(originalType)) {}

void PlaceholderFeature::recompute(RecomputeContext& ctx) {
  ctx.setError(std::format("unknown feature type '{}'", originalType_));
}

QJsonObject PlaceholderFeature::toJson() const {
  QJsonObject o = raw_;
  o["type"] = qs(originalType_);
  o["id"] = idToJson(id_);
  o["name"] = qs(name_);
  o["suppressed"] = suppressed_;
  return o;
}

void PlaceholderFeature::fromJson(const QJsonObject& o) {
  raw_ = o;
  Feature::fromJson(o);
}

}  // namespace fabricad::core
