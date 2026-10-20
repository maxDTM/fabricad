// FabriCAD assembly: snap frames from picked geometry.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "assembly/JointFrame.h"

#include "assembly/ComponentOps.h"
#include "core/Json.h"
#include "core/ShapeUtils.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepLProp_SLProps.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Circ.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Cone.hxx>
#include <gp_Elips.hxx>
#include <gp_Sphere.hxx>
#include <gp_Torus.hxx>

#include <algorithm>
#include <cmath>
#include <limits>

namespace fabricad::assembly {

using core::Document;
using core::kInvalidId;
using core::ObjectId;
using core::ShapeKind;
using core::ShapeRef;

namespace {

struct ModeName {
  SnapMode mode;
  const char* name;
  const char* label;
};
const ModeName kModes[] = {
    {SnapMode::Auto, "Auto", "Auto"},
    {SnapMode::FaceCenter, "FaceCenter", "Face centre"},
    {SnapMode::PickPoint, "PickPoint", "Point on face"},
    {SnapMode::EdgeMidpoint, "EdgeMidpoint", "Edge midpoint"},
    {SnapMode::EdgeStart, "EdgeStart", "Edge start"},
    {SnapMode::EdgeEnd, "EdgeEnd", "Edge end"},
    {SnapMode::CircleCenter, "CircleCenter", "Circle centre"},
    {SnapMode::AxisStart, "AxisStart", "Axis start"},
    {SnapMode::AxisMiddle, "AxisMiddle", "Axis middle"},
    {SnapMode::AxisEnd, "AxisEnd", "Axis end"},
    {SnapMode::Vertex, "Vertex", "Vertex"},
    {SnapMode::BodyCenter, "BodyCenter", "Body centre"},
    {SnapMode::ComponentOrigin, "ComponentOrigin", "Component origin"},
    {SnapMode::Geometry, "Geometry", "Geometry"},
};

const char* labelOf(SnapMode m) {
  for (const auto& e : kModes)
    if (e.mode == m) return e.label;
  return "Snap";
}

bool isLinearCurve(const BRepAdaptor_Curve& c) { return c.GetType() == GeomAbs_Line; }

std::optional<gp_Ax1> circleAxis(const BRepAdaptor_Curve& c) {
  if (c.GetType() == GeomAbs_Circle) return c.Circle().Axis();
  if (c.GetType() == GeomAbs_Ellipse) return c.Ellipse().Axis();
  return std::nullopt;
}

/// Oriented normal of a face at a point (projected onto the surface). Falls back to the plane normal.
std::optional<gp_Dir> faceNormalAt(const TopoDS_Face& face, const gp_Pnt& p, gp_Pnt* onSurface) {
  BRepAdaptor_Surface surf(face, false);
  try {
    GeomAPI_ProjectPointOnSurf proj(p, BRep_Tool::Surface(face));
    if (!proj.IsDone() || proj.NbPoints() == 0) return std::nullopt;
    double u, v;
    proj.LowerDistanceParameters(u, v);
    BRepLProp_SLProps props(surf, u, v, 1, 1e-7);
    if (!props.IsNormalDefined()) return std::nullopt;
    gp_Dir n = props.Normal();
    if (face.Orientation() == TopAbs_REVERSED) n.Reverse();
    if (onSurface) *onSurface = props.Value();
    return n;
  } catch (...) {
    return std::nullopt;
  }
}

/// Axis of a cylindrical/conical/toroidal face plus the parameter range of the face along it.
struct FaceAxis {
  gp_Ax1 axis;
  double tMin = 0, tMax = 0;
};
std::optional<FaceAxis> faceAxis(const TopoDS_Face& face) {
  BRepAdaptor_Surface surf(face, false);
  gp_Ax1 ax;
  switch (surf.GetType()) {
    case GeomAbs_Cylinder: ax = surf.Cylinder().Axis(); break;
    case GeomAbs_Cone: ax = surf.Cone().Axis(); break;
    case GeomAbs_Torus: ax = surf.Torus().Axis(); break;
    case GeomAbs_SurfaceOfRevolution: ax = surf.AxeOfRevolution(); break;
    default: return std::nullopt;
  }
  FaceAxis fa;
  fa.axis = ax;
  double tMin = std::numeric_limits<double>::max(), tMax = -tMin;
  for (TopExp_Explorer ex(face, TopAbs_VERTEX); ex.More(); ex.Next()) {
    gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(ex.Current()));
    double t = gp_Vec(ax.Location(), p).Dot(gp_Vec(ax.Direction()));
    tMin = std::min(tMin, t);
    tMax = std::max(tMax, t);
  }
  if (tMin > tMax) {
    // closed face without vertices on the ends (full torus...): use the bbox
    Bnd_Box box;
    BRepBndLib::Add(face, box, false);
    double xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    gp_Pnt c((xmin + xmax) / 2, (ymin + ymax) / 2, (zmin + zmax) / 2);
    double t = gp_Vec(ax.Location(), c).Dot(gp_Vec(ax.Direction()));
    tMin = tMax = t;
  }
  fa.tMin = tMin;
  fa.tMax = tMax;
  return fa;
}

gp_Pnt alongAxis(const gp_Ax1& ax, double t) { return ax.Location().Translated(gp_Vec(ax.Direction()) * t); }

void addUnique(std::vector<SnapCandidate>& out, SnapMode mode, const gp_Ax3& frame, const char* label = nullptr) {
  for (const auto& c : out)
    if (c.mode == mode && c.frame.Location().Distance(frame.Location()) < 1e-7) return;
  out.push_back({mode, frame, label ? label : labelOf(mode)});
}

/// Snap candidates on an edge. `normal` (optional) overrides Z (face normal when hovering a face).
void edgeCandidates(std::vector<SnapCandidate>& out, const TopoDS_Edge& edge, const gp_Dir* normal) {
  BRepAdaptor_Curve curve(edge);
  double t0 = curve.FirstParameter(), t1 = curve.LastParameter();
  if (!std::isfinite(t0) || !std::isfinite(t1)) return;
  gp_Pnt p0 = curve.Value(t0), p1 = curve.Value(t1), pm = curve.Value((t0 + t1) / 2);
  auto zAt = [&](double t) -> gp_Dir {
    if (normal) return *normal;
    if (auto ax = circleAxis(curve)) return ax->Direction();
    gp_Pnt p;
    gp_Vec d;
    curve.D1(t, p, d);
    if (d.Magnitude() < 1e-12) return gp_Dir(0, 0, 1);
    return gp_Dir(d);
  };
  bool closed = BRep_Tool::IsClosed(edge) || p0.Distance(p1) < 1e-7;
  if (auto ax = circleAxis(curve)) {
    gp_Ax3 f = frameFromNormal(ax->Location(), normal ? *normal : ax->Direction());
    addUnique(out, SnapMode::CircleCenter, f);
  }
  if (!closed) {
    addUnique(out, SnapMode::EdgeMidpoint, frameFromNormal(pm, zAt((t0 + t1) / 2)));
    addUnique(out, SnapMode::EdgeStart, frameFromNormal(p0, zAt(t0)));
    addUnique(out, SnapMode::EdgeEnd, frameFromNormal(p1, zAt(t1)));
  } else if (!circleAxis(curve)) {
    addUnique(out, SnapMode::EdgeMidpoint, frameFromNormal(pm, zAt((t0 + t1) / 2)));
  }
  (void)isLinearCurve;
}

void faceCandidates(std::vector<SnapCandidate>& out, const TopoDS_Face& face) {
  gp_Pnt centre = core::centroid(face).toPnt();
  std::optional<gp_Dir> normal;
  if (auto pl = core::facePlane(face)) {
    normal = pl->Direction();
    gp_Ax3 f(centre, *normal, pl->XDirection());
    addUnique(out, SnapMode::FaceCenter, f);
  } else if (auto fa = faceAxis(face)) {
    const gp_Ax1& ax = fa->axis;
    addUnique(out, SnapMode::AxisMiddle, frameFromNormal(alongAxis(ax, (fa->tMin + fa->tMax) / 2), ax.Direction()));
    if (fa->tMax - fa->tMin > 1e-7) {
      addUnique(out, SnapMode::AxisStart, frameFromNormal(alongAxis(ax, fa->tMin), ax.Direction()));
      addUnique(out, SnapMode::AxisEnd, frameFromNormal(alongAxis(ax, fa->tMax), ax.Direction()));
    }
  } else {
    BRepAdaptor_Surface surf(face, false);
    if (surf.GetType() == GeomAbs_Sphere) {
      addUnique(out, SnapMode::FaceCenter, frameFromNormal(surf.Sphere().Location(), gp_Dir(0, 0, 1)), "Sphere centre");
    } else {
      gp_Pnt on;
      if (auto n = faceNormalAt(face, centre, &on)) addUnique(out, SnapMode::FaceCenter, frameFromNormal(on, *n));
    }
  }
  const gp_Dir* nptr = normal ? &*normal : nullptr;
  for (TopExp_Explorer ex(face, TopAbs_EDGE); ex.More(); ex.Next()) {
    const TopoDS_Edge& e = TopoDS::Edge(ex.Current());
    // on curved faces an edge's own direction is more useful than a varying normal
    edgeCandidates(out, e, nptr);
  }
}

TopoDS_Shape resolveLocal(const Document& doc, const ShapeRef& ref, const core::BodyStateMap* state) {
  return core::resolveShape(doc, ref, state);
}

}  // namespace

const char* snapModeName(SnapMode m) {
  for (const auto& e : kModes)
    if (e.mode == m) return e.name;
  return "Auto";
}

SnapMode snapModeFromName(const std::string& name) {
  for (const auto& e : kModes)
    if (name == e.name) return e.mode;
  return SnapMode::Auto;
}

gp_Ax3 frameFromNormal(const gp_Pnt& origin, const gp_Dir& z) {
  // Prefer world X as the X direction; when Z is (anti)parallel to X, use Y.
  gp_Dir ref = std::abs(z.Dot(gp_Dir(1, 0, 0))) < 0.9 ? gp_Dir(1, 0, 0) : gp_Dir(0, 1, 0);
  gp_Vec x = gp_Vec(ref) - gp_Vec(z) * ref.Dot(z);
  return gp_Ax3(origin, z, gp_Dir(x));
}

QJsonObject JointOrigin::toJson() const {
  QJsonObject o;
  o["ref"] = ref.toJson();
  o["component"] = core::idToJson(component);
  o["mode"] = snapModeName(mode);
  o["pick"] = core::toJson(pickPoint);
  o["frame"] = core::ax3ToJson(frame);
  o["valid"] = valid;
  return o;
}

JointOrigin JointOrigin::fromJson(const QJsonValue& v) {
  JointOrigin jo;
  if (!v.isObject()) return jo;
  QJsonObject o = v.toObject();
  jo.ref = ShapeRef::fromJson(o["ref"].toObject());
  jo.component = core::idFromJson(o["component"]);
  jo.mode = snapModeFromName(core::ss(o["mode"].toString()));
  jo.pickPoint = core::vec3FromJson(o["pick"]);
  jo.frame = core::ax3FromJson(o["frame"]);
  jo.valid = o["valid"].toBool(true);
  return jo;
}

gp_Ax3 JointOrigin::toWorld(const Document& doc) const {
  if (component == kInvalidId || !doc.component(component)) return frame;
  return frame.Transformed(doc.worldPlacement(component));
}

std::vector<SnapCandidate> snapCandidates(const Document& doc, const ShapeRef& ref, const core::BodyStateMap* state) {
  std::vector<SnapCandidate> out;
  switch (ref.kind) {
    case ShapeKind::Face: {
      TopoDS_Shape s = resolveLocal(doc, ref, state);
      if (!s.IsNull() && s.ShapeType() == TopAbs_FACE) faceCandidates(out, TopoDS::Face(s));
      break;
    }
    case ShapeKind::Edge: {
      TopoDS_Shape s = resolveLocal(doc, ref, state);
      if (!s.IsNull() && s.ShapeType() == TopAbs_EDGE) edgeCandidates(out, TopoDS::Edge(s), nullptr);
      break;
    }
    case ShapeKind::Vertex: {
      TopoDS_Shape s = resolveLocal(doc, ref, state);
      if (!s.IsNull() && s.ShapeType() == TopAbs_VERTEX)
        addUnique(out, SnapMode::Vertex, frameFromNormal(BRep_Tool::Pnt(TopoDS::Vertex(s)), gp_Dir(0, 0, 1)));
      break;
    }
    case ShapeKind::Body: {
      TopoDS_Shape s = resolveLocal(doc, ref, state);
      if (s.IsNull()) break;
      Bnd_Box box;
      BRepBndLib::Add(s, box, false);
      if (!box.IsVoid()) {
        double xmin, ymin, zmin, xmax, ymax, zmax;
        box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
        addUnique(out, SnapMode::BodyCenter,
                  gp_Ax3(gp_Pnt((xmin + xmax) / 2, (ymin + ymax) / 2, (zmin + zmax) / 2), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0)));
      }
      break;
    }
    case ShapeKind::Component:
      addUnique(out, SnapMode::ComponentOrigin, gp_Ax3());
      break;
    default: {
      if (auto pl = core::resolvePlane(doc, ref, state)) {
        addUnique(out, SnapMode::Geometry, *pl, "Plane");
      } else if (auto ax = core::resolveAxis(doc, ref, state)) {
        addUnique(out, SnapMode::Geometry, frameFromNormal(ax->Location(), ax->Direction()), "Axis");
      } else if (auto pt = core::resolvePoint(doc, ref, state)) {
        addUnique(out, SnapMode::Geometry, frameFromNormal(*pt, gp_Dir(0, 0, 1)), "Point");
      }
      break;
    }
  }
  return out;
}

std::optional<gp_Ax3> snapFrame(const Document& doc, const ShapeRef& ref, SnapMode mode, const gp_Pnt& localPick,
                                const core::BodyStateMap* state) {
  if (mode == SnapMode::PickPoint && ref.kind == ShapeKind::Face) {
    TopoDS_Shape s = resolveLocal(doc, ref, state);
    if (s.IsNull() || s.ShapeType() != TopAbs_FACE) return std::nullopt;
    gp_Pnt on = localPick;
    if (auto n = faceNormalAt(TopoDS::Face(s), localPick, &on)) {
      if (auto pl = core::facePlane(s)) return gp_Ax3(on, *n, pl->XDirection());
      return frameFromNormal(on, *n);
    }
    return std::nullopt;
  }
  std::vector<SnapCandidate> cands = snapCandidates(doc, ref, state);
  if (cands.empty()) return std::nullopt;
  if (mode == SnapMode::Auto) return nearestCandidate(cands, localPick)->frame;
  // several candidates can share a mode (edge midpoints of a face): take the nearest of them
  std::vector<SnapCandidate> same;
  for (const auto& c : cands)
    if (c.mode == mode) same.push_back(c);
  if (same.empty()) return std::nullopt;
  return nearestCandidate(same, localPick)->frame;
}

std::optional<SnapCandidate> nearestCandidate(const std::vector<SnapCandidate>& candidates, const gp_Pnt& p) {
  if (candidates.empty()) return std::nullopt;
  const SnapCandidate* best = &candidates.front();
  double bestD = best->frame.Location().Distance(p);
  for (const auto& c : candidates) {
    double d = c.frame.Location().Distance(p);
    if (d < bestD) {
      bestD = d;
      best = &c;
    }
  }
  return *best;
}

JointOrigin makeJointOrigin(const Document& doc, const ShapeRef& ref, const gp_Pnt& worldPick, SnapMode mode,
                            const core::BodyStateMap* state) {
  JointOrigin jo;
  jo.ref = ref;
  jo.component = componentOfRef(doc, ref);
  jo.mode = mode;
  gp_Pnt local = worldPick;
  if (jo.component != kInvalidId) local = worldPick.Transformed(doc.worldPlacement(jo.component).Inverted());
  jo.pickPoint = core::Vec3::from(local);
  std::string warn;
  jo.valid = refreshJointOrigin(doc, jo, state, &warn);
  if (!jo.valid && ref.hasPoint) {
    jo.frame = frameFromNormal(local, gp_Dir(0, 0, 1));
  }
  return jo;
}

bool refreshJointOrigin(const Document& doc, JointOrigin& origin, const core::BodyStateMap* state, std::string* warning) {
  gp_Pnt local = origin.pickPoint.toPnt();
  std::vector<SnapCandidate> cands = snapCandidates(doc, origin.ref, state);
  if (origin.mode == SnapMode::PickPoint) {
    if (auto f = snapFrame(doc, origin.ref, SnapMode::PickPoint, local, state)) {
      origin.frame = *f;
      origin.valid = true;
      return true;
    }
  }
  if (cands.empty()) {
    if (warning) *warning = "joint origin reference lost (" + origin.ref.toString() + "); using stored frame";
    return false;
  }
  if (origin.mode == SnapMode::Auto) {
    // snap onto the nearest candidate and remember which kind it was
    auto best = nearestCandidate(cands, local);
    origin.frame = best->frame;
    origin.mode = best->mode;
    origin.valid = true;
    return true;
  }
  std::vector<SnapCandidate> same;
  for (const auto& c : cands)
    if (c.mode == origin.mode) same.push_back(c);
  if (same.empty()) {
    if (warning) *warning = std::string("snap '") + labelOf(origin.mode) + "' no longer exists; using stored frame";
    return false;
  }
  origin.frame = nearestCandidate(same, local)->frame;
  origin.valid = true;
  return true;
}

}  // namespace fabricad::assembly
