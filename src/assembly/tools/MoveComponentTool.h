// FabriCAD assembly tools: Move/Rotate components with a triad manipulator.
//
// Free components: arrows translate, rings rotate, squares drag in a plane; the dialog shows
// the accumulated X/Y/Z distances and angles. Jointed components: the triad sits on the joint
// frame and only the joint's degrees of freedom are enabled, so dragging drives the joint
// value (the solver keeps everything consistent). OK commits one undo step.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "assembly/Triad.h"
#include "core/Tool.h"
#include "core/ViewerContext.h"

#include <QObject>
#include <gp_Trsf.hxx>

#include <map>
#include <vector>

class QLabel;

namespace fabricad::assembly {

class ExpressionEdit;
class JointFeature;

class MoveComponentTool : public QObject, public core::ToolBase {
  Q_OBJECT
 public:
  MoveComponentTool();
  void activate(core::ToolContext& ctx) override;
  void deactivate(core::ToolContext& ctx) override;
  void accept(core::ToolContext& ctx) override;
  void cancel(core::ToolContext& ctx) override;
  bool mousePress(core::ToolContext& ctx, const core::ViewerMouseEvent& e) override;
  bool mouseMove(core::ToolContext& ctx, const core::ViewerMouseEvent& e) override;
  bool mouseRelease(core::ToolContext& ctx, const core::ViewerMouseEvent& e) override;
  void selectionChanged(core::ToolContext& ctx) override;
  bool commandLineInput(core::ToolContext& ctx, const std::string& text) override;
  QWidget* createDialog(core::ToolContext& ctx, QWidget* parent) override;

  // --- state / scripting -----------------------------------------------------------
  const std::vector<core::ObjectId>& components() const { return components_; }
  void setComponents(core::ToolContext& ctx, const std::vector<core::ObjectId>& ids);
  const Triad& triad() const { return triad_; }
  /// Apply an absolute delta (relative to the start placements) in world coordinates.
  void applyDelta(core::ToolContext& ctx, const gp_Trsf& delta);
  /// Translate by (x,y,z) and rotate by (rx,ry,rz) degrees about the pivot (triad origin).
  void setTransform(core::ToolContext& ctx, double x, double y, double z, double rx, double ry, double rz);
  const gp_Trsf& delta() const { return delta_; }
  /// The joint that constrains the moved component (nullptr for free move).
  const JointFeature* constrainingJoint() const { return joint_; }
  bool isDragging() const { return dragging_; }

 private:
  void setupTriad(core::ToolContext& ctx);
  void moveTriadToCurrent(core::ToolContext& ctx);
  void drawTriad(core::ToolContext& ctx, Triad::Handle hovered);
  void applyJointDrive(core::ToolContext& ctx, const gp_Trsf& newWorldA);
  void syncDialog();

  std::vector<core::ObjectId> components_;
  std::map<core::ObjectId, gp_Trsf> startWorld_;     ///< world placements of the moved roots at activation
  std::map<core::ObjectId, gp_Trsf> placementsBefore_;
  gp_Trsf delta_;      ///< accumulated delta since activation (world)
  gp_Trsf dragBase_;   ///< delta_ at the start of the current drag
  gp_Ax3 baseFrame_;   ///< triad frame before any move
  Triad triad_;
  bool dragging_ = false;
  Triad::Handle hovered_ = Triad::Handle::None;
  std::vector<core::PreviewHandle> previews_;
  JointFeature* joint_ = nullptr;
  QJsonObject jointBefore_;
  double jointValueStart_ = 0.0;
  double values_[6] = {0, 0, 0, 0, 0, 0};  ///< dialog x,y,z,rx,ry,rz
  core::ToolContext* ctx_ = nullptr;
  ExpressionEdit* edits_[6] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
  QLabel* targetLabel_ = nullptr;
  bool syncing_ = false;
};

}  // namespace fabricad::assembly
