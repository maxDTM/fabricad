// FabriCAD drawing: section cut implementation (OCCT booleans + 2D hatching).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "drawing/SectionCut.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Pln.hxx>

#include <algorithm>
#include <cmath>

namespace fabricad::drawing {

namespace {
std::vector<Point2> wireToPolygon(const TopoDS_Wire& wire, const gp_Ax2& frame, double deflection) {
  std::vector<Point2> poly;
  for (BRepTools_WireExplorer ex(wire); ex.More(); ex.Next()) {
    const TopoDS_Edge& e = ex.Current();
    BRepAdaptor_Curve adapt(e);
    double f = adapt.FirstParameter(), l = adapt.LastParameter();
    if (!(l > f)) continue;
    std::vector<gp_Pnt> pts;
    if (adapt.GetType() == GeomAbs_Line) {
      pts = {adapt.Value(f), adapt.Value(l)};
    } else {
      try {
        GCPnts_TangentialDeflection disc(adapt, f, l, 0.1, deflection, 2);
        for (int i = 1; i <= disc.NbPoints(); ++i) pts.push_back(disc.Value(i));
      } catch (const Standard_Failure&) {
      }
      if (pts.size() < 2) {
        pts.clear();
        for (int i = 0; i <= 16; ++i) pts.push_back(adapt.Value(f + (l - f) * i / 16));
      }
    }
    if (e.Orientation() == TopAbs_REVERSED) std::reverse(pts.begin(), pts.end());
    for (size_t i = 0; i < pts.size(); ++i) {
      Point2 p = worldToView(frame, pts[i]);
      if (poly.empty() || poly.back().distance(p) > 1e-7) poly.push_back(p);
    }
  }
  if (poly.size() > 2 && poly.front().distance(poly.back()) < 1e-7) poly.pop_back();
  return poly;
}
}  // namespace

std::vector<std::vector<Point2>> faceLoops(const TopoDS_Shape& faceShape, const gp_Ax2& frame, double deflection) {
  std::vector<std::vector<Point2>> loops;
  if (faceShape.IsNull() || faceShape.ShapeType() != TopAbs_FACE) return loops;
  const TopoDS_Face& face = TopoDS::Face(faceShape);
  for (TopExp_Explorer ex(face, TopAbs_WIRE); ex.More(); ex.Next()) {
    std::vector<Point2> poly = wireToPolygon(TopoDS::Wire(ex.Current()), frame, deflection);
    if (poly.size() >= 3) loops.push_back(std::move(poly));
  }
  return loops;
}

SectionOutput computeSection(const SectionInput& in) {
  SectionOutput out;
  std::vector<SourceShape> sources;
  for (const auto& s : in.sources)
    if (!s.shape.IsNull()) sources.push_back(s);
  if (sources.empty()) {
    out.error = "nothing to section";
    return out;
  }
  gp_Pnt center;
  double diagonal = 1.0;
  sourcesCenter(sources, center, &diagonal);
  const double big = std::max(10.0, diagonal * 4.0);
  const double deflection = std::clamp(diagonal * 5e-4, 0.005, 0.2);

  // Tool box occupying the half-space on the viewer side of the plane (Direction() points
  // towards the viewer, i.e. towards the material to remove).
  const gp_Ax2& fr = in.frame;
  gp_Pnt boxOrigin = fr.Location()
                         .Translated(gp_Vec(fr.XDirection()) * (-big))
                         .Translated(gp_Vec(fr.YDirection()) * (-big));
  gp_Ax2 boxAx(boxOrigin, fr.Direction(), fr.XDirection());
  TopoDS_Shape tool;
  TopoDS_Shape planeFace;
  try {
    tool = BRepPrimAPI_MakeBox(boxAx, 2 * big, 2 * big, big).Shape();
    gp_Pln pln(fr.Location(), fr.Direction());
    planeFace = BRepBuilderAPI_MakeFace(pln, -big, big, -big, big).Face();
  } catch (const Standard_Failure& e) {
    out.error = std::string("section tool failed: ") + (e.GetMessageString() ? e.GetMessageString() : "");
    return out;
  }

  for (const auto& s : sources) {
    try {
      BRepAlgoAPI_Cut cut(s.shape, tool);
      if (!cut.IsDone()) {
        out.error = "section cut failed";
        continue;
      }
      TopoDS_Shape kept = cut.Shape();
      // Cut faces: common of the solid with the plane face.
      BRepAlgoAPI_Common common(s.shape, planeFace);
      if (common.IsDone()) {
        for (TopExp_Explorer ex(common.Shape(), TopAbs_FACE); ex.More(); ex.Next()) {
          auto loops = faceLoops(ex.Current(), fr, deflection);
          for (auto& lp : loops) out.loops.push_back(std::move(lp));
        }
      }
      out.cutSources.push_back({s.bodyId, kept});
    } catch (const Standard_Failure& e) {
      out.error = std::string("section failed: ") + (e.GetMessageString() ? e.GetMessageString() : "");
    }
  }
  if (out.cutSources.empty()) {
    if (out.error.empty()) out.error = "section produced no geometry";
    return out;
  }

  for (const auto& loop : out.loops) {
    for (size_t i = 0, n = loop.size(); i < n; ++i)
      out.outlines.push_back(Curve2D::segment(loop[i], loop[(i + 1) % n], LineClass::Section));
  }
  if (in.hatch && !out.loops.empty()) out.hatch = hatchLoops(out.loops, in.hatchAngle, in.hatchSpacing);
  out.ok = true;
  return out;
}

}  // namespace fabricad::drawing
