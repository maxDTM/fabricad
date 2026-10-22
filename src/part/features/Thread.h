// FabriCAD part: Thread feature (Fusion CREATE > Thread). ISO metric coarse/fine threads on
// cylindrical faces, internal (holes) or external (rods), cosmetic (data only, for drawings
// and the browser) or modelled (helical groove cut with the ISO 68-1 basic profile). The
// size can follow the cylinder diameter (Auto size) or be picked from the ISO 261 table.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "part/ExtraUtils.h"
#include "part/PartFeature.h"
#include "part/PartUtils.h"

#include <TopoDS_Face.hxx>
#include <gp_Ax1.hxx>

#include <optional>
#include <string>
#include <vector>

namespace fabricad::part {

/// Geometry of a cylindrical face inside its body, as needed to place a thread.
struct ThreadCylinder {
  gp_Ax1 axis;          ///< cylinder axis, direction from the face's low end (v0) to its high end (v1)
  double radius = 0;
  bool internal = false;  ///< hole (material outside the cylinder)
  double v0 = 0, v1 = 0;  ///< face extent along the axis (parameter = distance from axis.Location())
  bool openAt0 = false;   ///< a thread may run out through the face boundary at v0 (end face / opening)
  bool openAt1 = false;
};
/// nullopt when `face` is not a full-turn cylindrical face of `body`.
std::optional<ThreadCylinder> analyseThreadCylinder(const TopoDS_Shape& body, const TopoDS_Shape& face);

/// One thread as placed on the model (cosmetic or modelled).
struct ThreadInfo {
  std::string designation;   ///< "M10x1.5"
  double nominal = 0, pitch = 0;
  bool internal = false;
  bool modeled = false;
  bool leftHanded = false;
  gp_Ax1 axis;               ///< start point of the thread, direction into the thread
  double length = 0;
  std::string threadClass;    ///< tolerance class; empty = ISO default (6H internal, 6g external)
  std::string callout() const;  ///< "M10x1.5 - 6H" (internal) / "M10x1.5 - 6g" (external), "LH" appended
};

/// Placement of a thread on an analysed cylinder: `fullLength` or [offset, offset + length]
/// from the start end (v0, or v1 when `flip`). Fills info; cutter (null for cosmetic) when modelled.
bool placeThread(const ThreadCylinder& cyl, const IsoThread& iso, bool fullLength, double offset, double length, bool flip,
                 bool leftHanded, bool modeled, ThreadInfo& info, TopoDS_Shape* cutter, std::string* error);

/// Cut thread cutters from a body (no unify, ShapeFix fallback); shared by Thread and Hole.
BoolResult threadCut(const TopoDS_Shape& body, const std::vector<TopoDS_Shape>& cutters);

class ThreadFeature : public PartFeature {
 public:
  static constexpr const char* kType = "Thread";
  ThreadFeature();
  void compute(core::RecomputeContext& ctx, Outcome& out) override;
  void updatePropertyFlags() override;
  void fromJson(const QJsonObject& o) override;
  std::string primaryInput() const override { return "faces"; }
  Operation operation() const override { return Operation::Cut; }
  /// Modelled threads pattern their cutters; cosmetic threads have nothing to pattern.
  TopoDS_Shape toolShape() const override { return outputShape(); }

  /// Threads from the last recompute (one per face), for drawings / inspection.
  const std::vector<ThreadInfo>& threads() const { return threads_; }

 private:
  /// Keep designation (stored) and size / sizeDesignation (dialog) consistent.
  void syncDesignation();
  std::vector<ThreadInfo> threads_;
  std::string lastDesignation_;
};

}  // namespace fabricad::part
