// FabriCAD core: basic value types shared by every module.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <cmath>
#include <cstdint>
#include <string>

class gp_Pnt;
class gp_Vec;
class gp_Dir;
class gp_XYZ;

namespace fabricad::core {

/// Identifier of any document object (feature, body, component, ...).
/// Allocated by Document::allocateId(); unique within one document; persisted.
using ObjectId = std::uint64_t;
constexpr ObjectId kInvalidId = 0;
/// Reserved ids. Document always owns these.
constexpr ObjectId kRootComponentId = 1;  ///< the root component ("Part1")
constexpr ObjectId kOriginId = 2;         ///< owner of the origin planes/axes/point
constexpr ObjectId kFirstFreeId = 16;

/// Plain 3-vector used in properties/JSON (keeps Qt/OCCT types off the data model).
struct Vec3 {
  double x = 0, y = 0, z = 0;
  constexpr Vec3() = default;
  constexpr Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

  Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
  Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }
  Vec3 operator-() const { return {-x, -y, -z}; }
  Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
  Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
  bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }
  bool operator!=(const Vec3& o) const { return !(*this == o); }

  double dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
  Vec3 cross(const Vec3& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
  double length() const { return std::sqrt(dot(*this)); }
  double distance(const Vec3& o) const { return (*this - o).length(); }
  Vec3 normalized() const { double l = length(); return l > 0 ? *this / l : *this; }
  bool isZero(double tol = 1e-12) const { return length() <= tol; }

  // OCCT conversions (implemented in Types.cpp so this header stays light).
  gp_Pnt toPnt() const;
  gp_Vec toVec() const;
  gp_Dir toDir() const;  ///< precondition: not zero
  static Vec3 from(const gp_Pnt& p);
  static Vec3 from(const gp_Vec& v);
  static Vec3 from(const gp_Dir& d);
  static Vec3 from(const gp_XYZ& v);
  std::string toString() const;
};

inline Vec3 operator*(double s, const Vec3& v) { return v * s; }

/// Kind of a referenceable entity. Also used as a bit mask for selection filters.
enum class ShapeKind : std::uint32_t {
  None = 0,
  // B-rep sub-shapes of a Body (owner = body id, index = 1-based index into
  // TopTools_IndexedMapOfShape of that type over the body shape)
  Vertex = 1u << 0,
  Edge = 1u << 1,
  Face = 1u << 2,
  Body = 1u << 3,
  // Sketch entities (owner = sketch feature id, index = sketch geometry id)
  SketchPoint = 1u << 4,   ///< subIndex = point role on curve (see SketchPointRole)
  SketchCurve = 1u << 5,
  Profile = 1u << 6,       ///< closed region of a sketch; point = seed inside region
  Sketch = 1u << 7,        ///< the sketch feature as a whole
  // Construction geometry features (owner = feature id)
  ConstructionPlane = 1u << 8,
  ConstructionAxis = 1u << 9,
  ConstructionPoint = 1u << 10,
  // Origin geometry (owner = kOriginId, name = "XY"/"XZ"/"YZ", "X"/"Y"/"Z", "O")
  OriginPlane = 1u << 11,
  OriginAxis = 1u << 12,
  OriginPoint = 1u << 13,
  // Assembly / misc
  Component = 1u << 14,
  Joint = 1u << 15,
  JointOrigin = 1u << 16,
  Feature = 1u << 17,      ///< any timeline feature (owner = feature id)
  DrawingView = 1u << 18,
  DrawingEntity = 1u << 19,
};

using ShapeKindMask = std::uint32_t;
constexpr ShapeKindMask kAllKinds = 0xFFFFFFFFu;
constexpr ShapeKindMask kNoKinds = 0u;
constexpr ShapeKindMask mask(ShapeKind k) { return static_cast<ShapeKindMask>(k); }
constexpr ShapeKindMask operator|(ShapeKind a, ShapeKind b) { return mask(a) | mask(b); }
constexpr ShapeKindMask operator|(ShapeKindMask a, ShapeKind b) { return a | mask(b); }
constexpr bool hasKind(ShapeKindMask m, ShapeKind k) { return (m & mask(k)) != 0; }
constexpr ShapeKindMask kBrepKinds = ShapeKind::Vertex | ShapeKind::Edge | ShapeKind::Face | ShapeKind::Body;
constexpr ShapeKindMask kPlaneKinds = ShapeKind::Face | ShapeKind::ConstructionPlane | ShapeKind::OriginPlane;
constexpr ShapeKindMask kAxisKinds = ShapeKind::Edge | ShapeKind::ConstructionAxis | ShapeKind::OriginAxis | ShapeKind::SketchCurve;
constexpr ShapeKindMask kPointKinds = ShapeKind::Vertex | ShapeKind::ConstructionPoint | ShapeKind::OriginPoint | ShapeKind::SketchPoint;

const char* shapeKindName(ShapeKind k);
ShapeKind shapeKindFromName(const std::string& name);

/// Role of a SketchPoint reference on its curve (ShapeRef::subIndex).
enum class SketchPointRole : int {
  Start = 0,
  End = 1,
  Center = 2,
  Mid = 3,
  Standalone = 4,  ///< a free sketch point entity (index = point id)
};

/// Top-level workspaces (Fusion-style switcher).
enum class Workspace { Design, Drawing };

}  // namespace fabricad::core
