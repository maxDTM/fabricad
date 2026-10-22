// FabriCAD assembly tools: Drive Joints (set / animate a joint's motion value).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Manipulator.h"
#include "core/Tool.h"

#include <QJsonObject>
#include <QObject>
#include <QTimer>

#include <vector>

class QComboBox;
class QSlider;
class QLabel;
class QPushButton;

namespace fabricad::assembly {

class ExpressionEdit;
class JointFeature;

class DriveJointTool : public QObject, public core::ToolBase {
  Q_OBJECT
 public:
  DriveJointTool();
  void activate(core::ToolContext& ctx) override;
  void deactivate(core::ToolContext& ctx) override;
  void accept(core::ToolContext& ctx) override;
  void cancel(core::ToolContext& ctx) override;
  void selectionChanged(core::ToolContext& ctx) override;
  bool commandLineInput(core::ToolContext& ctx, const std::string& text) override;
  QWidget* createDialog(core::ToolContext& ctx, QWidget* parent) override;

  JointFeature* joint() const { return joint_; }
  void setJoint(core::ToolContext& ctx, core::ObjectId jointId);
  /// Set the primary DOF value (clamped to limits) and recompute.
  void setValue(core::ToolContext& ctx, double v);
  double value() const;
  void setAnimating(core::ToolContext& ctx, bool on);
  bool isAnimating() const { return timer_.isActive(); }
  /// One animation step (what the timer does); exposed for tests.
  void step(core::ToolContext& ctx);
  /// On-canvas handle on the joint frame: a ring (rotation DOF) or an arrow (translation DOF)
  /// that drives the primary value; kInvalidManipulator without a joint.
  core::ManipulatorId manipulatorId() const { return manipId_; }
  void onManipulatorEvent(core::ToolContext& ctx, const core::ManipulatorEvent& e);
  /// Joints the dialog offers: active, not in error, with a motion DOF.
  std::vector<core::ObjectId> candidates(const core::Document& doc) const;

 private:
  void syncDialog();
  void showManipulator(core::ToolContext& ctx);
  void restore(core::ToolContext& ctx);

  JointFeature* joint_ = nullptr;
  core::ManipulatorId manipId_ = core::kInvalidManipulator;
  bool dragging_ = false;
  QJsonObject before_;
  double rangeMin_ = -180, rangeMax_ = 180;
  double speed_ = 1.0;     ///< units per step
  bool forward_ = true;
  core::ToolContext* ctx_ = nullptr;
  QTimer timer_;
  QComboBox* jointCombo_ = nullptr;
  QSlider* slider_ = nullptr;
  ExpressionEdit* valueEdit_ = nullptr;
  QLabel* rangeLabel_ = nullptr;
  QPushButton* animateButton_ = nullptr;
  bool syncing_ = false;
};

}  // namespace fabricad::assembly
