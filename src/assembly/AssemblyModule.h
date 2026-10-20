// FabriCAD assembly module entry point. OWNER: assembly agent.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Registry.h"

namespace fabricad::assembly {

/// Registers Joint/JointOrigin/RigidGroup feature types and tools ("assembly.*").
void registerModule(fabricad::core::ModuleContext& ctx);

}  // namespace fabricad::assembly
