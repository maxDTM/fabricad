// FabriCAD part module entry point. OWNER: part agent.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Registry.h"

namespace fabricad::part {

/// Registers part feature types (Extrude, Revolve, Fillet, ..., construction geometry,
/// Import) and their tools ("part.*", "construct.*").
void registerModule(fabricad::core::ModuleContext& ctx);

/// Feature types only (headless use / tests without tools).
void registerFeatureTypes(fabricad::core::FeatureFactory& factory);

}  // namespace fabricad::part
