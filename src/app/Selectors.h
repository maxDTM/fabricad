// FabriCAD app: textual geometry selectors for the command line and scripts (docs/SCRIPTING.md).
//
//   body:Seat                 body by name or #id          body:Seat/face:top        planar face by side
//   body:Seat/face:#3         face by index                body:Seat/face:@0,0,460   face nearest a point
//   body:Seat/edges:top       edges of the top face        body:Seat/edges:z         edges parallel to Z
//   body:Seat/face:top/edges  edges bounding a face        body:Seat/edge:Extrude1/Side/c1   history name
//   sketch:Sketch1/profile:@40,30   region containing a sketch point (also profile:#1, largest, all)
//   sketch:Sketch1/curve:#2   sketch geometry              feature:Extrude1  component:Lever  plane:XY  axis:Z
//
// Everything here is widget-free (unit tests in tests/test_selectors.cpp). World coordinates are
// used throughout: body shapes are read in their component's world placement, so "top" means
// +Z in the assembly, exactly what the user sees in the viewport.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/ShapeRef.h"
#include "core/Types.h"

#include <TopoDS_Shape.hxx>

#include <optional>
#include <string>
#include <vector>

namespace fabricad::core {
class Body;
class Component;
class Document;
class Feature;
class Property;
}  // namespace fabricad::core

namespace fabricad::app {

struct SelectorResult {
  std::vector<core::ShapeRef> refs;
  std::string error;  ///< empty = ok
  bool ok() const { return error.empty(); }
};

/// Resolve one selector against the document. Singular segments (face:, edge:) yield one ref and
/// report an error when the spec is ambiguous; plural segments (faces:, edges:) yield all matches.
SelectorResult resolveSelector(const core::Document& doc, const std::string& text);
/// Resolve several selectors; stops at the first error. "none"/"clear" yields an empty, ok result.
SelectorResult resolveSelectors(const core::Document& doc, const std::vector<std::string>& texts);

/// Lookups by user name ("Seat"), case-insensitive name, or "#<id>".
const core::Body* findBody(const core::Document& doc, const std::string& spec);
const core::Feature* findFeature(const core::Document& doc, const std::string& spec);
const core::Component* findComponent(const core::Document& doc, const std::string& spec);

/// Body shape in world coordinates (component placement applied). Null if the body has no shape.
TopoDS_Shape worldBodyShape(const core::Document& doc, core::ObjectId bodyId);

/// What a click at a world point would select (ray-pick against the model instead of the
/// screen). `kinds` is the active selection filter; the usual viewer priority applies: profiles
/// of visible sketches, then vertices, edges, faces of visible bodies, then construction
/// geometry, then whole bodies/components when nothing finer is allowed. With `rayDir` the
/// first face hit along the ray (through `p`) is used instead of the nearest entity.
struct PickHit {
  core::ShapeRef ref;
  core::Vec3 point;      ///< hit point (world)
  double distance = 0;   ///< distance from `p` (0 for ray hits)
};
std::optional<PickHit> pickAt(const core::Document& doc, const core::Vec3& p, core::ShapeKindMask kinds, double tolerance,
                              const std::optional<core::Vec3>& rayDir = std::nullopt);

/// One-line (or few-line) description of an entity for INFO / the log.
std::string describeRef(const core::Document& doc, const core::ShapeRef& ref);
/// Short label: "Body 'Seat' (#18)", "Face 3 of Seat", "Profile of Sketch1 at (40,30)".
std::string refLabel(const core::Document& doc, const core::ShapeRef& ref);
/// Property value as text ("20 mm", "Cut (options: 'Join' 'Cut' ...)", "2 selected: [...]").
std::string propertyText(const core::Document& doc, const core::Property& p);

/// Numeric metric of an entity for CHECK: volume, area, length, mass (g), faces, edges, vertices,
/// xmin xmax ymin ymax zmin zmax, dx dy dz (bbox size), cx cy cz (centroid), radius, profiles,
/// dof, geometry (sketch), bodies (component). nullopt when not applicable.
std::optional<double> refMetric(const core::Document& doc, const core::ShapeRef& ref, const std::string& metric);

/// Parse "x,y" / "x,y,z" (also "(x, y, z)"). Returns the number of coordinates read (0 = not a point).
int parsePoint(const std::string& text, core::Vec3& out);

}  // namespace fabricad::app
