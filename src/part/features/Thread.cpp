// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/features/Thread.h"

#include "core/Feature.h"
#include "core/ShapeUtils.h"
#include "part/PartUtils.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopoDS.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Cylinder.hxx>

#include <algorithm>
#include <cmath>
#include <format>

namespace fabricad::part {

namespace {

constexpr int kSamples = 8;
constexpr double kMaxModelledTurns = 200;

std::vector<gp_Pnt> edgeSamples(const TopoDS_Shape& edge) {
  std::vector<gp_Pnt> pts;
  try {
    BRepAdaptor_Curve c(TopoDS::Edge(edge));
    for (int i = 0; i <= kSamples; ++i)
      pts.push_back(c.Value(c.FirstParameter() + (c.LastParameter() - c.FirstParameter()) * i / kSamples));
  } catch (const Standard_Failure&) {
  }
  return pts;
}

double radialDistance(const gp_Ax1& axis, const gp_Pnt& p) {
  gp_Vec v(axis.Location(), p);
  gp_Vec along = gp_Vec(axis.Direction()) * v.Dot(gp_Vec(axis.Direction()));
  return (v - along).Magnitude();
}

}  // namespace

std::optional<ThreadCylinder> analyseThreadCylinder(const TopoDS_Shape& body, const TopoDS_Shape& faceShape) {
  if (faceShape.IsNull() || faceShape.ShapeType() != TopAbs_FACE) return std::nullopt;
  const TopoDS_Face& face = TopoDS::Face(faceShape);
  try {
    BRepAdaptor_Surface a(face);
    if (a.GetType() != GeomAbs_Cylinder) return std::nullopt;
    if (a.LastUParameter() - a.FirstUParameter() < 2 * M_PI - 1e-6) return std::nullopt;  // not a full turn
    gp_Cylinder cyl = a.Cylinder();
    ThreadCylinder out;
    out.radius = cyl.Radius();
    out.axis = cyl.Axis();
    const gp_Dir d = out.axis.Direction();
    const gp_Pnt o = out.axis.Location();
    auto vOf = [&](const gp_Pnt& p) { return gp_Vec(o, p).Dot(gp_Vec(d)); };
    double v0 = 1e100, v1 = -1e100;
    std::vector<TopoDS_Shape> edges;
    for (TopExp_Explorer x(face, TopAbs_EDGE); x.More(); x.Next()) {
      edges.push_back(x.Current());
      for (const auto& p : edgeSamples(x.Current())) v0 = std::min(v0, vOf(p)), v1 = std::max(v1, vOf(p));
    }
    if (!(v1 > v0 + 1e-9)) return std::nullopt;
    out.v0 = v0;
    out.v1 = v1;
    // side: outward normal of the face vs the radial direction
    double um = (a.FirstUParameter() + a.LastUParameter()) / 2, vm = (a.FirstVParameter() + a.LastVParameter()) / 2;
    gp_Pnt p;
    gp_Vec du, dv;
    a.D1(um, vm, p, du, dv);
    gp_Vec n = du.Crossed(dv);
    if (face.Orientation() == TopAbs_REVERSED) n.Reverse();
    gp_Vec radial(gp_Pnt(o.XYZ() + gp_Vec(d).XYZ() * vOf(p)), p);
    out.internal = n.Dot(radial) < 0;
    // can the thread run out at each end? Look at the neighbouring faces across the end edges.
    TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
    TopExp::MapShapesAndAncestors(body, TopAbs_EDGE, TopAbs_FACE, edgeFaces);
    const double tol = std::max(1e-6, out.radius * 1e-6) + 1e-4;
    auto openAt = [&](double v) {
      bool any = false, open = true;
      for (const auto& e : edges) {
        std::vector<gp_Pnt> pts = edgeSamples(e);
        bool atEnd = !pts.empty() && std::all_of(pts.begin(), pts.end(), [&](const gp_Pnt& q) { return std::abs(vOf(q) - v) < 1e-4; });
        if (!atEnd) continue;
        const TopTools_ListOfShape* fl = edgeFaces.Seek(e);
        if (!fl) continue;
        for (const auto& other : *fl) {
          if (other.IsSame(face)) continue;
          any = true;
          double maxR = 0;
          for (TopExp_Explorer x(other, TopAbs_EDGE); x.More(); x.Next())
            for (const auto& q : edgeSamples(x.Current())) maxR = std::max(maxR, radialDistance(out.axis, q));
          bool beyond = maxR > out.radius + tol;
          // rod: an end cap (no material beyond the radius) is open; a shoulder is not.
          // hole: a face reaching beyond the radius is the opening face; a bottom is not.
          if (out.internal ? !beyond : beyond) open = false;
        }
      }
      return any ? open : true;
    };
    out.openAt0 = openAt(v0);
    out.openAt1 = openAt(v1);
    return out;
  } catch (const Standard_Failure&) {
    return std::nullopt;
  }
}

std::string ThreadInfo::callout() const {
  return std::format("{} - {}{}", designation, internal ? "6H" : "6g", leftHanded ? " LH" : "");
}

bool placeThread(const ThreadCylinder& cyl, const IsoThread& iso, bool fullLength, double offset, double length, bool flip,
                 bool leftHanded, bool modeled, ThreadInfo& info, TopoDS_Shape* cutter, std::string* error) {
  auto fail = [&](const std::string& m) {
    if (error) *error = m;
    return false;
  };
  const double avail = cyl.v1 - cyl.v0;
  double from = fullLength ? 0.0 : std::max(0.0, offset);
  double to = fullLength ? avail : std::min(avail, from + length);
  if (!fullLength && length <= 1e-9) return fail("thread length must be positive");
  if (to - from <= 1e-6) return fail("the thread offset is beyond the end of the face");
  const gp_Dir axisDir = cyl.axis.Direction();
  const gp_Dir dir = flip ? axisDir.Reversed() : axisDir;
  const double startV = flip ? cyl.v1 : cyl.v0;
  const gp_Pnt start(cyl.axis.Location().XYZ() + gp_Vec(axisDir).XYZ() * startV + gp_Vec(dir).XYZ() * from);
  info.designation = iso.designation;
  info.nominal = iso.nominal;
  info.pitch = iso.pitch;
  info.internal = cyl.internal;
  info.modeled = modeled;
  info.leftHanded = leftHanded;
  info.axis = gp_Ax1(start, dir);
  info.length = to - from;
  if (!modeled || !cutter) return true;
  if (info.length / iso.pitch > kMaxModelledTurns)
    return fail(std::format("{:.0f} turns are too many to model; use a cosmetic thread", info.length / iso.pitch));
  const bool openStart = flip ? cyl.openAt1 : cyl.openAt0;
  const bool openEnd = flip ? cyl.openAt0 : cyl.openAt1;
  const bool runOutStart = from < 1e-6 && openStart;
  const bool runOutEnd = to > avail - 1e-6 && openEnd;
  TopoDS_Shape c = makeThreadCutter(iso, info.length, cyl.internal, leftHanded, runOutStart, runOutEnd, error);
  if (c.IsNull()) return false;
  if (cyl.internal) {
    // Cutting a hole wall with the bare helical cutter can silently do nothing in OCCT's
    // boolean (the flanks crossing the wall cylinder). Adding the hole's own core (air) to
    // the cutter makes the cut reliable; the core stays inside the threaded length except
    // where the thread runs out through an opening.
    const double zA = runOutStart ? -iso.pitch : 0.0;
    const double zB = runOutEnd ? info.length + iso.pitch : info.length;
    try {
      TopoDS_Shape core = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, zA), gp::DZ()), cyl.radius, zB - zA).Shape();
      BRepAlgoAPI_Fuse fuse(core, c);
      if (fuse.IsDone() && !fuse.Shape().IsNull()) c = fuse.Shape();
    } catch (const Standard_Failure&) {
      // keep the bare cutter
    }
  }
  *cutter = core::transformed(c, frameTransform(gp_Ax3(start, dir, anyPerpendicular(dir))));
  return true;
}

// ---------------------------------------------------------------------------
// ThreadFeature
// ---------------------------------------------------------------------------

ThreadFeature::ThreadFeature() : PartFeature(kType) {
  addRefs("faces", core::mask(core::ShapeKind::Face)).setLabel("Faces").setTooltip("Cylindrical faces (rods or holes)");
  addEnum("threadType", {"ISO Metric profile"}, 0).setLabel("Thread type");
  addBool("autoSize", true).setLabel("Auto size").setTooltip("Pick the ISO size from the cylinder diameter");
  addEnum("designation", isoMetricDesignations(), 0).setLabel("Designation").setEnum("M10x1.5");
  addBool("fullLength", true).setLabel("Full length");
  addLength("length", 10.0).setLabel("Thread length");
  addLength("offset", 0.0).setLabel("Offset").setTooltip("Distance of the thread start from the face end");
  addBool("flip", false).setLabel("Flip start").setTooltip("Start the thread at the other end of the face");
  addBool("modeled", false).setLabel("Modeled").setTooltip("Cut the real helical groove (slow); off = cosmetic thread");
  addBool("leftHanded", false).setLabel("Left hand");
  updatePropertyFlags();
}

void ThreadFeature::updatePropertyFlags() {
  prop("designation").flags().hidden = flag("autoSize");
  bool full = flag("fullLength");
  prop("length").flags().hidden = full;
  prop("offset").flags().hidden = full;
}

void ThreadFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  threads_.clear();
  std::vector<ResolvedGroup> groups = resolveByBody(ctx, prop("faces").refs(), core::ShapeKind::Face);
  if (groups.empty()) {
    ctx.setError("select cylindrical faces to thread");
    return;
  }
  const bool autoSize = flag("autoSize"), modeled = flag("modeled");
  const double length = ctx.length("length"), offset = ctx.length("offset");
  std::vector<TopoDS_Shape> allCutters;
  for (const ResolvedGroup& g : groups) {
    std::vector<TopoDS_Shape> cutters;
    for (const auto& f : g.shapes) {
      auto cyl = analyseThreadCylinder(g.bodyShape, f);
      if (!cyl) {
        ctx.setError("threads need full cylindrical faces");
        return;
      }
      const IsoThread* iso = nullptr;
      if (autoSize) {
        iso = isoThreadForDiameter(2 * cyl->radius, cyl->internal);
        if (!iso) {
          ctx.setError(std::format("no ISO metric thread fits a {} of diameter {:.4g} mm; turn off Auto size and pick a "
                                   "designation",
                                   cyl->internal ? "hole" : "rod", 2 * cyl->radius));
          return;
        }
      } else {
        iso = findIsoThread(enumName("designation"));
        if (!iso) {
          ctx.setError("unknown thread designation");
          return;
        }
        double ref = cyl->internal ? iso->minorDiameter() : iso->nominal;
        if (std::abs(2 * cyl->radius - ref) > 0.5 * iso->pitch + 0.2)
          ctx.setWarning(std::format("{} does not match the {} diameter {:.4g} mm", iso->designation,
                                     cyl->internal ? "hole" : "rod", 2 * cyl->radius));
      }
      ThreadInfo info;
      TopoDS_Shape cutter;
      std::string err;
      if (!placeThread(*cyl, *iso, flag("fullLength"), offset, length, flag("flip"), flag("leftHanded"), modeled, info,
                       modeled ? &cutter : nullptr, &err)) {
        ctx.setError(err);
        return;
      }
      threads_.push_back(info);
      if (!cutter.IsNull()) cutters.push_back(cutter);
    }
    if (cutters.empty()) continue;
    BoolResult r = booleanOp(Operation::Cut, g.bodyShape, cutters);
    if (!r) {
      ctx.setError("modelled thread failed: " + (r.error.empty() ? std::string("empty result") : r.error));
      return;
    }
    if (!replaceBody(ctx, g.body, r.shape, History::of(r.history), {}, out)) return;
    allCutters.insert(allCutters.end(), cutters.begin(), cutters.end());
  }
  out.operation = Operation::Cut;
  out.tool = compoundOf(allCutters);
}

}  // namespace fabricad::part
