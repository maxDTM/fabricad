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
#include <gp_Vec.hxx>

#include <algorithm>

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

/// Unlit, front-culled shell: seen only around the silhouette of the glyph inside it.
Handle(Graphic3d_AspectFillArea3d) hullAspect(const std::array<float, 4>& color) {
  Handle(Graphic3d_AspectFillArea3d) asp = shadedAspect(color, false);
  asp->SetShadingModel(Graphic3d_TypeOfShadingModel_Unlit);
  asp->SetFaceCulling(Graphic3d_TypeOfBackfacingModel_FrontCulled);
  return asp;
}

/// Octahedron (diamond) of half-size `s` in `frame`.
Handle(Graphic3d_ArrayOfTriangles) octahedron(const gp_Ax3& frame, double s) {
  const gp_Pnt o = frame.Location();
  const gp_Vec x(frame.XDirection()), y(frame.YDirection()), z(frame.Direction());
  const gp_Pnt v[6] = {o.Translated(x * s), o.Translated(-x * s), o.Translated(y * s), o.Translated(-y * s), o.Translated(z * s),
                       o.Translated(-z * s)};
  Handle(Graphic3d_ArrayOfTriangles) arr = new Graphic3d_ArrayOfTriangles(24, 0, Graphic3d_ArrayFlags_VertexNormal);
  const int faces[8][3] = {{0, 2, 4}, {2, 1, 4}, {1, 3, 4}, {3, 0, 4}, {2, 0, 5}, {1, 2, 5}, {3, 1, 5}, {0, 3, 5}};
  for (const auto& f : faces) {
    const gp_Vec n = gp_Vec(v[f[0]], v[f[1]]).Crossed(gp_Vec(v[f[0]], v[f[2]]));
    const gp_Dir nd = n.Magnitude() > 1e-12 ? gp_Dir(n) : gp_Dir(z);
    for (int k = 0; k < 3; ++k) arr->AddVertex(v[f[k]], nd);
  }
  return arr;
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
  // shells first (outline, glow), then the glyphs
  const double ow = myStyle.outlineWidth;
  auto shell = [&](const core::ManipulatorGlyph& g, double grow, const std::array<float, 4>& color) {
    if (grow <= 0.0 || color[3] <= 0.0f) return;
    Handle(Graphic3d_Group) group = thePrs->NewGroup();
    group->SetGroupPrimitivesAspect(hullAspect(color));
    switch (g.shape) {
      case Shape::Cylinder:
      case Shape::Cone: {
        const double top = g.shape == Shape::Cone ? grow * 0.6 : g.radius + grow;
        gp_Ax3 f = g.frame;
        f.SetLocation(f.Location().Translated(gp_Vec(f.Direction()) * -grow));
        group->AddPrimitiveArray(Prs3d_ToolCylinder::Create(g.radius + grow, top, g.length + 2 * grow, kSlices, 1, placement(f)));
        break;
      }
      case Shape::Sphere:
        group->AddPrimitiveArray(Prs3d_ToolSphere::Create(g.radius + grow, kSlices, kSlices / 2, placement(g.frame)));
        break;
      case Shape::Torus:
        group->AddPrimitiveArray(Prs3d_ToolTorus::Create(g.radius, g.radius2 + grow, 72, 12, placement(g.frame)));
        break;
      default: break;
    }
  };
  const core::ManipulatorGlyph* shaft = nullptr;
  for (const core::ManipulatorGlyph& g : myGlyphs) {
    if (g.shape == Shape::Cylinder && !shaft) shaft = &g;
    const bool solid = g.shape == Shape::Cylinder || g.shape == Shape::Cone || g.shape == Shape::Sphere || g.shape == Shape::Torus;
    if (!solid) continue;
    if (myStyle.glowHandle != core::ManipulatorHandle::None && g.handle == myStyle.glowHandle) shell(g, myStyle.glowWidth, myStyle.glow);
    shell(g, ow, myStyle.outline);
  }
  if (myStyle.baseDiamond && shaft) {
    const double s = shaft->radius * 2.6;
    Handle(Graphic3d_Group) hull = thePrs->NewGroup();
    hull->SetGroupPrimitivesAspect(hullAspect(myStyle.outline));
    hull->AddPrimitiveArray(octahedron(shaft->frame, s + ow * 1.4));
    Handle(Graphic3d_Group) gem = thePrs->NewGroup();
    gem->SetGroupPrimitivesAspect(shadedAspect(myStyle.diamond, false));
    gem->AddPrimitiveArray(octahedron(shaft->frame, s));
  }
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
