// FabriCAD part module: registration of the round-2 feature types and tools (primitives, coil,
// thread, rib, thicken, scale, split face). Kept apart from PartModule.cpp / PartTools.cpp so
// the registration lists of the round-1 features stay untouched.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

namespace fabricad::core {
class FeatureFactory;
class ToolRegistry;
}  // namespace fabricad::core

namespace fabricad::part {

void registerExtraFeatureTypes(core::FeatureFactory& f);
void registerExtraPartTools(core::ToolRegistry& t);

}  // namespace fabricad::part
