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
  Handle(Graphic3d_Group) group = thePrs->NewGroup();
  const int n = static_cast<int>(myPoints.size());
  switch (myKind) {
    case Kind::Segments: {
      const int nseg = n / 2;
      if (nseg == 0) return;
      Handle(Graphic3d_ArrayOfSegments) arr = new Graphic3d_ArrayOfSegments(nseg * 2);
      for (int i = 0; i < nseg * 2; ++i) arr->AddVertex(myPoints[static_cast<size_t>(i)]);
      Handle(Graphic3d_AspectLine3d) asp =
          new Graphic3d_AspectLine3d(myColorRgba.GetRGB(), myDashed ? Aspect_TOL_DASH : Aspect_TOL_SOLID, myWidth);
      group->SetGroupPrimitivesAspect(asp);
      group->AddPrimitiveArray(arr);
      break;
    }
    case Kind::Polyline: {
      if (n < 2) return;
      Handle(Graphic3d_ArrayOfPolylines) arr = new Graphic3d_ArrayOfPolylines(n);
      for (const gp_Pnt& p : myPoints) arr->AddVertex(p);
      Handle(Graphic3d_AspectLine3d) asp =
          new Graphic3d_AspectLine3d(myColorRgba.GetRGB(), myDashed ? Aspect_TOL_DASH : Aspect_TOL_SOLID, myWidth);
      group->SetGroupPrimitivesAspect(asp);
      group->AddPrimitiveArray(arr);
      break;
    }
    case Kind::Points: {
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
