// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/ShapeRef.h"

#include "core/Json.h"
#include "core/ShapeUtils.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Circ.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <gp_Sphere.hxx>

#include <algorithm>
#include <format>
#include <limits>

namespace fabricad::core {

// ---------------------------------------------------------------------------
// GeomSignature
// ---------------------------------------------------------------------------

QJsonObject GeomSignature::toJson() const {
  QJsonObject o;
  o["type"] = geomType;
  o["c"] = core::toJson(center);
  o["d"] = core::toJson(direction);
  o["size"] = size;
  o["r"] = radius;
  o["bmin"] = core::toJson(bboxMin);
  o["bmax"] = core::toJson(bboxMax);
  o["nv"] = vertexCount;
  return o;
}

GeomSignature GeomSignature::fromJson(const QJsonObject& o) {
  GeomSignature s;
  s.geomType = o["type"].toInt(-1);
  s.center = vec3FromJson(o["c"]);
  s.direction = vec3FromJson(o["d"]);
  s.size = o["size"].toDouble();
  s.radius = o["r"].toDouble();
  s.bboxMin = vec3FromJson(o["bmin"]);
  s.bboxMax = vec3FromJson(o["bmax"]);
  s.vertexCount = o["nv"].toInt();
  return s;
}

// ---------------------------------------------------------------------------
// ShapeRef
// ---------------------------------------------------------------------------

std::string ShapeRef::toString() const {
  std::string s = std::format("{}#{}", shapeKindName(kind), owner);
  if (index >= 0) s += std::format("[{}]", index);
  if (subIndex >= 0) s += std::format(".{}", subIndex);
  if (!name.empty()) s += " '" + name + "'";
  return s;
}

QJsonObject ShapeRef::toJson() const {
  QJsonObject o;
  o["owner"] = idToJson(owner);
  o["kind"] = shapeKindName(kind);
  if (index >= 0) o["index"] = index;
  if (subIndex >= 0) o["sub"] = subIndex;
  if (!name.empty()) o["name"] = qs(name);
  if (signature.isValid()) o["sig"] = signature.toJson();
  if (hasPoint) o["pt"] = core::toJson(point);
  return o;
}

ShapeRef ShapeRef::fromJson(const QJsonObject& o) {
  ShapeRef r;
  r.owner = idFromJson(o["owner"]);
  r.kind = shapeKindFromName(ss(o["kind"].toString()));
  r.index = o["index"].toInt(-1);
  r.subIndex = o["sub"].toInt(-1);
  r.name = ss(o["name"].toString());
  if (o.contains("sig")) r.signature = GeomSignature::fromJson(o["sig"].toObject());
  if (o.contains("pt")) {
    r.point = vec3FromJson(o["pt"]);
    r.hasPoint = true;
  }
  return r;
}

ShapeRef ShapeRef::make(ObjectId owner, ShapeKind kind, int index, const std::string& name) {
  ShapeRef r;
  r.owner = owner;
  r.kind = kind;
  r.index = index;
  r.name = name;
  return r;
}

ShapeRef ShapeRef::body(ObjectId bodyId) { return make(bodyId, ShapeKind::Body, -1); }

ShapeRef ShapeRef::subShape(ObjectId bodyId, const TopoDS_Shape& bodyShape, const TopoDS_Shape& sub) {
  ShapeRef r;
  r.owner = bodyId;
  r.kind = fromTopAbs(sub.ShapeType());
  if (r.kind == ShapeKind::None) {
    r.kind = ShapeKind::Body;
    return r;
  }
  r.index = indexOfSubShape(bodyShape, sub, sub.ShapeType());
  r.signature = computeSignature(sub);
  r.point = r.signature.center;
  r.hasPoint = true;
  return r;
}

ShapeRef ShapeRef::originPlane(const std::string& name) { return make(kOriginId, ShapeKind::OriginPlane, -1, name); }
ShapeRef ShapeRef::originAxis(const std::string& name) { return make(kOriginId, ShapeKind::OriginAxis, -1, name); }
ShapeRef ShapeRef::originPoint() { return make(kOriginId, ShapeKind::OriginPoint, -1, "O"); }
ShapeRef ShapeRef::feature(ObjectId featureId, ShapeKind kind) { return make(featureId, kind, -1); }
ShapeRef ShapeRef::sketchCurve(ObjectId sketchId, int curveId) { return make(sketchId, ShapeKind::SketchCurve, curveId); }

ShapeRef ShapeRef::sketchPoint(ObjectId sketchId, int curveId, SketchPointRole role) {
  ShapeRef r = make(sketchId, ShapeKind::SketchPoint, curveId);
  r.subIndex = static_cast<int>(role);
  return r;
}

ShapeRef ShapeRef::profile(ObjectId sketchId, const Vec3& seed, const std::vector<int>& curveIds) {
  ShapeRef r = make(sketchId, ShapeKind::Profile, -1);
  r.point = seed;
  r.hasPoint = true;
  std::vector<int> ids = curveIds;
  std::sort(ids.begin(), ids.end());
  std::string n;
  for (size_t i = 0; i < ids.size(); ++i) {
    if (i) n += ',';
    n += std::to_string(ids[i]);
  }
  r.name = n;
  return r;
}

// ---------------------------------------------------------------------------
// TopAbs mapping
// ---------------------------------------------------------------------------

TopAbs_ShapeEnum toTopAbs(ShapeKind k) {
  switch (k) {
    case ShapeKind::Vertex: return TopAbs_VERTEX;
    case ShapeKind::Edge: return TopAbs_EDGE;
    case ShapeKind::Face: return TopAbs_FACE;
    case ShapeKind::Body: return TopAbs_SOLID;
    default: return TopAbs_SHAPE;
  }
}

ShapeKind fromTopAbs(TopAbs_ShapeEnum t) {
  switch (t) {
    case TopAbs_VERTEX: return ShapeKind::Vertex;
    case TopAbs_EDGE: return ShapeKind::Edge;
    case TopAbs_FACE: return ShapeKind::Face;
    case TopAbs_SOLID:
    case TopAbs_COMPSOLID:
    case TopAbs_COMPOUND:
    case TopAbs_SHELL: return ShapeKind::Body;
    default: return ShapeKind::None;
  }
}

// ---------------------------------------------------------------------------
// Signatures
// ---------------------------------------------------------------------------

GeomSignature computeSignature(const TopoDS_Shape& sub) {
  GeomSignature s;
  if (sub.IsNull()) return s;
  Bnd_Box box;
  BRepBndLib::Add(sub, box, false);
  if (!box.IsVoid()) {
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    s.bboxMin = {x0, y0, z0};
    s.bboxMax = {x1, y1, z1};
  }
  s.vertexCount = subShapeCount(sub, TopAbs_VERTEX);

  switch (sub.ShapeType()) {
    case TopAbs_VERTEX: {
      s.geomType = 100;
      s.center = Vec3::from(BRep_Tool::Pnt(TopoDS::Vertex(sub)));
      break;
    }
    case TopAbs_EDGE: {
      const TopoDS_Edge& e = TopoDS::Edge(sub);
      BRepAdaptor_Curve c(e);
      s.geomType = static_cast<int>(c.GetType());
      double f = c.FirstParameter(), l = c.LastParameter();
      s.size = GCPnts_AbscissaPoint::Length(c);
      s.center = Vec3::from(c.Value(0.5 * (f + l)));
      if (c.GetType() == GeomAbs_Line) {
        gp_Lin lin = c.Line();
        s.direction = Vec3::from(lin.Direction());
        if (e.Orientation() == TopAbs_REVERSED) s.direction = -s.direction;
      } else if (c.GetType() == GeomAbs_Circle) {
        gp_Circ circ = c.Circle();
        s.direction = Vec3::from(circ.Axis().Direction());
        s.radius = circ.Radius();
        // centre of the circle is more stable than the arc midpoint for matching
        s.center = Vec3::from(circ.Location());
      } else {
        gp_Pnt p0 = c.Value(f), p1 = c.Value(l);
        Vec3 d = Vec3::from(p1) - Vec3::from(p0);
        if (!d.isZero()) s.direction = d.normalized();
      }
      break;
    }
    case TopAbs_FACE: {
      const TopoDS_Face& face = TopoDS::Face(sub);
      BRepAdaptor_Surface surf(face, false);
      s.geomType = static_cast<int>(surf.GetType());
      GProp_GProps props;
      BRepGProp::SurfaceProperties(face, props);
      s.size = props.Mass();
      s.center = Vec3::from(props.CentreOfMass());
      if (surf.GetType() == GeomAbs_Plane) {
        gp_Pln pln = surf.Plane();
        gp_Dir n = pln.Axis().Direction();
        if (face.Orientation() == TopAbs_REVERSED) n.Reverse();
        s.direction = Vec3::from(n);
      } else if (surf.GetType() == GeomAbs_Cylinder) {
        gp_Cylinder cyl = surf.Cylinder();
        s.direction = Vec3::from(cyl.Axis().Direction());
        s.radius = cyl.Radius();
      } else if (surf.GetType() == GeomAbs_Sphere) {
        s.radius = surf.Sphere().Radius();
        s.center = Vec3::from(surf.Sphere().Location());
      } else if (surf.GetType() == GeomAbs_Cone) {
        s.direction = Vec3::from(surf.Cone().Axis().Direction());
      } else if (surf.GetType() == GeomAbs_Torus) {
        s.direction = Vec3::from(surf.Torus().Axis().Direction());
        s.radius = surf.Torus().MajorRadius();
      }
      break;
    }
    default: {
      s.geomType = 200 + static_cast<int>(sub.ShapeType());
      GProp_GProps props;
      BRepGProp::VolumeProperties(sub, props);
      s.size = props.Mass();
      s.center = Vec3::from(props.CentreOfMass());
      break;
    }
  }
  return s;
}

double signatureDistance(const GeomSignature& a, const GeomSignature& b, double modelSize) {
  if (!a.isValid() || !b.isValid()) return std::numeric_limits<double>::infinity();
  if (a.geomType != b.geomType) return std::numeric_limits<double>::infinity();
  double scale = modelSize > 1e-9 ? modelSize : std::max(1.0, (a.bboxMax - a.bboxMin).length());
  double d = 0.0;
  // position
  d += a.center.distance(b.center) / scale;
  // size ratio
  double sa = std::abs(a.size), sb = std::abs(b.size);
  if (sa > 1e-12 || sb > 1e-12) d += std::abs(sa - sb) / std::max(sa, sb);
  // direction (unsigned for lines/axes; planes keep orientation)
  if (!a.direction.isZero() && !b.direction.isZero()) {
    double dot = a.direction.normalized().dot(b.direction.normalized());
    if (a.geomType == 0 && a.size > 0 && b.size > 0 && a.radius == 0) {
      // planar face (surface type 0 with area): oriented normal matters
      d += 0.5 * (1.0 - dot);
    } else {
      d += 0.5 * (1.0 - std::abs(dot));
    }
  }
  // radius
  if (a.radius > 0 || b.radius > 0) d += std::abs(a.radius - b.radius) / std::max({a.radius, b.radius, 1e-9});
  return d;
}

// ---------------------------------------------------------------------------
// Sub-shape indexing
// ---------------------------------------------------------------------------

int indexOfSubShape(const TopoDS_Shape& owner, const TopoDS_Shape& sub, TopAbs_ShapeEnum type) {
  if (owner.IsNull() || sub.IsNull()) return 0;
  TopTools_IndexedMapOfShape map;
  TopExp::MapShapes(owner, type, map);
  int idx = map.FindIndex(sub);
  if (idx > 0) return idx;
  // fall back to TShape identity ignoring orientation
  for (int i = 1; i <= map.Extent(); ++i)
    if (map(i).IsSame(sub)) return i;
  return 0;
}

TopoDS_Shape subShapeAt(const TopoDS_Shape& owner, TopAbs_ShapeEnum type, int index) {
  if (owner.IsNull() || index <= 0) return {};
  TopTools_IndexedMapOfShape map;
  TopExp::MapShapes(owner, type, map);
  if (index > map.Extent()) return {};
  return map(index);
}

int subShapeCount(const TopoDS_Shape& owner, TopAbs_ShapeEnum type) {
  if (owner.IsNull()) return 0;
  TopTools_IndexedMapOfShape map;
  TopExp::MapShapes(owner, type, map);
  return map.Extent();
}

ResolveResult resolveSubShape(const TopoDS_Shape& ownerShape, const ShapeRef& ref, double modelSize,
                              double acceptThreshold) {
  ResolveResult res;
  if (ownerShape.IsNull()) return res;
  if (ref.kind == ShapeKind::Body) {
    res.shape = ownerShape;
    res.exact = true;
    return res;
  }
  TopAbs_ShapeEnum type = toTopAbs(ref.kind);
  if (type == TopAbs_SHAPE) return res;

  TopTools_IndexedMapOfShape map;
  TopExp::MapShapes(ownerShape, type, map);
  if (map.Extent() == 0) return res;
  if (modelSize <= 0) modelSize = std::max(1.0, bboxDiagonal(ownerShape));

  // 1. direct index
  if (ref.index >= 1 && ref.index <= map.Extent()) {
    const TopoDS_Shape& cand = map(ref.index);
    if (!ref.signature.isValid()) {
      res.shape = cand;
      res.index = ref.index;
      res.exact = true;
      return res;
    }
    double d = signatureDistance(ref.signature, computeSignature(cand), modelSize);
    if (d < 1e-6) {
      res.shape = cand;
      res.index = ref.index;
      res.exact = true;
      return res;
    }
  }
  if (!ref.signature.isValid()) return res;

  // 2. best signature match
  double best = std::numeric_limits<double>::infinity(), second = best;
  int bestIdx = 0;
  for (int i = 1; i <= map.Extent(); ++i) {
    double d = signatureDistance(ref.signature, computeSignature(map(i)), modelSize);
    if (d < best) {
      second = best;
      best = d;
      bestIdx = i;
    } else if (d < second) {
      second = d;
    }
  }
  if (bestIdx == 0 || !(best < acceptThreshold)) return res;
  res.shape = map(bestIdx);
  res.index = bestIdx;
  res.score = best;
  res.exact = best < 1e-6;
  res.ambiguous = std::isfinite(second) && !(best < 0.6 * second) && best > 1e-6;
  return res;
}

}  // namespace fabricad::core
