// FabriCAD assembly: joint solver. Pure math on placements, no Document access, so it is
// unit-testable without a GL context.
//
// Model (docs/research/algorithms.md section 4): a joint between component A ("component 1",
// the one that moves) and component B ("component 2", the anchor) states
//
//     W_A * L_A = W_B * L_B * Offset * M(q) * Align
//
// where W_x are world placements, L_x the joint origins (frames in component-local
// coordinates, as transforms frame->component), Offset the user offset in the joint
// frame, M(q) the joint motion (JointTypes.h) and Align = RotX(pi) unless `flip` is set,
// so that two picked faces mate with opposed normals.
//
// Phase 1 (tree propagation): BFS from grounded components (the root component is always
// grounded) placing the unknown side of every joint by direct composition. Floating groups
// (no grounded member) keep component B of their first joint where it is.
// Phase 2 (numeric fallback): when a loop closes inconsistently, every joint of that
// connected group becomes a least-squares residual and the non-grounded members are solved
// with Levenberg-Marquardt (6 unknowns per component, Eigen). Joints whose motion is not
// locked leave their DOF free, which is what makes four-bar linkages and similar loops work.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "assembly/JointTypes.h"
#include "core/Types.h"

#include <gp_Ax3.hxx>
#include <gp_Trsf.hxx>

#include <map>
#include <set>
#include <string>
#include <vector>

namespace fabricad::assembly {

struct SolverComponent {
  core::ObjectId id = core::kInvalidId;
  core::ObjectId parent = core::kInvalidId;  ///< kInvalidId = root (always grounded)
  gp_Trsf world;                              ///< current world placement (input / warm start)
  bool grounded = false;
};

struct SolverJoint {
  core::ObjectId id = core::kInvalidId;     ///< joint feature id (for status reporting)
  core::ObjectId compA = core::kInvalidId;  ///< moving side (Fusion "component 1")
  core::ObjectId compB = core::kInvalidId;  ///< anchor side (Fusion "component 2")
  gp_Ax3 frameA;                            ///< joint origin on A, component-local
  gp_Ax3 frameB;                            ///< joint origin on B, component-local
  JointType type = JointType::Rigid;
  bool flip = false;                        ///< false: Z axes opposed (mating), true: aligned
  gp_Trsf offset;                           ///< user offset in the joint frame (B side)
  JointValues q{0, 0, 0};                   ///< motion values (deg / mm)
  bool lockMotion = false;                  ///< numeric phase: enforce q (driven) instead of leaving the DOF free
  bool enabled = true;                      ///< suppressed joints are skipped
};

enum class JointSolveStatus {
  Ok,        ///< placed by tree propagation (or consistent loop)
  Numeric,   ///< satisfied by the numeric fallback
  Conflict,  ///< loop could not be closed; placements from the tree phase kept
  Skipped,   ///< unknown component or disabled
};
const char* jointSolveStatusName(JointSolveStatus s);

struct SolverResult {
  std::map<core::ObjectId, gp_Trsf> world;                   ///< final world placement of every component
  std::set<core::ObjectId> moved;                            ///< components whose placement a joint determined
  std::map<core::ObjectId, JointSolveStatus> jointStatus;    ///< per joint id
  std::map<core::ObjectId, std::string> jointMessage;
  std::map<core::ObjectId, JointValues> jointValues;         ///< actual motion values after solving
  bool usedNumeric = false;
  int numericIterations = 0;
  double maxResidual = 0.0;    ///< largest joint residual (mm-ish) after solving
  int freeDof = 0;             ///< remaining degrees of freedom (numeric groups only)
  bool ok() const;             ///< no Conflict
};

struct SolverOptions {
  double positionTol = 1e-4;   ///< mm
  double directionTol = 1e-6;  ///< unit-vector difference
  int maxIterations = 60;
  bool allowNumeric = true;
};

class AssemblySolver {
 public:
  static SolverResult solve(const std::vector<SolverComponent>& components, const std::vector<SolverJoint>& joints,
                            const SolverOptions& options = {});

  // --- building blocks (also used by tools for previews) -------------------------
  /// Transform frame coordinates -> component coordinates for a joint origin.
  static gp_Trsf frameTransform(const gp_Ax3& frame);
  static gp_Trsf alignTransform(bool flip);
  /// W_A from W_B (component 1 snaps onto component 2).
  static gp_Trsf placeA(const gp_Trsf& worldB, const SolverJoint& j);
  /// W_B from W_A (joint traversed backwards in the tree).
  static gp_Trsf placeB(const gp_Trsf& worldA, const SolverJoint& j);
  /// World frame of the joint (B side, offset applied): where glyphs are drawn.
  static gp_Trsf jointWorldFrame(const gp_Trsf& worldB, const SolverJoint& j);
  /// Motion values that explain the current placements (inverse of placeA).
  static JointValues currentValues(const SolverJoint& j, const gp_Trsf& worldA, const gp_Trsf& worldB);
  /// Joint origins for an As-built joint: both components keep their placements; the joint
  /// frame is `worldFrame` (e.g. a picked axis). Fills frameA/frameB of `j`.
  static void asBuiltFrames(SolverJoint& j, const gp_Trsf& worldA, const gp_Trsf& worldB, const gp_Ax3& worldFrame);
  /// Residual vector of one joint for given placements (empty when the joint is disabled).
  static std::vector<double> residual(const SolverJoint& j, const gp_Trsf& worldA, const gp_Trsf& worldB);
  /// Largest absolute component of residual(), 0 for an exactly satisfied joint.
  static double residualNorm(const SolverJoint& j, const gp_Trsf& worldA, const gp_Trsf& worldB);
  /// Are two placements equal within tolerance?
  static bool placementsEqual(const gp_Trsf& a, const gp_Trsf& b, double positionTol, double directionTol);
};

}  // namespace fabricad::assembly
