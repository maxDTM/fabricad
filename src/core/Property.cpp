// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/Property.h"

#include "core/Json.h"

#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <array>

namespace fabricad::core {

namespace {
constexpr std::array<std::pair<PropertyType, const char*>, 11> kTypeNames{{
    {PropertyType::Bool, "Bool"},
    {PropertyType::Int, "Int"},
    {PropertyType::Double, "Double"},
    {PropertyType::Length, "Length"},
    {PropertyType::Angle, "Angle"},
    {PropertyType::String, "String"},
    {PropertyType::Enum, "Enum"},
    {PropertyType::Vec3, "Vec3"},
    {PropertyType::Ref, "Ref"},
    {PropertyType::RefList, "RefList"},
    {PropertyType::Json, "Json"},
}};
const std::string kEmpty;
const ShapeRef kNoRef;
const std::vector<ShapeRef> kNoRefs;
const QJsonValue kNoJson;
}  // namespace

const char* propertyTypeName(PropertyType t) {
  for (const auto& [type, name] : kTypeNames)
    if (type == t) return name;
  return "Double";
}

PropertyType propertyTypeFromName(const std::string& s) {
  for (const auto& [type, name] : kTypeNames)
    if (s == name) return type;
  return PropertyType::Double;
}

Property::Property(std::string name, PropertyType type) : name_(std::move(name)), type_(type) {}

Dimension Property::dimension() const {
  switch (type_) {
    case PropertyType::Length: return Dimension::Length();
    case PropertyType::Angle: return Dimension::Angle();
    default: return Dimension::Scalar();
  }
}

double Property::toDouble() const {
  switch (type_) {
    case PropertyType::Bool: return bool_ ? 1.0 : 0.0;
    case PropertyType::Int: return static_cast<double>(int_);
    case PropertyType::Enum: return static_cast<double>(int_);
    default: return number_;
  }
}

Property& Property::setDouble(double v) {
  if (type_ == PropertyType::Int || type_ == PropertyType::Enum) int_ = static_cast<std::int64_t>(v);
  else if (type_ == PropertyType::Bool) bool_ = v != 0.0;
  else number_ = v;
  expression_.clear();
  set_ = true;
  return *this;
}

Property& Property::setExpression(std::string expr) {
  expression_ = std::move(expr);
  set_ = true;
  return *this;
}

std::string Property::displayText(const UnitSystem& units) const {
  if (hasExpression()) return expression_;
  if (isNumeric()) return formatQuantity(toQuantity(), units);
  switch (type_) {
    case PropertyType::Bool: return bool_ ? "true" : "false";
    case PropertyType::Int: return std::to_string(int_);
    case PropertyType::String: return string_;
    case PropertyType::Enum: return enumName();
    case PropertyType::Vec3: return vec_.toString();
    case PropertyType::Ref: return ref_.toString();
    case PropertyType::RefList: return std::to_string(refs_.size()) + " selected";
    case PropertyType::Json: return jsonToString(json_);
    default: return {};
  }
}

bool Property::toBool() const { return type_ == PropertyType::Bool ? bool_ : toDouble() != 0.0; }
Property& Property::setBool(bool v) { bool_ = v; set_ = true; return *this; }

std::int64_t Property::toInt() const {
  if (type_ == PropertyType::Int || type_ == PropertyType::Enum) return int_;
  return static_cast<std::int64_t>(toDouble());
}
Property& Property::setInt(std::int64_t v) { int_ = v; set_ = true; return *this; }

const std::string& Property::toString() const { return string_; }
Property& Property::setString(std::string s) { string_ = std::move(s); set_ = true; return *this; }

Vec3 Property::toVec3() const { return vec_; }
Property& Property::setVec3(const Vec3& v) { vec_ = v; set_ = true; return *this; }

int Property::enumIndex() const { return static_cast<int>(int_); }

const std::string& Property::enumName() const {
  if (int_ >= 0 && static_cast<size_t>(int_) < options_.size()) return options_[static_cast<size_t>(int_)];
  return kEmpty;
}

Property& Property::setEnum(int index) {
  int_ = index;
  set_ = true;
  return *this;
}

Property& Property::setEnum(const std::string& optionName) {
  auto it = std::find(options_.begin(), options_.end(), optionName);
  if (it != options_.end()) int_ = static_cast<std::int64_t>(it - options_.begin());
  set_ = true;
  return *this;
}

Property& Property::setOptions(std::vector<std::string> opts) {
  options_ = std::move(opts);
  return *this;
}

const ShapeRef& Property::ref() const { return type_ == PropertyType::Ref ? ref_ : kNoRef; }
Property& Property::setRef(const ShapeRef& r) { ref_ = r; set_ = true; return *this; }

const std::vector<ShapeRef>& Property::refs() const { return refs_; }
std::vector<ShapeRef>& Property::refs() { set_ = true; return refs_; }
Property& Property::setRefs(std::vector<ShapeRef> refs) { refs_ = std::move(refs); set_ = true; return *this; }

Property& Property::addRef(const ShapeRef& r) {
  if (std::find(refs_.begin(), refs_.end(), r) == refs_.end()) refs_.push_back(r);
  set_ = true;
  return *this;
}

bool Property::removeRef(const ShapeRef& r) {
  auto it = std::find(refs_.begin(), refs_.end(), r);
  if (it == refs_.end()) return false;
  refs_.erase(it);
  return true;
}

const QJsonValue& Property::json() const { return json_; }
Property& Property::setJson(const QJsonValue& v) { json_ = v; set_ = true; return *this; }

std::vector<ObjectId> Property::referencedOwners() const {
  std::vector<ObjectId> out;
  if (type_ == PropertyType::Ref) {
    if (ref_.owner != kInvalidId) out.push_back(ref_.owner);
  } else if (type_ == PropertyType::RefList) {
    for (const auto& r : refs_)
      if (r.owner != kInvalidId && std::find(out.begin(), out.end(), r.owner) == out.end()) out.push_back(r.owner);
  }
  return out;
}

QJsonValue Property::toJson() const {
  QJsonObject o;
  o["type"] = propertyTypeName(type_);
  switch (type_) {
    case PropertyType::Bool: o["value"] = bool_; break;
    case PropertyType::Int:
    case PropertyType::Enum: o["value"] = static_cast<double>(int_); break;
    case PropertyType::Double:
    case PropertyType::Length:
    case PropertyType::Angle:
      o["value"] = number_;
      if (!expression_.empty()) o["expr"] = qs(expression_);
      break;
    case PropertyType::String: o["value"] = qs(string_); break;
    case PropertyType::Vec3: o["value"] = core::toJson(vec_); break;
    case PropertyType::Ref: o["value"] = ref_.toJson(); break;
    case PropertyType::RefList: {
      QJsonArray a;
      for (const auto& r : refs_) a.append(r.toJson());
      o["value"] = a;
      break;
    }
    case PropertyType::Json: o["value"] = json_; break;
  }
  if (type_ == PropertyType::Enum && int_ >= 0 && static_cast<size_t>(int_) < options_.size())
    o["name"] = qs(options_[static_cast<size_t>(int_)]);
  return o;
}

void Property::fromJson(const QJsonValue& v) {
  if (!v.isObject()) return;
  QJsonObject o = v.toObject();
  QJsonValue val = o["value"];
  set_ = true;
  switch (type_) {
    case PropertyType::Bool: bool_ = val.toBool(bool_); break;
    case PropertyType::Int: int_ = static_cast<std::int64_t>(val.toDouble(static_cast<double>(int_))); break;
    case PropertyType::Enum: {
      // prefer the option name (robust against reordering)
      if (o.contains("name")) {
        std::string n = ss(o["name"].toString());
        auto it = std::find(options_.begin(), options_.end(), n);
        if (it != options_.end()) {
          int_ = static_cast<std::int64_t>(it - options_.begin());
          break;
        }
      }
      int_ = static_cast<std::int64_t>(val.toDouble(static_cast<double>(int_)));
      break;
    }
    case PropertyType::Double:
    case PropertyType::Length:
    case PropertyType::Angle:
      number_ = val.toDouble(number_);
      expression_ = ss(o["expr"].toString());
      break;
    case PropertyType::String: string_ = ss(val.toString(qs(string_))); break;
    case PropertyType::Vec3: vec_ = vec3FromJson(val, vec_); break;
    case PropertyType::Ref: ref_ = ShapeRef::fromJson(val.toObject()); break;
    case PropertyType::RefList: {
      refs_.clear();
      for (const auto& r : val.toArray()) refs_.push_back(ShapeRef::fromJson(r.toObject()));
      break;
    }
    case PropertyType::Json: json_ = val; break;
  }
}

bool Property::valueEquals(const Property& o) const {
  if (type_ != o.type_) return false;
  switch (type_) {
    case PropertyType::Bool: return bool_ == o.bool_;
    case PropertyType::Int:
    case PropertyType::Enum: return int_ == o.int_;
    case PropertyType::Double:
    case PropertyType::Length:
    case PropertyType::Angle: return number_ == o.number_ && expression_ == o.expression_;
    case PropertyType::String: return string_ == o.string_;
    case PropertyType::Vec3: return vec_ == o.vec_;
    case PropertyType::Ref: return ref_ == o.ref_;
    case PropertyType::RefList: return refs_ == o.refs_;
    case PropertyType::Json: return json_ == o.json_;
  }
  return false;
}

}  // namespace fabricad::core
