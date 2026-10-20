// FabriCAD assembly: timeline features of the assembly module.
//
//   JointFeature      type "Joint"        a Fusion-style joint between two components
//   RigidGroupFeature type "RigidGroup"   locks several components together
//   NewComponentFeature type "NewComponent" timeline marker for component creation
//   InsertDesignFeature type "InsertDesign" bodies of an external .fcad file in a component
//
// Joint features recompute by gathering every active joint up to themselves in the timeline,
// running AssemblySolver and writing the resulting placements into the Document's
// components (Document::notifyComponentChanged). Body states are untouched.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "assembly/AssemblySolver.h"
#include "assembly/JointFrame.h"
#include "assembly/JointTypes.h"
#include "core/Document.h"
#include "core/Feature.h"
#include "core/Types.h"

#include <TopoDS_Shape.hxx>
#include <gp_Trsf.hxx>

#include <map>
#include <string>
#include <vector>

namespace fabricad::assembly {

/// Implemented by features that contribute joints to the solver.
class IJointProvider {
 public:
  virtual ~IJointProvider() = default;
  /// Append this feature's joints (frames refreshed from `state`). Warnings explain lost references.
  virtual void appendSolverJoints(const core::Document& doc, const core::BodyStateMap& state, std::vector<SolverJoint>& out,
                                  std::vector<std::string>* warnings) const = 0;
  virtual bool involvesComponent(core::ObjectId componentId) const = 0;
};

struct AssemblyInput {
  std::vector<SolverComponent> components;
  std::vector<SolverJoint> joints;
  std::vector<std::string> warnings;
};
/// Components (current placements) and joints of all active, non-suppressed joint features with
/// timeline index < `beforeIndex` (-1 = every active feature).
AssemblyInput gatherAssembly(const core::Document& doc, int beforeIndex = -1, const core::BodyStateMap* state = nullptr);
/// Write the placements of `result.moved` into the document. Returns the components that changed.
std::vector<core::ObjectId> applySolverResult(core::Document& doc, const SolverResult& result);
/// Convenience: gather + solve + (optionally) apply for the whole active assembly.
SolverResult solveAssembly(core::Document& doc, bool apply, const std::vector<SolverJoint>& extraJoints = {});

class JointFeature : public core::Feature, public IJointProvider {
 public:
  static constexpr const char* kType = "Joint";
  JointFeature();

  std::string browserFolder() const override { return "Joints"; }
  std::string iconName() const override;
  std::string editToolId() const override { return "assembly.joint"; }
  void updatePropertyFlags() override { updateValueVisibility(); }
  std::vector<core::ObjectId> extraDependencies() const override;
  void recompute(core::RecomputeContext& ctx) override;

  // --- typed access ------------------------------------------------------------
  JointType jointType() const;
  void setJointType(JointType t);
  core::ObjectId componentA() const { return ref("componentA").owner; }
  core::ObjectId componentB() const { return ref("componentB").owner; }
  void setComponents(core::ObjectId a, core::ObjectId b);
  JointOrigin originA() const;
  JointOrigin originB() const;
  void setOrigins(const JointOrigin& a, const JointOrigin& b);
  bool flip() const { return flag("flip"); }
  void setFlip(bool f);
  bool isAsBuilt() const { return flag("asBuilt"); }
  void setAsBuilt(bool b);
  bool lockMotion() const { return flag("lockMotion"); }
  void setLockMotion(bool l);
  /// Motion values in q order of the joint type (deg / mm).
  JointValues values() const;
  void setValues(const JointValues& q);
  /// Primary motion value (q0), clamped to the limits when enabled.
  double primaryValue() const { return values()[0]; }
  void setPrimaryValue(double v);
  /// User offset in the joint frame: translation X/Y/Z then rotation about Z.
  gp_Trsf offsetTransform() const;
  void setOffset(double x, double y, double z, double rotationDeg);
  bool limitsEnabled() const { return flag("limitsEnabled"); }
  double limitMin() const { return number("limitMin"); }
  double limitMax() const { return number("limitMax"); }
  double limitRest() const { return number("limitRest"); }
  void setLimits(bool enabled, double min, double max, double rest);
  double clampToLimits(double v) const;

  /// Solver joint for the current properties; origins refreshed from `state` when given.
  SolverJoint toSolverJoint(const core::Document& doc, const core::BodyStateMap* state, std::vector<std::string>* warnings) const;
  void appendSolverJoints(const core::Document& doc, const core::BodyStateMap& state, std::vector<SolverJoint>& out,
                          std::vector<std::string>* warnings) const override;
  bool involvesComponent(core::ObjectId c) const override { return c == componentA() || c == componentB(); }
  /// World frame of the joint (component B side, offset applied) for the current placements.
  gp_Trsf worldFrame(const core::Document& doc) const;
  /// Last solve status for this joint (set during recompute).
  JointSolveStatus solveStatus() const { return solveStatus_; }

  /// Display glyph for a joint type at a world frame (compound of edges). `size` in mm.
  static TopoDS_Shape makeGlyph(JointType type, const gp_Trsf& frame, double size);

 private:
  void updateValueVisibility();
  JointSolveStatus solveStatus_ = JointSolveStatus::Ok;
};

class RigidGroupFeature : public core::Feature, public IJointProvider {
 public:
  static constexpr const char* kType = "RigidGroup";
  RigidGroupFeature();
  std::string browserFolder() const override { return "Joints"; }
  std::string iconName() const override { return "rigid-group"; }
  void recompute(core::RecomputeContext& ctx) override;
  QJsonObject toJson() const override;
  void fromJson(const QJsonObject& o) override;

  std::vector<core::ObjectId> members() const;
  /// Set members and capture their current relative placements from the document.
  void setMembers(const core::Document& doc, const std::vector<core::ObjectId>& ids);
  void appendSolverJoints(const core::Document& doc, const core::BodyStateMap& state, std::vector<SolverJoint>& out,
                          std::vector<std::string>* warnings) const override;
  bool involvesComponent(core::ObjectId c) const override;

 private:
  std::vector<gp_Trsf> relative_;  ///< W_first^-1 * W_i at capture time, aligned with the "components" refs
};

class NewComponentFeature : public core::Feature {
 public:
  static constexpr const char* kType = "NewComponent";
  NewComponentFeature();
  std::string iconName() const override { return "component"; }
  void recompute(core::RecomputeContext& ctx) override;
  core::ObjectId component() const { return ref("component").owner; }
  void setComponent(core::ObjectId id);
};

class InsertDesignFeature : public core::Feature {
 public:
  static constexpr const char* kType = "InsertDesign";
  InsertDesignFeature();
  std::string iconName() const override { return "insert-design"; }
  void recompute(core::RecomputeContext& ctx) override;
  core::ObjectId component() const { return ref("component").owner; }
  void setComponent(core::ObjectId id);
  const std::string& path() const { return prop("path").toString(); }
  void setPath(const std::string& p);
  /// Drop cached external geometry (after the file changed on disk).
  static void clearCache();
};

}  // namespace fabricad::assembly
