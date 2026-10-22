// FabriCAD core: typed, serialisable feature parameters.
//
// A Property is a named, typed value. Length/Angle/Double properties may carry an
// expression ("width/2 + 3 mm", "d1*2"); the Document evaluates it on recompute and
// stores the result in the numeric value (RecomputeContext::eval). Ref/RefList hold
// ShapeRefs (feature inputs picked by the user). Enum holds an index into a fixed
// option list. Json is the escape hatch for module-specific structured data.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/ShapeRef.h"
#include "core/Types.h"
#include "core/Units.h"

#include <QJsonValue>

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace fabricad::core {

enum class PropertyType {
  Bool,
  Int,
  Double,   ///< dimensionless number (may have an expression)
  Length,   ///< mm (may have an expression)
  Angle,    ///< degrees (may have an expression)
  String,
  Enum,     ///< int index into options()
  Vec3,
  Ref,      ///< one ShapeRef
  RefList,  ///< several ShapeRefs
  Json,     ///< arbitrary QJsonValue
};

const char* propertyTypeName(PropertyType t);
PropertyType propertyTypeFromName(const std::string& s);

/// Hints for auto-generated command dialogs.
struct PropertyFlags {
  bool hidden = false;      ///< not shown in dialogs
  bool readOnly = false;    ///< shown but not editable (outputs, measured values)
  bool output = false;      ///< produced by recompute, not an input (not part of inputHash)
  bool allowNegative = true;
  /// Allowed range for Int properties (UI spinbox limits; compute validates too).
  /// Not serialised: set by the feature constructor.
  int intMin = -1000000;
  int intMax = 1000000;
  bool operator==(const PropertyFlags&) const = default;
};

class Property {
 public:
  Property() = default;
  Property(std::string name, PropertyType type);

  const std::string& name() const { return name_; }
  PropertyType type() const { return type_; }
  /// UI label (defaults to name).
  const std::string& label() const { return label_.empty() ? name_ : label_; }
  Property& setLabel(std::string l) { label_ = std::move(l); return *this; }
  const std::string& tooltip() const { return tooltip_; }
  Property& setTooltip(std::string t) { tooltip_ = std::move(t); return *this; }
  PropertyFlags& flags() { return flags_; }
  const PropertyFlags& flags() const { return flags_; }
  /// Allowed kinds for Ref/RefList (selection filter for the dialog's picker).
  ShapeKindMask refFilter() const { return refFilter_; }
  Property& setRefFilter(ShapeKindMask m) { refFilter_ = m; return *this; }
  /// Max number of refs for RefList (0 = unlimited).
  int maxRefs() const { return maxRefs_; }
  Property& setMaxRefs(int n) { maxRefs_ = n; return *this; }

  // --- numeric -------------------------------------------------------------
  bool isNumeric() const { return type_ == PropertyType::Double || type_ == PropertyType::Length || type_ == PropertyType::Angle; }
  Dimension dimension() const;
  /// Evaluated value (mm / deg / plain). For Int returns the int; Bool 0/1.
  double toDouble() const;
  Quantity toQuantity() const { return {toDouble(), dimension()}; }
  /// Sets the numeric value and clears any expression.
  Property& setDouble(double v);
  /// Expression source; empty if the value was set numerically.
  const std::string& expression() const { return expression_; }
  bool hasExpression() const { return !expression_.empty(); }
  /// Store an expression. The numeric value stays until setEvaluatedValue() is called
  /// (Document does this on recompute; dialogs do it live for preview).
  Property& setExpression(std::string expr);
  /// Update the cached numeric value without touching the expression.
  void setEvaluatedValue(double v) { number_ = v; }
  /// Display text: expression if present, else formatted value.
  std::string displayText(const UnitSystem& units) const;

  // --- other types ---------------------------------------------------------
  bool toBool() const;
  Property& setBool(bool v);
  std::int64_t toInt() const;
  Property& setInt(std::int64_t v);
  const std::string& toString() const;
  Property& setString(std::string s);
  Vec3 toVec3() const;
  Property& setVec3(const Vec3& v);
  /// Enum: option index (0-based) and option name.
  int enumIndex() const;
  const std::string& enumName() const;
  Property& setEnum(int index);
  Property& setEnum(const std::string& optionName);
  const std::vector<std::string>& options() const { return options_; }
  Property& setOptions(std::vector<std::string> opts);
  /// Ref: single ShapeRef (invalid ref when unset).
  const ShapeRef& ref() const;
  Property& setRef(const ShapeRef& r);
  /// RefList.
  const std::vector<ShapeRef>& refs() const;
  std::vector<ShapeRef>& refs();
  Property& setRefs(std::vector<ShapeRef> refs);
  Property& addRef(const ShapeRef& r);
  bool removeRef(const ShapeRef& r);
  /// Json.
  const QJsonValue& json() const;
  Property& setJson(const QJsonValue& v);

  /// Owner ids referenced by Ref/RefList (for dependency tracking).
  std::vector<ObjectId> referencedOwners() const;

  bool isSet() const { return set_; }  ///< false until a value was assigned

  /// Full serialisation: {"type":..., "value":..., "expr":...}.
  QJsonValue toJson() const;
  /// Reads value/expression. Type/label/options come from the feature's constructor, so
  /// only the value is restored (type mismatch is tolerated: value kept as default).
  void fromJson(const QJsonValue& v);

  /// Value equality (type, value, expression). Labels/flags excluded.
  bool valueEquals(const Property& o) const;

 private:
  std::string name_;
  std::string label_;
  std::string tooltip_;
  PropertyType type_ = PropertyType::Double;
  PropertyFlags flags_;
  ShapeKindMask refFilter_ = kAllKinds;
  int maxRefs_ = 0;
  std::vector<std::string> options_;
  bool set_ = false;

  // storage (one is live depending on type_)
  bool bool_ = false;
  std::int64_t int_ = 0;
  double number_ = 0.0;
  std::string expression_;
  std::string string_;
  Vec3 vec_;
  ShapeRef ref_;
  std::vector<ShapeRef> refs_;
  QJsonValue json_;
};

}  // namespace fabricad::core
