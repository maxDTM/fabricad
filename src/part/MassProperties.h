// FabriCAD part: mass properties (volume, area, mass, centre of gravity, inertia tensor,
// principal axes, radius of gyration) of bodies or component sub-trees.
//
// Each body instance is integrated once with BRepGProp::VolumeProperties in world
// coordinates and the results are combined with GProp_GProps::Add weighted by the body
// density, so occurrences and placements are honoured. Internal units: mm, g, g/cm^3;
// inertia in g*mm^2. Bodies without a density contribute volume and area only.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Document.h"
#include "core/Types.h"

#include <TopoDS_Shape.hxx>

#include <array>
#include <string>
#include <vector>

namespace fabricad::part {

/// One integrable item: a world-space solid and its density (g/cm^3, 0 = unknown).
struct MassItem {
  TopoDS_Shape shape;
  double density = 0.0;
  std::string label;  ///< body / component name for the report
};

struct MassProperties {
  double volume = 0.0;  ///< mm^3, all bodies
  double area = 0.0;    ///< mm^2, all bodies
  double mass = 0.0;    ///< g, bodies with a density
  core::Vec3 cog;       ///< mass centre (volume centroid when no body has a density), world mm
  /// Inertia tensor components [Ixx, Iyy, Izz, Ixy, Ixz, Iyz] in g*mm^2. Off-diagonal entries
  /// are tensor entries (-integral of x*y dm), so the 3x3 matrix is symmetric positive definite.
  std::array<double, 6> inertiaCog{};
  std::array<double, 6> inertiaOrigin{};
  std::array<core::Vec3, 3> principalAxes{};    ///< unit vectors, sorted by moment ascending
  std::array<double, 3> principalMoments{};     ///< g*mm^2, ascending
  std::array<double, 3> radiusOfGyration{};     ///< mm, sqrt(I_i / m) per principal axis
  core::Vec3 bboxMin, bboxMax;
  bool hasBox = false;
  int bodies = 0;
  int bodiesWithoutDensity = 0;
  /// True when the moments/CoG are mass based; false when computed from volume (unit density).
  bool massBased = false;
};

MassProperties computeMassProperties(const std::vector<MassItem>& items);

/// Body instances of the given components and their sub-trees (empty = the whole design), in
/// world coordinates. Hidden components/bodies are skipped. Explicit body ids may be added.
std::vector<MassItem> massItemsFor(const core::Document& doc, const std::vector<core::ObjectId>& components,
                                   const std::vector<core::ObjectId>& bodies = {});

/// 3x3 matrix accessor for a packed [xx, yy, zz, xy, xz, yz] tensor.
double tensorAt(const std::array<double, 6>& t, int r, int c);

/// Reports. `inch` switches lengths to inches and masses to pounds.
std::string massPropertiesText(const MassProperties& p, bool inch = false, int precision = 3);  ///< tab separated
std::string massPropertiesCsv(const MassProperties& p, bool inch = false, int precision = 6);
std::string massPropertiesMarkdown(const MassProperties& p, bool inch = false, int precision = 3);

}  // namespace fabricad::part
