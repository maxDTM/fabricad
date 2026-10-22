// FabriCAD viewer: lightweight primitive presentation.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "viewer/PrimitivePrs.h"

#include <Graphic3d_ArrayOfPoints.hxx>
#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_AspectMarker3d.hxx>
#include <Graphic3d_Group.hxx>
#include <Prs3d_Presentation.hxx>

namespace fabricad::viewer {

PrimitivePrs::PrimitivePrs(Kind kind, const Quantity_ColorRGBA& color, float width, bool dashed, float pointSize)
    : myKind(kind), myColorRgba(color), myWidth(width), myDashed(dashed), myPointSize(pointSize) {
  SetInfiniteState(false);
}

void PrimitivePrs::Compute(const Handle(PrsMgr_PresentationManager)&, const Handle(Prs3d_Presentation)& thePrs,
                           const Standard_Integer theMode) {
  if (theMode != 0 || myPoints.empty()) return;
  const int n = static_cast<int>(myPoints.size());
  auto lines = [&](const Quantity_ColorRGBA& color, float width, bool dashed) {
    Handle(Graphic3d_Group) group = thePrs->NewGroup();
    Handle(Graphic3d_ArrayOfPrimitives) arr;
    if (myKind == Kind::Segments) {
      const int nseg = n / 2;
      if (nseg == 0) return;
      arr = new Graphic3d_ArrayOfSegments(nseg * 2);
      for (int i = 0; i < nseg * 2; ++i) arr->AddVertex(myPoints[static_cast<size_t>(i)]);
    } else {
      if (n < 2) return;
      arr = new Graphic3d_ArrayOfPolylines(n);
      for (const gp_Pnt& p : myPoints) arr->AddVertex(p);
    }
    Handle(Graphic3d_AspectLine3d) asp = new Graphic3d_AspectLine3d(color.GetRGB(), dashed ? Aspect_TOL_DASH : Aspect_TOL_SOLID, width);
    group->SetGroupPrimitivesAspect(asp);
    group->AddPrimitiveArray(arr);
  };
  switch (myKind) {
    case Kind::Segments:
    case Kind::Polyline:
      // the halo goes first so the line is drawn over it (same depth, depth test less-or-equal)
      if (myHaloExtra > 0.0f) lines(myHaloColor, myWidth + 2.0f * myHaloExtra, false);
      lines(myColorRgba, myWidth, myDashed);
      break;
    case Kind::Points: {
      Handle(Graphic3d_Group) group = thePrs->NewGroup();
      Handle(Graphic3d_ArrayOfPoints) arr = new Graphic3d_ArrayOfPoints(n);
      for (const gp_Pnt& p : myPoints) arr->AddVertex(p);
      Handle(Graphic3d_AspectMarker3d) asp =
          new Graphic3d_AspectMarker3d(Aspect_TOM_BALL, myColorRgba.GetRGB(), myPointSize / 5.0);
      group->SetGroupPrimitivesAspect(asp);
      group->AddPrimitiveArray(arr);
      break;
    }
  }
}

}  // namespace fabricad::viewer
