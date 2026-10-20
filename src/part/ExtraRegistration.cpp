// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/ExtraRegistration.h"

#include "core/Registry.h"
#include "core/Tool.h"
#include "part/features/BodyOps.h"
#include "part/features/Primitives.h"
#include "part/features/Thread.h"
#include "part/tools/FeatureTool.h"

#include <memory>

namespace fabricad::part {

namespace {

template <class F>
void reg(core::FeatureFactory& f) {
  f.registerType(F::kType, [] { return std::make_unique<F>(); });
}

template <class F>
void tool(core::ToolRegistry& t, const std::string& id, const std::string& name, const std::string& tooltip,
          const std::string& group, int order, std::vector<std::string> aliases) {
  core::ToolInfo i;
  i.id = id;
  i.name = name;
  i.tooltip = tooltip;
  i.icon = id;
  i.aliases = std::move(aliases);
  i.workspace = core::Workspace::Design;
  i.tab = "SOLID";
  i.group = group;
  i.order = order;
  i.promoted = false;
  t.registerTool(std::make_unique<FeatureTool>(std::move(i), F::kType, [] { return std::make_unique<F>(); }));
}

}  // namespace

void registerExtraFeatureTypes(core::FeatureFactory& f) {
  reg<BoxFeature>(f);
  reg<CylinderFeature>(f);
  reg<SphereFeature>(f);
  reg<TorusFeature>(f);
  reg<CoilFeature>(f);
  reg<ThreadFeature>(f);
  reg<RibFeature>(f);
  reg<ThickenFeature>(f);
  reg<ScaleBodyFeature>(f);
  reg<SplitFaceFeature>(f);
}

void registerExtraPartTools(core::ToolRegistry& t) {
  const std::string CREATE = "CREATE", MODIFY = "MODIFY";
  tool<RibFeature>(t, "part.rib", "Rib", "Thin wall from an open sketch curve to the next body", CREATE, 55, {"RIB"});
  tool<ThreadFeature>(t, "part.thread", "Thread", "ISO metric thread on a cylindrical face (cosmetic or modelled)", CREATE,
                      65, {"THREAD"});
  tool<BoxFeature>(t, "part.box", "Box", "Box primitive on a plane", CREATE, 80, {"BOX"});
  tool<CylinderFeature>(t, "part.cylinder", "Cylinder", "Cylinder primitive on a plane", CREATE, 81, {"CYLINDER", "CYL"});
  tool<SphereFeature>(t, "part.sphere", "Sphere", "Sphere primitive centred on a plane", CREATE, 82, {"SPHERE"});
  tool<TorusFeature>(t, "part.torus", "Torus", "Torus primitive on a plane", CREATE, 83, {"TORUS"});
  tool<CoilFeature>(t, "part.coil", "Coil", "Helical coil (spring) on a plane", CREATE, 84, {"COIL", "HELIX", "SPRING"});
  tool<ThickenFeature>(t, "part.thicken", "Thicken", "Give faces a thickness to make a solid", CREATE, 90, {"THICKEN"});
  tool<ScaleBodyFeature>(t, "part.scale", "Scale", "Scale bodies uniformly or per axis about a point", MODIFY, 55,
                         {"SCALE", "SC"});
  tool<SplitFaceFeature>(t, "part.split-face", "Split Face", "Split faces with a plane, face or body", MODIFY, 61,
                         {"SPLITFACE"});
}

}  // namespace fabricad::part
