// FabriCAD part: tool registration (toolbar placement, shortcuts, aliases) and the
// instant import/export tools.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Registry.h"

#include "part/io/MeshExport.h"
#include "part/io/StepIO.h"

#include <TopoDS_Shape.hxx>

#include <map>

#include <string>
#include <vector>

namespace fabricad::core {
class Document;
class ToolContext;
}  // namespace fabricad::core

namespace fabricad::part {

/// Registers every part/construction tool with `ctx.tools` (feature types must already be registered).
void registerPartTools(core::ModuleContext& ctx);

/// The shapes an export writes: the selected bodies (or every visible body) moved into their
/// component's world placement, with the body names. False (with a message) when there is nothing.
bool collectExportShapes(core::ToolContext& ctx, std::vector<TopoDS_Shape>& shapes, std::vector<std::string>& names);
/// Same bodies as collectExportShapes with their colour and component name; `properties` (optional)
/// gets the owning component's part number / description / material by body name.
bool collectExportItems(core::ToolContext& ctx, std::vector<io::MeshItem>& items,
                        std::map<std::string, io::ProductProperties>* properties = nullptr);

/// Export the selected bodies (or all live bodies) of the document. Path from the first
/// non-option argument when given, otherwise a save dialog (needs a main window). STL options
/// (`deflection=`, `angle=`, `ascii`) as in ExportStlTool. Returns false on failure/cancel.
bool exportBodies(core::ToolContext& ctx, const std::string& format);

}  // namespace fabricad::part
