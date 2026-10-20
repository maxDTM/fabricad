// FabriCAD viewer: lightweight AIS object drawing raw segments / polylines / points with
// Graphic3d primitive arrays (no TopoDS edges), used for tool previews (rubber bands,
// inference lines) that are rebuilt at mouse-move rate.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <AIS_InteractiveObject.hxx>
#include <Aspect_TypeOfLine.hxx>
#include <Quantity_ColorRGBA.hxx>
#include <gp_Pnt.hxx>

#include <vector>

namespace fabricad::viewer {

class PrimitivePrs : public AIS_InteractiveObject {
  DEFINE_STANDARD_RTTI_INLINE(PrimitivePrs, AIS_InteractiveObject)
 public:
  enum class Kind { Segments, Polyline, Points };

  PrimitivePrs(Kind kind, const Quantity_ColorRGBA& color, float width, bool dashed, float pointSize);

  void setPoints(std::vector<gp_Pnt> pts) { myPoints = std::move(pts); }
  const std::vector<gp_Pnt>& points() const { return myPoints; }
  Kind kind() const { return myKind; }

  bool AcceptDisplayMode(const Standard_Integer theMode) const override { return theMode == 0; }

 protected:
  void Compute(const Handle(PrsMgr_PresentationManager)& thePrsMgr, const Handle(Prs3d_Presentation)& thePrs,
               const Standard_Integer theMode) override;
  void ComputeSelection(const Handle(SelectMgr_Selection)&, const Standard_Integer) override {}

 private:
  Kind myKind;
  Quantity_ColorRGBA myColorRgba;
  float myWidth;
  bool myDashed;
  float myPointSize;
  std::vector<gp_Pnt> myPoints;
};

}  // namespace fabricad::viewer
