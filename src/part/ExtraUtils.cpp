// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/ExtraUtils.h"

#include "core/Feature.h"
#include "core/ShapeUtils.h"
#include "part/Naming.h"
#include "part/PartUtils.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepLib.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRep_Tool.hxx>
#include <GeomAdaptor_Surface.hxx>
#include <Geom2d_Line.hxx>
#include <Geom_ConicalSurface.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_Surface.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS.hxx>
#include <gp_Lin2d.hxx>

#include <cmath>
#include <format>

namespace fabricad::part {

std::optional<gp_Ax3> resolvePlaneInput(core::RecomputeContext& ctx, core::Property& p) {
  const core::ShapeRef& r = p.ref();
  if (!r.isValid()) return std::nullopt;
  if (r.kind == core::ShapeKind::Face) {
    TopoDS_Shape f = resolveRefProperty(ctx, p);
    if (f.IsNull()) return std::nullopt;
    return core::facePlane(f);
  }
  return ctx.plane(r);
}

gp_Trsf frameTransform(const gp_Ax3& frame) {
  gp_Trsf t;
  t.SetDisplacement(gp_Ax3(), frame);
  return t;
}

TopoDS_Wire makeHelix(double radius, double pitch, double height, double taperDeg, bool leftHanded) {
  if (radius <= Precision::Confusion() || pitch <= Precision::Confusion() || height <= Precision::Confusion()) return {};
  try {
    const double turns = height / pitch;
    const double du = leftHanded ? -2 * M_PI : 2 * M_PI;
    Handle(Geom_Surface) surf;
    double dv = pitch;  // parameter rise per turn
    if (std::abs(taperDeg) > 1e-9) {
      double semi = -taperDeg * M_PI / 180.0;  // positive taper narrows the coil upwards
      if (std::abs(semi) >= M_PI / 2 - 1e-6) return {};
      surf = new Geom_ConicalSurface(gp_Ax3(gp::Origin(), gp::DZ(), gp::DX()), semi, radius);
      dv = pitch / std::cos(semi);  // v runs along the generatrix
      double endRadius = radius + turns * dv * std::sin(semi);
      if (endRadius <= Precision::Confusion()) return {};
    } else {
      surf = new Geom_CylindricalSurface(gp_Ax3(gp::Origin(), gp::DZ(), gp::DX()), radius);
    }
    gp_Lin2d line(gp_Pnt2d(0, 0), gp_Dir2d(du, dv));
    const double len = turns * std::sqrt(du * du + dv * dv);
    TopoDS_Edge e = BRepBuilderAPI_MakeEdge(new Geom2d_Line(line), surf, 0.0, len).Edge();
    BRepLib::BuildCurves3d(e, 1e-6, GeomAbs_C1, 14, 200);
    return BRepBuilderAPI_MakeWire(e).Wire();
  } catch (const Standard_Failure&) {
    return {};
  }
}

TopoDS_Shape sweepAlongHelix(const TopoDS_Wire& profile, const TopoDS_Wire& helix, std::string* error) {
  auto fail = [&](const std::string& m) {
    if (error) *error = m;
    return TopoDS_Shape();
  };
  if (profile.IsNull() || helix.IsNull()) return fail("invalid helix or profile");
  try {
    BRepOffsetAPI_MakePipeShell ps(helix);
    ps.SetMode(gp_Dir(0, 0, 1));  // constant binormal: the profile stays in the meridian plane
    ps.Add(profile, Standard_False, Standard_False);
    ps.Build();
    if (!ps.IsDone()) return fail("helical sweep failed");
    if (!ps.MakeSolid()) return fail("helical sweep is not closed");
    TopoDS_Shape s = ps.Shape();
    if (!core::isValidShape(s)) return fail("helical sweep produced an invalid solid");
    return s;
  } catch (const Standard_Failure& e) {
    return fail(e.GetMessageString() ? e.GetMessageString() : "helical sweep failed");
  }
}

TopoDS_Wire polygonWire(const std::vector<gp_Pnt>& pts) {
  BRepBuilderAPI_MakePolygon poly;
  for (const auto& p : pts) poly.Add(p);
  poly.Close();
  if (!poly.IsDone()) return {};
  return poly.Wire();
}

TopoDS_Shape extendedFace(const TopoDS_Face& face, double size) {
  if (face.IsNull()) return face;
  try {
    Handle(Geom_Surface) s = BRep_Tool::Surface(face);
    GeomAdaptor_Surface a(s);
    const double tol = Precision::Confusion();
    switch (a.GetType()) {
      case GeomAbs_Plane: return BRepBuilderAPI_MakeFace(a.Plane(), -size, size, -size, size).Face();
      case GeomAbs_Cylinder: return BRepBuilderAPI_MakeFace(s, 0, 2 * M_PI, -size, size, tol).Face();
      case GeomAbs_Cone: {
        gp_Cone c = a.Cone();
        // stop just short of the apex (v_apex = -R / sin(semiAngle))
        double vApex = -c.RefRadius() / std::sin(c.SemiAngle());
        double v0 = vApex < 0 ? std::max(-size, vApex + 1e-3 * size) : -size;
        double v1 = vApex > 0 ? std::min(size, vApex - 1e-3 * size) : size;
        return BRepBuilderAPI_MakeFace(s, 0, 2 * M_PI, v0, v1, tol).Face();
      }
      case GeomAbs_Sphere: return BRepBuilderAPI_MakeFace(s, 0, 2 * M_PI, -M_PI / 2, M_PI / 2, tol).Face();
      case GeomAbs_Torus: return BRepBuilderAPI_MakeFace(s, 0, 2 * M_PI, 0, 2 * M_PI, tol).Face();
      default: return face;
    }
  } catch (const Standard_Failure&) {
    return face;
  }
}

TopoDS_Shape resolveSplitTool(core::RecomputeContext& ctx, core::Property& p, bool extend, std::optional<gp_Ax3>* plane) {
  const core::ShapeRef& r = p.ref();
  if (!r.isValid()) {
    ctx.setError("select a splitting plane, face or body");
    return {};
  }
  const double size = throughAllLength(ctx);
  if (r.kind == core::ShapeKind::Body) {
    TopoDS_Shape s = resolveRefProperty(ctx, p);
    if (s.IsNull()) ctx.setError("splitting body not found");
    return s;
  }
  if (r.kind == core::ShapeKind::Face) {
    TopoDS_Shape f = resolveRefProperty(ctx, p);
    if (f.IsNull()) {
      ctx.setError("splitting face not found");
      return {};
    }
    if (auto pl = core::facePlane(f)) {
      if (plane) *plane = pl;
      return extend ? TopoDS_Shape(core::planeFace(*pl, size)) : f;
    }
    return extend ? extendedFace(TopoDS::Face(f), size) : f;
  }
  auto pl = ctx.plane(r);
  if (!pl) {
    ctx.setError("splitting tool is not a plane");
    return {};
  }
  if (plane) *plane = pl;
  return core::planeFace(*pl, size);
}

// ---------------------------------------------------------------------------
// ISO metric thread table
// ---------------------------------------------------------------------------

const std::vector<IsoThread>& isoMetricThreads() {
  static const std::vector<IsoThread> kTable = [] {
    // ISO 261 preferred sizes: nominal, coarse pitch, fine pitches (largest first)
    struct Row {
      double d;
      double coarse;
      std::vector<double> fine;
    };
    const std::vector<Row> rows{
        {1, 0.25, {0.2}},         {1.2, 0.25, {0.2}},     {1.4, 0.3, {0.2}},         {1.6, 0.35, {0.2}},
        {2, 0.4, {0.25}},         {2.5, 0.45, {0.35}},    {3, 0.5, {0.35}},          {3.5, 0.6, {0.35}},
        {4, 0.7, {0.5}},          {5, 0.8, {0.5}},        {6, 1, {0.75}},            {7, 1, {0.75}},
        {8, 1.25, {1, 0.75}},     {10, 1.5, {1.25, 1, 0.75}}, {12, 1.75, {1.5, 1.25, 1}}, {14, 2, {1.5, 1.25, 1}},
        {16, 2, {1.5, 1}},        {18, 2.5, {2, 1.5, 1}}, {20, 2.5, {2, 1.5, 1}},    {22, 2.5, {2, 1.5, 1}},
        {24, 3, {2, 1.5, 1}},     {27, 3, {2, 1.5, 1}},   {30, 3.5, {3, 2, 1.5, 1}}, {33, 3.5, {3, 2, 1.5}},
        {36, 4, {3, 2, 1.5}},     {39, 4, {3, 2, 1.5}},   {42, 4.5, {4, 3, 2, 1.5}}, {45, 4.5, {4, 3, 2, 1.5}},
        {48, 5, {4, 3, 2, 1.5}},  {52, 5, {4, 3, 2, 1.5}}, {56, 5.5, {4, 3, 2, 1.5}}, {60, 5.5, {4, 3, 2, 1.5}},
        {64, 6, {4, 3, 2, 1.5}},
    };
    std::vector<IsoThread> out;
    for (const Row& r : rows) {
      out.push_back({std::format("M{}x{}", r.d, r.coarse), r.d, r.coarse, true});
      for (double p : r.fine) out.push_back({std::format("M{}x{}", r.d, p), r.d, p, false});
    }
    return out;
  }();
  return kTable;
}

const std::vector<std::string>& isoMetricDesignations() {
  static const std::vector<std::string> kNames = [] {
    std::vector<std::string> out;
    for (const auto& t : isoMetricThreads()) out.push_back(t.designation);
    return out;
  }();
  return kNames;
}

const IsoThread* findIsoThread(const std::string& designation) {
  for (const auto& t : isoMetricThreads())
    if (t.designation == designation) return &t;
  for (const auto& t : isoMetricThreads())  // "M8" = coarse
    if (t.coarse && std::format("M{}", t.nominal) == designation) return &t;
  return nullptr;
}

const IsoThread* isoThreadForDiameter(double diameter, bool internal) {
  const IsoThread* best = nullptr;
  double bestErr = 1e9;
  for (const auto& t : isoMetricThreads()) {
    if (!t.coarse) continue;
    if (internal) {
      for (double ref : {t.tapDrill(), t.minorDiameter()}) {
        double err = std::abs(ref - diameter);
        if (err <= 0.15 && err < bestErr - 1e-9) {
          bestErr = err;
          best = &t;
        }
      }
    } else {
      double err = std::abs(t.nominal - diameter);
      if (err <= 0.1 && err < bestErr - 1e-9) {
        bestErr = err;
        best = &t;
      }
    }
  }
  return best;
}

TopoDS_Shape makeThreadCutter(const IsoThread& t, double length, bool internal, bool leftHanded, bool runOutStart,
                              bool runOutEnd, std::string* error, double surfaceRadius) {
  auto fail = [&](const std::string& m) {
    if (error) *error = m;
    return TopoDS_Shape();
  };
  const double P = t.pitch;
  if (P <= 0 || length <= P * 0.5) return fail("thread length must be at least half a pitch");
  const double rMaj = t.nominal / 2.0;
  const double rMin = t.minorDiameter() / 2.0;
  const double ext = 0.25 * P;
  // Groove cross-section in the meridian plane (x radial, z axial), centred on z = 0. The flank
  // kink (where the groove stops widening) is kept 0.05 P off the body surface: a profile vertex
  // exactly on the cylinder (Auto size: nominal == cylinder diameter) sweeps a helical edge that
  // lies on the body face, which makes the boolean result invalid. The groove width beyond the
  // flank is clamped to 0.95 P so neighbouring turns never touch.
  const double maxHalfW = 0.475 * P;
  std::vector<gp_Pnt> pts;
  if (!internal) {
    const double wRoot = P / 4, wTop = 7 * P / 8;  // groove width at d1 and at D (basic profile)
    const double slope = (wTop - wRoot) / 2 / (rMaj - rMin);  // half-width growth per radial mm
    double rK = std::max(rMaj, surfaceRadius) + 0.05 * P;
    double hw = wRoot / 2 + slope * (rK - rMin);
    double rF = rK;
    if (hw > maxHalfW) {
      hw = maxHalfW;
      rF = rMin + (maxHalfW - wRoot / 2) / slope;
    }
    const double rOut = rK + ext;
    pts = {{rMin, 0, -wRoot / 2}, {rF, 0, -hw}, {rOut, 0, -hw}, {rOut, 0, hw}, {rF, 0, hw}, {rMin, 0, wRoot / 2}};
  } else {
    const double wIn = 3 * P / 4, wOut = P / 8;  // groove width at D1 and at D
    const double slope = (wIn - wOut) / 2 / (rMaj - rMin);  // half-width growth per radial mm inwards
    double rK = (surfaceRadius > 0 ? std::min(rMin, surfaceRadius) : rMin) - 0.05 * P;
    double hw = wOut / 2 + slope * (rMaj - rK);
    double rF = rK;
    if (hw > maxHalfW) {
      hw = maxHalfW;
      rF = rMaj - (maxHalfW - wOut / 2) / slope;
    }
    const double rIn = rK - ext;
    pts = {{rIn, 0, -hw}, {rF, 0, -hw}, {rMaj, 0, -wOut / 2}, {rMaj, 0, wOut / 2}, {rF, 0, hw}, {rIn, 0, hw}};
  }
  // Start/end: run out through an end face by a pitch, else keep the groove inside the length.
  const double halfW = maxHalfW;
  const double z0 = runOutStart ? -P : halfW;
  const double z1 = runOutEnd ? length + P : length - halfW;
  if (z1 - z0 < P * 0.25) return fail("thread is too short for its pitch");
  gp_Trsf lift;
  lift.SetTranslation(gp_Vec(0, 0, z0));
  for (auto& p : pts) p.Transform(lift);
  TopoDS_Wire prof = polygonWire(pts);
  TopoDS_Wire helix = makeHelix(rMin, P, z1 - z0, 0.0, leftHanded);
  if (helix.IsNull()) return fail("could not build the thread helix");
  helix = TopoDS::Wire(core::transformed(helix, lift));
  return sweepAlongHelix(prof, helix, error);
}

}  // namespace fabricad::part
