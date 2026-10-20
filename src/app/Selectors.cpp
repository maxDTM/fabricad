// FabriCAD app: textual geometry selectors (see Selectors.h and docs/SCRIPTING.md).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/Selectors.h"

#include "app/AppActions.h"
#include "core/Document.h"
#include "core/Feature.h"
#include "core/GeometryRefs.h"
#include "core/Selection.h"
#include "core/ShapeUtils.h"
#include "sketch/SketchFeature.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRep_Tool.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Lin.hxx>
#include <gp_Quaternion.hxx>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>
#include <cmath>
#include <format>
#include <limits>
#include <sstream>

namespace fabricad::app {

using core::ObjectId;
using core::ShapeKind;
using core::ShapeRef;
using core::Vec3;

namespace {

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::string trim(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
  return s;
}

bool parseId(const std::string& spec, ObjectId& id) {
  if (spec.size() < 2 || spec[0] != '#') return false;
  unsigned long long v = 0;
  auto [p, ec] = std::from_chars(spec.data() + 1, spec.data() + spec.size(), v);
  if (ec != std::errc() || p != spec.data() + spec.size()) return false;
  id = static_cast<ObjectId>(v);
  return true;
}

bool parseIndex(const std::string& spec, int& index) {
  if (spec.size() < 2 || spec[0] != '#') return false;
  int v = 0;
  auto [p, ec] = std::from_chars(spec.data() + 1, spec.data() + spec.size(), v);
  if (ec != std::errc() || p != spec.data() + spec.size()) return false;
  index = v;
  return true;
}

bool parseDouble(const std::string& s, double& out) {
  std::string t = trim(s);
  if (t.empty()) return false;
  char* end = nullptr;
  out = std::strtod(t.c_str(), &end);
  return end && *end == '\0';
}

std::string fmt(double v) {
  if (std::abs(v - std::round(v)) < 1e-9) return std::format("{}", static_cast<long long>(std::llround(v)));
  return std::format("{:.4g}", v);
}

std::string fmtPnt(const Vec3& p) { return std::format("({}, {}, {})", fmt(p.x), fmt(p.y), fmt(p.z)); }

// --- world-space body geometry ---------------------------------------------------------------

/// Sub-shapes of `shape` in indexed-map order (same order as the body's local shape, since the
/// world shape is the local one with a location applied).
std::vector<TopoDS_Shape> subs(const TopoDS_Shape& shape, TopAbs_ShapeEnum type) { return core::subShapes(shape, type); }

/// Build a ref for sub-shape number `index` (1-based, world shape) of a body: the index is the same
/// in the local shape, where ShapeRef::subShape computes the signature; the pick point is world.
ShapeRef bodySubRef(const core::Document& doc, ObjectId bodyId, TopAbs_ShapeEnum type, int index, const Vec3& worldPoint) {
  const TopoDS_Shape& local = doc.bodyShape(bodyId);
  TopoDS_Shape sub = core::subShapeAt(local, type, index);
  ShapeRef r = sub.IsNull() ? ShapeRef::make(bodyId, core::fromTopAbs(type), index) : ShapeRef::subShape(bodyId, local, sub);
  r.point = worldPoint;
  r.hasPoint = true;
  if (!sub.IsNull()) {
    std::string name = doc.subShapeName(bodyId, sub);
    if (!name.empty()) r.name = name;
  }
  return r;
}

Vec3 shapeCenter(const TopoDS_Shape& s) {
  if (s.ShapeType() == TopAbs_VERTEX) return Vec3::from(BRep_Tool::Pnt(TopoDS::Vertex(s)));
  return core::computeSignature(s).center;
}

double pointDistance(const TopoDS_Shape& s, const Vec3& p, Vec3* nearest = nullptr) {
  if (s.IsNull()) return std::numeric_limits<double>::infinity();
  if (s.ShapeType() == TopAbs_VERTEX) {
    Vec3 v = Vec3::from(BRep_Tool::Pnt(TopoDS::Vertex(s)));
    if (nearest) *nearest = v;
    return v.distance(p);
  }
  try {
    BRepExtrema_DistShapeShape d(BRepBuilderAPI_MakeVertex(p.toPnt()).Vertex(), s);
    if (!d.IsDone() || d.NbSolution() < 1) return std::numeric_limits<double>::infinity();
    if (nearest) *nearest = Vec3::from(d.PointOnShape2(1));
    return d.Value();
  } catch (...) {
    return std::numeric_limits<double>::infinity();
  }
}

/// Face type word -> GeomAbs surface type. -1 = any, -2 = unknown word.
int faceTypeWord(const std::string& w) {
  if (w == "plane" || w == "planar" || w == "flat") return GeomAbs_Plane;
  if (w == "cylinder" || w == "cylindrical" || w == "cyl") return GeomAbs_Cylinder;
  if (w == "cone" || w == "conical") return GeomAbs_Cone;
  if (w == "sphere" || w == "spherical") return GeomAbs_Sphere;
  if (w == "torus" || w == "toroidal") return GeomAbs_Torus;
  if (w == "any" || w == "all" || w.empty()) return -1;
  return -2;
}

int edgeTypeWord(const std::string& w) {
  if (w == "line" || w == "linear" || w == "straight") return GeomAbs_Line;
  if (w == "circle" || w == "circular" || w == "arc" || w == "round") return GeomAbs_Circle;
  if (w == "any" || w == "all" || w.empty()) return -1;
  return -2;
}

/// Outward side names -> world direction.
std::optional<Vec3> sideDirection(const std::string& w) {
  if (w == "top" || w == "up" || w == "+z") return Vec3{0, 0, 1};
  if (w == "bottom" || w == "down" || w == "-z") return Vec3{0, 0, -1};
  if (w == "left" || w == "-x") return Vec3{-1, 0, 0};
  if (w == "right" || w == "+x") return Vec3{1, 0, 0};
  if (w == "front" || w == "-y") return Vec3{0, -1, 0};
  if (w == "back" || w == "rear" || w == "+y") return Vec3{0, 1, 0};
  return std::nullopt;
}

std::optional<Vec3> axisWord(const std::string& w) {
  if (w == "x") return Vec3{1, 0, 0};
  if (w == "y") return Vec3{0, 1, 0};
  if (w == "z" || w == "vertical") return Vec3{0, 0, 1};
  return std::nullopt;
}

struct FaceInfo {
  int index = 0;
  TopoDS_Shape shape;
  core::GeomSignature sig;
  std::optional<gp_Ax3> plane;
};

std::vector<FaceInfo> faceInfos(const TopoDS_Shape& world) {
  std::vector<FaceInfo> out;
  int i = 0;
  for (const TopoDS_Shape& f : subs(world, TopAbs_FACE)) {
    FaceInfo fi;
    fi.index = ++i;
    fi.shape = f;
    fi.sig = core::computeSignature(f);
    fi.plane = core::facePlane(f);
    out.push_back(std::move(fi));
  }
  return out;
}

/// Split "cylinder@1,2,3" / "@1,2,3" / "top" into (word, point).
void splitSpec(const std::string& spec, std::string& word, std::optional<Vec3>& at) {
  word = lower(spec);
  at.reset();
  auto a = spec.find('@');
  if (a == std::string::npos) return;
  Vec3 p;
  if (parsePoint(spec.substr(a + 1), p) == 3) {
    at = p;
    word = lower(spec.substr(0, a));
  }
}

/// Faces of one body matching a spec. Singular picks the best; plural returns all.
SelectorResult facesOf(const core::Document& doc, ObjectId bodyId, const std::string& spec, bool plural) {
  SelectorResult res;
  TopoDS_Shape world = worldBodyShape(doc, bodyId);
  if (world.IsNull()) {
    res.error = std::format("body #{} has no shape", bodyId);
    return res;
  }
  const std::vector<FaceInfo> faces = faceInfos(world);
  int index = 0;
  if (parseIndex(spec, index)) {
    if (index < 1 || index > static_cast<int>(faces.size())) {
      res.error = std::format("face index {} out of range (1..{})", index, faces.size());
      return res;
    }
    res.refs.push_back(bodySubRef(doc, bodyId, TopAbs_FACE, index, faces[index - 1].sig.center));
    return res;
  }
  // history name ("Extrude1/End")
  if (TopoDS_Shape named = doc.namedSubShape(bodyId, spec); !named.IsNull() && named.ShapeType() == TopAbs_FACE) {
    int idx = core::indexOfSubShape(doc.bodyShape(bodyId), named, TopAbs_FACE);
    if (idx > 0) {
      res.refs.push_back(bodySubRef(doc, bodyId, TopAbs_FACE, idx, faces[idx - 1].sig.center));
      return res;
    }
  }
  std::string word;
  std::optional<Vec3> at;
  splitSpec(spec, word, at);
  if (!at && (word == "all" || word == "*")) {
    for (const FaceInfo& f : faces) res.refs.push_back(bodySubRef(doc, bodyId, TopAbs_FACE, f.index, f.sig.center));
    return res;
  }
  if (!at && (word == "largest" || word == "smallest")) {
    const FaceInfo* best = nullptr;
    for (const FaceInfo& f : faces)
      if (!best || (word == "largest" ? f.sig.size > best->sig.size : f.sig.size < best->sig.size)) best = &f;
    if (best) res.refs.push_back(bodySubRef(doc, bodyId, TopAbs_FACE, best->index, best->sig.center));
    return res;
  }
  if (auto dir = sideDirection(word)) {
    // planar faces whose outward normal points that way; the extreme one (then the largest)
    std::vector<const FaceInfo*> cands;
    for (const FaceInfo& f : faces)
      if (f.plane && f.sig.direction.dot(*dir) > 0.999) cands.push_back(&f);
    if (cands.empty()) {
      res.error = std::format("body '{}' has no planar face facing {}", doc.body(bodyId) ? doc.body(bodyId)->name : "?", word);
      return res;
    }
    auto along = [&](const FaceInfo* f) { return f->sig.center.dot(*dir); };
    if (at) {
      std::sort(cands.begin(), cands.end(), [&](const FaceInfo* a, const FaceInfo* b) {
        return pointDistance(a->shape, *at) < pointDistance(b->shape, *at);
      });
      res.refs.push_back(bodySubRef(doc, bodyId, TopAbs_FACE, cands[0]->index, *at));
      return res;
    }
    double extreme = -std::numeric_limits<double>::infinity();
    for (const FaceInfo* f : cands) extreme = std::max(extreme, along(f));
    std::vector<const FaceInfo*> top;
    for (const FaceInfo* f : cands)
      if (along(f) > extreme - 1e-6) top.push_back(f);
    std::sort(top.begin(), top.end(), [](const FaceInfo* a, const FaceInfo* b) { return a->sig.size > b->sig.size; });
    if (plural)
      for (const FaceInfo* f : cands) res.refs.push_back(bodySubRef(doc, bodyId, TopAbs_FACE, f->index, f->sig.center));
    else
      res.refs.push_back(bodySubRef(doc, bodyId, TopAbs_FACE, top[0]->index, top[0]->sig.center));
    return res;
  }
  const int type = faceTypeWord(word);
  if (type == -2) {
    res.error = std::format("unknown face spec '{}' (use top/bottom/left/right/front/back, #n, @x,y,z, plane, cylinder, "
                            "largest, all or a history name)",
                            spec);
    return res;
  }
  std::vector<const FaceInfo*> cands;
  for (const FaceInfo& f : faces)
    if (type < 0 || f.sig.geomType == type) cands.push_back(&f);
  if (cands.empty()) {
    res.error = std::format("no {} face found", word.empty() ? "matching" : word);
    return res;
  }
  if (at) {
    std::sort(cands.begin(), cands.end(), [&](const FaceInfo* a, const FaceInfo* b) {
      return pointDistance(a->shape, *at) < pointDistance(b->shape, *at);
    });
    if (plural)
      for (const FaceInfo* f : cands) res.refs.push_back(bodySubRef(doc, bodyId, TopAbs_FACE, f->index, f->sig.center));
    else
      res.refs.push_back(bodySubRef(doc, bodyId, TopAbs_FACE, cands[0]->index, *at));
    return res;
  }
  if (!plural && cands.size() > 1) {
    res.error = std::format("'{}' matches {} faces; use faces:{} for all of them or {}@x,y,z for the nearest", spec,
                            cands.size(), spec, spec);
    return res;
  }
  for (const FaceInfo* f : cands) res.refs.push_back(bodySubRef(doc, bodyId, TopAbs_FACE, f->index, f->sig.center));
  return res;
}

struct EdgeInfo {
  int index = 0;
  TopoDS_Shape shape;
  core::GeomSignature sig;
};

std::vector<EdgeInfo> edgeInfos(const TopoDS_Shape& world) {
  std::vector<EdgeInfo> out;
  int i = 0;
  for (const TopoDS_Shape& e : subs(world, TopAbs_EDGE)) {
    EdgeInfo ei;
    ei.index = ++i;
    ei.shape = e;
    ei.sig = core::computeSignature(e);
    out.push_back(std::move(ei));
  }
  return out;
}

/// Edges of a body (optionally restricted to the edges bounding `withinFaces`, world sub-shapes).
SelectorResult edgesOf(const core::Document& doc, ObjectId bodyId, const std::string& spec, bool plural,
                       const std::vector<TopoDS_Shape>* withinFaces) {
  SelectorResult res;
  TopoDS_Shape world = worldBodyShape(doc, bodyId);
  if (world.IsNull()) {
    res.error = std::format("body #{} has no shape", bodyId);
    return res;
  }
  std::vector<EdgeInfo> all = edgeInfos(world);
  std::vector<const EdgeInfo*> edges;
  if (withinFaces) {
    TopTools_IndexedMapOfShape faceEdges;
    for (const TopoDS_Shape& f : *withinFaces) TopExp::MapShapes(f, TopAbs_EDGE, faceEdges);
    for (const EdgeInfo& e : all)
      if (faceEdges.Contains(e.shape)) edges.push_back(&e);
  } else {
    for (const EdgeInfo& e : all) edges.push_back(&e);
  }
  auto push = [&](const EdgeInfo* e, const Vec3& at) { res.refs.push_back(bodySubRef(doc, bodyId, TopAbs_EDGE, e->index, at)); };
  int index = 0;
  if (parseIndex(spec, index)) {
    if (index < 1 || index > static_cast<int>(all.size())) {
      res.error = std::format("edge index {} out of range (1..{})", index, all.size());
      return res;
    }
    push(&all[index - 1], all[index - 1].sig.center);
    return res;
  }
  if (TopoDS_Shape named = doc.namedSubShape(bodyId, spec); !named.IsNull() && named.ShapeType() == TopAbs_EDGE) {
    int idx = core::indexOfSubShape(doc.bodyShape(bodyId), named, TopAbs_EDGE);
    if (idx > 0) {
      push(&all[idx - 1], all[idx - 1].sig.center);
      return res;
    }
  }
  std::string word;
  std::optional<Vec3> at;
  splitSpec(spec, word, at);
  // "edges:top" = the edges bounding the top face (any face spec works)
  if (!at && !withinFaces && (sideDirection(word) || word == "largest" || word == "smallest")) {
    SelectorResult faces = facesOf(doc, bodyId, spec, false);
    if (!faces.ok()) return faces;
    std::vector<TopoDS_Shape> fs;
    for (const ShapeRef& r : faces.refs) fs.push_back(core::subShapeAt(world, TopAbs_FACE, r.index));
    return edgesOf(doc, bodyId, "all", true, &fs);
  }
  std::vector<const EdgeInfo*> cands;
  if (word == "all" || word == "*" || word.empty()) {
    cands = edges;
  } else if (auto ax = axisWord(word)) {
    for (const EdgeInfo* e : edges)
      if (e->sig.geomType == GeomAbs_Line && std::abs(e->sig.direction.dot(*ax)) > 0.999) cands.push_back(e);
  } else if (word == "horizontal") {
    for (const EdgeInfo* e : edges)
      if (e->sig.geomType == GeomAbs_Line && std::abs(e->sig.direction.z) < 1e-3) cands.push_back(e);
  } else if (int type = edgeTypeWord(word); type != -2) {
    for (const EdgeInfo* e : edges)
      if (type < 0 || e->sig.geomType == type) cands.push_back(e);
  } else if (word.starts_with("z=") || word.starts_with("x=") || word.starts_with("y=")) {
    // edges lying at a coordinate: both ends (bbox) at that value
    double v = 0;
    if (!parseDouble(word.substr(2), v)) {
      res.error = "bad coordinate in '" + spec + "'";
      return res;
    }
    for (const EdgeInfo* e : edges) {
      double lo = word[0] == 'x' ? e->sig.bboxMin.x : word[0] == 'y' ? e->sig.bboxMin.y : e->sig.bboxMin.z;
      double hi = word[0] == 'x' ? e->sig.bboxMax.x : word[0] == 'y' ? e->sig.bboxMax.y : e->sig.bboxMax.z;
      if (std::abs(lo - v) < 1e-4 && std::abs(hi - v) < 1e-4) cands.push_back(e);
    }
  } else {
    res.error = std::format("unknown edge spec '{}' (use #n, @x,y,z, line, circle, x, y, z, horizontal, z=<value>, "
                            "a face side such as top, all, or a history name)",
                            spec);
    return res;
  }
  if (cands.empty()) {
    res.error = std::format("no edge matches '{}'", spec);
    return res;
  }
  if (at) {
    std::sort(cands.begin(), cands.end(), [&](const EdgeInfo* a, const EdgeInfo* b) {
      return pointDistance(a->shape, *at) < pointDistance(b->shape, *at);
    });
    if (plural)
      for (const EdgeInfo* e : cands) push(e, e->sig.center);
    else
      push(cands[0], *at);
    return res;
  }
  if (!plural && cands.size() > 1) {
    res.error = std::format("'{}' matches {} edges; use edges:{} for all of them or {}@x,y,z for the nearest", spec,
                            cands.size(), spec, spec);
    return res;
  }
  for (const EdgeInfo* e : cands) push(e, e->sig.center);
  return res;
}

SelectorResult verticesOf(const core::Document& doc, ObjectId bodyId, const std::string& spec, bool plural) {
  SelectorResult res;
  TopoDS_Shape world = worldBodyShape(doc, bodyId);
  if (world.IsNull()) {
    res.error = std::format("body #{} has no shape", bodyId);
    return res;
  }
  std::vector<TopoDS_Shape> vs = subs(world, TopAbs_VERTEX);
  int index = 0;
  if (parseIndex(spec, index)) {
    if (index < 1 || index > static_cast<int>(vs.size())) {
      res.error = std::format("vertex index {} out of range (1..{})", index, vs.size());
      return res;
    }
    res.refs.push_back(bodySubRef(doc, bodyId, TopAbs_VERTEX, index, shapeCenter(vs[index - 1])));
    return res;
  }
  std::string word;
  std::optional<Vec3> at;
  splitSpec(spec, word, at);
  if (at) {
    int best = -1;
    double bestD = std::numeric_limits<double>::infinity();
    for (int i = 0; i < static_cast<int>(vs.size()); ++i) {
      double d = shapeCenter(vs[i]).distance(*at);
      if (d < bestD) {
        bestD = d;
        best = i;
      }
    }
    if (best >= 0) res.refs.push_back(bodySubRef(doc, bodyId, TopAbs_VERTEX, best + 1, shapeCenter(vs[best])));
    return res;
  }
  if (word == "all" || word == "*" || word.empty()) {
    if (!plural && vs.size() > 1) {
      res.error = std::format("the body has {} vertices; use vertices:all or vertex:@x,y,z", vs.size());
      return res;
    }
    for (int i = 0; i < static_cast<int>(vs.size()); ++i)
      res.refs.push_back(bodySubRef(doc, bodyId, TopAbs_VERTEX, i + 1, shapeCenter(vs[i])));
    return res;
  }
  res.error = std::format("unknown vertex spec '{}' (use #n, @x,y,z or all)", spec);
  return res;
}

const sketch::SketchFeature* sketchOf(const core::Document& doc, ObjectId id) {
  return doc.featureAs<sketch::SketchFeature>(id);
}

SelectorResult profilesOf(const core::Document& doc, const sketch::SketchFeature& sk, const std::string& spec, bool plural) {
  SelectorResult res;
  std::vector<core::ProfileInfo> profiles = sk.profiles();
  if (profiles.empty()) {
    res.error = std::format("sketch '{}' has no closed profiles", sk.name());
    return res;
  }
  std::string word;
  std::optional<Vec3> at;
  word = lower(spec);
  if (auto a = spec.find('@'); a != std::string::npos) {
    Vec3 p;
    const int n = parsePoint(spec.substr(a + 1), p);
    if (n == 2) {
      // sketch coordinates
      if (const sketch::ProfileRegion* r = sk.regionAt({p.x, p.y})) {
        res.refs.push_back(sk.profileRef(*r));
        return res;
      }
      res.error = std::format("no profile of '{}' contains sketch point ({}, {})", sk.name(), fmt(p.x), fmt(p.y));
      return res;
    }
    if (n == 3) {
      sketch::Pt2 local = sk.toLocal(p);
      if (const sketch::ProfileRegion* r = sk.regionAt(local)) {
        res.refs.push_back(sk.profileRef(*r));
        return res;
      }
      res.error = std::format("no profile of '{}' contains {}", sk.name(), fmtPnt(p));
      return res;
    }
    res.error = "bad point in '" + spec + "'";
    return res;
  }
  int index = 0;
  if (parseIndex(spec, index)) {
    if (index < 1 || index > static_cast<int>(profiles.size())) {
      res.error = std::format("profile index {} out of range (1..{})", index, profiles.size());
      return res;
    }
    res.refs.push_back(profiles[index - 1].ref);
    return res;
  }
  if (word == "all" || word == "*") {
    for (const auto& p : profiles) res.refs.push_back(p.ref);
    return res;
  }
  if (word == "largest" || word == "smallest") {
    const core::ProfileInfo* best = nullptr;
    for (const auto& p : profiles)
      if (!best || (word == "largest" ? p.area > best->area : p.area < best->area)) best = &p;
    res.refs.push_back(best->ref);
    return res;
  }
  if (word.empty() || word == "any") {
    if (profiles.size() == 1 || plural) {
      for (const auto& p : profiles) res.refs.push_back(p.ref);
      return res;
    }
    res.error = std::format("sketch '{}' has {} profiles; use profile:@x,y, profile:#n, largest or profiles:all", sk.name(),
                            profiles.size());
    return res;
  }
  res.error = std::format("unknown profile spec '{}' (use @x,y, #n, largest, smallest or all)", spec);
  return res;
}

SelectorResult curvesOf(const core::Document& doc, const sketch::SketchFeature& sk, const std::string& spec, bool plural, bool points) {
  (void)doc;
  SelectorResult res;
  const sketch::Sketch& s = sk.sketch();
  int id = 0;
  if (parseIndex(spec, id)) {
    if (!s.geometry(id)) {
      res.error = std::format("sketch '{}' has no geometry #{}", sk.name(), id);
      return res;
    }
    if (points) res.refs.push_back(ShapeRef::sketchPoint(sk.id(), id, core::SketchPointRole::Standalone));
    else res.refs.push_back(ShapeRef::sketchCurve(sk.id(), id));
    return res;
  }
  std::string word;
  std::optional<Vec3> at;
  word = lower(spec);
  if (auto a = spec.find('@'); a != std::string::npos) {
    Vec3 p;
    const int n = parsePoint(spec.substr(a + 1), p);
    sketch::Pt2 local = n == 3 ? sk.toLocal(p) : sketch::Pt2{p.x, p.y};
    if (n < 2) {
      res.error = "bad point in '" + spec + "'";
      return res;
    }
    const double tol = 1.0;
    auto hit = points ? s.hitPoint(local, tol) : s.hitCurve(local, tol);
    if (!hit) {
      res.error = std::format("no sketch {} of '{}' near ({}, {})", points ? "point" : "curve", sk.name(), fmt(local.x), fmt(local.y));
      return res;
    }
    if (points) res.refs.push_back(ShapeRef::sketchPoint(sk.id(), hit->ref.geo, sketch::toRole(hit->ref.pos)));
    else res.refs.push_back(ShapeRef::sketchCurve(sk.id(), hit->ref.geo));
    return res;
  }
  if (word == "all" || word == "*" || (word.empty() && plural)) {
    for (const sketch::Geometry& g : s.geometries()) {
      if (points) {
        if (g.type == sketch::GeomType::Point) res.refs.push_back(ShapeRef::sketchPoint(sk.id(), g.id, core::SketchPointRole::Standalone));
      } else if (g.type != sketch::GeomType::Point) {
        res.refs.push_back(ShapeRef::sketchCurve(sk.id(), g.id));
      }
    }
    return res;
  }
  res.error = std::format("unknown sketch {} spec '{}' (use #id, @x,y or all)", points ? "point" : "curve", spec);
  return res;
}

ShapeRef refForFeature(const core::Feature& f) { return featureRef(f); }

/// One "kind:spec" step.
struct Segment {
  std::string kind;
  std::string spec;
};

bool splitSegments(const std::string& text, std::vector<Segment>& out, std::string* err) {
  // split on '/' except inside a history name after "edge:"/"face:" (names contain '/'): a
  // segment boundary is a '/' followed by a known kind word and ':'.
  static const char* kinds[] = {"bodies", "body", "faces", "face", "edges", "edge", "vertices", "vertex", "sketch",
                                "profiles", "profile", "curves", "curve", "points", "point", "feature", "component",
                                "comp", "plane", "axis", "origin", "joint", "all"};
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t colon = text.find(':', pos);
    if (colon == std::string::npos) {
      std::string rest = trim(text.substr(pos));
      if (!rest.empty()) {
        if (rest == "all" || rest == "edges" || rest == "faces" || rest == "vertices" || rest == "profiles" || rest == "bodies") {
          out.push_back({lower(rest), "all"});
          return true;
        }
        if (out.empty()) {
          // bare name: a body, feature or component by name
          out.push_back({"name", rest});
          return true;
        }
        if (err) *err = "unexpected '" + rest + "' in selector '" + text + "'";
        return false;
      }
      return true;
    }
    Segment seg;
    seg.kind = lower(trim(text.substr(pos, colon - pos)));
    size_t specStart = colon + 1;
    size_t next = std::string::npos;
    size_t search = specStart;
    while (true) {
      size_t slash = text.find('/', search);
      if (slash == std::string::npos) break;
      bool boundary = false;
      for (const char* k : kinds) {
        const size_t kl = std::strlen(k);
        if (text.compare(slash + 1, kl, k) == 0) {
          size_t after = slash + 1 + kl;
          if (after == text.size() || text[after] == ':' || text[after] == '/') boundary = true;
          if (boundary) break;
        }
      }
      if (boundary) {
        next = slash;
        break;
      }
      search = slash + 1;
    }
    seg.spec = trim(text.substr(specStart, next == std::string::npos ? std::string::npos : next - specStart));
    out.push_back(seg);
    if (next == std::string::npos) return true;
    pos = next + 1;
  }
  return true;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// lookups
// ---------------------------------------------------------------------------------------------

int parsePoint(const std::string& text, Vec3& out) {
  std::string t = trim(text);
  if (!t.empty() && t.front() == '(') t.erase(t.begin());
  if (!t.empty() && t.back() == ')') t.pop_back();
  std::vector<double> v;
  std::stringstream ss(t);
  std::string item;
  while (std::getline(ss, item, ',')) {
    double d;
    if (!parseDouble(item, d)) return 0;
    v.push_back(d);
  }
  if (v.size() < 2 || v.size() > 3) return 0;
  out = Vec3{v[0], v[1], v.size() == 3 ? v[2] : 0.0};
  return static_cast<int>(v.size());
}

const core::Body* findBody(const core::Document& doc, const std::string& spec) {
  ObjectId id;
  if (parseId(spec, id)) return doc.body(id);
  const core::Body* ci = nullptr;
  for (const core::Body* b : doc.bodies()) {
    if (b->name == spec) return b;
    if (!ci && lower(b->name) == lower(spec)) ci = b;
  }
  return ci;
}

const core::Feature* findFeature(const core::Document& doc, const std::string& spec) {
  ObjectId id;
  if (parseId(spec, id)) return doc.feature(id);
  const core::Feature* ci = nullptr;
  for (const auto& f : doc.features()) {
    if (f->name() == spec) return f.get();
    if (!ci && lower(f->name()) == lower(spec)) ci = f.get();
  }
  return ci;
}

const core::Component* findComponent(const core::Document& doc, const std::string& spec) {
  ObjectId id;
  if (parseId(spec, id)) return doc.component(id);
  if (lower(spec) == "root") return &doc.rootComponent();
  const core::Component* ci = nullptr;
  for (const core::Component* c : doc.components()) {
    if (c->name == spec) return c;
    if (!ci && lower(c->name) == lower(spec)) ci = c;
  }
  return ci;
}

TopoDS_Shape worldBodyShape(const core::Document& doc, ObjectId bodyId) {
  const core::Body* b = doc.body(bodyId);
  const TopoDS_Shape& local = doc.bodyShape(bodyId);
  if (!b || local.IsNull()) return {};
  gp_Trsf t = doc.worldPlacement(b->component);
  if (t.Form() == gp_Identity) return local;
  return local.Moved(TopLoc_Location(t));
}

// ---------------------------------------------------------------------------------------------
// selectors
// ---------------------------------------------------------------------------------------------

SelectorResult resolveSelector(const core::Document& doc, const std::string& textIn) {
  SelectorResult res;
  const std::string text = trim(textIn);
  if (text.empty()) {
    res.error = "empty selector";
    return res;
  }
  std::vector<Segment> segs;
  if (!splitSegments(text, segs, &res.error)) return res;
  if (segs.empty()) {
    res.error = "empty selector";
    return res;
  }
  // context while walking the chain
  ObjectId bodyId = core::kInvalidId;
  const sketch::SketchFeature* sk = nullptr;
  std::vector<TopoDS_Shape> faceContext;  // world faces picked by a previous face segment
  for (size_t i = 0; i < segs.size(); ++i) {
    const Segment& s = segs[i];
    const bool last = i + 1 == segs.size();
    const std::string& k = s.kind;
    if (k == "name") {
      if (const core::Body* b = findBody(doc, s.spec)) {
        res.refs = {ShapeRef::body(b->id)};
        return res;
      }
      if (const core::Component* c = findComponent(doc, s.spec)) {
        res.refs = {ShapeRef::component(c->id)};
        return res;
      }
      if (const core::Feature* f = findFeature(doc, s.spec)) {
        res.refs = {refForFeature(*f)};
        return res;
      }
      res.error = std::format("nothing named '{}' (use body:, component:, feature:, sketch:...)", s.spec);
      return res;
    }
    if (k == "body") {
      const core::Body* b = findBody(doc, s.spec);
      if (!b && !s.spec.empty() && s.spec[0] == '@') {
        // body:@x,y,z = the visible body nearest that world point (0 when the point is on it)
        Vec3 p;
        if (parsePoint(s.spec.substr(1), p) != 3) {
          res.error = "bad point in '" + s.spec + "'";
          return res;
        }
        double best = std::numeric_limits<double>::infinity();
        for (ObjectId id : doc.liveBodies(true)) {
          const double d = pointDistance(worldBodyShape(doc, id), p);
          if (d < best) {
            best = d;
            b = doc.body(id);
          }
        }
      }
      if (!b) {
        res.error = std::format("no body '{}'", s.spec);
        return res;
      }
      bodyId = b->id;
      if (last) res.refs = {ShapeRef::body(bodyId)};
      continue;
    }
    if (k == "component" || k == "comp") {
      const core::Component* c = findComponent(doc, s.spec);
      if (!c) {
        res.error = std::format("no component '{}'", s.spec);
        return res;
      }
      if (last) {
        res.refs = {ShapeRef::component(c->id)};
        return res;
      }
      // component:X/body:Y -> body of that component; component:X/bodies -> all its bodies
      const Segment& n = segs[i + 1];
      if (n.kind == "bodies" || (n.kind == "body" && (n.spec == "all" || n.spec == "*"))) {
        for (ObjectId id : doc.displayedBodies(c->id)) res.refs.push_back(ShapeRef::body(id));
        return res;
      }
      continue;
    }
    if (k == "feature" || k == "joint" || k == "construction") {
      const core::Feature* f = findFeature(doc, s.spec);
      if (!f) {
        res.error = std::format("no feature '{}'", s.spec);
        return res;
      }
      res.refs = {refForFeature(*f)};
      if (k == "feature") res.refs[0] = ShapeRef::feature(f->id());
      return res;
    }
    if (k == "sketch") {
      const core::Feature* f = findFeature(doc, s.spec);
      sk = f ? dynamic_cast<const sketch::SketchFeature*>(f) : nullptr;
      if (!sk) {
        res.error = std::format("no sketch '{}'", s.spec);
        return res;
      }
      if (last) res.refs = {ShapeRef::sketch(sk->id())};
      continue;
    }
    if (k == "plane") {
      std::string u = s.spec;
      std::transform(u.begin(), u.end(), u.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
      if (u == "XY" || u == "XZ" || u == "YZ") {
        res.refs = {ShapeRef::originPlane(u)};
        return res;
      }
      const core::Feature* f = findFeature(doc, s.spec);
      if (f && (f->asPlane() || f->profileSource())) {
        res.refs = {refForFeature(*f)};
        return res;
      }
      res.error = std::format("no plane '{}' (XY, XZ, YZ, a construction plane or a sketch)", s.spec);
      return res;
    }
    if (k == "axis") {
      std::string u = s.spec;
      std::transform(u.begin(), u.end(), u.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
      if (u == "X" || u == "Y" || u == "Z") {
        res.refs = {ShapeRef::originAxis(u)};
        return res;
      }
      const core::Feature* f = findFeature(doc, s.spec);
      if (f && f->asAxis()) {
        res.refs = {refForFeature(*f)};
        return res;
      }
      res.error = std::format("no axis '{}' (X, Y, Z or a construction axis)", s.spec);
      return res;
    }
    if (k == "origin") {
      res.refs = {ShapeRef::originPoint()};
      return res;
    }
    if (k == "point" && !sk && s.spec.find('@') == std::string::npos) {
      const core::Feature* f = findFeature(doc, s.spec);
      if (f && f->asPoint()) {
        res.refs = {refForFeature(*f)};
        return res;
      }
      res.error = std::format("no construction point '{}'", s.spec);
      return res;
    }
    // --- sketch children -------------------------------------------------------------------
    if (k == "profile" || k == "profiles") {
      if (!sk) {
        // search all sketches: the one that has a region at the point (or the only sketch)
        SelectorResult found;
        int hits = 0;
        double foundArea = 0;
        // a world point (@x,y,z) only matches sketches whose plane contains it
        std::optional<Vec3> worldPt;
        if (auto a = s.spec.find('@'); a != std::string::npos) {
          Vec3 p;
          if (parsePoint(s.spec.substr(a + 1), p) == 3) worldPt = p;
        }
        for (const auto& f : doc.features()) {
          const auto* cand = dynamic_cast<const sketch::SketchFeature*>(f.get());
          if (!cand || cand->status() != core::FeatureStatus::Ok) continue;
          if (worldPt) {
            const gp_Ax3& pl = cand->plane();
            const double off = std::abs(Vec3::from(pl.Direction()).dot(*worldPt - Vec3::from(pl.Location())));
            if (off > 1e-3) continue;
          }
          SelectorResult r = profilesOf(doc, *cand, s.spec, k == "profiles");
          if (r.ok() && !r.refs.empty()) {
            // several sketches on one plane: the innermost region (smallest area) is what a click
            // at that point would pick; equal areas stay ambiguous
            double area = 0;
            for (const ShapeRef& ref : r.refs) {
              TopoDS_Shape face = core::resolveProfileFace(doc, ref);
              area += face.IsNull() ? 0.0 : core::area(face);
            }
            if (hits == 0 || area < foundArea - 1e-6) {
              found = r;
              foundArea = area;
              hits = 1;
            } else if (std::abs(area - foundArea) <= 1e-6) {
              ++hits;
            }
          }
        }
        if (hits == 1) return found;
        res.error = hits == 0 ? std::format("no sketch has a profile matching '{}'", s.spec)
                              : std::format("'{}' matches equal profiles in {} sketches; prefix with sketch:<name>/", s.spec, hits);
        return res;
      }
      return profilesOf(doc, *sk, s.spec, k == "profiles");
    }
    if (k == "curve" || k == "curves" || k == "point" || k == "points") {
      if (!sk) {
        // a world point (@x,y,z) finds the sketch whose plane contains it and that has a hit there
        Vec3 p;
        auto a = s.spec.find('@');
        if (a == std::string::npos || parsePoint(s.spec.substr(a + 1), p) != 3) {
          res.error = "sketch curves need a sketch: sketch:<name>/" + k + ":" + s.spec + " (or a world point @x,y,z)";
          return res;
        }
        SelectorResult found;
        int hits = 0;
        for (const auto& f : doc.features()) {
          const auto* cand = dynamic_cast<const sketch::SketchFeature*>(f.get());
          if (!cand || cand->status() != core::FeatureStatus::Ok) continue;
          const gp_Ax3& pl = cand->plane();
          if (std::abs(Vec3::from(pl.Direction()).dot(p - Vec3::from(pl.Location()))) > 1e-3) continue;
          SelectorResult r = curvesOf(doc, *cand, s.spec, k.back() == 's', k == "point" || k == "points");
          if (r.ok() && !r.refs.empty()) {
            found = r;
            ++hits;
          }
        }
        if (hits == 1) return found;
        res.error = hits == 0 ? std::format("no sketch has a {} at '{}'", k, s.spec)
                              : std::format("'{}' matches in {} sketches; prefix with sketch:<name>/", s.spec, hits);
        return res;
      }
      return curvesOf(doc, *sk, s.spec, k.back() == 's', k == "point" || k == "points");
    }
    // --- body children ---------------------------------------------------------------------
    if (k == "face" || k == "faces" || k == "edge" || k == "edges" || k == "vertex" || k == "vertices") {
      const bool plural = k.back() == 's';
      std::vector<ObjectId> bodies;
      if (bodyId != core::kInvalidId) bodies.push_back(bodyId);
      else bodies = doc.liveBodies(false);
      if (bodies.empty()) {
        res.error = "the document has no bodies";
        return res;
      }
      SelectorResult acc;
      int okBodies = 0;
      std::string firstErr;
      for (ObjectId b : bodies) {
        SelectorResult r;
        if (k.starts_with("face")) r = facesOf(doc, b, s.spec, plural);
        else if (k.starts_with("edge")) r = edgesOf(doc, b, s.spec, plural, faceContext.empty() ? nullptr : &faceContext);
        else r = verticesOf(doc, b, s.spec, plural);
        if (!r.ok()) {
          if (firstErr.empty()) firstErr = r.error;
          continue;
        }
        ++okBodies;
        acc.refs.insert(acc.refs.end(), r.refs.begin(), r.refs.end());
      }
      if (okBodies == 0) {
        res.error = firstErr;
        return res;
      }
      if (bodyId == core::kInvalidId && okBodies > 1 && !plural) {
        res.error = std::format("'{}' matches in {} bodies; prefix with body:<name>/", text, okBodies);
        return res;
      }
      if (last) return acc;
      // a face segment followed by edges: remember the faces (world shapes)
      if (k.starts_with("face")) {
        faceContext.clear();
        for (const ShapeRef& r : acc.refs) {
          TopoDS_Shape world = worldBodyShape(doc, r.owner);
          faceContext.push_back(core::subShapeAt(world, TopAbs_FACE, r.index));
          bodyId = r.owner;
        }
        continue;
      }
      res.error = std::format("'{}:' cannot be followed by '{}:'", k, segs[i + 1].kind);
      return res;
    }
    res.error = std::format("unknown selector kind '{}' in '{}'", k, text);
    return res;
  }
  if (res.refs.empty() && res.ok()) res.error = "selector '" + text + "' selects nothing";
  return res;
}

SelectorResult resolveSelectors(const core::Document& doc, const std::vector<std::string>& texts) {
  SelectorResult all;
  for (const std::string& t : texts) {
    const std::string l = lower(trim(t));
    if (l.empty()) continue;
    if (l == "none" || l == "clear" || l == "nothing") continue;
    SelectorResult r = resolveSelector(doc, t);
    if (!r.ok()) {
      all.error = r.error;
      return all;
    }
    for (const ShapeRef& ref : r.refs)
      if (std::find(all.refs.begin(), all.refs.end(), ref) == all.refs.end()) all.refs.push_back(ref);
  }
  return all;
}

// ---------------------------------------------------------------------------------------------
// picking
// ---------------------------------------------------------------------------------------------

std::optional<PickHit> pickAt(const core::Document& doc, const Vec3& p, core::ShapeKindMask kinds, double tolerance,
                              const std::optional<Vec3>& rayDir) {
  struct Cand {
    ShapeRef ref;
    Vec3 at;
    double d;
    int prio;
  };
  std::vector<Cand> cands;
  auto add = [&](const ShapeRef& ref, const Vec3& at, double d, int prio) { cands.push_back({ref, at, d, prio}); };

  // --- sketch entities (profiles shaded in the viewport, curves and points) -------------------
  if (kinds & (ShapeKind::Profile | ShapeKind::SketchCurve | ShapeKind::SketchPoint)) {
    for (const auto& f : doc.features()) {
      const auto* sk = dynamic_cast<const sketch::SketchFeature*>(f.get());
      if (!sk || sk->status() != core::FeatureStatus::Ok || !sk->isVisible() || !doc.isFeatureActive(sk->id())) continue;
      const gp_Ax3& pl = sk->plane();
      const double off = std::abs(Vec3::from(pl.Direction()).dot(p - Vec3::from(pl.Location())));
      if (off > tolerance) continue;
      sketch::Pt2 local = sk->toLocal(p);
      if (core::hasKind(kinds, ShapeKind::SketchPoint))
        if (auto h = sk->sketch().hitPoint(local, tolerance))
          add(ShapeRef::sketchPoint(sk->id(), h->ref.geo, sketch::toRole(h->ref.pos)), sk->toWorldVec(h->point), h->distance, 0);
      if (core::hasKind(kinds, ShapeKind::SketchCurve))
        if (auto h = sk->sketch().hitCurve(local, tolerance))
          add(ShapeRef::sketchCurve(sk->id(), h->ref.geo), sk->toWorldVec(h->point), h->distance, 1);
      if (core::hasKind(kinds, ShapeKind::Profile))
        if (const sketch::ProfileRegion* r = sk->regionAt(local)) add(sk->profileRef(*r), p, off, 2);
    }
  }
  // --- bodies ------------------------------------------------------------------------------------
  if (kinds & core::kBrepKinds) {
    for (ObjectId id : doc.liveBodies(true)) {
      const core::Body* b = doc.body(id);
      if (!b) continue;
      if (const core::Component* c = doc.component(b->component); c && !c->visible) continue;
      TopoDS_Shape world = worldBodyShape(doc, id);
      if (world.IsNull()) continue;
      Vec3 hit = p;
      bool onBody = false;
      if (rayDir && !rayDir->isZero()) {
        try {
          IntCurvesFace_ShapeIntersector inter;
          inter.Load(world, 1e-6);
          inter.Perform(gp_Lin(p.toPnt(), rayDir->toDir()), -RealLast(), RealLast());
          if (inter.IsDone() && inter.NbPnt() > 0) {
            int bestI = -1;
            double bestW = std::numeric_limits<double>::infinity();
            for (int i = 1; i <= inter.NbPnt(); ++i)
              if (inter.WParameter(i) >= -1e-9 && inter.WParameter(i) < bestW) {
                bestW = inter.WParameter(i);
                bestI = i;
              }
            if (bestI > 0) {
              hit = Vec3::from(inter.Pnt(bestI));
              onBody = true;
              const TopoDS_Shape face = inter.Face(bestI);
              const int fi = core::indexOfSubShape(world, face, TopAbs_FACE);
              const double dRay = bestW;  // distance along the ray orders the candidates
              // vertex / edge of that face near the hit point win over the face
              bool finer = false;
              if (core::hasKind(kinds, ShapeKind::Vertex))
                for (const TopoDS_Shape& v : subs(face, TopAbs_VERTEX))
                  if (shapeCenter(v).distance(hit) <= tolerance) {
                    add(bodySubRef(doc, id, TopAbs_VERTEX, core::indexOfSubShape(world, v, TopAbs_VERTEX), hit), hit, dRay, 3);
                    finer = true;
                  }
              if (!finer && core::hasKind(kinds, ShapeKind::Edge))
                for (const TopoDS_Shape& e : subs(face, TopAbs_EDGE))
                  if (pointDistance(e, hit) <= tolerance) {
                    add(bodySubRef(doc, id, TopAbs_EDGE, core::indexOfSubShape(world, e, TopAbs_EDGE), hit), hit, dRay, 4);
                    finer = true;
                  }
              if (!finer && core::hasKind(kinds, ShapeKind::Face) && fi > 0) add(bodySubRef(doc, id, TopAbs_FACE, fi, hit), hit, dRay, 5);
              if (!finer && !core::hasKind(kinds, ShapeKind::Face) && core::hasKind(kinds, ShapeKind::Body)) {
                ShapeRef r = ShapeRef::body(id);
                r.point = hit;
                r.hasPoint = true;
                add(r, hit, dRay, 6);
              }
            }
          }
        } catch (...) {
        }
        continue;
      }
      // nearest entity within the tolerance
      if (core::hasKind(kinds, ShapeKind::Vertex)) {
        int i = 0;
        for (const TopoDS_Shape& v : subs(world, TopAbs_VERTEX)) {
          ++i;
          const Vec3 c = shapeCenter(v);
          const double d = c.distance(p);
          if (d <= tolerance) add(bodySubRef(doc, id, TopAbs_VERTEX, i, c), c, d, 3);
        }
      }
      if (core::hasKind(kinds, ShapeKind::Edge)) {
        int i = 0;
        for (const TopoDS_Shape& e : subs(world, TopAbs_EDGE)) {
          ++i;
          Vec3 n;
          const double d = pointDistance(e, p, &n);
          if (d <= tolerance) add(bodySubRef(doc, id, TopAbs_EDGE, i, n), n, d, 4);
        }
      }
      if (core::hasKind(kinds, ShapeKind::Face) || core::hasKind(kinds, ShapeKind::Body)) {
        int i = 0;
        for (const TopoDS_Shape& f : subs(world, TopAbs_FACE)) {
          ++i;
          Vec3 n;
          const double d = pointDistance(f, p, &n);
          if (d <= tolerance) {
            onBody = true;
            if (core::hasKind(kinds, ShapeKind::Face)) add(bodySubRef(doc, id, TopAbs_FACE, i, n), n, d, 5);
            else {
              ShapeRef r = ShapeRef::body(id);
              r.point = n;
              r.hasPoint = true;
              add(r, n, d, 6);
            }
          }
        }
      }
      (void)onBody;
    }
  }
  // --- construction geometry / components ----------------------------------------------------
  if (kinds & (ShapeKind::ConstructionPlane | ShapeKind::ConstructionAxis | ShapeKind::ConstructionPoint)) {
    for (const auto& f : doc.features()) {
      if (!doc.isFeatureActive(f->id()) || f->status() != core::FeatureStatus::Ok) continue;
      const ShapeRef r = refForFeature(*f);
      if (!(kinds & core::mask(r.kind)) || r.kind == ShapeKind::Feature || r.kind == ShapeKind::Sketch || r.kind == ShapeKind::Joint) continue;
      if (f->asPoint()) {
        const Vec3 c = Vec3::from(*f->asPoint());
        if (c.distance(p) <= tolerance) add(r, c, c.distance(p), 7);
      } else if (f->asAxis()) {
        const gp_Ax1 ax = *f->asAxis();
        Vec3 o = Vec3::from(ax.Location()), d = Vec3::from(ax.Direction());
        Vec3 n = o + d * (p - o).dot(d);
        if (n.distance(p) <= tolerance) add(r, n, n.distance(p), 7);
      } else if (f->asPlane()) {
        const gp_Ax3 pl = *f->asPlane();
        Vec3 nrm = Vec3::from(pl.Direction());
        const double off = std::abs(nrm.dot(p - Vec3::from(pl.Location())));
        if (off <= tolerance) add(r, p - nrm * nrm.dot(p - Vec3::from(pl.Location())), off, 8);
      }
    }
  }
  if (cands.empty() && core::hasKind(kinds, ShapeKind::Component) && !(kinds & core::kBrepKinds)) {
    // component-only filter: the component of the nearest body surface
    for (ObjectId id : doc.liveBodies(true)) {
      TopoDS_Shape world = worldBodyShape(doc, id);
      Vec3 n;
      const double d = pointDistance(world, p, &n);
      if (d <= tolerance) {
        const core::Body* b = doc.body(id);
        ShapeRef r = ShapeRef::component(b ? b->component : core::kRootComponentId);
        r.point = n;
        r.hasPoint = true;
        add(r, n, d, 9);
      }
    }
  }
  if (cands.empty()) return std::nullopt;
  std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
    if (a.prio != b.prio) return a.prio < b.prio;
    return a.d < b.d;
  });
  return PickHit{cands.front().ref, cands.front().at, cands.front().d};
}

// ---------------------------------------------------------------------------------------------
// descriptions / metrics
// ---------------------------------------------------------------------------------------------

namespace {

TopoDS_Shape refWorldShape(const core::Document& doc, const ShapeRef& ref) {
  if (ref.kind == ShapeKind::Body) return worldBodyShape(doc, ref.owner);
  if (ref.isBrep()) {
    TopoDS_Shape world = worldBodyShape(doc, ref.owner);
    if (world.IsNull()) return {};
    core::ResolveResult rr = core::resolveSubShape(doc.bodyShape(ref.owner), ref);
    if (!rr) return {};
    return core::subShapeAt(world, core::toTopAbs(ref.kind), rr.index);
  }
  if (ref.kind == ShapeKind::Component) {
    std::vector<TopoDS_Shape> shapes;
    for (ObjectId id : doc.displayedBodies(ref.owner)) {
      TopoDS_Shape s = worldBodyShape(doc, id);
      if (!s.IsNull()) shapes.push_back(s);
    }
    return core::makeCompound(shapes);
  }
  return core::resolveShape(doc, ref);
}

const char* surfaceName(int t) {
  switch (t) {
    case GeomAbs_Plane: return "planar";
    case GeomAbs_Cylinder: return "cylindrical";
    case GeomAbs_Cone: return "conical";
    case GeomAbs_Sphere: return "spherical";
    case GeomAbs_Torus: return "toroidal";
    default: return "freeform";
  }
}
const char* curveName(int t) {
  switch (t) {
    case GeomAbs_Line: return "line";
    case GeomAbs_Circle: return "circle";
    case GeomAbs_Ellipse: return "ellipse";
    default: return "curve";
  }
}

/// Tight bounding box (BRepBndLib::AddOptimal; the default Add() pads curved faces by their
/// tolerance gap, which is useless for a CHECK to 0.01 mm).
bool tightBbox(const TopoDS_Shape& s, Vec3& mn, Vec3& mx) {
  if (s.IsNull()) return false;
  Bnd_Box box;
  try {
    BRepBndLib::AddOptimal(s, box, false, false);
  } catch (...) {
    return core::bbox(s, mn, mx);
  }
  if (box.IsVoid()) return core::bbox(s, mn, mx);
  double x0, y0, z0, x1, y1, z1;
  box.Get(x0, y0, z0, x1, y1, z1);
  mn = {x0, y0, z0};
  mx = {x1, y1, z1};
  return true;
}

std::string bboxText(const TopoDS_Shape& s) {
  Vec3 mn, mx;
  if (!tightBbox(s, mn, mx)) return "bbox n/a";
  return std::format("bbox X[{}, {}] Y[{}, {}] Z[{}, {}]", fmt(mn.x), fmt(mx.x), fmt(mn.y), fmt(mx.y), fmt(mn.z), fmt(mx.z));
}

}  // namespace

std::string propertyText(const core::Document& doc, const core::Property& p) {
  using core::PropertyType;
  switch (p.type()) {
    case PropertyType::Bool: return p.toBool() ? "true" : "false";
    case PropertyType::Int: return std::to_string(p.toInt());
    case PropertyType::Double:
    case PropertyType::Length:
    case PropertyType::Angle: return p.displayText(doc.units());
    case PropertyType::String: return "'" + p.toString() + "'";
    case PropertyType::Enum: {
      std::string s = p.enumName() + "  (options:";
      for (const std::string& o : p.options()) s += " '" + o + "'";
      return s + ")";
    }
    case PropertyType::Vec3: return fmtPnt(p.toVec3());
    case PropertyType::Ref: return p.ref().isValid() ? refLabel(doc, p.ref()) : std::string("(none)");
    case PropertyType::RefList: {
      if (p.refs().empty()) return "(none)";
      std::string s = std::to_string(p.refs().size()) + " selected:";
      for (const ShapeRef& r : p.refs()) s += " [" + refLabel(doc, r) + "]";
      return s;
    }
    case PropertyType::Json: return "(json)";
  }
  return {};
}

std::string refLabel(const core::Document& doc, const ShapeRef& ref) {
  switch (ref.kind) {
    case ShapeKind::Body: {
      const core::Body* b = doc.body(ref.owner);
      return std::format("Body '{}' (#{})", b ? b->name : "?", ref.owner);
    }
    case ShapeKind::Face:
    case ShapeKind::Edge:
    case ShapeKind::Vertex: {
      const core::Body* b = doc.body(ref.owner);
      std::string s = std::format("{} #{} of {}", core::shapeKindName(ref.kind), ref.index, b ? b->name : std::format("body #{}", ref.owner));
      if (!ref.name.empty()) s += " '" + ref.name + "'";
      return s;
    }
    case ShapeKind::Component: {
      const core::Component* c = doc.component(ref.owner);
      return std::format("Component '{}' (#{})", c ? c->name : "?", ref.owner);
    }
    case ShapeKind::Profile: {
      const core::Feature* f = doc.feature(ref.owner);
      return std::format("Profile of {} at {}", f ? f->name() : "?", fmtPnt(ref.point));
    }
    case ShapeKind::SketchCurve:
    case ShapeKind::SketchPoint: {
      const core::Feature* f = doc.feature(ref.owner);
      return std::format("{} #{} of {}", core::shapeKindName(ref.kind), ref.index, f ? f->name() : "?");
    }
    case ShapeKind::OriginPlane:
    case ShapeKind::OriginAxis:
    case ShapeKind::OriginPoint: return std::format("Origin {} {}", core::shapeKindName(ref.kind), ref.name);
    default: {
      const core::Feature* f = doc.feature(ref.owner);
      return std::format("{} '{}' (#{})", core::shapeKindName(ref.kind), f ? f->name() : "?", ref.owner);
    }
  }
}

std::string describeRef(const core::Document& doc, const ShapeRef& ref) {
  std::string out = refLabel(doc, ref);
  switch (ref.kind) {
    case ShapeKind::Body: {
      const core::Body* b = doc.body(ref.owner);
      TopoDS_Shape s = worldBodyShape(doc, ref.owner);
      if (!b) return out + ": unknown body";
      const core::Component* c = doc.component(b->component);
      out += std::format(": component '{}', {}", c ? c->name : "?", b->visible ? "visible" : "hidden");
      if (!b->material.empty()) out += ", material " + b->material;
      if (s.IsNull()) return out + ", no shape";
      out += std::format("\n  volume {} mm3, area {} mm2, {} faces, {} edges, {} vertices", fmt(core::volume(s)), fmt(core::area(s)),
                         core::subShapeCount(s, TopAbs_FACE), core::subShapeCount(s, TopAbs_EDGE), core::subShapeCount(s, TopAbs_VERTEX));
      out += "\n  " + bboxText(s) + ", centroid " + fmtPnt(core::centroid(s));
      if (b->density > 0) out += std::format(", mass {} g", fmt(core::volume(s) * b->density / 1000.0));
      return out;
    }
    case ShapeKind::Face: {
      TopoDS_Shape s = refWorldShape(doc, ref);
      if (s.IsNull()) return out + ": not found";
      core::GeomSignature sig = core::computeSignature(s);
      out += std::format(": {} face, area {} mm2, centre {}", surfaceName(sig.geomType), fmt(sig.size), fmtPnt(sig.center));
      if (sig.geomType == GeomAbs_Plane) out += ", normal " + fmtPnt(sig.direction);
      else if (sig.radius > 0) out += std::format(", radius {}, axis {}", fmt(sig.radius), fmtPnt(sig.direction));
      out += ", " + std::to_string(core::subShapeCount(s, TopAbs_EDGE)) + " edges";
      return out;
    }
    case ShapeKind::Edge: {
      TopoDS_Shape s = refWorldShape(doc, ref);
      if (s.IsNull()) return out + ": not found";
      core::GeomSignature sig = core::computeSignature(s);
      out += std::format(": {}, length {} mm, midpoint {}", curveName(sig.geomType), fmt(sig.size), fmtPnt(sig.center));
      if (sig.geomType == GeomAbs_Line) out += ", direction " + fmtPnt(sig.direction);
      else if (sig.radius > 0) out += std::format(", radius {}", fmt(sig.radius));
      return out;
    }
    case ShapeKind::Vertex: {
      TopoDS_Shape s = refWorldShape(doc, ref);
      if (s.IsNull()) return out + ": not found";
      return out + ": at " + fmtPnt(shapeCenter(s));
    }
    case ShapeKind::Component: {
      const core::Component* c = doc.component(ref.owner);
      if (!c) return out + ": unknown";
      gp_Trsf t = doc.worldPlacement(c->id);
      gp_XYZ tr = t.TranslationPart();
      out += std::format(": {}{}{}, {} bodies, origin at ({}, {}, {})", c->grounded ? "grounded" : "free",
                         c->isOccurrence() ? ", occurrence" : "", c->visible ? "" : ", hidden", doc.displayedBodies(c->id).size(),
                         fmt(tr.X()), fmt(tr.Y()), fmt(tr.Z()));
      {
        gp_Trsf local = c->placement;
        gp_XYZ lt = local.TranslationPart();
        double ax, ay, az;
        local.GetRotation().GetEulerAngles(gp_Extrinsic_XYZ, ax, ay, az);
        const double toDeg = 180.0 / 3.14159265358979323846;
        out += std::format("\n  placement: translation ({}, {}, {}), rotation XYZ ({}, {}, {}) deg", fmt(lt.X()), fmt(lt.Y()), fmt(lt.Z()),
                           fmt(ax * toDeg), fmt(ay * toDeg), fmt(az * toDeg));
      }
      if (const core::Component* parent = doc.component(c->parent))
        out += std::format("\n  parent: {} (#{})", parent->isRoot() ? "root" : parent->name, parent->id);
      if (!c->children.empty()) {
        out += "\n  children:";
        for (ObjectId ch : c->children)
          if (const core::Component* cc = doc.component(ch)) out += std::format(" {} (#{})", cc->name, ch);
      }
      TopoDS_Shape s = refWorldShape(doc, ref);
      if (!s.IsNull()) out += std::format("\n  volume {} mm3, {}", fmt(core::volume(s)), bboxText(s));
      for (ObjectId id : doc.displayedBodies(c->id))
        if (const core::Body* b = doc.body(id)) out += std::format("\n  body '{}' (#{})", b->name, id);
      return out;
    }
    case ShapeKind::Profile: {
      TopoDS_Shape face = core::resolveProfileFace(doc, ref);
      if (face.IsNull()) return out + ": not found";
      return out + std::format(": area {} mm2, curves {}", fmt(core::area(face)), ref.name);
    }
    case ShapeKind::Sketch: {
      const auto* sk = sketchOf(doc, ref.owner);
      if (!sk) return out + ": not a sketch";
      out += std::format(": {} ({}), {} curves/points, {} constraints, {}", core::featureStatusName(sk->status()),
                         sk->statusText(), sk->sketch().geometryCount(), sk->sketch().constraints().size(),
                         sk->isVisible() ? "visible" : "hidden");
      gp_Pnt o = sk->plane().Location();
      out += std::format("\n  plane origin ({}, {}, {}) normal {}, sketch x {} y {} (typed x,y are sketch coordinates)", fmt(o.X()),
                         fmt(o.Y()), fmt(o.Z()), fmtPnt(Vec3::from(sk->plane().Direction())), fmtPnt(Vec3::from(sk->plane().XDirection())),
                         fmtPnt(Vec3::from(sk->plane().YDirection())));
      int i = 0;
      for (const sketch::ProfileRegion& r : sk->regions())
        out += std::format("\n  profile #{}: area {} mm2, seed ({}, {}) sketch / {} world, curves {}", ++i, fmt(r.area), fmt(r.seed.x),
                           fmt(r.seed.y), fmtPnt(sk->toWorldVec(r.seed)), [&] {
                             std::string ids;
                             for (int id : r.curveIds) ids += (ids.empty() ? "" : ",") + std::to_string(id);
                             return ids;
                           }());
      for (const sketch::Geometry& g : sk->sketch().geometries()) {
        out += std::format("\n  geometry #{}: {}{}", g.id, sketch::geomTypeName(g.type), g.construction ? " (construction)" : "");
        if (g.type == sketch::GeomType::Line && g.p.size() >= 4) out += std::format(" ({}, {}) -> ({}, {})", fmt(g.p[0]), fmt(g.p[1]), fmt(g.p[2]), fmt(g.p[3]));
        else if (g.type == sketch::GeomType::Circle && g.p.size() >= 3) out += std::format(" centre ({}, {}) r {}", fmt(g.p[0]), fmt(g.p[1]), fmt(g.p[2]));
        else if (g.type == sketch::GeomType::Arc && g.p.size() >= 3) out += std::format(" centre ({}, {}) r {}", fmt(g.p[0]), fmt(g.p[1]), fmt(g.p[2]));
        else if (g.type == sketch::GeomType::Point && g.p.size() >= 2) out += std::format(" ({}, {})", fmt(g.p[0]), fmt(g.p[1]));
      }
      for (const sketch::Constraint& c : sk->sketch().constraints())
        if (c.isDimensional()) out += std::format("\n  dimension {} (#{}) = {}{}", c.name, c.id, fmt(c.value), c.driven ? " (driven)" : "");
      return out;
    }
    case ShapeKind::OriginPlane:
    case ShapeKind::OriginAxis:
    case ShapeKind::OriginPoint: return out;
    default: {
      const core::Feature* f = doc.feature(ref.owner);
      if (!f) return out + ": unknown feature";
      out += std::format(": type {}, {}{}{}", f->type(), core::featureStatusName(f->status()),
                         f->statusMessage().empty() ? "" : " '" + f->statusMessage() + "'", f->isSuppressed() ? ", suppressed" : "");
      if (!doc.isFeatureActive(f->id())) out += ", rolled back";
      for (const core::Property& p : f->properties()) {
        if (p.flags().hidden) continue;
        out += std::format("\n  {} = {}", p.name(), propertyText(doc, p));
      }
      for (ObjectId id : f->createdBodies())
        if (const core::Body* b = doc.body(id)) out += std::format("\n  created body '{}' (#{})", b->name, id);
      return out;
    }
  }
}

std::optional<double> refMetric(const core::Document& doc, const ShapeRef& ref, const std::string& metricIn) {
  const std::string m = lower(metricIn);
  if (ref.kind == ShapeKind::Sketch || ref.kind == ShapeKind::Profile) {
    const auto* sk = sketchOf(doc, ref.owner);
    if (ref.kind == ShapeKind::Sketch && sk) {
      if (m == "profiles") return static_cast<double>(sk->regions().size());
      if (m == "dof") return static_cast<double>(sk->sketch().dofs());
      if (m == "geometry" || m == "curves") return static_cast<double>(sk->sketch().geometryCount());
      if (m == "constraints") return static_cast<double>(sk->sketch().constraints().size());
    }
    if (ref.kind == ShapeKind::Profile && m == "area") {
      TopoDS_Shape face = core::resolveProfileFace(doc, ref);
      return face.IsNull() ? std::nullopt : std::optional<double>(core::area(face));
    }
  }
  if (ref.kind == ShapeKind::Component) {
    if (m == "bodies") return static_cast<double>(doc.displayedBodies(ref.owner).size());
    if (m == "x" || m == "y" || m == "z") {
      gp_XYZ t = doc.worldPlacement(ref.owner).TranslationPart();
      return m == "x" ? t.X() : m == "y" ? t.Y() : t.Z();
    }
  }
  if (ref.kind == ShapeKind::Body && m == "mass") {
    const core::Body* b = doc.body(ref.owner);
    TopoDS_Shape s = worldBodyShape(doc, ref.owner);
    if (!b || s.IsNull() || b->density <= 0) return std::nullopt;
    return core::volume(s) * b->density / 1000.0;
  }
  TopoDS_Shape s = refWorldShape(doc, ref);
  if (s.IsNull()) return std::nullopt;
  if (m == "volume") return core::volume(s);
  if (m == "area") return core::area(s);
  if (m == "length") return core::computeSignature(s).size;
  if (m == "radius") return core::computeSignature(s).radius;
  if (m == "faces") return static_cast<double>(core::subShapeCount(s, TopAbs_FACE));
  if (m == "edges") return static_cast<double>(core::subShapeCount(s, TopAbs_EDGE));
  if (m == "vertices") return static_cast<double>(core::subShapeCount(s, TopAbs_VERTEX));
  if (m == "solids") return static_cast<double>(core::subShapeCount(s, TopAbs_SOLID));
  Vec3 mn, mx;
  if (m.size() == 4 && (m.ends_with("min") || m.ends_with("max"))) {
    if (!tightBbox(s, mn, mx)) return std::nullopt;
    const Vec3& v = m.ends_with("min") ? mn : mx;
    if (m[0] == 'x') return v.x;
    if (m[0] == 'y') return v.y;
    if (m[0] == 'z') return v.z;
  }
  if (m == "dx" || m == "dy" || m == "dz") {
    if (!tightBbox(s, mn, mx)) return std::nullopt;
    return m == "dx" ? mx.x - mn.x : m == "dy" ? mx.y - mn.y : mx.z - mn.z;
  }
  if (m == "cx" || m == "cy" || m == "cz") {
    Vec3 c = ref.kind == ShapeKind::Vertex ? shapeCenter(s) : core::centroid(s);
    return m == "cx" ? c.x : m == "cy" ? c.y : c.z;
  }
  return std::nullopt;
}

}  // namespace fabricad::app
