// FabriCAD core: Feature base class and the recompute context.
//
// A Feature is one entry of the parametric timeline. It owns typed Properties
// (its inputs), and recompute() turns them into geometry by reading and writing
// body shapes through the RecomputeContext. Features never touch the Document's
// body shapes directly; the context gives them the state "as of the previous
// feature" and records what they produce.
//
// Sub-classing checklist (see docs/ARCHITECTURE.md "Writing a feature"):
//   class Extrude : public core::Feature {
//     Extrude() : Feature("Extrude") {
//       addRefs("profiles", ShapeKind::Profile | ShapeKind::Face).setLabel("Profiles");
//       addLength("distance", 10.0).setLabel("Distance");
//       addEnum("operation", {"Join","Cut","Intersect","New Body"}, 0);
//     }
//     void recompute(core::RecomputeContext& ctx) override { ... }
//   };
//   Register: FeatureFactory::instance().registerType("Extrude", []{ return std::make_unique<Extrude>(); });
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Expression.h"
#include "core/Property.h"
#include "core/ShapeRef.h"
#include "core/Types.h"
#include "core/Units.h"

#include <QJsonObject>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fabricad::core {

class Document;
class RecomputeContext;

enum class FeatureStatus {
  Stale,    ///< not computed yet (new, or after the rollback marker)
  Ok,
  Warning,  ///< computed, but something is off (lost reference re-matched, upstream error, ...)
  Error,    ///< failed; its effect is skipped and the previous body state is kept
};
const char* featureStatusName(FeatureStatus s);

/// A closed region of a sketch that can be extruded/revolved.
struct ProfileInfo {
  ShapeRef ref;                ///< ShapeKind::Profile ref (owner = sketch, point = seed, name = curve ids)
  double area = 0;
  std::vector<int> curveIds;   ///< sketch geometry ids bounding the region
};

/// Implemented by sketch-like features so Part/Drawing can consume their geometry
/// without depending on the sketch module's concrete classes.
class IProfileSource {
 public:
  virtual ~IProfileSource() = default;
  /// Sketch plane in world coordinates (after the last recompute).
  virtual gp_Ax3 sketchPlane() const = 0;
  /// All closed regions of the current geometry (non-construction curves).
  virtual std::vector<ProfileInfo> profiles() const = 0;
  /// Face (in world coordinates, on sketchPlane) for a Profile ref. The ref is
  /// matched by seed point first, then by curve-id overlap. Null face if not found.
  virtual TopoDS_Face profileFace(const ShapeRef& profileRef) const = 0;
  /// 3D edge for one sketch curve (null if the id is unknown). Construction curves included.
  virtual TopoDS_Shape curveEdge(int curveId) const = 0;
  /// Compound of all curve edges (optionally including construction geometry).
  virtual TopoDS_Shape allEdges(bool includeConstruction) const = 0;
  /// World position of a sketch point ref (SketchPoint kind). nullopt if unknown.
  virtual std::optional<gp_Pnt> sketchPoint(const ShapeRef& pointRef) const = 0;
};

class Feature {
 public:
  explicit Feature(std::string type);
  virtual ~Feature();
  Feature(const Feature&) = delete;
  Feature& operator=(const Feature&) = delete;

  // --- identity ------------------------------------------------------------
  ObjectId id() const { return id_; }
  const std::string& type() const { return type_; }   ///< registered type name, e.g. "Extrude"
  const std::string& name() const { return name_; }   ///< user-visible, e.g. "Extrude1"
  void setName(std::string n) { name_ = std::move(n); }
  bool isSuppressed() const { return suppressed_; }
  void setSuppressed(bool s) { suppressed_ = s; }
  /// Icon resource name for timeline/browser (default: type name, lower-case).
  virtual std::string iconName() const;
  /// Whether the feature shows in the timeline (construction/joints: yes; hidden helpers: no).
  virtual bool isTimelineVisible() const { return true; }
  /// Browser folder: "Bodies", "Sketches", "Construction", "Joints", or "" (timeline only).
  virtual std::string browserFolder() const { return {}; }
  /// Tool id that edits this feature ("" = the shell dispatches by convention / generic editor).
  virtual std::string editToolId() const { return {}; }
  /// Re-derive PropertyFlags::hidden/readOnly from the current values (after an Enum/Bool
  /// changed). Dialogs call this before rebuilding their rows.
  virtual void updatePropertyFlags() {}
  /// Component this feature belongs to. Document::addFeature captures the active component;
  /// persisted as "component". Bodies created by the feature default to it (kInvalidId = unknown,
  /// then the live active component is used).
  ObjectId component() const { return component_; }
  void setComponent(ObjectId c) { component_ = c; }

  // --- status (set by Document during recompute) ----------------------------
  FeatureStatus status() const { return status_; }
  const std::string& statusMessage() const { return statusMessage_; }

  // --- properties ----------------------------------------------------------
  std::vector<Property>& properties() { return properties_; }
  const std::vector<Property>& properties() const { return properties_; }
  Property* property(const std::string& name);
  const Property* property(const std::string& name) const;
  bool hasProperty(const std::string& name) const { return property(name) != nullptr; }
  /// Returns the property or throws std::out_of_range (programming error).
  Property& prop(const std::string& name);
  const Property& prop(const std::string& name) const;
  Property& addProperty(Property p);  ///< replaces an existing property of the same name
  // typed helpers (constructor use)
  Property& addBool(const std::string& name, bool value);
  Property& addInt(const std::string& name, std::int64_t value);
  Property& addDouble(const std::string& name, double value);
  Property& addLength(const std::string& name, double mm);
  Property& addAngle(const std::string& name, double deg);
  Property& addString(const std::string& name, std::string value);
  Property& addEnum(const std::string& name, std::vector<std::string> options, int index = 0);
  Property& addVec3(const std::string& name, const Vec3& v);
  Property& addRef(const std::string& name, ShapeKindMask filter = kAllKinds);
  Property& addRefs(const std::string& name, ShapeKindMask filter = kAllKinds, int maxRefs = 0);
  // typed getters (recompute use): values already evaluated by the Document
  double length(const std::string& name) const { return prop(name).toDouble(); }
  double angle(const std::string& name) const { return prop(name).toDouble(); }
  double number(const std::string& name) const { return prop(name).toDouble(); }
  bool flag(const std::string& name) const { return prop(name).toBool(); }
  int enumIndex(const std::string& name) const { return prop(name).enumIndex(); }
  const std::string& enumName(const std::string& name) const { return prop(name).enumName(); }
  const ShapeRef& ref(const std::string& name) const { return prop(name).ref(); }
  const std::vector<ShapeRef>& refs(const std::string& name) const { return prop(name).refs(); }

  // --- recompute -----------------------------------------------------------
  /// Rebuild outputs from properties. Read inputs via ctx (bodyShape, resolve, eval, ...),
  /// write results via ctx.setBodyShape / ctx.createBody / setOutputShape. Throwing
  /// (Standard_Failure or std::exception) marks the feature Error.
  virtual void recompute(RecomputeContext& ctx) = 0;
  /// Objects this feature reads: owners of all Ref/RefList properties plus
  /// extraDependencies(). Body ids are mapped to "all earlier features touching that body".
  std::vector<ObjectId> dependencies() const;
  virtual std::vector<ObjectId> extraDependencies() const { return {}; }
  /// Bodies this feature modified or created in its last recompute (Document fills it).
  const std::vector<ObjectId>& touchedBodies() const { return touchedBodies_; }
  /// Bodies this feature created (slot order, persisted so ids survive recompute/reload).
  const std::vector<ObjectId>& createdBodies() const { return createdBodies_; }

  // --- cheap removal / re-homing (Document::takeFeature, assembly DeleteComponent) ---
  // A feature that changed no body shape can be taken out of the timeline without replaying the
  // features after it, unless one of them depends on it or it moved components that later
  // features solve from. These hooks say which; the defaults are the conservative answers.
  /// Whether the last recompute changed component placements (joints, rigid groups, Move
  /// Component positions, component patterns). Default: true.
  virtual bool affectsPlacements() const { return true; }
  /// Whether the result depends on component placements. Default: true. Body features answer
  /// false: bodies live in component-local coordinates (see assembly::recomputeJoints).
  virtual bool readsPlacements() const { return true; }
  /// How the last recompute used the feature's component(): automatic booleans test their tool
  /// against the bodies of the component tree (`scopeProbes`, the tools); any other use of the
  /// component marks `scopeOpaque`. Document::recompute clears both before each run.
  const std::vector<TopoDS_Shape>& scopeProbes() const { return scopeProbes_; }
  bool scopeOpaque() const { return scopeOpaque_; }
  void noteScopeProbe(const TopoDS_Shape& tool) { scopeProbes_.push_back(tool); }
  void noteScopeOpaque() { scopeOpaque_ = true; }
  void clearScopeUse() {
    scopeProbes_.clear();
    scopeOpaque_ = false;
  }

  // --- optional geometric roles (construction geometry, sketches) ------------
  virtual std::optional<gp_Ax3> asPlane() const { return std::nullopt; }
  virtual std::optional<gp_Ax1> asAxis() const { return std::nullopt; }
  virtual std::optional<gp_Pnt> asPoint() const { return std::nullopt; }
  virtual const IProfileSource* profileSource() const { return nullptr; }
  /// Feature-specific output (sketch compound, construction plane face, tool solid...).
  /// Displayed by the viewer when non-null; used by Pattern/Mirror for the "tool" of a feature.
  const TopoDS_Shape& outputShape() const { return outputShape_; }
  void setOutputShape(const TopoDS_Shape& s) { outputShape_ = s; }
  /// Named sub-shapes produced by this feature (history names like "End", "Side/3").
  /// Part features fill this so later refs can carry stable names.
  const std::map<std::string, TopoDS_Shape>& namedShapes() const { return namedShapes_; }
  std::map<std::string, TopoDS_Shape>& namedShapes() { return namedShapes_; }

  // --- serialisation -------------------------------------------------------
  /// Base writes id/type/name/suppressed/properties/createdBodies. Override to add
  /// extra keys; call the base first.
  virtual QJsonObject toJson() const;
  /// Base reads the keys above (type must already match). Override to read extras; call base first.
  virtual void fromJson(const QJsonObject& o);
  /// Snapshot of inputs for undo (properties + name + suppressed; not outputs).
  QJsonObject inputsToJson() const { return toJson(); }

 protected:
  friend class Document;
  friend class RecomputeContext;
  void setStatus(FeatureStatus s, std::string msg = {}) { status_ = s; statusMessage_ = std::move(msg); }

  ObjectId id_ = kInvalidId;
  std::string type_;
  std::string name_;
  ObjectId component_ = kInvalidId;
  bool suppressed_ = false;
  FeatureStatus status_ = FeatureStatus::Stale;
  std::string statusMessage_;
  std::vector<Property> properties_;
  std::vector<ObjectId> createdBodies_;
  std::vector<ObjectId> touchedBodies_;
  TopoDS_Shape outputShape_;
  std::map<std::string, TopoDS_Shape> namedShapes_;
  /// Body shapes after this feature (filled by Document; used for rollback/edit).
  std::map<ObjectId, TopoDS_Shape> bodyStatesAfter_;
  std::vector<TopoDS_Shape> scopeProbes_;
  bool scopeOpaque_ = false;
};

/// Everything a Feature::recompute() may touch.
class RecomputeContext {
 public:
  RecomputeContext(Document& doc, Feature& self, std::map<ObjectId, TopoDS_Shape> stateBefore);

  Document& document() { return doc_; }
  const Document& document() const { return doc_; }
  Feature& self() { return self_; }

  // --- bodies ---------------------------------------------------------------
  /// Shape of a body as of the previous feature (null if the body does not exist yet).
  const TopoDS_Shape& bodyShape(ObjectId bodyId) const;
  /// Replace a body's shape (the body must exist). Records the body as touched.
  void setBodyShape(ObjectId bodyId, const TopoDS_Shape& shape);
  /// Create (or re-use on replay) a body owned by this feature. Slots are matched in
  /// call order, so a feature creating N bodies must call this N times every recompute.
  /// `component` defaults to the document's active component.
  ObjectId createBody(const std::string& name, const TopoDS_Shape& shape, ObjectId component = kInvalidId);
  /// Bodies that currently have a non-null shape, in creation order.
  std::vector<ObjectId> bodies() const;
  /// Bodies of a component (non-null shapes only).
  std::vector<ObjectId> bodiesInComponent(ObjectId componentId) const;
  /// Full state after this feature (Document reads it back).
  const std::map<ObjectId, TopoDS_Shape>& state() const { return state_; }

  // --- upstream features -----------------------------------------------------
  /// An earlier feature (nullptr if unknown or later in the timeline than self).
  const Feature* feature(ObjectId id) const;
  template <class T>
  const T* featureAs(ObjectId id) const { return dynamic_cast<const T*>(feature(id)); }
  /// Profile source for a Profile/Sketch/SketchCurve ref (nullptr if none).
  const IProfileSource* profileSource(const ShapeRef& ref) const;

  // --- values -----------------------------------------------------------------
  /// Evaluate a numeric property's expression (if any) in the document's parameter
  /// scope, store the value in the property and return it (mm / deg / plain).
  double eval(Property& p);
  double length(const std::string& propName) { return eval(self_.prop(propName)); }
  double angle(const std::string& propName) { return eval(self_.prop(propName)); }
  double number(const std::string& propName) { return eval(self_.prop(propName)); }
  /// Evaluate a free-standing expression.
  EvalResult evaluate(const std::string& expr, std::optional<Dimension> expected = std::nullopt) const;

  // --- references -------------------------------------------------------------
  /// Resolve a B-rep ref against the current state. Updates ref.index on success and
  /// adds a Warning if the match was by signature only. Returns null shape on failure
  /// (and records a Warning "reference lost: ..."). Body refs return the body shape.
  TopoDS_Shape resolve(ShapeRef& ref);
  /// Resolve to a plane/axis/point, covering origin geometry, construction features,
  /// planar faces, linear/circular edges, vertices and sketch entities.
  std::optional<gp_Ax3> plane(const ShapeRef& ref) const;
  std::optional<gp_Ax1> axis(const ShapeRef& ref) const;
  std::optional<gp_Pnt> point(const ShapeRef& ref) const;
  /// Face for a Profile ref (or a planar Face ref). Null if unavailable.
  TopoDS_Face profileFace(const ShapeRef& ref) const;

  // --- diagnostics ------------------------------------------------------------
  void setWarning(const std::string& msg);
  void setError(const std::string& msg);
  bool hasError() const { return error_; }
  bool hasWarning() const { return !warnings_.empty(); }
  const std::vector<std::string>& warnings() const { return warnings_; }
  const std::string& errorMessage() const { return errorMessage_; }
  /// Size of the model (bbox diagonal of all bodies, >= 1) for tolerances.
  double modelSize() const;
  /// Set by the Document when a newer edit supersedes this recompute (long ops may poll).
  bool isCancelled() const;

 private:
  Document& doc_;
  Feature& self_;
  std::map<ObjectId, TopoDS_Shape> state_;
  std::vector<ObjectId> touched_;
  size_t createdSlot_ = 0;
  bool error_ = false;
  std::string errorMessage_;
  std::vector<std::string> warnings_;
  friend class Document;
};

}  // namespace fabricad::core
