// FabriCAD part: on-canvas manipulators of the feature dialogs (Fusion 360 style).
//
// A binding ties one numeric property of a feature to a core::ManipulatorSpec placed on the
// geometry: Extrude distance arrows at the profile (one per side) plus taper rings on the end
// faces, Revolve angle rings about the axis, Fillet/Chamfer radius arrows at the first edge,
// Shell thickness, Offset Face / Press Pull distance, Offset Plane distance and Hole depth
// arrows. FeatureTool asks manipulatorsFor() after every preview, shows/updates the gizmos and
// writes dragged values back into the property (dialog <-> gizmo both ways). Providers resolve
// references through the RecomputeContext the preview used, so everything is headless.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Manipulator.h"

#include <gp_Pnt.hxx>

#include <functional>
#include <string>
#include <vector>

namespace fabricad::core {
class RecomputeContext;
}

namespace fabricad::part {

class PartFeature;

struct ManipulatorBinding {
  std::string property;        ///< numeric property the handle drives ("distance", "angle", "radius"...)
  core::ManipulatorSpec spec;  ///< spec.value is already property * scale
  double scale = 1.0;          ///< handle value = property value * scale (0.5: symmetric whole length)
};

/// World size of one pixel at a point (IViewerContext::pixelSize); gizmo sizes are chosen from it.
using PixelSizeFn = std::function<double(const gp_Pnt&)>;
using ManipulatorProvider = std::function<std::vector<ManipulatorBinding>(PartFeature&, core::RecomputeContext&, const PixelSizeFn&)>;

/// Bindings for the feature's current inputs (empty when the geometry they anchor to is missing).
std::vector<ManipulatorBinding> manipulatorsFor(PartFeature& feature, core::RecomputeContext& ctx, const PixelSizeFn& pixelSize);
/// Register (or replace) the provider of a feature type. The built-in ones are installed on first use.
void registerManipulatorProvider(const std::string& featureType, ManipulatorProvider provider);
bool hasManipulatorProvider(const std::string& featureType);

}  // namespace fabricad::part
