// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/Types.h"

#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <gp_XYZ.hxx>

#include <array>
#include <format>
#include <utility>

namespace fabricad::core {

gp_Pnt Vec3::toPnt() const { return gp_Pnt(x, y, z); }
gp_Vec Vec3::toVec() const { return gp_Vec(x, y, z); }
gp_Dir Vec3::toDir() const { return gp_Dir(x, y, z); }
Vec3 Vec3::from(const gp_Pnt& p) { return {p.X(), p.Y(), p.Z()}; }
Vec3 Vec3::from(const gp_Vec& v) { return {v.X(), v.Y(), v.Z()}; }
Vec3 Vec3::from(const gp_Dir& d) { return {d.X(), d.Y(), d.Z()}; }
Vec3 Vec3::from(const gp_XYZ& v) { return {v.X(), v.Y(), v.Z()}; }
std::string Vec3::toString() const { return std::format("({:.6g}, {:.6g}, {:.6g})", x, y, z); }

namespace {
constexpr std::array<std::pair<ShapeKind, const char*>, 21> kKindNames{{
    {ShapeKind::None, "None"},
    {ShapeKind::Vertex, "Vertex"},
    {ShapeKind::Edge, "Edge"},
    {ShapeKind::Face, "Face"},
    {ShapeKind::Body, "Body"},
    {ShapeKind::SketchPoint, "SketchPoint"},
    {ShapeKind::SketchCurve, "SketchCurve"},
    {ShapeKind::Profile, "Profile"},
    {ShapeKind::Sketch, "Sketch"},
    {ShapeKind::ConstructionPlane, "ConstructionPlane"},
    {ShapeKind::ConstructionAxis, "ConstructionAxis"},
    {ShapeKind::ConstructionPoint, "ConstructionPoint"},
    {ShapeKind::OriginPlane, "OriginPlane"},
    {ShapeKind::OriginAxis, "OriginAxis"},
    {ShapeKind::OriginPoint, "OriginPoint"},
    {ShapeKind::Component, "Component"},
    {ShapeKind::Joint, "Joint"},
    {ShapeKind::JointOrigin, "JointOrigin"},
    {ShapeKind::Feature, "Feature"},
    {ShapeKind::DrawingView, "DrawingView"},
    {ShapeKind::DrawingEntity, "DrawingEntity"},
}};
}  // namespace

const char* shapeKindName(ShapeKind k) {
  for (const auto& [kind, name] : kKindNames)
    if (kind == k) return name;
  return "None";
}

ShapeKind shapeKindFromName(const std::string& name) {
  for (const auto& [kind, n] : kKindNames)
    if (name == n) return kind;
  return ShapeKind::None;
}

}  // namespace fabricad::core
