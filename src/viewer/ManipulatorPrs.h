// FabriCAD viewer: AIS presentation of one on-canvas manipulator (core::ManipulatorGlyph list).
// Shaded arrows (cylinder + cone), rings (torus), knobs (sphere), translucent plane squares
// and dashed guide lines, rebuilt from the glyph list whenever the gizmo moves, is hovered or
// dragged. It is never selectable by AIS: picking is done by core::ManipulatorController on
// rays, so the same hit logic runs headless.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Manipulator.h"

#include <AIS_InteractiveObject.hxx>

#include <array>
#include <vector>

namespace fabricad::viewer {

class ManipulatorPrs : public AIS_InteractiveObject {
  DEFINE_STANDARD_RTTI_INLINE(ManipulatorPrs, AIS_InteractiveObject)
 public:
  ManipulatorPrs();

  void setGlyphs(std::vector<core::ManipulatorGlyph> glyphs) { myGlyphs = std::move(glyphs); }

  /// Two-tone look (docs/plans/ui-redesign.md 5.3 drag handle): a dark ink outline around the
  /// shaded handles (an inverted hull `outlineWidth` world units thick), a faint glow shell around
  /// the hovered / dragged handle, and a diamond at the base of a distance arrow.
  struct Style {
    std::array<float, 4> outline{};
    std::array<float, 4> glow{};
    std::array<float, 4> diamond{};
    double outlineWidth = 0.0;  ///< world units (about 1 px)
    double glowWidth = 0.0;     ///< world units (about 6 px)
    core::ManipulatorHandle glowHandle = core::ManipulatorHandle::None;
    bool baseDiamond = false;
  };
  void setStyle(const Style& s) { myStyle = s; }
  const std::vector<core::ManipulatorGlyph>& glyphs() const { return myGlyphs; }

  bool AcceptDisplayMode(const Standard_Integer theMode) const override { return theMode == 0; }

 protected:
  void Compute(const Handle(PrsMgr_PresentationManager)& thePrsMgr, const Handle(Prs3d_Presentation)& thePrs,
               const Standard_Integer theMode) override;
  void ComputeSelection(const Handle(SelectMgr_Selection)&, const Standard_Integer) override {}

 private:
  std::vector<core::ManipulatorGlyph> myGlyphs;
  Style myStyle;
};

}  // namespace fabricad::viewer
