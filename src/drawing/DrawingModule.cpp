// FabriCAD drawing module entry point: registers the Drawing-workspace tools. Drawing state
// (sheets, views, dimensions) is not a feature type: DrawingModel attaches itself to a
// Document on first use and persists through Document::extensionData("drawing").
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "drawing/DrawingModule.h"

#include "core/Log.h"
#include "core/Tool.h"
#include "drawing/tools/DrawingTools.h"

namespace fabricad::drawing {

void registerModule(fabricad::core::ModuleContext& ctx) {
  registerDrawingTools(ctx.tools);
  FC_DEBUG("drawing module: {} tools registered", ctx.tools.toolsFor(core::Workspace::Drawing).size());
}

}  // namespace fabricad::drawing
