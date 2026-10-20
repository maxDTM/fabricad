// FabriCAD sketch module entry point. OWNER: sketch agent.
// Keep this header's signature stable: the app calls registerModule() at startup.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Registry.h"

namespace fabricad::sketch {

/// Registers the "Sketch" feature type and all sketch tools ("sketch.*").
void registerModule(fabricad::core::ModuleContext& ctx);

}  // namespace fabricad::sketch
