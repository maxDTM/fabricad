// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/Feature.h"

#include "core/Document.h"
#include "core/GeometryRefs.h"
#include "core/Json.h"
#include "core/ShapeUtils.h"

#include <QJsonArray>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>

#include <algorithm>
#include <cctype>
#include <format>
#include <stdexcept>

namespace fabricad::core {

const char* featureStatusName(FeatureStatus s) {
  switch (s) {
    case FeatureStatus::Stale: return "Stale";
    case FeatureStatus::Ok: return "Ok";
    case FeatureStatus::Warning: return "Warning";
    case FeatureStatus::Error: return "Error";
  }
  return "Stale";
}

// ---------------------------------------------------------------------------
// Feature
// ---------------------------------------------------------------------------

Feature::Feature(std::string type) : type_(std::move(type)) {}
Feature::~Feature() = default;

std::string Feature::iconName() const {
  std::string s = type_;
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

Property* Feature::property(const std::string& name) {
  for (auto& p : properties_)
    if (p.name() == name) return &p;
  return nullptr;
}

const Property* Feature::property(const std::string& name) const {
  for (const auto& p : properties_)
    if (p.name() == name) return &p;
  return nullptr;
}

Property& Feature::prop(const std::string& name) {
  Property* p = property(name);
  if (!p) throw std::out_of_range(std::format("feature '{}' has no property '{}'", type_, name));
  return *p;
}

const Property& Feature::prop(const std::string& name) const {
  const Property* p = property(name);
  if (!p) throw std::out_of_range(std::format("feature '{}' has no property '{}'", type_, name));
  return *p;
}

Property& Feature::addProperty(Property p) {
  for (auto& existing : properties_) {
    if (existing.name() == p.name()) {
      existing = std::move(p);
      return existing;
    }
  }
  properties_.push_back(std::move(p));
  return properties_.back();
}

Property& Feature::addBool(const std::string& name, bool value) {
  Property p(name, PropertyType::Bool);
  p.setBool(value);
  return addProperty(std::move(p));
}
Property& Feature::addInt(const std::string& name, std::int64_t value) {
  Property p(name, PropertyType::Int);
  p.setInt(value);
  return addProperty(std::move(p));
}
Property& Feature::addDouble(const std::string& name, double value) {
  Property p(name, PropertyType::Double);
  p.setDouble(value);
  return addProperty(std::move(p));
}
Property& Feature::addLength(const std::string& name, double mm) {
  Property p(name, PropertyType::Length);
  p.setDouble(mm);
  return addProperty(std::move(p));
}
Property& Feature::addAngle(const std::string& name, double deg) {
  Property p(name, PropertyType::Angle);
  p.setDouble(deg);
  return addProperty(std::move(p));
}
Property& Feature::addString(const std::string& name, std::string value) {
  Property p(name, PropertyType::String);
  p.setString(std::move(value));
  return addProperty(std::move(p));
}
Property& Feature::addEnum(const std::string& name, std::vector<std::string> options, int index) {
  Property p(name, PropertyType::Enum);
  p.setOptions(std::move(options));
  p.setEnum(index);
  return addProperty(std::move(p));
}
Property& Feature::addVec3(const std::string& name, const Vec3& v) {
  Property p(name, PropertyType::Vec3);
  p.setVec3(v);
  return addProperty(std::move(p));
}
Property& Feature::addRef(const std::string& name, ShapeKindMask filter) {
  Property p(name, PropertyType::Ref);
  p.setRefFilter(filter);
  return addProperty(std::move(p));
}
Property& Feature::addRefs(const std::string& name, ShapeKindMask filter, int maxRefs) {
  Property p(name, PropertyType::RefList);
  p.setRefFilter(filter);
  p.setMaxRefs(maxRefs);
  return addProperty(std::move(p));
}

std::vector<ObjectId> Feature::dependencies() const {
  std::vector<ObjectId> out;
  auto push = [&](ObjectId id) {
    if (id == kInvalidId || id == id_ || id == kOriginId) return;
    if (std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
  };
  for (const auto& p : properties_)
    for (ObjectId id : p.referencedOwners()) push(id);
  for (ObjectId id : extraDependencies()) push(id);
  return out;
}

QJsonObject Feature::toJson() const {
  QJsonObject o;
  o["id"] = idToJson(id_);
  o["type"] = qs(type_);
  o["name"] = qs(name_);
  if (suppressed_) o["suppressed"] = true;
  if (component_ != kInvalidId) o["component"] = idToJson(component_);
  QJsonObject props;
  for (const auto& p : properties_) props[qs(p.name())] = p.toJson();
  o["properties"] = props;
  if (!createdBodies_.empty()) o["createdBodies"] = core::toJson(createdBodies_);
  return o;
}

void Feature::fromJson(const QJsonObject& o) {
  if (o.contains("id")) id_ = idFromJson(o["id"], id_);
  if (o.contains("name")) name_ = ss(o["name"].toString());
  suppressed_ = o["suppressed"].toBool(false);
  if (o.contains("component")) component_ = idFromJson(o["component"], component_);
  QJsonObject props = o["properties"].toObject();
  for (auto& p : properties_) {
    QString key = qs(p.name());
    if (props.contains(key)) p.fromJson(props[key]);
  }
  if (o.contains("createdBodies")) createdBodies_ = idsFromJson(o["createdBodies"]);
}

// ---------------------------------------------------------------------------
// RecomputeContext
// ---------------------------------------------------------------------------

RecomputeContext::RecomputeContext(Document& doc, Feature& self, std::map<ObjectId, TopoDS_Shape> stateBefore)
    : doc_(doc), self_(self), state_(std::move(stateBefore)) {}

const TopoDS_Shape& RecomputeContext::bodyShape(ObjectId bodyId) const {
  static const TopoDS_Shape kNull;
  auto it = state_.find(bodyId);
  return it == state_.end() ? kNull : it->second;
}

void RecomputeContext::setBodyShape(ObjectId bodyId, const TopoDS_Shape& shape) {
  if (!doc_.body(bodyId)) {
    setWarning(std::format("setBodyShape: unknown body {}", bodyId));
    return;
  }
  state_[bodyId] = shape;
  if (std::find(touched_.begin(), touched_.end(), bodyId) == touched_.end()) touched_.push_back(bodyId);
}

ObjectId RecomputeContext::createBody(const std::string& name, const TopoDS_Shape& shape, ObjectId component) {
  ObjectId id = kInvalidId;
  // Default owner: the component the feature was created in (stable across later
  // activations), falling back to the live active component for features without one.
  if (component == kInvalidId && self_.component_ != kInvalidId && doc_.component(self_.component_))
    component = self_.component_;
  if (createdSlot_ < self_.createdBodies_.size()) {
    id = self_.createdBodies_[createdSlot_];
    if (!doc_.body(id)) {
      // record vanished (e.g. removed manually): recreate with the same id
      Body b;
      b.id = id;
      b.name = name.empty() ? doc_.uniqueBodyName() : name;
      b.component = component != kInvalidId ? component : doc_.activeComponent();
      b.creator = self_.id();
      doc_.addBody(std::move(b));
    }
  } else {
    Body b;
    b.name = name.empty() ? doc_.uniqueBodyName() : name;
    b.component = component != kInvalidId ? component : doc_.activeComponent();
    b.creator = self_.id();
    id = doc_.addBody(std::move(b)).id;
    self_.createdBodies_.push_back(id);
  }
  ++createdSlot_;
  state_[id] = shape;
  if (std::find(touched_.begin(), touched_.end(), id) == touched_.end()) touched_.push_back(id);
  return id;
}

std::vector<ObjectId> RecomputeContext::bodies() const {
  std::vector<ObjectId> out;
  for (const Body* b : doc_.bodies()) {
    auto it = state_.find(b->id);
    if (it != state_.end() && !it->second.IsNull()) out.push_back(b->id);
  }
  return out;
}

std::vector<ObjectId> RecomputeContext::bodiesInComponent(ObjectId componentId) const {
  std::vector<ObjectId> out;
  for (ObjectId id : bodies())
    if (const Body* b = doc_.body(id); b && b->component == componentId) out.push_back(id);
  return out;
}

const Feature* RecomputeContext::feature(ObjectId id) const {
  const Feature* f = doc_.feature(id);
  if (!f) return nullptr;
  int mine = doc_.indexOf(self_.id());
  int theirs = doc_.indexOf(id);
  if (mine >= 0 && theirs >= mine) return nullptr;  // not upstream
  return f;
}

const IProfileSource* RecomputeContext::profileSource(const ShapeRef& ref) const {
  const Feature* f = feature(ref.owner);
  return f ? f->profileSource() : nullptr;
}

double RecomputeContext::eval(Property& p) {
  if (!p.isNumeric()) return p.toDouble();
  if (!p.hasExpression()) return p.toDouble();
  EvalResult r = doc_.evaluate(p.expression(), p.dimension());
  if (!r.ok) {
    setError(std::format("{}: {}", p.label(), r.error));
    return p.toDouble();
  }
  p.setEvaluatedValue(r.value.value);
  return r.value.value;
}

EvalResult RecomputeContext::evaluate(const std::string& expr, std::optional<Dimension> expected) const {
  return doc_.evaluate(expr, expected);
}

TopoDS_Shape RecomputeContext::resolve(ShapeRef& ref) {
  if (ref.isBrep()) {
    // Layer 1 (history names): a named sub-shape of the body is looked up in the name map of
    // the last upstream feature that touched the body; exact when found in the current shape.
    if (ref.kind != ShapeKind::Body && !ref.name.empty()) {
      TopoDS_Shape named = doc_.namedSubShape(ref.owner, ref.name, doc_.indexOf(self_.id()));
      const TopoDS_Shape& owner = bodyShape(ref.owner);
      if (!named.IsNull() && !owner.IsNull()) {
        TopAbs_ShapeEnum want = ref.kind == ShapeKind::Face ? TopAbs_FACE : ref.kind == ShapeKind::Edge ? TopAbs_EDGE : TopAbs_VERTEX;
        if (named.ShapeType() == want) {
          TopTools_IndexedMapOfShape map;
          TopExp::MapShapes(owner, want, map);
          int idx = map.Contains(named) ? map.FindIndex(named) : 0;
          if (idx > 0) {
            ref.index = idx;
            ref.signature = computeSignature(named);
            return named;
          }
        }
      }
    }
    ResolveResult rr;
    TopoDS_Shape s = resolveShape(doc_, ref, &state_, &rr);
    if (s.IsNull()) {
      setWarning(std::format("reference lost: {}", ref.toString()));
      return s;
    }
    if (ref.kind != ShapeKind::Body) {
      if (!rr.exact) setWarning(std::format("reference re-matched by geometry: {}", ref.toString()));
      if (rr.ambiguous) setWarning(std::format("ambiguous reference: {}", ref.toString()));
      ref.index = rr.index;
      // Follow the geometry: the stored fingerprint tracks the shape it matched so the next
      // edit is measured from here, not from the original pick (otherwise two successive edits
      // of a face drift past the acceptance threshold although each one is small). Learn the
      // history name too, so layer 1 takes over from now on.
      if (!rr.exact) ref.signature = computeSignature(s);
      if (ref.name.empty()) ref.name = doc_.subShapeName(ref.owner, s, doc_.indexOf(self_.id()));
    }
    return s;
  }
  TopoDS_Shape s = resolveShape(doc_, ref, &state_);
  if (s.IsNull()) setWarning(std::format("reference not found: {}", ref.toString()));
  return s;
}

std::optional<gp_Ax3> RecomputeContext::plane(const ShapeRef& ref) const { return resolvePlane(doc_, ref, &state_); }
std::optional<gp_Ax1> RecomputeContext::axis(const ShapeRef& ref) const { return resolveAxis(doc_, ref, &state_); }
std::optional<gp_Pnt> RecomputeContext::point(const ShapeRef& ref) const { return resolvePoint(doc_, ref, &state_); }
TopoDS_Face RecomputeContext::profileFace(const ShapeRef& ref) const { return resolveProfileFace(doc_, ref, &state_); }

void RecomputeContext::setWarning(const std::string& msg) { warnings_.push_back(msg); }

void RecomputeContext::setError(const std::string& msg) {
  if (!error_) errorMessage_ = msg;
  error_ = true;
}

double RecomputeContext::modelSize() const {
  double d = 0;
  for (const auto& [id, shape] : state_) d = std::max(d, bboxDiagonal(shape));
  return std::max(1.0, d);
}

bool RecomputeContext::isCancelled() const { return doc_.cancelRequested_; }

}  // namespace fabricad::core
