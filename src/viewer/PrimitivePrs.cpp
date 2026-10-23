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

#include <cmath>
#include <map>

namespace fabricad::viewer {

int dotDiameter(float sizePx) { return std::max(3, static_cast<int>(std::lround(sizePx)) | 1); }

std::vector<std::uint8_t> roundDotBits(int d) {
  d = std::max(1, d | 1);
  const int stride = (d + 7) / 8;
  std::vector<std::uint8_t> bits(static_cast<size_t>(stride * d), 0);
  const double c = (d - 1) / 2.0, r = d / 2.0;
  for (int y = 0; y < d; ++y)
    for (int x = 0; x < d; ++x)
      if ((x - c) * (x - c) + (y - c) * (y - c) <= r * r - 0.25)
        bits[static_cast<size_t>(y * stride + x / 8)] |= static_cast<std::uint8_t>(0x80u >> (x % 8));
  return bits;
}

Handle(TColStd_HArray1OfByte) roundDotBitmap(int d) {
  static std::map<int, Handle(TColStd_HArray1OfByte)> cache;
  d = std::max(1, d | 1);
  Handle(TColStd_HArray1OfByte)& h = cache[d];
  if (h.IsNull()) {
    const std::vector<std::uint8_t> bits = roundDotBits(d);
    h = new TColStd_HArray1OfByte(0, static_cast<int>(bits.size()) - 1);
    for (size_t i = 0; i < bits.size(); ++i) h->SetValue(static_cast<int>(i), bits[i]);
  }
  return h;
}


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
      const int d = dotDiameter(myPointSize);
      Handle(Graphic3d_AspectMarker3d) asp = new Graphic3d_AspectMarker3d(myColorRgba.GetRGB(), d, d, roundDotBitmap(d));
      group->SetGroupPrimitivesAspect(asp);
      group->AddPrimitiveArray(arr);
      break;
    }
  }
}

}  // namespace fabricad::viewer
