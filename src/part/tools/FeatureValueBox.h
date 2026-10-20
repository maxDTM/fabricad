// FabriCAD part: Fusion-style on-screen value box for a feature tool's primary numeric input
// (extrude distance, fillet radius, shell thickness, offset plane distance, revolve angle...).
//
// Reuses the sketch module's ValueInputModel / ValueInputOverlay: one box anchored at the top
// of the feature's preview (or the edited feature's solid), mirroring the dialog's field. Keys
// forwarded by the shell to the tool while the viewport has focus type into the box; each
// evaluable keystroke updates the property (live preview), Enter commits the feature. Works
// headlessly: without a viewer widget only the model exists (tests read it).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Tool.h"
#include "sketch/ValueInput.h"

#include <memory>
#include <string>

namespace fabricad::sketch {
class ValueInputOverlay;
}

namespace fabricad::part {

class FeatureTool;

class FeatureValueBox {
 public:
  FeatureValueBox(core::ToolContext& ctx, FeatureTool& tool);
  ~FeatureValueBox();

  /// Re-read the property (label, value, kind) and the anchor from the tool's preview.
  void update(core::ToolContext& ctx);
  /// Key from the shell. True when consumed (typing / Tab / Esc clearing the box / Enter committed).
  bool keyPress(core::ToolContext& ctx, const core::ViewerKeyEvent& e);

  const sketch::ValueInputModel& model() const { return model_; }
  const std::string& propertyName() const { return prop_; }
  bool isActive() const { return !prop_.empty(); }

 private:
  void applyTyped(core::ToolContext& ctx);

  FeatureTool& tool_;
  std::string prop_;
  sketch::ValueInputModel model_;
  std::unique_ptr<sketch::ValueInputOverlay> overlay_;
};

}  // namespace fabricad::part
