// FabriCAD part module: helpers shared by the round-2 features (primitives, coil, thread,
// rib, thicken, scale, split face): plane inputs, helices and helical sweeps, analytic face
// extension, ISO metric thread table.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Property.h"

#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>

#include <optional>
#include <string>
#include <vector>

namespace fabricad::core {
class RecomputeContext;
}

namespace fabricad::part {

/// Plane of a Ref property: origin/construction planes, sketches and planar faces (the face is
/// resolved through the naming layer first). nullopt when unset or not planar.
std::optional<gp_Ax3> resolvePlaneInput(core::RecomputeContext& ctx, core::Property& p);

/// Transformation that maps the standard frame (origin, Z up, X = +X) onto `frame`.
gp_Trsf frameTransform(const gp_Ax3& frame);

/// Helix about +Z through the origin, starting at (radius, 0, 0) and rising `height` with the
/// given pitch. `taperDeg` != 0 puts it on a cone (positive = radius shrinks with height).
/// `leftHanded` turns clockwise seen from +Z. Null on invalid input.
TopoDS_Wire makeHelix(double radius, double pitch, double height, double taperDeg = 0.0, bool leftHanded = false);

/// Sweep a closed profile wire lying in the meridian (XZ) plane along a helix about Z (binormal
/// mode keeps the profile in the meridian plane, so the volume is A * 2*pi*r * turns by Pappus).
/// Returns a solid or null (error text filled).
TopoDS_Shape sweepAlongHelix(const TopoDS_Wire& profile, const TopoDS_Wire& helix, std::string* error);

/// Closed polygon wire through the points (last connects to first).
TopoDS_Wire polygonWire(const std::vector<gp_Pnt>& pts);

/// Face on the untrimmed surface of `face` large enough to cut a model of size `size`: planes,
/// cylinders, cones, spheres and tori are extended; other surfaces return the face unchanged.
TopoDS_Shape extendedFace(const TopoDS_Face& face, double size);

/// Splitting tool of Split Body / Split Face: planes (origin, construction, sketch, planar
/// face) become a large planar face (`plane` receives it); other faces are extended when
/// `extend` is set; bodies are used as they are. Null + ctx error when unresolvable.
TopoDS_Shape resolveSplitTool(core::RecomputeContext& ctx, core::Property& p, bool extend, std::optional<gp_Ax3>* plane);

// --- ISO metric screw threads (ISO 261 / ISO 724 / ISO 965) -------------------------------

struct IsoThread {
  std::string designation;  ///< "M10x1.5" (coarse threads are listed first for each size)
  double nominal = 0;       ///< major diameter D (mm)
  double pitch = 0;         ///< P (mm)
  bool coarse = false;
  /// Basic minor diameter D1 = d1 = D - 1.082532 P (ISO 68-1 basic profile; nut minor diameter
  /// and the root of a modelled external thread).
  double minorDiameter() const { return nominal - 1.0825318 * pitch; }
  double pitchDiameter() const { return nominal - 0.6495191 * pitch; }  ///< d2 = D2
  double tapDrill() const { return nominal - pitch; }                   ///< common tap drill D - P
};

/// The full table, ordered by nominal diameter then coarse/fine (deterministic).
const std::vector<IsoThread>& isoMetricThreads();
/// Designations in table order (enum options).
const std::vector<std::string>& isoMetricDesignations();
/// Lookup by designation ("M8x1.25"; "M8" alone means the coarse pitch). nullptr if unknown.
const IsoThread* findIsoThread(const std::string& designation);
/// Coarse thread whose nominal diameter matches `majorDiameter` (external, +-0.1 mm) or whose
/// internal minor diameter / tap drill matches it (internal, +-0.15 mm). nullptr if none.
const IsoThread* isoThreadForDiameter(double diameter, bool internal);

/// Helical cutter for a modelled ISO thread of the given length on a cylinder (axis +Z through
/// the origin, thread from z = 0 up to z = length). External: removes the groove from a rod of
/// diameter D (cutter reaches 0.25 P outside D). Internal: removes the groove from a hole of
/// diameter D1 (cutter reaches 0.25 P inside D1). The helix starts one pitch below z = 0 and
/// ends one pitch above `length` when `runOut` is set (thread through an end face); profile
/// truncations follow the ISO basic profile (crest P/8, root P/4). `surfaceRadius` (the real
/// cylinder radius, 0 = unknown) keeps the cutter's flank kink 0.05 P off the body surface.
/// Null + error on failure.
TopoDS_Shape makeThreadCutter(const IsoThread& t, double length, bool internal, bool leftHanded, bool runOutStart,
                              bool runOutEnd, std::string* error, double surfaceRadius = 0);

}  // namespace fabricad::part
