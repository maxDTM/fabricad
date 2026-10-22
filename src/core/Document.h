// FabriCAD core: Document = parametric timeline + bodies + components + parameters.
//
// Recompute model (Fusion-style linear timeline):
//   * features_[0 .. rollbackIndex) are active; the rest are rolled back (Stale).
//   * Each feature stores the complete body-shape map *after* it ran, so the state
//     before feature i is features_[i-1]->bodyStatesAfter_. Rolling back is a lookup.
//   * markDirty(feature) remembers the smallest dirty index; recompute() replays from
//     there to the marker, in order, each feature through a RecomputeContext.
//   * Document never recomputes implicitly (except setRollbackIndex forward and load):
//     mutate, then call recompute(). Undo commands in Commands.h do this for you.
//
// Threading: Document is GUI-thread only. Recompute is synchronous in this MVP; the
// Document keeps emitting signals from recompute(), so a later worker-thread version
// can post results back without changing the API.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Body.h"
#include "core/Component.h"
#include "core/Expression.h"
#include "core/Feature.h"
#include "core/TimelineGroup.h"
#include "core/Types.h"
#include "core/Units.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <TopoDS_Shape.hxx>
#include <gp_Trsf.hxx>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fabricad::core {

class UndoStack;
class SelectionModel;

class Document : public QObject {
  Q_OBJECT
 public:
  static constexpr int kFormatVersion = 1;
  static constexpr const char* kFileExtension = "fcad";

  explicit Document(QObject* parent = nullptr);
  ~Document() override;

  // --- identity / file -------------------------------------------------------
  const std::string& name() const { return name_; }
  void setName(std::string n);
  const std::string& filePath() const { return filePath_; }
  bool isModified() const { return modified_; }
  void setModified(bool m);
  const UnitSystem& units() const { return units_; }
  void setUnits(const UnitSystem& u);

  /// Fresh unique id (also used by modules for their own objects, e.g. joints origins).
  ObjectId allocateId();

  // --- features / timeline ---------------------------------------------------
  /// Insert at `index` (-1 = at the rollback marker, which then advances past it).
  /// Assigns an id if the feature has none and a unique name if empty. Marks dirty.
  Feature* addFeature(std::unique_ptr<Feature> f, int index = -1);
  /// Remove and hand back ownership (nullptr if unknown). Bodies created by it are removed.
  std::unique_ptr<Feature> takeFeature(ObjectId id);
  bool canMoveFeature(ObjectId id, int newIndex, std::string* why = nullptr) const;
  bool moveFeature(ObjectId id, int newIndex);
  Feature* feature(ObjectId id);
  const Feature* feature(ObjectId id) const;
  template <class T>
  T* featureAs(ObjectId id) { return dynamic_cast<T*>(feature(id)); }
  template <class T>
  const T* featureAs(ObjectId id) const { return dynamic_cast<const T*>(feature(id)); }
  Feature* featureAt(int index);
  const Feature* featureAt(int index) const;
  int indexOf(ObjectId featureId) const;  ///< -1 if not a feature of this document
  int featureCount() const { return static_cast<int>(features_.size()); }
  const std::vector<std::unique_ptr<Feature>>& features() const { return features_; }
  /// "Extrude" -> "Extrude1", "Extrude2", ... (first unused).
  std::string uniqueFeatureName(const std::string& base) const;
  /// Call after changing a feature's properties/name/suppression outside a core command.
  void notifyFeatureChanged(ObjectId id);

  /// Move with the group rules (TimelineGroup.h). `targetGroup`: std::nullopt = automatic (the
  /// feature stays in its groups where the new slot touches them, joins a group whose inside it
  /// lands in, otherwise leaves); kInvalidId = top level; a group id = become its member. An
  /// explicit target that would split a group is refused. The dependency rules of canMoveFeature
  /// apply in every case; canMoveFeature/moveFeature are the automatic variant.
  bool canMoveFeatureTo(ObjectId id, int newIndex, std::optional<ObjectId> targetGroup, std::string* why = nullptr) const;
  bool moveFeatureTo(ObjectId id, int newIndex, std::optional<ObjectId> targetGroup);

  int rollbackIndex() const { return rollback_; }
  /// Move the marker. Moving forward recomputes the newly active features.
  void setRollbackIndex(int index);
  bool isFeatureActive(ObjectId id) const;

  // --- dependencies ----------------------------------------------------------
  /// Earlier features this feature reads (ref owners; body refs -> features touching that body).
  std::vector<ObjectId> dependenciesOf(ObjectId featureId) const;
  /// Later features that read this feature's outputs or the bodies it touched.
  std::vector<ObjectId> dependentsOf(ObjectId featureId, bool transitive = false) const;
  /// Features before `beforeIndex` (default: all) that created or modified a body, in timeline order.
  std::vector<ObjectId> featuresTouchingBody(ObjectId bodyId, int beforeIndex = -1) const;
  /// History-named sub-shape of a body: `name` looked up in namedShapes() of the last feature
  /// before `beforeIndex` that touched the body (null if unknown).
  TopoDS_Shape namedSubShape(ObjectId bodyId, const std::string& name, int beforeIndex = -1) const;
  /// Reverse lookup: history name of `sub` in the same name map ("" if unnamed).
  std::string subShapeName(ObjectId bodyId, const TopoDS_Shape& sub, int beforeIndex = -1) const;

  // --- recompute ---------------------------------------------------------------
  void markDirty(ObjectId featureId);
  void markAllDirty();
  bool needsRecompute() const { return firstDirty_ < rollback_; }
  int firstDirtyIndex() const { return firstDirty_; }
  /// Replay dirty features up to the marker. Emits featureStatusChanged / bodyChanged /
  /// recomputeFinished. Safe to call when nothing is dirty (no-op, still emits finished).
  void recompute();
  /// Forces a full replay (parameters, imports).
  void recomputeAll();

  // --- timeline groups (TimelineGroup.h; undoable edits: GroupCommands.h) ---------------------
  const TimelineGroup* group(ObjectId groupId) const;
  int groupCount() const { return static_cast<int>(groups_.size()); }
  /// All groups, outer before inner, in timeline order.
  std::vector<ObjectId> groupIds() const;
  /// First group with this name (exact, then case-insensitive), kInvalidId if none.
  ObjectId findGroup(const std::string& name) const;
  /// Innermost group holding a feature, or the parent of a group; kInvalidId = top level.
  ObjectId groupOf(ObjectId featureOrGroupId) const;
  /// Enclosing groups of a feature or group, innermost first (a group is not part of its own chain).
  std::vector<ObjectId> groupChain(ObjectId featureOrGroupId) const;
  /// True if `featureOrGroupId` lies inside `groupId` (at any depth).
  bool isInGroup(ObjectId featureOrGroupId, ObjectId groupId) const;
  /// Every feature of the group including nested groups, in timeline order.
  std::vector<ObjectId> groupFeatures(ObjectId groupId) const;
  /// Direct children (features and groups) in timeline order; kInvalidId = the top level.
  std::vector<ObjectId> groupChildren(ObjectId groupId) const;
  /// Timeline indices of the group's first and last feature ({-1, -1} if unknown).
  std::pair<int, int> groupRange(ObjectId groupId) const;
  /// None / Some / All of the group's features suppressed (None for an unknown group).
  GroupSuppression groupSuppression(ObjectId groupId) const;
  /// Flattened rows: each group header directly before its first member, with depths.
  std::vector<TimelineItem> timelineItems() const;
  std::string uniqueGroupName(const std::string& base = "Group") const;

  /// Items are feature and/or group ids that together cover consecutive features; the new group
  /// must not cut through an existing group (it may enclose groups or sit inside one).
  bool canCreateGroup(const std::vector<ObjectId>& items, std::string* why = nullptr) const;
  /// Returns the new group's id (kInvalidId when refused). An empty name picks "Group<n>".
  /// `id` reuses a fixed id (undo/redo); kInvalidId allocates one.
  ObjectId createGroup(const std::vector<ObjectId>& items, std::string name = {}, ObjectId id = kInvalidId,
                       std::string* why = nullptr);
  /// Dissolve a group: its children move to its parent. Features are not touched.
  bool ungroup(ObjectId groupId);
  bool renameGroup(ObjectId groupId, std::string name);
  bool setGroupCollapsed(ObjectId groupId, bool collapsed);
  /// Move a whole group so its first feature lands at `newIndex` (index in the final order).
  /// Dependencies are checked for every member against the features outside the group.
  bool canMoveGroup(ObjectId groupId, int newIndex, std::string* why = nullptr) const;
  bool moveGroup(ObjectId groupId, int newIndex);

  /// Snapshot / restore for undo. Restoring repairs anything that does not fit the current
  /// features (unknown ids, empty or split groups) so the invariants always hold.
  TimelineGroupState groupState() const { return {groups_, featureGroup_}; }
  void setGroupState(TimelineGroupState state);
  QJsonArray groupsToJson() const;
  void groupsFromJson(const QJsonArray& groups);

  // --- bodies -------------------------------------------------------------------
  Body* body(ObjectId id);
  const Body* body(ObjectId id) const;
  std::vector<Body*> bodies();
  std::vector<const Body*> bodies() const;
  /// Register a body record (id assigned if kInvalidId). Normally done via RecomputeContext::createBody.
  Body& addBody(Body b);
  bool removeBody(ObjectId id);
  /// Shape at the current timeline position (null if none / rolled back before creation).
  const TopoDS_Shape& bodyShape(ObjectId id) const;
  const std::map<ObjectId, TopoDS_Shape>& bodyStates() const { return currentState_; }
  /// Body states after the feature at `featureIndex` (-1 or empty document -> {}).
  const std::map<ObjectId, TopoDS_Shape>& bodyStatesAfter(int featureIndex) const;
  /// Bodies with a non-null current shape, visible flag honoured when `visibleOnly`.
  std::vector<ObjectId> liveBodies(bool visibleOnly = false) const;
  /// Call after editing Body fields (name/visible/color) so views refresh.
  void notifyBodyChanged(ObjectId id);
  std::string uniqueBodyName(const std::string& base = "Body") const;

  // --- components ---------------------------------------------------------------
  Component& rootComponent();
  const Component& rootComponent() const;
  Component* component(ObjectId id);
  const Component* component(ObjectId id) const;
  std::vector<Component*> components();
  std::vector<const Component*> components() const;
  Component& addComponent(const std::string& name, ObjectId parent = kRootComponentId);
  /// Insert a prepared component. `c.id == kInvalidId` allocates; an explicit id is honoured
  /// (and nextId bumped) so undo/redo can restore the original ids. The parent must exist.
  Component& addComponent(Component c);
  /// Follow Component::definition to the component whose bodies/features an occurrence shows.
  ObjectId definitionOf(ObjectId componentId) const;
  /// Bodies a component displays: its own bodies, or its definition's for an occurrence.
  std::vector<ObjectId> displayedBodies(ObjectId componentId) const;
  /// Also removes descendants; bodies move to the parent. Root cannot be removed.
  bool removeComponent(ObjectId id);
  /// Move a component (with its sub-tree) under another parent. `placement` becomes its new
  /// local placement. False for the root, an unknown parent or a parent inside its own sub-tree.
  bool reparentComponent(ObjectId id, ObjectId newParent, const gp_Trsf& placement);
  ObjectId activeComponent() const { return activeComponent_; }
  void setActiveComponent(ObjectId id);
  /// Placement of a component in world coordinates (parents composed).
  gp_Trsf worldPlacement(ObjectId componentId) const;
  /// Bodies directly owned by a component.
  std::vector<ObjectId> bodiesOf(ObjectId componentId) const;
  void notifyComponentChanged(ObjectId id);

  // --- user parameters -----------------------------------------------------------
  struct Parameter {
    std::string name;
    std::string expression;
    std::string comment;
    Quantity value{};
    bool ok = false;
    std::string error;
  };
  const std::vector<Parameter>& parameters() const { return parameters_; }
  const Parameter* parameter(const std::string& name) const;
  /// Add or update. Returns false for an invalid name. Marks everything dirty.
  bool setParameter(const std::string& name, const std::string& expression, const std::string& comment = {});
  bool removeParameter(const std::string& name);
  /// Re-evaluate all parameters (multi-pass for ordering). Called by recompute().
  void evaluateParameters();
  /// Evaluate an expression in this document's scope (parameters + units).
  EvalResult evaluate(const std::string& expr, std::optional<Dimension> expected = std::nullopt) const;
  ExpressionContext expressionContext() const;

  // --- extension data ----------------------------------------------------------------
  /// Per-module JSON blobs saved with the document (e.g. "drawing": sheets). Modules
  /// write before save (aboutToSave signal) and read after load (loaded signal).
  QJsonObject extensionData(const std::string& key) const;
  void setExtensionData(const std::string& key, const QJsonObject& data);

  // --- undo / selection -------------------------------------------------------------
  UndoStack& undoStack() { return *undo_; }
  SelectionModel& selection() { return *selection_; }

  // --- serialisation ------------------------------------------------------------------
  QJsonObject toJson() const;
  /// Replace the content from JSON. Needs the feature types registered in FeatureFactory.
  /// Unknown feature types are kept as PlaceholderFeature (error status) so nothing is lost.
  bool fromJson(const QJsonObject& root, std::string* error = nullptr);
  bool save(const std::string& path, std::string* error = nullptr);
  bool saveAs(const std::string& path, std::string* error = nullptr) { return save(path, error); }
  bool load(const std::string& path, std::string* error = nullptr);
  /// Back to an empty document (root component only).
  void clear();

 signals:
  void featureAdded(fabricad::core::ObjectId id, int index);
  void featureRemoved(fabricad::core::ObjectId id, int oldIndex);
  void featureMoved(fabricad::core::ObjectId id, int oldIndex, int newIndex);
  void featureChanged(fabricad::core::ObjectId id);         ///< inputs/name/suppressed edited
  void featureStatusChanged(fabricad::core::ObjectId id);   ///< after recompute
  void rollbackChanged(int index);
  void groupsChanged();  ///< groups created/removed/renamed/collapsed or membership changed
  void recomputeStarted();
  void recomputeFinished();
  void bodyAdded(fabricad::core::ObjectId id);
  void bodyRemoved(fabricad::core::ObjectId id);
  void bodyChanged(fabricad::core::ObjectId id);            ///< shape or attributes changed
  void componentAdded(fabricad::core::ObjectId id);
  void componentRemoved(fabricad::core::ObjectId id);
  void componentChanged(fabricad::core::ObjectId id);       ///< placement/name/grounded/visible
  void activeComponentChanged(fabricad::core::ObjectId id);
  void parametersChanged();
  void unitsChanged();
  void modifiedChanged(bool modified);
  void nameChanged();
  void documentReset();   ///< after clear()/load(): rebuild all views
  void aboutToSave();     ///< modules flush extension data
  void loaded();          ///< modules read extension data

 private:
  void resetToEmpty();
  void applyState(const std::map<ObjectId, TopoDS_Shape>& newState);
  void touchModified();
  // timeline groups (DocumentGroups.cpp)
  void rawMoveFeature(int oldIndex, int newIndex);
  void clearGroups();
  void placeInsertedFeature(ObjectId featureId, int index);
  void detachFeatureFromGroups(ObjectId featureId);
  void normalizeGroups();
  bool groupHasChildren(ObjectId groupId, const std::vector<ObjectId>& ignoring = {}) const;
  bool pruneEmptyGroups(ObjectId from, bool remember);
  ObjectId commonGroup(ObjectId a, ObjectId b) const;
  bool placementLegal(ObjectId group, ObjectId left, ObjectId right, const std::vector<ObjectId>& moving) const;
  ObjectId choosePlacement(const std::vector<ObjectId>& hints, ObjectId left, ObjectId right,
                           const std::vector<ObjectId>& moving) const;

  std::string name_ = "Untitled";
  std::string filePath_;
  bool modified_ = false;
  UnitSystem units_;
  ObjectId nextId_ = kFirstFreeId;

  std::vector<std::unique_ptr<Feature>> features_;
  int rollback_ = 0;
  std::map<ObjectId, TimelineGroup> groups_;
  std::map<ObjectId, ObjectId> featureGroup_;                ///< feature -> innermost group
  std::map<ObjectId, std::vector<ObjectId>> removedChains_;  ///< taken feature -> its group chain
  std::map<ObjectId, TimelineGroup> removedGroups_;          ///< groups removed with their last feature
  int firstDirty_ = 0;
  bool cancelRequested_ = false;

  std::map<ObjectId, Body> bodies_;
  std::vector<ObjectId> bodyOrder_;
  std::map<ObjectId, TopoDS_Shape> currentState_;
  std::map<ObjectId, TopoDS_Shape> emptyState_;

  std::map<ObjectId, Component> components_;
  ObjectId activeComponent_ = kRootComponentId;

  std::vector<Parameter> parameters_;
  std::map<std::string, QJsonObject> extensions_;

  std::unique_ptr<UndoStack> undo_;
  std::unique_ptr<SelectionModel> selection_;

  friend class RecomputeContext;
};

/// Stands in for a feature whose type is not registered (file from a newer/other build).
class PlaceholderFeature : public Feature {
 public:
  explicit PlaceholderFeature(std::string originalType);
  void recompute(RecomputeContext& ctx) override;
  QJsonObject toJson() const override;
  void fromJson(const QJsonObject& o) override;
  const std::string& originalType() const { return originalType_; }

 private:
  std::string originalType_;
  QJsonObject raw_;
};

}  // namespace fabricad::core
