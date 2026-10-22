// FabriCAD viewer: every viewport colour in one plain struct (docs/plans/ui-redesign.md 2.3, 4.5).
// The app fills it from the theme tokens (fromTokens) and hands it to
// ViewerWidget::setViewerPalette(); the viewer then re-styles OCCT (background gradient, grid,
// highlight drawers, ViewCube, body material and edges, previews, manipulators) and redisplays.
// The viewer holds no colour literals: everything below comes from resources/theme/tokens.json.
//
// Rules carried here so the drawing code stays simple:
//   * halo rule (4.5): an accent line drawn over shaded bodies (selection, preselection, live
//     previews) gets a 1 px halo on each side (`halo`, `haloWidth`);
//   * two-tone live previews: fill + bright edge + dark halo + a wide, faint glow stroke;
//   * axis colours always travel with their letters (dial, triad, readout ticks).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/ViewerContext.h"
#include "theme/Tokens.h"

#include <array>

namespace fabricad::viewer {

struct ViewerPalette {
  using Rgba = std::array<float, 4>;  ///< sRGB 0..1, straight alpha

  theme::Theme theme = theme::Theme::Dark;  ///< Qt overlays (dial, bars, readouts) read tokens of this theme

  // --- background, grid, lights
  Rgba bgTop{}, bgBottom{};  ///< vertical background gradient
  Rgba gridMinor{}, gridMajor{};
  Rgba light{};  ///< head, key and ambient light colour

  // --- bodies
  Rgba bodyDefault{};     ///< a body with the document's default colour is drawn in this one
  Rgba faceEdge{};        ///< body face boundaries
  Rgba hiddenLine{};      ///< hidden lines (HLR modes)
  Rgba hiddenLineFill{};  ///< unlit fill of the hidden-line mode (the viewport background)
  Rgba wire{};            ///< wireframe display mode
  float ghostAlpha = 0.25f;  ///< ghosted components, rolled-back preview
  /// Material multipliers (reflectance, not colours): ambient, diffuse, specular.
  float matAmbient = 0.32f, matDiffuse = 0.62f, matSpecular = 0.18f;

  // --- selection and preselection (halo rule)
  Rgba preselect{}, select{};  ///< hover and selection edge colours
  Rgba selFill{};              ///< selected face tint (alpha carried in the token)
  Rgba preFill{};              ///< hovered face tint
  Rgba halo{};                 ///< 1 px halo under accent lines over bodies
  float selWidth = 2.2f, preWidth = 1.5f, haloWidth = 1.0f;  ///< halo: extra px on each side

  // --- previews by core::ColorRole
  struct PreviewColors {
    Rgba fill{}, edge{}, halo{}, glow{};
    float edgeWidth = 1.8f;
    bool dashed = false;
    bool glowOn = false;  ///< draw the wide, faint glow stroke (live previews only)
    bool haloOn = false;  ///< draw the 1 px halo under the edges
  };
  PreviewColors live, cut, newBody;
  Rgba inference{}, dimLine{}, dimText{}, dimChip{};
  float glowWidth = 6.0f, glowAlpha = 0.35f;

  // --- manipulators
  Rgba manipHandle{}, manipOutline{}, manipBright{}, manipGuide{};
  std::array<Rgba, 3> axis{};  ///< X, Y, Z
  Rgba onAxis{};               ///< letters on axis squares
  float originAxisAlpha = 0.7f;

  // --- sketch (finished sketches; the open sketch session draws its own previews)
  Rgba skFree{}, skConstrained{}, skConstruction{}, skProjected{}, skFixed{}, skConflict{};
  Rgba profileFill{};  ///< closed profiles of visible, finished sketches (SK-12)
  Rgba profilePick{};  ///< profiles while a tool picks them (sk.profileFillSel)

  // --- construction and origin geometry
  Rgba construction{}, constructionFill{}, originFill{}, originEdge{}, originLabel{}, originPoint{}, joint{};

  // --- ViewCube and Qt overlays painted on the GL view
  Rgba cubeBox{}, cubeEdge{}, cubeText{}, cubeInner{}, cubeHover{};
  Rgba overlayText{}, overlayHot{}, overlayHotBg{};  ///< ViewCube controls, messages
  Rgba rubberBand{}, rubberBandFill{};
  Rgba labelBg{}, labelText{}, labelBorder{};  ///< manipulator value chip

  static ViewerPalette fromTokens(theme::Theme th);

  /// The colours a preview with `style` is drawn with: the role's palette entry, or the style's
  /// own colour (role None, or a SketchFree/Inference/Dimension preset whose colour was changed).
  PreviewColors preview(const core::PreviewStyle& style) const;
  /// Manipulator glyph colour: axis colours for triad handles (shown with their letters), gold for
  /// distance/angle/point handles, live.bright when hovered or dragged, neutral for guides.
  Rgba manipulatorColor(core::ManipulatorKind kind, core::ManipulatorHandle handle, bool highlighted, bool guide) const;
  /// `c` with alpha multiplied by `a`.
  static Rgba withAlpha(Rgba c, float a) {
    c[3] *= a;
    return c;
  }
};

}  // namespace fabricad::viewer
