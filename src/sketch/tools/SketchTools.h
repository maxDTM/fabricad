// FabriCAD sketch tools: registration of every "sketch.*" tool and shared entry points.
//
// Tool ids (tab SKETCH unless noted):
//   mode      sketch.create (SOLID/CREATE), sketch.edit (hidden), sketch.finish (FINISH SKETCH),
//             sketch.select (hidden idle tool), sketch.look-at, sketch.toggle-* (OPTIONS)
//   CREATE    sketch.line L, sketch.rectangle R, sketch.rectangle-3point, sketch.rectangle-center,
//             sketch.circle C, sketch.circle-2point, sketch.circle-3point, sketch.arc-3point,
//             sketch.arc-center, sketch.arc-tangent, sketch.polygon, sketch.polygon-inscribed,
//             sketch.slot, sketch.point, sketch.mirror, sketch.project P, sketch.dimension D
//   MODIFY    sketch.fillet, sketch.trim T, sketch.extend, sketch.break, sketch.offset O,
//             sketch.construction X, sketch.dimension-edit (hidden)
//   CONSTRAINTS sketch.constraint.<horizontal-vertical|coincident|tangent|equal|parallel|
//             perpendicular|fix|midpoint|concentric|collinear|symmetric>
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/ShapeRef.h"
#include "core/Tool.h"
#include "core/Types.h"

namespace fabricad::sketch {

void registerSketchTools(core::ToolRegistry& registry);
void registerModeTools(core::ToolRegistry& registry);
void registerCreateTools(core::ToolRegistry& registry);
void registerModifyTools(core::ToolRegistry& registry);
void registerConstraintTools(core::ToolRegistry& registry);

/// Create a Sketch feature on `plane` (an origin plane, construction plane or planar face
/// ref), add it at the timeline marker with undo, and enter sketch mode on it.
/// Returns the new feature id or kInvalidId (message shown) when the plane cannot be resolved.
core::ObjectId createSketch(core::ToolContext& ctx, const core::ShapeRef& plane);

}  // namespace fabricad::sketch
