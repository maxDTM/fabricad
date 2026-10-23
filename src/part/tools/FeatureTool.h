// FabriCAD part: generic command tool for a part feature (create or edit).
//
// Create: a working feature is built from the preselection, previewed live by running
// PartFeature::compute() against the document state at the rollback marker, and committed
// with core::AddFeatureCommand on OK. Edit (args {"edit", "<id>"} or a Feature ref in the
// preselection): the live feature is modified in place with notifyFeatureChanged +
// recompute for preview; OK pushes a ModifyFeatureCommand(before, after), Cancel restores.
// Selection picks go to the active selection input of the dialog (PropertyPanel), with the
// selection filter set from the property's ref filter. Everything works headlessly with
// NullViewerContext / NullAppHost.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Tool.h"
#include "core/ViewerContext.h"
#include "part/PartFeature.h"

#include <QJsonObject>
#include <QPointer>

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fabricad::part {

class PropertyPanel;
class FeatureValueBox;

class FeatureTool : public core::ToolBase {
 public:
  using Creator = std::function<std::unique_ptr<PartFeature>()>;
  FeatureTool(core::ToolInfo info, std::string featureType, Creator creator);
  ~FeatureTool() override;

  const std::string& featureType() const { return featureType_; }

  void activate(core::ToolContext& ctx) override;
  void deactivate(core::ToolContext& ctx) override;
  void accept(core::ToolContext& ctx) override;
  void cancel(core::ToolContext& ctx) override;
  void selectionChanged(core::ToolContext& ctx) override;
  void documentChanged(core::ToolContext& ctx) override;
  bool commandLineInput(core::ToolContext& ctx, const std::string& text) override;
  /// Keys typed in the viewport go to the on-screen value box of the primary numeric input.
  bool keyPress(core::ToolContext& ctx, const core::ViewerKeyEvent& e) override;
  QWidget* createDialog(core::ToolContext& ctx, QWidget* parent) override;

  // --- state inspection (dialog, tests) ------------------------------------------
  /// The feature being defined (working copy when creating, the live one when editing).
  PartFeature* feature() const;
  bool isEditing() const { return editing_ != nullptr; }
  const std::string& activeInput() const { return activeInput_; }
  void setActiveInput(core::ToolContext& ctx, const std::string& propName);
  /// Result of the last preview computation (a deferred batch-mode preview is computed first).
  bool isValid() const {
    ensurePreview();
    return valid_;
  }
  const std::string& message() const {
    ensurePreview();
    return message_;
  }
  const Outcome& lastOutcome() const {
    ensurePreview();
    return outcome_;
  }
  /// On-screen value box (Fusion dynamic input) for the primary numeric; nullptr before the first preview.
  const FeatureValueBox* valueBox() const {
    ensurePreview();
    return valueBox_.get();
  }
  /// Re-run the preview after a programmatic property change. In batch mode (core::isBatchMode(),
  /// script runs) a new feature's preview is only marked out of date: nobody watches it, and it is
  /// computed once when its result is needed (OK, OPTIONS, the queries above). Edit mode always
  /// recomputes at once, because its preview is the live document.
  void updatePreview(core::ToolContext& ctx);
  /// Compute a deferred preview now (no-op when it is up to date).
  void flushPreview(core::ToolContext& ctx);
  bool isPreviewPending() const { return previewPending_; }
  /// Set a numeric property from text (expression allowed). False if invalid.
  bool setNumeric(core::ToolContext& ctx, const std::string& propName, const std::string& text);

  // --- on-canvas manipulators (FeatureManipulators.h) ------------------------------
  /// One gizmo shown in the viewer, bound to a numeric property of the feature.
  struct ManipulatorLink {
    core::ManipulatorId id = core::kInvalidManipulator;
    std::string property;
    double scale = 1.0;  ///< gizmo value = property * scale
    bool triad = false;  ///< Move triad: drives dx/dy/dz/rx/ry/rz by handle (see onManipulatorEvent)
  };
  const std::vector<ManipulatorLink>& manipulatorLinks() const { return manipulators_; }
  const ManipulatorLink* manipulatorFor(const std::string& property) const;
  /// Gizmo event (drag) -> property value -> preview. Public so scripts/tests can feed events.
  void onManipulatorEvent(core::ToolContext& ctx, const core::ManipulatorEvent& e);
  bool isManipulatorDragging() const { return manipulatorDragging_; }

 protected:
  /// Show/update/remove the gizmos for the feature's current inputs (after each preview).
  void syncManipulators(core::ToolContext& ctx, core::RecomputeContext& rctx);
  void clearManipulators(core::ToolContext& ctx);
  /// Fusion: a Join extrusion dragged into existing material becomes a Cut (until the user picks an operation).
  void autoOperation(core::ToolContext& ctx);
  /// Share of `tool`'s volume that lies inside the bodies of the feature's component tree (0..1),
  /// -1 when the tool has no volume.
  double materialFraction(core::ToolContext& ctx, const TopoDS_Shape& tool) const;
  /// Fusion: a primitive is New Body when it touches no body in scope, else Join (while the
  /// operation is automatic). Returns true when the operation changed.
  bool autoPrimitiveOperation(core::RecomputeContext& rctx);
  /// The user picked Cut on a new one-side distance Extrude that points away from the material:
  /// flip it into the body when that makes the tool enter material (PART-04).
  void autoFlipForCut(core::ToolContext& ctx);
  /// The operation left Cut: undo an automatic flip.
  void undoAutoFlip(core::ToolContext& ctx);
  /// Hook for subclasses that decide the feature type lazily (Press Pull).
  virtual std::unique_ptr<PartFeature> createFeature(core::ToolContext& ctx);
  void startWith(core::ToolContext& ctx, std::unique_ptr<PartFeature> f);
  void applyPreselection(core::ToolContext& ctx, const std::vector<core::ShapeRef>& refs);
  bool addRefToInput(core::ToolContext& ctx, const std::string& propName, core::ShapeRef ref);
  void advanceActiveInput(core::ToolContext& ctx);
  /// Status prompt for the active input: "Select Profiles" while it is empty, "Profiles:
  /// 1 selected" once something is picked (p1-check 4), "Editing Extrude1" while editing.
  void updatePrompt(core::ToolContext& ctx);
  void applyFilter(core::ToolContext& ctx);
  void clearPreviews(core::ToolContext& ctx);
  /// The preview itself (what updatePreview() did before batch mode deferred it).
  void computePreview(core::ToolContext& ctx);
  void ensurePreview() const;
  void highlightRefs(core::ToolContext& ctx);
  /// Edit mode: move the rollback marker just after the edited feature so previews recompute
  /// only that feature; restoreRollback() replays the downstream history (OK / Cancel).
  void rollbackToFeature(core::ToolContext& ctx);
  void restoreRollback(core::ToolContext& ctx);
  std::string primaryNumeric() const;
  /// A new feature still waits for its first selection (no error message yet).
  bool awaitingFirstPick() const;
  /// Fresh per-command state (activate of every feature command, Press Pull included).
  void resetState();
  /// The dialog's rows no longer match the visible properties: rebuild, keeping the focus (PART-06).
  void rebuildPanelIfNeeded();
  /// Rebuild the dialog rows now, or with the deferred preview (batch mode).
  void rebuildPanel();

  std::string featureType_;
  Creator creator_;
  std::unique_ptr<PartFeature> working_;
  PartFeature* editing_ = nullptr;
  QJsonObject before_;
  int savedRollback_ = -1;  ///< rollback index before the edit moved the marker (-1 = unchanged)
  std::string activeInput_;
  std::vector<core::PreviewHandle> previews_;
  QPointer<PropertyPanel> panel_;
  std::unique_ptr<FeatureValueBox> valueBox_;
  Outcome outcome_;
  bool valid_ = false;
  std::string message_;
  bool inSelectionUpdate_ = false;
  bool inPreview_ = false;
  bool previewPending_ = false;                 ///< batch mode: inputs changed since the last preview
  core::ToolContext* pendingCtx_ = nullptr;     ///< context to compute the deferred preview with
  bool panelRebuildPending_ = false;
  std::vector<std::string> previewWarnings_;              ///< warnings of the last new-feature preview
  std::map<core::ObjectId, TopoDS_Shape> previewInput_;   ///< the body states it was computed from            ///< batch mode: a row rebuild waits for the preview
  std::vector<ManipulatorLink> manipulators_;
  bool manipulatorDragging_ = false;
  bool operationAuto_ = true;  ///< the user has not chosen an operation explicitly yet
  bool flipAuto_ = false;      ///< "flip" was set by autoFlipForCut (not by the user)
  bool flipUser_ = false;      ///< the user toggled "flip": never flip automatically again
  std::array<double, 6> triadStart_{};  ///< dx dy dz rx ry rz when a triad drag began
};

/// Press Pull (Q): context command. Face -> Offset Face, edge -> Fillet, profile -> Extrude.
class PressPullTool : public FeatureTool {
 public:
  explicit PressPullTool(core::ToolInfo info);
  void activate(core::ToolContext& ctx) override;
  void selectionChanged(core::ToolContext& ctx) override;

 protected:
  std::unique_ptr<PartFeature> featureFor(const std::vector<core::ShapeRef>& refs) const;
};

/// "part.edit-feature <id>": opens the feature's own tool in edit mode.
class EditFeatureTool : public core::ToolBase {
 public:
  explicit EditFeatureTool(core::ToolInfo info) : ToolBase(std::move(info)) {}
  void activate(core::ToolContext& ctx) override;
};

/// Tool id that edits features of the given type (empty if none registered).
std::string toolIdForFeatureType(const std::string& type);
void registerFeatureToolId(const std::string& type, const std::string& toolId);

}  // namespace fabricad::part
