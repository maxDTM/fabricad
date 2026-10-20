// FabriCAD viewer: AIS presentation of a manipulator.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "viewer/ManipulatorPrs.h"

#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_ArrayOfTriangles.hxx>
#include <Graphic3d_AspectFillArea3d.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_Group.hxx>
#include <Graphic3d_MaterialAspect.hxx>
#include <Prs3d_ToolCylinder.hxx>
#include <Prs3d_ToolSphere.hxx>
#include <Prs3d_ToolTorus.hxx>
#include <Prs3d_Presentation.hxx>
#include <Quantity_ColorRGBA.hxx>
#include <gp_Trsf.hxx>

namespace fabricad::viewer {

namespace {

constexpr int kSlices = 24;

Quantity_ColorRGBA rgba(const std::array<float, 4>& c) {
  return Quantity_ColorRGBA(Quantity_Color(c[0], c[1], c[2], Quantity_TOC_sRGB), c[3]);
}

Handle(Graphic3d_AspectFillArea3d) shadedAspect(const std::array<float, 4>& color, bool doubleSided) {
  Handle(Graphic3d_AspectFillArea3d) asp = new Graphic3d_AspectFillArea3d();
  const Quantity_ColorRGBA c = rgba(color);
  Graphic3d_MaterialAspect mat(Graphic3d_NameOfMaterial_Plastified);
  mat.SetColor(c.GetRGB());
  mat.SetTransparency(1.0f - c.Alpha());
  asp->SetInteriorStyle(Aspect_IS_SOLID);
  asp->SetInteriorColor(c);
  asp->SetFrontMaterial(mat);
  asp->SetBackMaterial(mat);
  asp->SetFaceCulling(doubleSided ? Graphic3d_TypeOfBackfacingModel_DoubleSided : Graphic3d_TypeOfBackfacingModel_BackCulled);
  if (c.Alpha() < 0.999f) asp->SetAlphaMode(Graphic3d_AlphaMode_Blend);
  return asp;
}

gp_Trsf placement(const gp_Ax3& frame) {
  gp_Trsf t;
  t.SetDisplacement(gp_Ax3(), frame);
  return t;
}

}  // namespace

ManipulatorPrs::ManipulatorPrs() {
  SetInfiniteState(false);
  SetMutable(true);
}

void ManipulatorPrs::Compute(const Handle(PrsMgr_PresentationManager)&, const Handle(Prs3d_Presentation)& thePrs,
                             const Standard_Integer theMode) {
  if (theMode != 0) return;
  using Shape = core::ManipulatorGlyph::Shape;
  for (const core::ManipulatorGlyph& g : myGlyphs) {
    Handle(Graphic3d_Group) group = thePrs->NewGroup();
    switch (g.shape) {
      case Shape::Cylinder:
      case Shape::Cone: {
        const double top = g.shape == Shape::Cone ? 0.0 : g.radius;
        Handle(Graphic3d_ArrayOfTriangles) arr = Prs3d_ToolCylinder::Create(g.radius, top, g.length, kSlices, 1, placement(g.frame));
        group->SetGroupPrimitivesAspect(shadedAspect(g.color, true));
        group->AddPrimitiveArray(arr);
        if (g.shape == Shape::Cone) {
          // closed cone base so the arrow head reads as a solid from behind
          Handle(Graphic3d_ArrayOfTriangles) cap = Prs3d_ToolCylinder::Create(g.radius, g.radius, 1e-6 * std::max(g.length, 1.0), kSlices, 1, placement(g.frame));
          group->AddPrimitiveArray(cap);
        }
        break;
      }
      case Shape::Sphere: {
        Handle(Graphic3d_ArrayOfTriangles) arr = Prs3d_ToolSphere::Create(g.radius, kSlices, kSlices / 2, placement(g.frame));
        group->SetGroupPrimitivesAspect(shadedAspect(g.color, false));
        group->AddPrimitiveArray(arr);
        break;
      }
      case Shape::Torus: {
        Handle(Graphic3d_ArrayOfTriangles) arr = Prs3d_ToolTorus::Create(g.radius, g.radius2, 72, 12, placement(g.frame));
        group->SetGroupPrimitivesAspect(shadedAspect(g.color, false));
        group->AddPrimitiveArray(arr);
        break;
      }
      case Shape::Quad: {
        if (g.points.size() < 4) break;
        Handle(Graphic3d_ArrayOfTriangles) arr = new Graphic3d_ArrayOfTriangles(4, 6);
        for (int i = 0; i < 4; ++i) arr->AddVertex(g.points[static_cast<size_t>(i)].toPnt());
        arr->AddEdges(1, 2, 3);
        arr->AddEdges(1, 3, 4);
        group->SetGroupPrimitivesAspect(shadedAspect(g.color, true));
        group->AddPrimitiveArray(arr);
        // outline
        Handle(Graphic3d_Group) outline = thePrs->NewGroup();
        Handle(Graphic3d_ArrayOfPolylines) line = new Graphic3d_ArrayOfPolylines(5);
        for (int i = 0; i < 4; ++i) line->AddVertex(g.points[static_cast<size_t>(i)].toPnt());
        line->AddVertex(g.points[0].toPnt());
        std::array<float, 4> opaque = g.color;
        opaque[3] = 1.0f;
        outline->SetGroupPrimitivesAspect(new Graphic3d_AspectLine3d(rgba(opaque).GetRGB(), Aspect_TOL_SOLID, 1.5f));
        outline->AddPrimitiveArray(line);
        break;
      }
      case Shape::Segments: {
        const int nseg = static_cast<int>(g.points.size()) / 2;
        if (nseg == 0) break;
        Handle(Graphic3d_ArrayOfSegments) arr = new Graphic3d_ArrayOfSegments(nseg * 2);
        for (int i = 0; i < nseg * 2; ++i) arr->AddVertex(g.points[static_cast<size_t>(i)].toPnt());
        group->SetGroupPrimitivesAspect(new Graphic3d_AspectLine3d(rgba(g.color).GetRGB(), g.dashed ? Aspect_TOL_DASH : Aspect_TOL_SOLID, 1.5f));
        group->AddPrimitiveArray(arr);
        break;
      }
      case Shape::Polyline: {
        if (g.points.size() < 2) break;
        Handle(Graphic3d_ArrayOfPolylines) arr = new Graphic3d_ArrayOfPolylines(static_cast<int>(g.points.size()));
        for (const core::Vec3& p : g.points) arr->AddVertex(p.toPnt());
        group->SetGroupPrimitivesAspect(new Graphic3d_AspectLine3d(rgba(g.color).GetRGB(), g.dashed ? Aspect_TOL_DASH : Aspect_TOL_SOLID, 1.5f));
        group->AddPrimitiveArray(arr);
        break;
      }
    }
  }
}

}  // namespace fabricad::viewer
