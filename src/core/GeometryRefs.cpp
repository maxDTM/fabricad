// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/GeometryRefs.h"

#include "core/Document.h"
#include "core/ShapeUtils.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRep_Tool.hxx>
#include <TopoDS.hxx>
#include <gp_Circ.hxx>
#include <gp_Cone.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Lin.hxx>

namespace fabricad::core {

namespace {

const BodyStateMap& stateOf(const Document& doc, const BodyStateMap* state) {
  return state ? *state : doc.bodyStates();
}

TopoDS_Shape ownerShape(const Document& doc, ObjectId bodyId, const BodyStateMap* state) {
  const auto& st = stateOf(doc, state);
  auto it = st.find(bodyId);
  return it == st.end() ? TopoDS_Shape() : it->second;
}

}  // namespace

TopoDS_Shape resolveShape(const Document& doc, const ShapeRef& ref, const BodyStateMap* state, ResolveResult* detail) {
  switch (ref.kind) {
    case ShapeKind::Body:
    case ShapeKind::Face:
    case ShapeKind::Edge:
    case ShapeKind::Vertex: {
      TopoDS_Shape owner = ownerShape(doc, ref.owner, state);
      if (owner.IsNull()) return {};
      ResolveResult rr = resolveSubShape(owner, ref);
      if (detail) *detail = rr;
      return rr.shape;
    }
    case ShapeKind::Profile: return resolveProfileFace(doc, ref, state);
    case ShapeKind::SketchCurve: {
      const Feature* f = doc.feature(ref.owner);
      const IProfileSource* ps = f ? f->profileSource() : nullptr;
      return ps ? ps->curveEdge(ref.index) : TopoDS_Shape();
    }
    case ShapeKind::Sketch: {
      const Feature* f = doc.feature(ref.owner);
      const IProfileSource* ps = f ? f->profileSource() : nullptr;
      if (ps) return ps->allEdges(false);
      return f ? f->outputShape() : TopoDS_Shape();
    }
    case ShapeKind::OriginPlane: return planeFace(originPlane(ref.name), 50.0);
    case ShapeKind::Feature:
    case ShapeKind::ConstructionPlane:
    case ShapeKind::ConstructionAxis:
    case ShapeKind::ConstructionPoint:
    case ShapeKind::Joint:
    case ShapeKind::JointOrigin: {
      const Feature* f = doc.feature(ref.owner);
      return f ? f->outputShape() : TopoDS_Shape();
    }
    case ShapeKind::Component: {
      std::vector<TopoDS_Shape> parts;
      for (ObjectId b : doc.bodiesOf(ref.owner)) {
        TopoDS_Shape s = ownerShape(doc, b, state);
        if (!s.IsNull()) parts.push_back(s);
      }
      return makeCompound(parts);
    }
    default: return {};
  }
}

TopoDS_Face resolveProfileFace(const Document& doc, const ShapeRef& ref, const BodyStateMap* state) {
  if (ref.kind == ShapeKind::Profile) {
    const Feature* f = doc.feature(ref.owner);
    const IProfileSource* ps = f ? f->profileSource() : nullptr;
    return ps ? ps->profileFace(ref) : TopoDS_Face();
  }
  if (ref.kind == ShapeKind::Face) {
    TopoDS_Shape s = resolveShape(doc, ref, state);
    if (!s.IsNull() && s.ShapeType() == TopAbs_FACE && facePlane(s)) return TopoDS::Face(s);
  }
  return {};
}

std::optional<gp_Ax3> resolvePlane(const Document& doc, const ShapeRef& ref, const BodyStateMap* state) {
  switch (ref.kind) {
    case ShapeKind::OriginPlane: return originPlane(ref.name);
    case ShapeKind::Face: {
      TopoDS_Shape s = resolveShape(doc, ref, state);
      return facePlane(s);
    }
    case ShapeKind::Sketch: {
      const Feature* f = doc.feature(ref.owner);
      const IProfileSource* ps = f ? f->profileSource() : nullptr;
      if (ps) return ps->sketchPlane();
      return f ? f->asPlane() : std::nullopt;
    }
    default: {
      const Feature* f = doc.feature(ref.owner);
      return f ? f->asPlane() : std::nullopt;
    }
  }
}

std::optional<gp_Ax1> resolveAxis(const Document& doc, const ShapeRef& ref, const BodyStateMap* state) {
  switch (ref.kind) {
    case ShapeKind::OriginAxis: return originAxis(ref.name);
    case ShapeKind::Edge:
    case ShapeKind::SketchCurve: {
      TopoDS_Shape s = resolveShape(doc, ref, state);
      if (s.IsNull() || s.ShapeType() != TopAbs_EDGE) return std::nullopt;
      BRepAdaptor_Curve c(TopoDS::Edge(s));
      if (c.GetType() == GeomAbs_Line) {
        gp_Lin l = c.Line();
        gp_Pnt p0 = c.Value(c.FirstParameter());
        gp_Dir d = l.Direction();
        if (s.Orientation() == TopAbs_REVERSED) d.Reverse();
        return gp_Ax1(p0, d);
      }
      if (c.GetType() == GeomAbs_Circle) return c.Circle().Axis();
      if (c.GetType() == GeomAbs_Ellipse) return c.Ellipse().Axis();
      return std::nullopt;
    }
    case ShapeKind::Face: {
      TopoDS_Shape s = resolveShape(doc, ref, state);
      if (s.IsNull() || s.ShapeType() != TopAbs_FACE) return std::nullopt;
      BRepAdaptor_Surface surf(TopoDS::Face(s), false);
      if (surf.GetType() == GeomAbs_Cylinder) return surf.Cylinder().Axis();
      if (surf.GetType() == GeomAbs_Cone) return surf.Cone().Axis();
      if (surf.GetType() == GeomAbs_Torus) return surf.Torus().Axis();
      if (surf.GetType() == GeomAbs_Plane) {
        auto pl = facePlane(s);
        if (pl) return gp_Ax1(pl->Location(), pl->Direction());  // plane normal through centroid
      }
      return std::nullopt;
    }
    default: {
      const Feature* f = doc.feature(ref.owner);
      return f ? f->asAxis() : std::nullopt;
    }
  }
}

std::optional<gp_Pnt> resolvePoint(const Document& doc, const ShapeRef& ref, const BodyStateMap* state) {
  switch (ref.kind) {
    case ShapeKind::OriginPoint: return gp_Pnt(0, 0, 0);
    case ShapeKind::Vertex: {
      TopoDS_Shape s = resolveShape(doc, ref, state);
      if (s.IsNull() || s.ShapeType() != TopAbs_VERTEX) break;
      return BRep_Tool::Pnt(TopoDS::Vertex(s));
    }
    case ShapeKind::Edge: {
      TopoDS_Shape s = resolveShape(doc, ref, state);
      if (s.IsNull() || s.ShapeType() != TopAbs_EDGE) break;
      BRepAdaptor_Curve c(TopoDS::Edge(s));
      if (c.GetType() == GeomAbs_Circle) return c.Circle().Location();
      return c.Value(0.5 * (c.FirstParameter() + c.LastParameter()));
    }
    case ShapeKind::Face: {
      TopoDS_Shape s = resolveShape(doc, ref, state);
      if (s.IsNull()) break;
      return centroid(s).toPnt();
    }
    case ShapeKind::SketchPoint: {
      const Feature* f = doc.feature(ref.owner);
      const IProfileSource* ps = f ? f->profileSource() : nullptr;
      if (ps) {
        if (auto p = ps->sketchPoint(ref)) return p;
      }
      break;
    }
    default: {
      const Feature* f = doc.feature(ref.owner);
      if (f) {
        if (auto p = f->asPoint()) return p;
      }
      break;
    }
  }
  if (ref.hasPoint) return ref.point.toPnt();
  return std::nullopt;
}

ShapeRef makeBrepRef(const Document& doc, ObjectId bodyId, const TopoDS_Shape& sub, const BodyStateMap* state) {
  TopoDS_Shape owner = ownerShape(doc, bodyId, state);
  if (owner.IsNull() || sub.IsNull()) return ShapeRef::body(bodyId);
  return ShapeRef::subShape(bodyId, owner, sub);
}

}  // namespace fabricad::core
