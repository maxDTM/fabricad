// FabriCAD sketch: the 2D sketch model (geometry + constraints), solving, queries and
// editing operations (trim, extend, offset, mirror, fillet). Plane-local coordinates in mm.
// No Qt widgets, no OCCT: fully unit-testable.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "sketch/SketchGeometry.h"

#include <QJsonObject>

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace fabricad::sketch {

/// Result of Sketch::solve().
struct SolveResult {
  enum class Status { Success, Converged, Failed, Conflicting };
  Status status = Status::Success;
  int dofs = 0;                   ///< remaining degrees of freedom (-1 unknown)
  std::vector<int> conflicting;   ///< constraint ids
  std::vector<int> redundant;     ///< constraint ids
  std::set<int> freeGeometry;     ///< geometry ids that still have free parameters
  std::string message;
  bool ok() const { return status == Status::Success || status == Status::Converged; }
  bool isFullyConstrained(int geoId) const { return !freeGeometry.count(geoId); }
};

/// Visual/logical status of one geometry (colour in the viewer).
enum class GeometryStatus { Free, FullyConstrained, Fixed, Projected, Conflicting };

/// Nearest entity to a cursor position.
struct SketchHit {
  GeoRef ref;         ///< geometry id + point position (None = on the curve)
  Pt2 point;          ///< snapped position
  double distance = 0;
};

class Sketch {
 public:
  Sketch();

  // --- geometry -----------------------------------------------------------------
  /// Adds a copy and returns its id (assigned here; the id in `g` is ignored).
  int addGeometry(Geometry g);
  /// Removes the geometry and every constraint that references it.
  bool removeGeometry(int id);
  Geometry* geometry(int id);
  const Geometry* geometry(int id) const;
  const std::vector<Geometry>& geometries() const { return geometries_; }
  std::vector<Geometry>& geometries() { return geometries_; }
  int geometryCount() const { return static_cast<int>(geometries_.size()); }
  bool isEmpty() const { return geometries_.empty(); }
  /// Position of a referenced point (nullopt if the ref is not a point of an existing geometry).
  std::optional<Pt2> pointOf(const GeoRef& ref) const;
  /// Move the referenced point (start/end/center) directly (no solve).
  bool setPointOf(const GeoRef& ref, Pt2 p);
  void setConstruction(const std::vector<int>& ids, bool construction);
  void toggleConstruction(const std::vector<int>& ids);

  // --- constraints --------------------------------------------------------------
  /// Adds a copy and returns its id. Dimensional constraints get a name ("d3") if empty.
  int addConstraint(Constraint c);
  bool removeConstraint(int id);
  Constraint* constraint(int id);
  const Constraint* constraint(int id) const;
  const std::vector<Constraint>& constraints() const { return constraints_; }
  std::vector<Constraint>& constraints() { return constraints_; }
  std::vector<const Constraint*> constraintsOf(int geoId) const;
  /// An existing non-dimensional constraint of this type between these refs (either order).
  const Constraint* findConstraint(ConstraintType type, const GeoRef& a, const GeoRef& b = {}) const;
  /// Points coincident with `ref` through Coincident constraints (transitively), including itself.
  std::vector<GeoRef> coincidentGroup(const GeoRef& ref) const;
  bool areCoincident(const GeoRef& a, const GeoRef& b) const;
  /// Next free dimension name "d<n>".
  std::string nextDimensionName() const;
  const Constraint* dimensionByName(const std::string& name) const;
  /// Current measured value of a dimensional constraint from the geometry (mm or degrees).
  std::optional<double> measure(const Constraint& c) const;
  /// Default label position for a dimension (used when none was placed).
  Pt2 defaultLabelPos(const Constraint& c) const;

  // --- solving ------------------------------------------------------------------
  /// Full solve + diagnosis (DOF, conflicts, free geometry). Geometry is updated on success.
  SolveResult solve();
  const SolveResult& lastResult() const { return last_; }
  int dofs() const { return last_.dofs; }
  GeometryStatus statusOf(int geoId) const;
  /// Conflicting constraints after the last solve.
  bool hasConflicts() const { return !last_.conflicting.empty(); }

  // --- dragging (live solve; call solve() at the end) -----------------------------
  bool beginDrag(const GeoRef& ref, Pt2 at);
  /// Solve with the dragged entity pulled towards `target`. Returns false if the solve failed.
  bool drag(Pt2 target);
  void endDrag();
  bool isDragging() const { return dragging_; }

  // --- queries --------------------------------------------------------------------
  /// Nearest referencable point (endpoints, centres, midpoints) within tol.
  std::optional<SketchHit> hitPoint(Pt2 q, double tol, const std::set<int>& exclude = {}) const;
  /// Nearest curve within tol (point positions excluded).
  std::optional<SketchHit> hitCurve(Pt2 q, double tol, const std::set<int>& exclude = {}) const;
  /// Point first, then curve.
  std::optional<SketchHit> hit(Pt2 q, double tol, const std::set<int>& exclude = {}) const;
  /// Curve ids whose bbox intersects the rectangle (window selection; `crossing` = touching counts).
  std::vector<int> curvesInRect(Pt2 a, Pt2 b, bool crossing) const;
  /// Intersection points of a curve with all other (non-point) curves.
  std::vector<Pt2> intersectionsOn(int curveId, bool includeConstruction = true) const;
  /// Bounding box of all geometry (false if empty).
  bool bbox(Pt2& mn, Pt2& mx, bool includeConstruction = true) const;
  /// Curves connected end-to-end to `curveId` (chain through coincident endpoints), including it.
  std::vector<int> chain(int curveId) const;

  // --- operations --------------------------------------------------------------------
  /// Remove the piece of `curveId` containing `at` between the nearest intersections.
  /// Returns the ids of the remaining pieces (possibly empty). Constraints on the trimmed
  /// curve are transferred to the pieces where endpoints survive.
  std::vector<int> trim(int curveId, Pt2 at);
  /// The piece of `curveId` that trim() would remove at `at` (for hover preview).
  std::optional<Geometry> trimPiece(int curveId, Pt2 at) const;
  /// Extend the end of the curve nearest to `near` to the next intersection with another curve.
  bool extend(int curveId, Pt2 near);
  /// Where extend() would move the end nearest to `near` (for hover preview).
  std::optional<Pt2> extendTarget(int curveId, Pt2 near, PointPos* whichEnd = nullptr) const;
  /// Offset copies of the curves by `distance` (positive = to the left of the first curve's
  /// direction, or towards `side` if given). Chains are offset together with corners trimmed.
  std::vector<int> offset(const std::vector<int>& ids, double distance, std::optional<Pt2> side = std::nullopt);
  /// Mirror copies about the line geometry `mirrorLineId`. Adds Symmetric constraints when `constrain`.
  std::vector<int> mirror(const std::vector<int>& ids, int mirrorLineId, bool constrain = true);
  /// Mirror copies about an arbitrary line (a, direction d); no constraints.
  std::vector<int> mirror(const std::vector<int>& ids, Pt2 a, Pt2 d);
  /// Fillet between two lines (or line/arc) near the pick points: trims both and inserts a tangent arc.
  std::optional<int> fillet(int id1, Pt2 near1, int id2, Pt2 near2, double radius);
  /// Split a curve at a point into two (returns the new second piece id).
  std::optional<int> split(int curveId, Pt2 at);

  // --- expressions ------------------------------------------------------------------
  /// Evaluate dimension expressions. `lookup` resolves external parameter names and
  /// returns nullopt if unknown; returns the list of error messages (empty = all good).
  using ExternalEval = std::function<std::optional<double>(const std::string& expression, bool angle, std::string* error)>;
  std::vector<std::string> evaluateExpressions(const ExternalEval& external);

  // --- serialisation ------------------------------------------------------------------
  QJsonObject toJson() const;
  void fromJson(const QJsonObject& o);
  void clear();

 private:
  int nextGeoId_ = 1;
  int nextConstraintId_ = 1;
  std::vector<Geometry> geometries_;
  std::vector<Constraint> constraints_;
  SolveResult last_;

  // drag state
  bool dragging_ = false;
  GeoRef dragRef_;
  Pt2 dragStart_;
  std::vector<double> dragParams_;  ///< original params of the dragged geometry

  friend class SketchSolver;
};

}  // namespace fabricad::sketch
