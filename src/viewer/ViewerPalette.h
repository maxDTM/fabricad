// FabriCAD viewer: every viewport colour in one plain struct (docs/plans/ui-redesign.md 2.3, 4.5).
// Phase 0 interface: the app fills it from the theme tokens (fromTokens) and hands it to
// ViewerWidget::setViewerPalette(); phase 4 extends it (manipulators, previews, sketch roles, halo).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "theme/Tokens.h"

#include <array>

namespace fabricad::viewer {

struct ViewerPalette {
  using Rgba = std::array<float, 4>;  ///< sRGB 0..1, straight alpha
  Rgba bgTop{}, bgBottom{};           ///< vertical background gradient
  Rgba gridMinor{}, gridMajor{};
  Rgba faceEdge{};                    ///< body face boundaries
  Rgba wire{};                        ///< wireframe display mode
  Rgba preselect{}, select{};         ///< hover and selection highlight
  Rgba cubeBox{}, cubeText{}, cubeInner{}, cubeHover{};
  Rgba overlayText{}, overlayHot{}, overlayHotBg{};  ///< ViewCube controls, messages
  Rgba rubberBand{}, rubberBandFill{};

  static ViewerPalette fromTokens(theme::Theme th);
};

}  // namespace fabricad::viewer
