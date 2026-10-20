// FabriCAD drawing module entry point. OWNER: drawing agent.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Registry.h"

namespace fabricad::drawing {

/// Registers drawing tools ("drawing.*", Workspace::Drawing). Drawing sheets are stored
/// in Document::extensionData("drawing") (see docs/ARCHITECTURE.md).
void registerModule(fabricad::core::ModuleContext& ctx);

}  // namespace fabricad::drawing
