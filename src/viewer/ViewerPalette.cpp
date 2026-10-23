// FabriCAD viewer: viewport palette from tokens.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "viewer/ViewerPalette.h"

#include <cmath>

namespace fabricad::viewer {

namespace {
bool sameColor(const ViewerPalette::Rgba& a, const ViewerPalette::Rgba& b) {
  for (int i = 0; i < 4; ++i)
    if (std::abs(a[static_cast<size_t>(i)] - b[static_cast<size_t>(i)]) > 1e-4f) return false;
  return true;
}
}  // namespace

namespace {
constexpr float kLightReflectance = 1.25f;
}  // namespace

ViewerPalette ViewerPalette::fromTokens(theme::Theme th) {
  using theme::Tok;
  auto c = [th](Tok t) { return theme::rgba(t, th); };
  const bool light = th == theme::Theme::Light;
  ViewerPalette p;
  p.theme = th;
  p.bgTop = c(Tok::VpBgTop);
  p.bgBottom = c(Tok::VpBgBottom);
  p.gridMinor = c(Tok::VpGridMinor);
  p.gridMajor = c(Tok::VpGridMajor);
  p.light = c(Tok::VpLight);

  p.bodyDefault = c(Tok::BodyDefault);
  p.faceEdge = c(Tok::BodyEdge);
  p.hiddenLine = c(Tok::BodyHiddenLine);
  p.hiddenLineFill = c(Tok::VpBgTop);
  p.wire = c(Tok::TextSecondary);

  p.preselect = c(Tok::PreEdge);
  p.select = c(Tok::SelEdge);
  p.selFill = c(Tok::SelFill);
  p.preFill = withAlpha(c(Tok::PreEdge), 0.16f);
  p.halo = c(Tok::Halo);
  p.selWidth = light ? 2.5f : 2.2f;
  if (light) {
    // P5-LIGHT-7: the light body rendered about 10 L under the mockup (#AAAEB2 lit top for
    // body.default #BFC3C8): more reflectance brings the lit top back to about body.default
    p.matAmbient *= kLightReflectance;
    p.matDiffuse *= kLightReflectance;
  }
  p.preWidth = 1.5f;

  p.live.fill = c(Tok::PreviewLiveFill);
  p.live.edge = c(Tok::PreviewLiveEdge);
  p.live.halo = c(Tok::PreviewLiveHalo);
  p.live.glow = c(Tok::LiveBase);
  p.live.glowOn = true;
  p.live.haloOn = true;
  p.cut.fill = c(Tok::PreviewCutFill);
  p.cut.edge = c(Tok::PreviewCutEdge);
  p.cut.halo = c(Tok::Halo);
  p.cut.dashed = true;
  p.cut.haloOn = true;
  p.newBody.fill = c(Tok::PreviewNewBodyFill);
  p.newBody.edge = c(Tok::PreviewNewBodyEdge);
  p.newBody.halo = c(Tok::Halo);
  p.newBody.haloOn = true;
  p.inference = c(Tok::TextTertiary);
  p.dimLine = c(Tok::DimLine);
  p.dimText = c(Tok::DimText);
  p.dimChip = c(Tok::DimChip);

  p.manipHandle = c(Tok::ManipHandle);
  p.manipOutline = c(Tok::ManipHandleOutline);
  p.manipBright = c(Tok::LiveBright);
  p.manipGuide = c(Tok::TextTertiary);
  p.axis = {c(Tok::AxisX), c(Tok::AxisY), c(Tok::AxisZ)};
  p.onAxis = c(Tok::AxisOnAxis);

  p.skFree = c(Tok::SkFree);
  p.skConstrained = c(Tok::SkConstrained);
  p.skConstruction = c(Tok::SkConstruction);
  p.skProjected = c(Tok::SkProjected);
  p.skFixed = c(Tok::SkFixed);
  p.skConflict = c(Tok::SkConflict);
  p.profileFill = c(Tok::SkProfileFill);
  p.profilePick = c(Tok::SkProfileFillSel);
  p.skGlyph = c(Tok::TextSecondary);  // sk.constraintGlyph: glyph text.secondary

  // construction and origin geometry stay neutral: gold is reserved for live operations (4.2)
  p.construction = c(Tok::SkConstruction);
  p.constructionFill = withAlpha(c(Tok::SkConstruction), 0.22f);
  p.originFill = withAlpha(c(Tok::BorderControl), 0.16f);
  p.originEdge = c(Tok::BorderControl);
  p.originLabel = c(Tok::VpOverlayText);
  p.originPoint = c(Tok::TextSecondary);
  p.joint = c(Tok::TextPrimary);

  p.cubeBox = c(Tok::VpCubeFace);
  p.cubeEdge = c(Tok::VpCubeEdge);
  p.cubeText = c(Tok::TextPrimary);
  p.cubeInner = c(Tok::VpCubeEdge);
  p.cubeHover = c(Tok::AccentSelFill);
  p.overlayText = c(Tok::VpOverlayText);
  p.overlayHot = c(Tok::AccentText);
  p.overlayHotBg = withAlpha(c(Tok::SurfaceOverlay), 0.8f);
  p.rubberBand = c(Tok::VpRubberBand);
  p.rubberBandFill = c(Tok::VpRubberBandFill);
  p.labelBg = c(Tok::SurfaceInput);
  p.labelText = c(Tok::TextPrimary);
  p.labelBorder = c(Tok::LiveBase);
  return p;
}

ViewerPalette::PreviewColors ViewerPalette::preview(const core::PreviewStyle& s) const {
  using core::ColorRole;
  auto plain = [&s](Rgba col) {
    PreviewColors pc;
    pc.fill = col;
    pc.edge = {col[0], col[1], col[2], 1.0f};
    pc.halo = pc.edge;
    pc.glow = pc.edge;
    pc.edgeWidth = s.lineWidth;
    pc.dashed = s.dashed;
    return pc;
  };
  // a preset whose colour the tool changed keeps that colour (neutral roles only)
  auto preset = [](ColorRole r) {
    switch (r) {
      case ColorRole::SketchFree: return core::PreviewStyle::sketchLine().color;
      case ColorRole::Inference: return core::PreviewStyle::inference().color;
      default: return Rgba{};
    }
  };
  switch (s.role) {
    case ColorRole::Live: return live;
    case ColorRole::Cut: return cut;
    case ColorRole::NewBody: return newBody;
    case ColorRole::SketchFree:
      if (!sameColor(s.color, preset(s.role))) return plain(s.color);
      return plain(skFree);
    case ColorRole::Inference:
      if (!sameColor(s.color, preset(s.role))) return plain(s.color);
      return plain(inference);
    case ColorRole::Dimension: return plain(dimLine);
    case ColorRole::SketchConstrained: return plain(skConstrained);
    case ColorRole::SketchConstruction: return plain(skConstruction);
    case ColorRole::SketchProjected: return plain(skProjected);
    case ColorRole::SketchFixed: return plain(skFixed);
    case ColorRole::SketchConflict: return plain(skConflict);
    case ColorRole::SketchSelected: return plain(select);
    case ColorRole::SketchGlyph: return plain(skGlyph);
    case ColorRole::SketchProfile: {
      PreviewColors pc = plain(profileFill);
      pc.fill = profileFill;  // tint only: the curves are the boundary
      return pc;
    }
    case ColorRole::SketchOrigin: return plain(originPoint);
    case ColorRole::Preselect: return plain(preselect);
    case ColorRole::None: break;
  }
  return plain(s.color);
}

ViewerPalette::Rgba ViewerPalette::manipulatorColor(core::ManipulatorKind kind, core::ManipulatorHandle h, bool highlighted,
                                                    bool guide) const {
  using core::ManipulatorHandle;
  if (guide) return manipGuide;
  if (highlighted) return manipBright;
  if (kind != core::ManipulatorKind::Triad) return manipHandle;
  switch (h) {
    case ManipulatorHandle::MoveX:
    case ManipulatorHandle::RotX: return axis[0];
    case ManipulatorHandle::MoveY:
    case ManipulatorHandle::RotY: return axis[1];
    case ManipulatorHandle::MoveZ:
    case ManipulatorHandle::RotZ: return axis[2];
    default: return withAlpha(manipHandle, 0.45f);  // plane squares, translucent gold
  }
}

}  // namespace fabricad::viewer
