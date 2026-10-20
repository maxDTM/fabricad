// FabriCAD part: tool registration (toolbar placement, shortcuts, aliases) and the
// instant import/export tools.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Registry.h"

#include <string>
#include <vector>

namespace fabricad::core {
class Document;
class ToolContext;
}  // namespace fabricad::core

namespace fabricad::part {

/// Registers every part/construction tool with `ctx.tools` (feature types must already be registered).
void registerPartTools(core::ModuleContext& ctx);

/// Export the selected bodies (or all live bodies) of the document. Path from args[0] when
/// given, otherwise a save dialog (needs a main window). Returns false on failure/cancel.
bool exportBodies(core::ToolContext& ctx, const std::string& format);

}  // namespace fabricad::part
