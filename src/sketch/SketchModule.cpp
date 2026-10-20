// FabriCAD sketch module entry point: registers the "Sketch" feature type and all sketch tools.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sketch/SketchModule.h"

#include "core/Log.h"
#include "core/Registry.h"
#include "core/Tool.h"
#include "sketch/SketchFeature.h"
#include "sketch/tools/SketchTools.h"

#include <memory>

namespace fabricad::sketch {

void registerModule(fabricad::core::ModuleContext& ctx) {
  ctx.features.registerType(SketchFeature::kType, [] { return std::make_unique<SketchFeature>(); });
  registerSketchTools(ctx.tools);
  FC_DEBUG("sketch module: registered feature '{}' and {} tools", SketchFeature::kType,
           ctx.tools.toolsFor(core::Workspace::Design, "SKETCH").size());
}

}  // namespace fabricad::sketch
