// FabriCAD viewer: viewport palette from tokens.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "viewer/ViewerPalette.h"

namespace fabricad::viewer {

ViewerPalette ViewerPalette::fromTokens(theme::Theme th) {
  using theme::Tok;
  auto c = [th](Tok t) { return theme::rgba(t, th); };
  ViewerPalette p;
  p.bgTop = c(Tok::VpBgTop);
  p.bgBottom = c(Tok::VpBgBottom);
  p.gridMinor = c(Tok::VpGridMinor);
  p.gridMajor = c(Tok::VpGridMajor);
  p.faceEdge = c(Tok::BodyEdge);
  p.wire = c(Tok::TextSecondary);
  p.preselect = c(Tok::PreEdge);
  p.select = c(Tok::SelEdge);
  p.cubeBox = c(Tok::SurfaceOverlay);
  p.cubeText = c(Tok::TextPrimary);
  p.cubeInner = c(Tok::BorderControl);
  p.cubeHover = c(Tok::AccentSelFill);
  p.overlayText = c(Tok::VpOverlayText);
  p.overlayHot = c(Tok::AccentText);
  p.overlayHotBg = c(Tok::SurfaceOverlay);
  p.overlayHotBg[3] = 0.8f;
  p.rubberBand = c(Tok::VpRubberBand);
  p.rubberBandFill = c(Tok::VpRubberBandFill);
  return p;
}

}  // namespace fabricad::viewer
