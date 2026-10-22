// FabriCAD assembly tools: Joint (J) and As-built Joint (Shift+J), Fusion 360 style.
//
// Joint: hover a face/edge/vertex -> snap glyphs; click a snap on component 1, then on
// component 2. Component 1 immediately jumps so its snap frame mates with component 2's
// (live placement edit, restored on cancel). The dialog offers motion type, flip, offsets,
// rotation, the motion value, animation and joint limits. OK adds a JointFeature.
// As-built: pick component 1, component 2, (for non-rigid types) the axis position; nothing moves.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "assembly/JointFeature.h"
#include "assembly/JointFrame.h"
#include "assembly/JointTypes.h"
#include "core/Tool.h"
#include "core/ViewerContext.h"

#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <gp_Trsf.hxx>

#include <map>
#include <memory>
#include <optional>
#include <vector>

class QComboBox;
class QCheckBox;
class QLabel;
class QPushButton;
class QGroupBox;

namespace fabricad::assembly {

class ExpressionEdit;

class JointTool : public QObject, public core::ToolBase {
  Q_OBJECT
 public:
  enum class Stage { PickA, PickB, PickAxis, Ready };
  explicit JointTool(bool asBuilt);

  void activate(core::ToolContext& ctx) override;
  void deactivate(core::ToolContext& ctx) override;
  void accept(core::ToolContext& ctx) override;
  void cancel(core::ToolContext& ctx) override;
  bool mouseMove(core::ToolContext& ctx, const core::ViewerMouseEvent& e) override;
  bool mousePress(core::ToolContext& ctx, const core::ViewerMouseEvent& e) override;
  void selectionChanged(core::ToolContext& ctx) override;
  void documentChanged(core::ToolContext& ctx) override;
  bool commandLineInput(core::ToolContext& ctx, const std::string& text) override;
  QWidget* createDialog(core::ToolContext& ctx, QWidget* parent) override;

  // --- state (also for tests / scripting) ---------------------------------------
  Stage stage() const { return stage_; }
  bool isAsBuilt() const { return asBuilt_; }
  const JointOrigin& originA() const { return originA_; }
  const JointOrigin& originB() const { return originB_; }
  JointType jointType() const { return type_; }
  void setJointType(core::ToolContext& ctx, JointType t);
  bool flip() const { return flip_; }
  void setFlip(core::ToolContext& ctx, bool f);
  void setOffset(core::ToolContext& ctx, double x, double y, double z, double rotationDeg);
  void setValue(core::ToolContext& ctx, double primary);
  double value() const { return values_[0]; }
  void setLimits(bool enabled, double min, double max, double rest);
  void setAnimating(core::ToolContext& ctx, bool on);
  bool isAnimating() const { return timer_.isActive(); }
  /// Feed a pick programmatically (what a click on a snap does).
  void pick(core::ToolContext& ctx, const core::ShapeRef& ref, const gp_Pnt& worldPoint, SnapMode mode = SnapMode::Auto);
  /// Joint feature id created by the last accept (kInvalidId if none).
  core::ObjectId createdJoint() const { return created_; }
  /// Edit mode (args {"edit", id}): the joint being edited, kInvalidId when creating.
  bool isEditing() const { return editing_ != core::kInvalidId; }
  core::ObjectId editingJoint() const { return editing_; }
  /// Build the solver joint for the current state (frames refreshed).
  std::optional<SolverJoint> currentSolverJoint(const core::Document& doc) const;

 private:
  void reset(core::ToolContext& ctx);
  void setStage(core::ToolContext& ctx, Stage s);
  void placeOrigin(core::ToolContext& ctx, const JointOrigin& origin);
  void updatePreview(core::ToolContext& ctx);
  void restorePlacements(core::ToolContext& ctx);
  void showSnapGlyphs(core::ToolContext& ctx, const core::ShapeRef& ref, const gp_Pnt& worldHit);
  void clearSnapGlyphs(core::ToolContext& ctx);
  std::unique_ptr<JointFeature> buildFeature(const core::Document& doc) const;
  /// Write the tool state into a feature (shared by create and edit).
  void fillFeature(JointFeature& f, const core::Document& doc) const;
  /// Load the tool state from an existing feature (edit mode).
  void loadFromFeature(const core::Document& doc, const JointFeature& f);
  /// Edit mode preview: push the state into the live feature and recompute.
  void previewEdit(core::ToolContext& ctx);
  void restoreEdited(core::ToolContext& ctx);
  void syncDialog();
  void onAnimationTick();
  /// Stop the Animate preview and put back the value the user typed (ASM-05).
  void stopAnimation(core::ToolContext* ctx);
  void clearPickError(core::ToolContext& ctx);

  const bool asBuiltTool_;  ///< flavour this tool was registered as
  bool asBuilt_;            ///< current flavour (follows the feature in edit mode)
  core::ObjectId editing_ = core::kInvalidId;
  QJsonObject before_;      ///< edited feature's JSON at activation
  bool editAccepted_ = false;
  Stage stage_ = Stage::PickA;
  JointOrigin originA_, originB_;
  core::ObjectId compA_ = core::kInvalidId, compB_ = core::kInvalidId;  ///< as-built picks
  std::optional<gp_Ax3> asBuiltAxis_;                                   ///< world frame for as-built
  JointType type_ = JointType::Rigid;
  bool flip_ = false;
  double offset_[3] = {0, 0, 0};
  double rotation_ = 0.0;
  JointValues values_{0, 0, 0};
  bool limitsEnabled_ = false;
  double limitMin_ = 0, limitMax_ = 0, limitRest_ = 0;
  std::map<core::ObjectId, gp_Trsf> placementsBefore_;
  bool placementsChanged_ = false;
  core::ObjectId created_ = core::kInvalidId;
  core::ToolContext* ctx_ = nullptr;  ///< valid while active (for timer / dialog callbacks)
  QTimer timer_;
  double animPhase_ = 0.0;
  double typedValue_ = 0.0;
  bool pickErrorShown_ = false;  ///< a red pick error is on the status line  ///< the primary joint value as typed (animation writes values_[0])
  // hover state
  core::ShapeRef hoverRef_;
  std::vector<SnapCandidate> hoverCandidates_;
  int hoverBest_ = -1;
  std::vector<core::PreviewHandle> glyphs_;
  std::vector<core::PreviewHandle> framePreviews_;
  // dialog
  QLabel* labelA_ = nullptr;
  QLabel* labelB_ = nullptr;
  QComboBox* typeCombo_ = nullptr;
  QPushButton* flipButton_ = nullptr;
  ExpressionEdit* offsetEdits_[3] = {nullptr, nullptr, nullptr};
  ExpressionEdit* rotationEdit_ = nullptr;
  ExpressionEdit* valueEdit_ = nullptr;
  QLabel* valueLabel_ = nullptr;
  QPushButton* animateButton_ = nullptr;
  QCheckBox* limitsCheck_ = nullptr;
  ExpressionEdit* limitEdits_[3] = {nullptr, nullptr, nullptr};
  bool syncing_ = false;
};

}  // namespace fabricad::assembly
