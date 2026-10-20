// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/PartModule.h"

#include "core/Log.h"
#include "core/Registry.h"
#include "part/features/Construction.h"
#include "part/features/Extrude.h"
#include "part/features/Fillet.h"
#include "part/features/Hole.h"
#include "part/features/Import.h"
#include "part/features/Pattern.h"
#include "part/features/Revolve.h"
#include "part/features/Shell.h"
#include "part/features/SweepLoft.h"
#include "part/tools/PartTools.h"

#include <memory>

namespace fabricad::part {

namespace {
template <class F>
void reg(core::FeatureFactory& f) {
  f.registerType(F::kType, [] { return std::make_unique<F>(); });
}
}  // namespace

void registerFeatureTypes(core::FeatureFactory& f) {
  reg<ExtrudeFeature>(f);
  reg<RevolveFeature>(f);
  reg<SweepFeature>(f);
  reg<LoftFeature>(f);
  reg<FilletFeature>(f);
  reg<ChamferFeature>(f);
  reg<ShellFeature>(f);
  reg<DraftFeature>(f);
  reg<OffsetFaceFeature>(f);
  reg<HoleFeature>(f);
  reg<RectangularPatternFeature>(f);
  reg<CircularPatternFeature>(f);
  reg<MirrorFeature>(f);
  reg<CombineFeature>(f);
  reg<MoveBodyFeature>(f);
  reg<SplitBodyFeature>(f);
  reg<ImportFeature>(f);
  reg<OffsetPlaneFeature>(f);
  reg<PlaneAtAngleFeature>(f);
  reg<TangentPlaneFeature>(f);
  reg<MidplaneFeature>(f);
  reg<PlaneThreePointsFeature>(f);
  reg<AxisFeature>(f);
  reg<PointFeature>(f);
}

void registerModule(fabricad::core::ModuleContext& ctx) {
  registerFeatureTypes(ctx.features);
  registerPartTools(ctx);
  FC_DEBUG("part module: feature types and tools registered");
}

}  // namespace fabricad::part
