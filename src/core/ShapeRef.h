// FabriCAD core: references to geometry (selection items, feature inputs).
//
// A ShapeRef names "something the user can pick": a face of a body, a sketch curve,
// a profile region, an origin plane, a component, ... It is a plain value type that
// serialises to JSON. B-rep sub-shape refs (Vertex/Edge/Face) carry both a direct
// index into the owner body's shape and a GeomSignature so they can be re-matched
// after the model changes (topological naming mitigation, see docs/ARCHITECTURE.md).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Types.h"

#include <QJsonObject>
#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS_Shape.hxx>

#include <string>
#include <vector>

namespace fabricad::core {

/// Geometric fingerprint of a sub-shape, taken when a reference is created.
struct GeomSignature {
  /// For faces: GeomAbs_SurfaceType (0 Plane, 1 Cylinder, 2 Cone, 3 Sphere, 4 Torus, ...).
  /// For edges: GeomAbs_CurveType (0 Line, 1 Circle, 2 Ellipse, ...). Vertices: 100. -1 = unset.
  int geomType = -1;
  Vec3 center;      ///< face centroid / edge midpoint / vertex position
  Vec3 direction;   ///< plane normal (oriented), cylinder/cone axis, line direction, circle axis
  double size = 0;  ///< face area / edge length / 0 for vertices
  double radius = 0;  ///< cylinder/sphere/circle radius, 0 otherwise
  Vec3 bboxMin, bboxMax;
  int vertexCount = 0;  ///< number of vertices of the sub-shape (cheap topology hint)

  bool isValid() const { return geomType >= 0; }
  QJsonObject toJson() const;
  static GeomSignature fromJson(const QJsonObject& o);
  bool operator==(const GeomSignature&) const = default;
};

struct ShapeRef {
  ObjectId owner = kInvalidId;      ///< body id (Vertex/Edge/Face/Body), feature id (Sketch*, Profile,
                                    ///< Construction*, Feature, Joint), component id, kOriginId for Origin*
  ShapeKind kind = ShapeKind::None;
  int index = -1;                   ///< B-rep: 1-based index in TopTools_IndexedMapOfShape(owner shape, kind).
                                    ///< Sketch: geometry id. Drawing: view/entity index.
  int subIndex = -1;                ///< SketchPoint: SketchPointRole. Otherwise unused (-1).
  std::string name;                 ///< Stable name: history name ("Extrude1/End"), origin name ("XY"),
                                    ///< profile curve-id list ("1,2,3,4"), joint origin name...
  GeomSignature signature;          ///< for B-rep re-matching
  Vec3 point;                       ///< pick point (B-rep) or seed point inside region (Profile)
  bool hasPoint = false;

  bool isValid() const { return kind != ShapeKind::None && (owner != kInvalidId || kind == ShapeKind::None); }
  bool isBrep() const { return hasKind(kBrepKinds, kind); }
  /// Identity comparison (owner/kind/index/subIndex/name). Signature and point are ignored.
  bool operator==(const ShapeRef& o) const {
    return owner == o.owner && kind == o.kind && index == o.index && subIndex == o.subIndex && name == o.name;
  }
  bool operator!=(const ShapeRef& o) const { return !(*this == o); }

  std::string toString() const;
  QJsonObject toJson() const;
  static ShapeRef fromJson(const QJsonObject& o);

  // --- factories -----------------------------------------------------------
  static ShapeRef body(ObjectId bodyId);
  /// Sub-shape of a body. `sub` must be a sub-shape of `bodyShape`; computes index + signature.
  static ShapeRef subShape(ObjectId bodyId, const TopoDS_Shape& bodyShape, const TopoDS_Shape& sub);
  static ShapeRef face(ObjectId bodyId, int index) { return make(bodyId, ShapeKind::Face, index); }
  static ShapeRef edge(ObjectId bodyId, int index) { return make(bodyId, ShapeKind::Edge, index); }
  static ShapeRef vertex(ObjectId bodyId, int index) { return make(bodyId, ShapeKind::Vertex, index); }
  static ShapeRef originPlane(const std::string& name);  ///< "XY", "XZ", "YZ"
  static ShapeRef originAxis(const std::string& name);   ///< "X", "Y", "Z"
  static ShapeRef originPoint();
  static ShapeRef feature(ObjectId featureId, ShapeKind kind = ShapeKind::Feature);
  static ShapeRef sketch(ObjectId sketchId) { return feature(sketchId, ShapeKind::Sketch); }
  static ShapeRef sketchCurve(ObjectId sketchId, int curveId);
  static ShapeRef sketchPoint(ObjectId sketchId, int curveId, SketchPointRole role);
  static ShapeRef profile(ObjectId sketchId, const Vec3& seed, const std::vector<int>& curveIds);
  static ShapeRef component(ObjectId componentId) { return make(componentId, ShapeKind::Component, -1); }
  static ShapeRef make(ObjectId owner, ShapeKind kind, int index, const std::string& name = {});
};

/// ShapeKind (Vertex/Edge/Face/Body) -> TopAbs. Others map to TopAbs_SHAPE.
TopAbs_ShapeEnum toTopAbs(ShapeKind k);
ShapeKind fromTopAbs(TopAbs_ShapeEnum t);

/// Fingerprint a sub-shape (vertex, edge, face, or whole solid).
GeomSignature computeSignature(const TopoDS_Shape& sub);
/// Weighted distance between signatures (0 = identical). `modelSize` normalises positions.
double signatureDistance(const GeomSignature& a, const GeomSignature& b, double modelSize);

/// 1-based index of `sub` within the sub-shapes of `type` of `owner` (TopTools_IndexedMapOfShape order), 0 if absent.
int indexOfSubShape(const TopoDS_Shape& owner, const TopoDS_Shape& sub, TopAbs_ShapeEnum type);
/// Sub-shape at a 1-based index; null shape if out of range.
TopoDS_Shape subShapeAt(const TopoDS_Shape& owner, TopAbs_ShapeEnum type, int index);
int subShapeCount(const TopoDS_Shape& owner, TopAbs_ShapeEnum type);

struct ResolveResult {
  TopoDS_Shape shape;    ///< null when not found
  int index = 0;         ///< index in the current shape
  bool exact = false;    ///< true: resolved by index + matching signature (or no signature stored)
  double score = 0;      ///< signature distance of the chosen candidate (0 for exact)
  bool ambiguous = false;  ///< best match not clearly better than the runner-up
  explicit operator bool() const { return !shape.IsNull(); }
};

/// Resolve a B-rep ShapeRef against the owner's *current* shape:
///  1. if ref.index is in range and its signature matches (or ref has no signature) -> exact;
///  2. else score all candidates of the same kind by signatureDistance; accept the best if
///     score < acceptThreshold and best < 0.6 * secondBest (otherwise ambiguous=true, shape kept but flagged).
/// Callers should write back `result.index` into the ref (ShapeRef::index) after a successful match.
ResolveResult resolveSubShape(const TopoDS_Shape& ownerShape, const ShapeRef& ref, double modelSize = 0.0,
                              double acceptThreshold = 0.35);

}  // namespace fabricad::core
