// FabriCAD drawing: section cut implementation (OCCT booleans + 2D hatching).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "drawing/SectionCut.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Trsf.hxx>
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
#include <map>
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

static SectionOutput finishSection(const SectionInput& in, SectionOutput& out);

namespace {

using Poly2 = std::vector<Point2>;

/// Clip a convex polygon to the half-plane (q - o) . n >= 0 (Sutherland-Hodgman).
Poly2 clipHalfPlane(const Poly2& poly, const Point2& o, const Point2& n) {
  Poly2 out;
  const size_t k = poly.size();
  for (size_t i = 0; i < k; ++i) {
    const Point2& a = poly[i];
    const Point2& b = poly[(i + 1) % k];
    double da = (a - o).dot(n), db = (b - o).dot(n);
    if (da >= 0) out.push_back(a);
    if ((da >= 0) != (db >= 0)) out.push_back(a + (b - a) * (da / (da - db)));
  }
  return out;
}

Point2 rotate2(const Point2& v, double ang) {
  return {v.x * std::cos(ang) - v.y * std::sin(ang), v.x * std::sin(ang) + v.y * std::cos(ang)};
}

/// Helper for building tools in the parent view's frame.
struct ParentSpace {
  gp_Ax2 frame;
  double big;
  gp_Vec z() const { return gp_Vec(frame.Direction()); }
  gp_Pnt w(const Point2& p) const { return viewToWorld(frame, p); }
  /// Prism over a parent-local polygon, through the whole part (-big .. +big along the view axis).
  TopoDS_Shape prism(const Poly2& poly) const {
    BRepBuilderAPI_MakePolygon mp;
    for (const auto& p : poly) mp.Add(w(p).Translated(z() * (-big)));
    mp.Close();
    TopoDS_Face f = BRepBuilderAPI_MakeFace(mp.Wire(), true).Face();
    return BRepPrimAPI_MakePrism(f, z() * (2 * big)).Shape();
  }
  /// Planar rectangle containing the parent-local segment a-b and the view axis.
  TopoDS_Shape cutFace(const Point2& a, const Point2& b) const {
    BRepBuilderAPI_MakePolygon mp(w(a).Translated(z() * (-big)), w(b).Translated(z() * (-big)),
                                  w(b).Translated(z() * big), w(a).Translated(z() * big), true);
    return BRepBuilderAPI_MakeFace(mp.Wire(), true).Face();
  }
};

void collectLoops(const TopoDS_Shape& shape, const gp_Ax2& frame, double deflection, SectionOutput& out) {
  for (TopExp_Explorer ex(shape, TopAbs_FACE); ex.More(); ex.Next())
    for (auto& lp : faceLoops(ex.Current(), frame, deflection)) out.loops.push_back(std::move(lp));
}

/// Stepped and aligned sections (path with >= 3 points).
void computeComplexSection(const SectionInput& in, const std::vector<SourceShape>& sources, double big,
                           double deflection, SectionOutput& out) {
  ParentSpace ps{in.parentFrame, big};
  const Poly2& pts = in.path;
  Point2 a = in.arrow.normalized();
  if (a.length() < 0.5) a = (pts[1] - pts[0]).normalized().perp();
  if (!in.aligned) {
    // stepped: remove the material on the observer side of the polyline
    Point2 d0 = (pts[1] - pts[0]).normalized(), dn = (pts.back() - pts[pts.size() - 2]).normalized();
    Poly2 removal;
    Point2 e0 = pts.front() - d0 * big, en = pts.back() + dn * big;
    removal.push_back(e0);
    for (const auto& p : pts) removal.push_back(p);
    removal.push_back(en);
    removal.push_back(en - a * big);
    removal.push_back(e0 - a * big);
    TopoDS_Shape tool = ps.prism(removal);
    int srcIndex = -1;
    for (const auto& s : sources) {
      ++srcIndex;
      BRepAlgoAPI_Cut cut(s.shape, tool);
      if (!cut.IsDone()) {
        out.error = "stepped section cut failed";
        continue;
      }
      // hatched faces: the cutting segments (perpendicular to the viewing direction)
      for (size_t i = 0; i + 1 < pts.size(); ++i) {
        Point2 d = (pts[i + 1] - pts[i]).normalized();
        if (std::abs(d.dot(a)) > 1e-3) continue;  // step segment: seen edge-on
        Point2 s0 = pts[i], s1 = pts[i + 1];
        if (i == 0) s0 = s0 - d * big;
        if (i + 2 == pts.size()) s1 = s1 + d * big;
        BRepAlgoAPI_Common common(s.shape, ps.cutFace(s0, s1));
        if (common.IsDone()) collectLoops(common.Shape(), in.frame, deflection, out);
      }
      out.loopSource.resize(out.loops.size(), srcIndex);
      out.cutSources.push_back({s.bodyId, cut.Shape()});
    }
    return;
  }
  // aligned: arm A = p0 -> bend (stays), arm B = bend -> p2 (revolved into arm A's plane)
  const Point2 bend = pts[1];
  Point2 dA0 = (pts[0] - bend).normalized(), dB = (pts[2] - bend).normalized();
  Point2 dFirst = -dA0;
  double theta = std::atan2(dB.cross(dFirst), dB.dot(dFirst));  // rotation taking arm B onto A's line
  Point2 nA = a, nB = rotate2(a, -theta);
  Point2 b = (dA0 + dB).normalized();
  if (b.length() < 0.5) b = dA0.perp();
  double sgn = b.cross(dA0) >= 0 ? 1.0 : -1.0;
  Point2 mA = b.perp() * sgn;
  Poly2 square = {bend + Point2{-big, -big}, bend + Point2{big, -big}, bend + Point2{big, big}, bend + Point2{-big, big}};
  Poly2 regionA = clipHalfPlane(clipHalfPlane(square, bend, nA), bend, mA);
  Poly2 regionB = clipHalfPlane(clipHalfPlane(square, bend, nB), bend, -mA);
  gp_Trsf rot;
  rot.SetRotation(gp_Ax1(ps.w(bend), in.parentFrame.Direction()), theta);
  int srcIndex = -1;
  for (const auto& s : sources) {
    ++srcIndex;
    BRep_Builder bb;
    TopoDS_Compound kept;
    bb.MakeCompound(kept);
    bool any = false;
    if (regionA.size() >= 3) {
      BRepAlgoAPI_Common ca(s.shape, ps.prism(regionA));
      if (ca.IsDone() && !ca.Shape().IsNull()) {
        bb.Add(kept, ca.Shape());
        any = true;
      }
    }
    if (regionB.size() >= 3) {
      BRepAlgoAPI_Common cb(s.shape, ps.prism(regionB));
      if (cb.IsDone() && !cb.Shape().IsNull()) {
        bb.Add(kept, BRepBuilderAPI_Transform(cb.Shape(), rot, true).Shape());
        any = true;
      }
    }
    if (!any) {
      out.error = "aligned section cut failed";
      continue;
    }
    BRepAlgoAPI_Common faceA(s.shape, ps.cutFace(bend, bend + dA0 * big));
    if (faceA.IsDone()) collectLoops(faceA.Shape(), in.frame, deflection, out);
    BRepAlgoAPI_Common faceB(s.shape, ps.cutFace(bend, bend + dB * big));
    if (faceB.IsDone() && !faceB.Shape().IsNull())
      collectLoops(BRepBuilderAPI_Transform(faceB.Shape(), rot, true).Shape(), in.frame, deflection, out);
    out.loopSource.resize(out.loops.size(), srcIndex);
    out.cutSources.push_back({s.bodyId, kept});
  }
}

}  // namespace

void hatchVariant(int k, double angle, double spacing, double& angleOut, double& spacingOut) {
  // 45, 135, 45 at 0.6 pitch, 135 at 0.6 pitch, then 30 / 60 degrees, repeating
  static const double kAngle[] = {0, 90, 0, 90, -15, 15};
  static const double kPitch[] = {1, 1, 0.6, 0.6, 0.8, 0.8};
  const int n = static_cast<int>(sizeof kAngle / sizeof kAngle[0]);
  const int i = ((k % n) + n) % n;
  angleOut = angle + kAngle[i];
  spacingOut = spacing * kPitch[i];
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

  if (in.path.size() >= 3) {
    try {
      computeComplexSection(in, sources, big, deflection, out);
    } catch (const Standard_Failure& e) {
      out.error = std::string("section failed: ") + (e.GetMessageString() ? e.GetMessageString() : "");
    }
    return finishSection(in, out);
  }

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

  int srcIndex = -1;
  for (const auto& s : sources) {
    ++srcIndex;
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
      out.loopSource.resize(out.loops.size(), srcIndex);
      out.cutSources.push_back({s.bodyId, kept});
    } catch (const Standard_Failure& e) {
      out.error = std::string("section failed: ") + (e.GetMessageString() ? e.GetMessageString() : "");
    }
  }
  return finishSection(in, out);
}

static SectionOutput finishSection(const SectionInput& in, SectionOutput& out) {
  if (out.cutSources.empty()) {
    if (out.error.empty()) out.error = "section produced no geometry";
    return out;
  }

  for (const auto& loop : out.loops) {
    for (size_t i = 0, n = loop.size(); i < n; ++i)
      out.outlines.push_back(Curve2D::segment(loop[i], loop[(i + 1) % n], LineClass::Section));
  }
  if (in.hatch && !out.loops.empty()) {
    // DRW-VIEWS-ANNOT-11: each part (source) gets its own hatch so adjacent parts read apart
    out.loopSource.resize(out.loops.size(), 0);
    std::map<int, std::vector<std::vector<Point2>>> bySource;
    for (size_t i = 0; i < out.loops.size(); ++i) bySource[out.loopSource[i]].push_back(out.loops[i]);
    int k = 0;
    for (const auto& [src, loops] : bySource) {
      (void)src;
      double angle = in.hatchAngle, spacing = in.hatchSpacing;
      hatchVariant(k++, in.hatchAngle, in.hatchSpacing, angle, spacing);
      for (auto& h : hatchLoops(loops, angle, spacing)) out.hatch.push_back(std::move(h));
    }
  }
  out.ok = true;
  return out;
}

}  // namespace fabricad::drawing
