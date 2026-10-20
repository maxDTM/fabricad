// FabriCAD viewer: AIS presentation of one on-canvas manipulator (core::ManipulatorGlyph list).
// Shaded arrows (cylinder + cone), rings (torus), knobs (sphere), translucent plane squares
// and dashed guide lines, rebuilt from the glyph list whenever the gizmo moves, is hovered or
// dragged. It is never selectable by AIS: picking is done by core::ManipulatorController on
// rays, so the same hit logic runs headless.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Manipulator.h"

#include <AIS_InteractiveObject.hxx>

#include <vector>

namespace fabricad::viewer {

class ManipulatorPrs : public AIS_InteractiveObject {
  DEFINE_STANDARD_RTTI_INLINE(ManipulatorPrs, AIS_InteractiveObject)
 public:
  ManipulatorPrs();

  void setGlyphs(std::vector<core::ManipulatorGlyph> glyphs) { myGlyphs = std::move(glyphs); }
  const std::vector<core::ManipulatorGlyph>& glyphs() const { return myGlyphs; }

  bool AcceptDisplayMode(const Standard_Integer theMode) const override { return theMode == 0; }

 protected:
  void Compute(const Handle(PrsMgr_PresentationManager)& thePrsMgr, const Handle(Prs3d_Presentation)& thePrs,
               const Standard_Integer theMode) override;
  void ComputeSelection(const Handle(SelectMgr_Selection)&, const Standard_Integer) override {}

 private:
  std::vector<core::ManipulatorGlyph> myGlyphs;
};

}  // namespace fabricad::viewer
