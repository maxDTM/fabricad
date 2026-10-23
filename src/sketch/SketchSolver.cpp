// FabriCAD sketch: PlaneGCS wrapper (build system, solve, diagnose, copy back).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sketch/SketchSolver.h"

#include "core/Log.h"
#include "sketch/SketchCurves.h"

#include "GCS.h"  // PlaneGCS (third_party/planegcs)

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <optional>
#include <tuple>

namespace fabricad::sketch {

namespace {

struct GeoSlot {
  int offset = 0;         ///< index of the first parameter in the buffer
  int count = 0;
  const Geometry* geo = nullptr;
  // GCS views into the buffer
  GCS::Point point;       ///< Point geometry
  GCS::Line line;
  GCS::Circle circle;
  GCS::Arc arc;
  GCS::Ellipse ellipse;
  GCS::ArcOfEllipse arcEllipse;
  GCS::BSpline bspline;
  std::vector<GCS::Point> verts;  ///< spline control / fit points (user-referencable)
  bool fitRules = false;          ///< FitSpline: add the interpolation constraints
  std::vector<double> fitParams;  ///< FitSpline: parameter of each fit point
};

class Builder {
 public:
  explicit Builder(Sketch& sk) : sketch_(sk) {}

  SolveResult run(const DragTarget* drag) {
    sys_.debugMode = GCS::NoDebug;
    allocate();
    buildGeometry();
    declareUnknowns();
    addFitRules();
    buildConstraints();
    if (drag) addDrag(*drag);
    const bool soft = addKeepShape(drag);

    GCS::VEC_pD unknowns = unknowns_;
    sys_.declareUnknowns(unknowns);
    sys_.initSolution(GCS::DogLeg);

    SolveResult res;
    GCS::SolveStatus st = sys_.solve(GCS::DogLeg);
    if (st == GCS::SolveStatus::Failed) {
      // LM fallback
      sys_.initSolution(GCS::LevenbergMarquardt);
      st = sys_.solve(GCS::LevenbergMarquardt);
    }
    if (st == GCS::SolveStatus::Failed && soft) {
      // the keep-shape preferences can steer the solve badly: retry once without them
      sys_.undoSolution();
      sys_.clearByTag(kKeepShapeTag);
      sys_.declareUnknowns(unknowns);
      sys_.initSolution(GCS::DogLeg);
      st = sys_.solve(GCS::DogLeg);
      if (st == GCS::SolveStatus::Failed) {
        sys_.initSolution(GCS::LevenbergMarquardt);
        st = sys_.solve(GCS::LevenbergMarquardt);
      }
    }
    switch (st) {
      case GCS::SolveStatus::Success: res.status = SolveResult::Status::Success; break;
      case GCS::SolveStatus::Converged: res.status = SolveResult::Status::Converged; break;
      case GCS::SolveStatus::SuccessfulSolutionInvalid: res.status = SolveResult::Status::Converged; break;
      case GCS::SolveStatus::Failed: res.status = SolveResult::Status::Failed; break;
    }
    if (res.ok()) {
      sys_.applySolution();
      // a "successful" solve of contradictory constraints (e.g. a line coincident with itself
      // end to end, from a damaged file) can leave NaN in the unknowns: keep the old geometry
      if (!std::all_of(buf_.begin(), buf_.end(), [](double v) { return std::isfinite(v); })) {
        sys_.undoSolution();
        res.status = SolveResult::Status::Failed;
        res.message = "solver failed to converge";
      } else if (collapsed()) {
        // Degenerate solution (zero-length line, zero radius): the constraints are
        // inconsistent for real geometry. Keep the previous geometry and report a conflict.
        for (const auto& [id, slot] : slots_)
          if (collapsedSlot(slot))
            for (const Constraint* c : sketch_.constraintsOf(id))
              if (!c->isDimensional() && !std::count(res.conflicting.begin(), res.conflicting.end(), c->id))
                res.conflicting.push_back(c->id);
        sys_.undoSolution();
        res.status = SolveResult::Status::Conflicting;
        res.message = "constraints collapse geometry";
      } else {
        copyBack();
      }
    } else {
      sys_.undoSolution();
      res.message = "solver failed to converge";
    }

    // diagnosis (initSolution already ran it; read the results)
    res.dofs = sys_.dofsNumber();
    // PlaneGCS reports "parameters - non-redundant constraints", which goes negative when the
    // rank check finds redundancy in a fully determined system (a centre-to-centre slot with both
    // position dimensions on projected edges): that is 0 DOF, not "unknown"
    if (res.dofs < 0 && sys_.hasDiagnosisResult()) res.dofs = 0;
    if (res.dofs < 0) res.dofs = static_cast<int>(unknowns_.size());
    GCS::VEC_I tags;
    sys_.getConflicting(tags);
    for (int t : tags)
      if (t > 0 && !std::count(res.conflicting.begin(), res.conflicting.end(), t)) res.conflicting.push_back(t);
    sys_.getRedundant(tags);
    for (int t : tags)
      if (t > 0 && !std::count(res.redundant.begin(), res.redundant.end(), t)) res.redundant.push_back(t);
    sys_.getPartiallyRedundant(tags);
    for (int t : tags)
      if (t > 0 && !std::count(res.redundant.begin(), res.redundant.end(), t)) res.redundant.push_back(t);
    std::sort(res.conflicting.begin(), res.conflicting.end());
    std::sort(res.redundant.begin(), res.redundant.end());
    if (!res.conflicting.empty()) {
      res.status = SolveResult::Status::Conflicting;
      if (res.message.empty()) res.message = "conflicting constraints";
    }

    // free geometry: any of its unknown parameters is reported dependent (i.e. still free)
    GCS::VEC_pD dep;
    sys_.getDependentParams(dep);
    std::set<const double*> depSet(dep.begin(), dep.end());
    for (const auto& [id, slot] : slots_) {
      bool free = false;
      for (int i = 0; i < slot.count; ++i) {
        const double* p = &buf_[static_cast<size_t>(slot.offset + i)];
        if (depSet.count(p)) {
          free = true;
          break;
        }
      }
      // with no driving constraints at all PlaneGCS reports an empty matrix: everything movable is free
      if (sys_.isEmptyDiagnoseMatrix() && !unknownsOf_[id].empty()) free = true;
      if (free) res.freeGeometry.insert(id);
    }
    // an entity with no unknowns at all is never free
    for (const auto& [id, u] : unknownsOf_)
      if (u.empty()) res.freeGeometry.erase(id);
    return res;
  }

 private:
  bool collapsedSlot(const GeoSlot& s) const {
    const Geometry& g = *s.geo;
    const double* b = &buf_[static_cast<size_t>(s.offset)];
    const double eps = 1e-7;
    switch (g.type) {
      case GeomType::Point: return false;
      case GeomType::Line: return g.length() > 1e-6 && std::hypot(b[2] - b[0], b[3] - b[1]) < eps;
      case GeomType::Circle: return g.radius() > 1e-6 && std::abs(b[2]) < eps;
      case GeomType::Arc: return g.radius() > 1e-6 && std::abs(b[2]) < eps;
      case GeomType::Ellipse:
      case GeomType::EllipseArc: return g.minorRadius() > 1e-6 && std::abs(b[4]) < eps;
      case GeomType::Spline:
      case GeomType::FitSpline:
      case GeomType::Text: return false;
    }
    return false;
  }
  bool collapsed() const {
    for (const auto& [id, s] : slots_)
      if (collapsedSlot(s)) return true;
    return false;
  }

  void allocate() {
    size_t total = 0;
    for (const Geometry& g : sketch_.geometries()) total += g.p.size();
    // auxiliary mid points
    for (const Constraint& c : sketch_.constraints()) {
      for (const GeoRef* r : {&c.a, &c.b, &c.c})
        if (r->isValid() && r->pos == PointPos::Mid) {
          const Geometry* g = sketch_.geometry(r->geo);
          if (g && g->type != GeomType::Point) total += 2;
        }
    }
    buf_.assign(total + 4, 0.0);
    next_ = 0;
  }

  void buildGeometry() {
    for (const Geometry& g : sketch_.geometries()) {
      GeoSlot s;
      s.geo = &g;
      s.offset = next_;
      s.count = static_cast<int>(g.p.size());
      for (size_t i = 0; i < g.p.size(); ++i) buf_[static_cast<size_t>(next_ + static_cast<int>(i))] = g.p[i];
      double* b = &buf_[static_cast<size_t>(s.offset)];
      switch (g.type) {
        case GeomType::Point: s.point = GCS::Point(b, b + 1); break;
        case GeomType::Line:
          s.line.p1 = GCS::Point(b, b + 1);
          s.line.p2 = GCS::Point(b + 2, b + 3);
          break;
        case GeomType::Circle:
          s.circle.center = GCS::Point(b, b + 1);
          s.circle.rad = b + 2;
          break;
        case GeomType::Arc:
          s.arc.center = GCS::Point(b, b + 1);
          s.arc.rad = b + 2;
          s.arc.startAngle = b + 3;
          s.arc.endAngle = b + 4;
          s.arc.start = GCS::Point(b + 5, b + 6);
          s.arc.end = GCS::Point(b + 7, b + 8);
          break;
        case GeomType::Ellipse:
          s.ellipse.center = GCS::Point(b, b + 1);
          s.ellipse.focus1 = GCS::Point(b + 2, b + 3);
          s.ellipse.radmin = b + 4;
          break;
        case GeomType::EllipseArc:
          s.arcEllipse.center = GCS::Point(b, b + 1);
          s.arcEllipse.focus1 = GCS::Point(b + 2, b + 3);
          s.arcEllipse.radmin = b + 4;
          s.arcEllipse.startAngle = b + 5;
          s.arcEllipse.endAngle = b + 6;
          s.arcEllipse.start = GCS::Point(b + 7, b + 8);
          s.arcEllipse.end = GCS::Point(b + 9, b + 10);
          break;
        case GeomType::Spline:
        case GeomType::FitSpline:
          for (int k = 0; k < g.vertexCount(); ++k) s.verts.push_back(GCS::Point(b + 2 * k, b + 2 * k + 1));
          break;
        case GeomType::Text: s.point = GCS::Point(b, b + 1); break;
      }
      next_ += s.count;
      slots_[g.id] = s;
    }
    for (auto& [id, s] : slots_) {
      if (s.geo->type == GeomType::Arc) sys_.addConstraintArcRules(s.arc, 0);
      if (s.geo->type == GeomType::EllipseArc) sys_.addConstraintArcOfEllipseRules(s.arcEllipse, 0);
      if (s.geo->isSpline()) setupSpline(s);
    }
  }

  /// GCS B-spline of a Spline (poles = the geometry's points) or FitSpline (auxiliary poles
  /// tied to the fit points by interpolation constraints added in addFitRules()).
  void setupSpline(GeoSlot& s) {
    const Geometry& g = *s.geo;
    BSpline2 sp = g.bspline();
    if (!sp.isValid() || s.verts.size() < 2) return;
    GCS::BSpline& b = s.bspline;
    b.degree = sp.degree;
    b.periodic = false;
    if (g.type == GeomType::Spline) {
      b.poles = s.verts;
    } else {
      for (const Pt2& q : sp.poles) {
        double* x = aux(q.x);
        double* y = aux(q.y);
        b.poles.push_back(GCS::Point(x, y));
      }
      s.fitRules = true;
      s.fitParams = g.knots.size() == s.verts.size() ? g.knots : chordLengthParams([&] {
        std::vector<Pt2> v;
        for (int k = 0; k < g.vertexCount(); ++k) v.push_back(g.vertex(k));
        return v;
      }());
    }
    for (size_t i = 0; i < b.poles.size(); ++i) b.weights.push_back(value(1.0));
    std::vector<double> vals;
    std::vector<int> mults;
    sp.distinctKnots(vals, mults);
    for (double v : vals) b.knots.push_back(value(v));
    b.mult = mults;
    b.start = s.verts.front();
    b.end = s.verts.back();
    b.setupFlattenedKnots();
  }

  /// Interpolation constraints of fit-point splines (after the unknowns are known).
  void addFitRules() {
    for (auto& [id, s] : slots_) {
      if (!s.fitRules || s.bspline.poles.empty()) continue;
      bool movable = !unknownsOf_[id].empty();
      if (movable)
        for (auto& pole : s.bspline.poles) {
          unknowns_.push_back(pole.x);
          unknowns_.push_back(pole.y);
        }
      if (!movable) continue;  // fixed spline: the poles keep their interpolated values
      for (size_t k = 0; k < s.verts.size() && k < s.fitParams.size(); ++k)
        sys_.addConstraintPointOnBSpline(s.verts[k], s.bspline, value(s.fitParams[k]), 0);
    }
  }

  void declareUnknowns() {
    // Fix constraints: whole geometry or single point
    std::set<int> fixedGeos;
    std::set<std::tuple<int, PointPos, int>> fixedPoints;
    for (const Constraint& c : sketch_.constraints()) {
      if (c.type != ConstraintType::Fix || !c.a.isValid()) continue;
      if (c.a.pos == PointPos::None) fixedGeos.insert(c.a.geo);
      else fixedPoints.insert({c.a.geo, c.a.pos, c.a.pos == PointPos::Vertex ? c.a.index : -1});
    }
    for (auto& [id, s] : slots_) {
      std::vector<double*> list;
      const Geometry& g = *s.geo;
      if (g.isMovable() && !fixedGeos.count(id)) {
        double* b = &buf_[static_cast<size_t>(s.offset)];
        auto isFixed = [&](PointPos pos) { return fixedPoints.count({id, pos, -1}) > 0; };
        auto vertexFixed = [&](int k, int n) {
          return fixedPoints.count({id, PointPos::Vertex, k}) > 0 || (k == 0 && isFixed(PointPos::Start)) ||
                 (k == n - 1 && isFixed(PointPos::End));
        };
        switch (g.type) {
          case GeomType::Point:
            if (!isFixed(PointPos::Center)) list = {b, b + 1};
            break;
          case GeomType::Line:
            if (!isFixed(PointPos::Start)) { list.push_back(b); list.push_back(b + 1); }
            if (!isFixed(PointPos::End)) { list.push_back(b + 2); list.push_back(b + 3); }
            break;
          case GeomType::Circle:
            if (!isFixed(PointPos::Center)) { list.push_back(b); list.push_back(b + 1); }
            list.push_back(b + 2);
            break;
          case GeomType::Arc:
            if (!isFixed(PointPos::Center)) { list.push_back(b); list.push_back(b + 1); }
            list.push_back(b + 2);
            list.push_back(b + 3);
            list.push_back(b + 4);
            if (!isFixed(PointPos::Start)) { list.push_back(b + 5); list.push_back(b + 6); }
            if (!isFixed(PointPos::End)) { list.push_back(b + 7); list.push_back(b + 8); }
            break;
          case GeomType::Ellipse:
            if (!isFixed(PointPos::Center)) { list.push_back(b); list.push_back(b + 1); }
            list.push_back(b + 2);
            list.push_back(b + 3);
            list.push_back(b + 4);
            break;
          case GeomType::EllipseArc:
            if (!isFixed(PointPos::Center)) { list.push_back(b); list.push_back(b + 1); }
            for (int i = 2; i <= 6; ++i) list.push_back(b + i);
            if (!isFixed(PointPos::Start)) { list.push_back(b + 7); list.push_back(b + 8); }
            if (!isFixed(PointPos::End)) { list.push_back(b + 9); list.push_back(b + 10); }
            break;
          case GeomType::Spline:
          case GeomType::FitSpline: {
            int n = g.vertexCount();
            for (int k = 0; k < n; ++k)
              if (!vertexFixed(k, n)) { list.push_back(b + 2 * k); list.push_back(b + 2 * k + 1); }
            break;
          }
          case GeomType::Text:
            if (!isFixed(PointPos::Start)) list = {b, b + 1};
            break;
        }
      }
      unknownsOf_[id] = list;
      unknowns_.insert(unknowns_.end(), list.begin(), list.end());
    }
  }

  double* value(double v) {
    values_.push_back(v);
    return &values_.back();
  }
  /// Auxiliary unknown storage (stable addresses).
  double* aux(double v) {
    aux_.push_back(v);
    return &aux_.back();
  }

  GeoSlot* slot(int geoId) {
    auto it = slots_.find(geoId);
    return it == slots_.end() ? nullptr : &it->second;
  }

  GCS::Curve* curve(GeoSlot& s) {
    switch (s.geo->type) {
      case GeomType::Line: return &s.line;
      case GeomType::Circle: return &s.circle;
      case GeomType::Arc: return &s.arc;
      case GeomType::Ellipse: return &s.ellipse;
      case GeomType::EllipseArc: return &s.arcEllipse;
      case GeomType::Spline:
      case GeomType::FitSpline: return s.bspline.poles.empty() ? nullptr : &s.bspline;
      case GeomType::Point:
      case GeomType::Text: return nullptr;
    }
    return nullptr;
  }

  /// GCS point for a reference. Mid points get an auxiliary unknown point tied to the curve.
  std::optional<GCS::Point> point(const GeoRef& r) {
    GeoSlot* s = slot(r.geo);
    if (!s) return std::nullopt;
    const Geometry& g = *s->geo;
    switch (g.type) {
      case GeomType::Point: return s->point;
      case GeomType::Line:
        if (r.pos == PointPos::Start) return s->line.p1;
        if (r.pos == PointPos::End) return s->line.p2;
        if (r.pos == PointPos::Mid || r.pos == PointPos::Center) return midPoint(*s);
        return std::nullopt;
      case GeomType::Circle:
        if (r.pos == PointPos::Center) return s->circle.center;
        return std::nullopt;
      case GeomType::Arc:
        if (r.pos == PointPos::Start) return s->arc.start;
        if (r.pos == PointPos::End) return s->arc.end;
        if (r.pos == PointPos::Center) return s->arc.center;
        if (r.pos == PointPos::Mid) return midPoint(*s);
        return std::nullopt;
      case GeomType::Ellipse:
        if (r.pos == PointPos::Center) return s->ellipse.center;
        return std::nullopt;
      case GeomType::EllipseArc:
        if (r.pos == PointPos::Start) return s->arcEllipse.start;
        if (r.pos == PointPos::End) return s->arcEllipse.end;
        if (r.pos == PointPos::Center) return s->arcEllipse.center;
        return std::nullopt;
      case GeomType::Spline:
      case GeomType::FitSpline:
        if (s->verts.empty()) return std::nullopt;
        if (r.pos == PointPos::Start) return s->verts.front();
        if (r.pos == PointPos::End) return s->verts.back();
        if (r.pos == PointPos::Vertex && r.index >= 0 && r.index < static_cast<int>(s->verts.size()))
          return s->verts[static_cast<size_t>(r.index)];
        return std::nullopt;
      case GeomType::Text:
        if (r.pos == PointPos::Start || r.pos == PointPos::Center) return s->point;
        return std::nullopt;
    }
    return std::nullopt;
  }

  GCS::Point midPoint(GeoSlot& s) {
    auto it = midPoints_.find(s.geo->id);
    if (it != midPoints_.end()) return it->second;
    Pt2 m = s.geo->midpoint();
    double* b = &buf_[static_cast<size_t>(next_)];
    next_ += 2;
    b[0] = m.x;
    b[1] = m.y;
    GCS::Point p(b, b + 1);
    unknowns_.push_back(b);
    unknowns_.push_back(b + 1);
    midPoints_[s.geo->id] = p;
    // tie: equidistant from the ends and on the curve
    if (s.geo->type == GeomType::Line) {
      sys_.addConstraintPointOnPerpBisector(p, s.line, 0);
      sys_.addConstraintPointOnLine(p, s.line, 0);
    } else if (s.geo->type == GeomType::Arc) {
      sys_.addConstraintPointOnPerpBisector(p, s.arc.start, s.arc.end, 0);
      sys_.addConstraintPointOnArc(p, s.arc, 0);
    }
    return p;
  }

  /// Point-on-curve. For splines `param` (if given) receives the curve parameter unknown.
  bool pointOnCurve(GCS::Point& p, GeoSlot& s, int tag, double** param = nullptr) {
    switch (s.geo->type) {
      case GeomType::Line: sys_.addConstraintPointOnLine(p, s.line, tag); return true;
      case GeomType::Circle: sys_.addConstraintPointOnCircle(p, s.circle, tag); return true;
      case GeomType::Arc: sys_.addConstraintPointOnArc(p, s.arc, tag); return true;
      case GeomType::Ellipse: sys_.addConstraintPointOnEllipse(p, s.ellipse, tag); return true;
      case GeomType::EllipseArc: sys_.addConstraintPointOnEllipse(p, s.arcEllipse, tag); return true;
      case GeomType::Spline:
      case GeomType::FitSpline: {
        if (s.bspline.poles.empty()) return false;
        BSpline2 sp = s.geo->bspline();
        double* u = aux(sp.closestParam({*p.x, *p.y}));
        unknowns_.push_back(u);
        sys_.addConstraintPointOnBSpline(p, s.bspline, u, tag);
        if (param) *param = u;
        return true;
      }
      case GeomType::Point:
      case GeomType::Text: return false;
    }
    return false;
  }

  void buildConstraints() {
    for (const Constraint& c : sketch_.constraints()) {
      if (c.isDimensional() && c.driven) continue;
      int tag = c.id;
      GeoSlot* sa = c.a.isValid() ? slot(c.a.geo) : nullptr;
      GeoSlot* sb = c.b.isValid() ? slot(c.b.geo) : nullptr;
      GeoSlot* sc = c.c.isValid() ? slot(c.c.geo) : nullptr;
      if (!sa) continue;
      switch (c.type) {
        case ConstraintType::Fix: break;  // handled through the unknown list
        case ConstraintType::Coincident: {
          auto pa = point(c.a);
          if (!pa || !sb) break;
          if (c.b.pos == PointPos::None || (c.b.pos == PointPos::Mid && sb->geo->type != GeomType::Point)) {
            if (c.b.pos == PointPos::Mid) {
              // coincident with a midpoint: midpoint equations
              if (sb->geo->type == GeomType::Line) {
                sys_.addConstraintPointOnPerpBisector(*pa, sb->line, tag);
                sys_.addConstraintPointOnLine(*pa, sb->line, tag);
              } else if (sb->geo->type == GeomType::Arc) {
                sys_.addConstraintPointOnPerpBisector(*pa, sb->arc.start, sb->arc.end, tag);
                sys_.addConstraintPointOnArc(*pa, sb->arc, tag);
              }
            } else {
              pointOnCurve(*pa, *sb, tag);
            }
          } else {
            auto pb = point(c.b);
            if (pb) sys_.addConstraintP2PCoincident(*pa, *pb, tag);
          }
          break;
        }
        case ConstraintType::PointOnCurve: {
          auto pa = point(c.a);
          if (pa && sb) pointOnCurve(*pa, *sb, tag);
          break;
        }
        case ConstraintType::Horizontal:
        case ConstraintType::Vertical: {
          bool h = c.type == ConstraintType::Horizontal;
          if (c.a.pos == PointPos::None && sa->geo->type == GeomType::Line) {
            if (h) sys_.addConstraintHorizontal(sa->line, tag);
            else sys_.addConstraintVertical(sa->line, tag);
          } else {
            auto pa = point(c.a);
            auto pb = point(c.b);
            if (pa && pb) {
              if (h) sys_.addConstraintHorizontal(*pa, *pb, tag);
              else sys_.addConstraintVertical(*pa, *pb, tag);
            }
          }
          break;
        }
        case ConstraintType::Parallel:
          if (sb && sa->geo->type == GeomType::Line && sb->geo->type == GeomType::Line)
            sys_.addConstraintParallel(sa->line, sb->line, tag);
          break;
        case ConstraintType::Perpendicular:
          if (sb && sa->geo->type == GeomType::Line && sb->geo->type == GeomType::Line)
            sys_.addConstraintPerpendicular(sa->line, sb->line, tag);
          break;
        case ConstraintType::Tangent: addTangent(c, *sa, sb, tag); break;
        case ConstraintType::Equal: {
          if (!sb) break;
          const Geometry& ga = *sa->geo;
          const Geometry& gb = *sb->geo;
          if (ga.type == GeomType::Line && gb.type == GeomType::Line) sys_.addConstraintEqualLength(sa->line, sb->line, tag);
          else if (ga.type == GeomType::Circle && gb.type == GeomType::Circle) sys_.addConstraintEqualRadius(sa->circle, sb->circle, tag);
          else if (ga.type == GeomType::Circle && gb.type == GeomType::Arc) sys_.addConstraintEqualRadius(sa->circle, sb->arc, tag);
          else if (ga.type == GeomType::Arc && gb.type == GeomType::Circle) sys_.addConstraintEqualRadius(sb->circle, sa->arc, tag);
          else if (ga.type == GeomType::Arc && gb.type == GeomType::Arc) sys_.addConstraintEqualRadius(sa->arc, sb->arc, tag);
          else if (ga.isElliptical() && gb.isElliptical())
            sys_.addConstraintEqualRadii(ellipseOf(*sa), ellipseOf(*sb), tag);
          break;
        }
        case ConstraintType::Concentric: {
          if (!sb) break;
          auto ca = point(GeoRef{c.a.geo, PointPos::Center});
          auto cb = point(GeoRef{c.b.geo, PointPos::Center});
          bool okA = sa->geo->isCircular() || sa->geo->isElliptical();
          bool okB = sb->geo->isCircular() || sb->geo->isElliptical();
          if (!okA || !okB || !ca || !cb) break;
          sys_.addConstraintP2PCoincident(*ca, *cb, tag);
          break;
        }
        case ConstraintType::Collinear:
          if (sb && sa->geo->type == GeomType::Line && sb->geo->type == GeomType::Line) {
            sys_.addConstraintPointOnLine(sb->line.p1, sa->line, tag);
            sys_.addConstraintPointOnLine(sb->line.p2, sa->line, tag);
          }
          break;
        case ConstraintType::Midpoint: {
          auto pa = point(c.a);
          if (!pa || !sb) break;
          if (sb->geo->type == GeomType::Line) {
            sys_.addConstraintPointOnPerpBisector(*pa, sb->line, tag);
            sys_.addConstraintPointOnLine(*pa, sb->line, tag);
          } else if (sb->geo->type == GeomType::Arc) {
            sys_.addConstraintPointOnPerpBisector(*pa, sb->arc.start, sb->arc.end, tag);
            sys_.addConstraintPointOnArc(*pa, sb->arc, tag);
          }
          break;
        }
        case ConstraintType::Symmetric: {
          auto pa = point(c.a);
          auto pb = point(c.b);
          if (!pa || !pb || !sc) break;
          if (c.c.pos == PointPos::None && sc->geo->type == GeomType::Line) {
            sys_.addConstraintP2PSymmetric(*pa, *pb, sc->line, tag);
          } else {
            auto pc = point(c.c);
            if (pc) sys_.addConstraintP2PSymmetric(*pa, *pb, *pc, tag);
          }
          break;
        }
        case ConstraintType::Distance: {
          double* d = value(std::abs(c.value));
          if (c.a.isPoint() && c.b.isPoint()) {
            auto pa = point(c.a), pb = point(c.b);
            if (pa && pb) sys_.addConstraintP2PDistance(*pa, *pb, d, tag);
          } else if (c.a.isPoint() && sb && sb->geo->type == GeomType::Line) {
            auto pa = point(c.a);
            if (pa) {
              bool ccw = signedDistanceToLine(sb->geo->start(), sb->geo->direction(), sketch_.pointOf(c.a).value_or(Pt2{})) >= 0;
              sys_.addConstraintP2LDistance(*pa, sb->line, d, ccw, tag);
            }
          } else if (c.b.isPoint() && sa->geo->type == GeomType::Line && c.a.pos == PointPos::None) {
            auto pb = point(c.b);
            if (pb) {
              bool ccw = signedDistanceToLine(sa->geo->start(), sa->geo->direction(), sketch_.pointOf(c.b).value_or(Pt2{})) >= 0;
              sys_.addConstraintP2LDistance(*pb, sa->line, d, ccw, tag);
            }
          } else if (sa->geo->type == GeomType::Line && !c.b.isValid()) {
            sys_.addConstraintP2PDistance(sa->line.p1, sa->line.p2, d, tag);
          } else if (sb && sa->geo->type == GeomType::Line && sb->geo->type == GeomType::Line) {
            bool ccw = signedDistanceToLine(sa->geo->start(), sa->geo->direction(), sb->geo->start()) >= 0;
            sys_.addConstraintP2LDistance(sb->line.p1, sa->line, d, ccw, tag);
          } else if (sa->geo->isCircular() && c.a.pos == PointPos::None && c.b.isPoint()) {
            auto pb = point(c.b);
            if (pb) {
              if (sa->geo->type == GeomType::Circle) sys_.addConstraintP2CDistance(*pb, sa->circle, d, tag);
              else sys_.addConstraintP2CDistance(*pb, sa->arc, d, tag);
            }
          }
          break;
        }
        case ConstraintType::DistanceX:
        case ConstraintType::DistanceY: {
          bool x = c.type == ConstraintType::DistanceX;
          double* d = value(c.value);
          if (c.a.isPoint() && c.b.isPoint()) {
            auto pa = point(c.a), pb = point(c.b);
            if (pa && pb) sys_.addConstraintDifference(x ? pa->x : pa->y, x ? pb->x : pb->y, d, tag);
          } else if (c.a.isPoint() && !c.b.isValid()) {
            auto pa = point(c.a);
            if (pa) {
              if (x) sys_.addConstraintCoordinateX(*pa, d, tag);
              else sys_.addConstraintCoordinateY(*pa, d, tag);
            }
          } else if (sa->geo->type == GeomType::Line) {
            sys_.addConstraintDifference(x ? sa->line.p1.x : sa->line.p1.y, x ? sa->line.p2.x : sa->line.p2.y, d, tag);
          }
          break;
        }
        case ConstraintType::Radius: {
          double* r = value(std::abs(c.value));
          if (sa->geo->type == GeomType::Circle) sys_.addConstraintCircleRadius(sa->circle, r, tag);
          else if (sa->geo->type == GeomType::Arc) sys_.addConstraintArcRadius(sa->arc, r, tag);
          break;
        }
        case ConstraintType::Diameter: {
          double* dia = value(std::abs(c.value));
          if (sa->geo->type == GeomType::Circle) sys_.addConstraintCircleDiameter(sa->circle, dia, tag);
          else if (sa->geo->type == GeomType::Arc) sys_.addConstraintArcDiameter(sa->arc, dia, tag);
          break;
        }
        case ConstraintType::MinorRadius: {
          if (!sa->geo->isElliptical()) break;
          sys_.addConstraintEqual(ellipseOf(*sa).radmin, value(std::abs(c.value)), tag);
          break;
        }
        case ConstraintType::MajorRadius: {
          if (!sa->geo->isElliptical()) break;
          // an auxiliary point held at the positive end of the major axis (2 equations, 2
          // unknowns: no DOF change), at the dimension's distance from the centre
          GCS::Ellipse& e = ellipseOf(*sa);
          EllipseFrame f = sa->geo->ellipse();
          Pt2 tip = f.center + f.major * f.a;
          double* x = aux(tip.x);
          double* y = aux(tip.y);
          unknowns_.push_back(x);
          unknowns_.push_back(y);
          GCS::Point& p = auxPoints_.emplace_back(x, y);
          sys_.addConstraintInternalAlignmentPoint2Ellipse(e, p, GCS::EllipsePositiveMajorX, 0);
          sys_.addConstraintInternalAlignmentPoint2Ellipse(e, p, GCS::EllipsePositiveMajorY, 0);
          sys_.addConstraintP2PDistance(e.center, p, value(std::abs(c.value)), tag);
          break;
        }
        case ConstraintType::PointCurveDistance: {
          // FreeCAD semantics: point to the circle, the inside/outside side kept by PlaneGCS
          auto pa = point(c.a);
          if (!pa || !sb || !sb->geo->isCircular()) break;
          double* d = value(std::abs(c.value));
          if (sb->geo->type == GeomType::Circle) sys_.addConstraintP2CDistance(*pa, sb->circle, d, tag);
          else sys_.addConstraintP2CDistance(*pa, sb->arc, d, tag);
          break;
        }
        case ConstraintType::CurveCurveDistance: {
          if (!sb || !sa->geo->isCircular() || !sb->geo->isCircular()) break;
          GCS::Circle& ca = sa->geo->type == GeomType::Circle ? static_cast<GCS::Circle&>(sa->circle) : sa->arc;
          GCS::Circle& cb = sb->geo->type == GeomType::Circle ? static_cast<GCS::Circle&>(sb->circle) : sb->arc;
          const double r1 = sa->geo->radius(), r2 = sb->geo->radius();
          const double dc = sa->geo->center().distance(sb->geo->center());
          std::optional<bool> c1Bigger;
          if (!(dc >= r1 && dc >= r2)) c1Bigger = r1 >= r2;  // inside: keep which one is outer
          sys_.addConstraintC2CDistance(ca, cb, value(std::abs(c.value)), c1Bigger, tag);
          break;
        }
        case ConstraintType::ArcLength: {
          if (sa->geo->type != GeomType::Arc) break;
          sys_.addConstraintArcLength(sa->arc, value(std::abs(c.value)), tag);
          break;
        }
        case ConstraintType::Angle: {
          double* ang = value(c.value * kPi / 180.0);
          if (sb && sa->geo->type == GeomType::Line && sb->geo->type == GeomType::Line) {
            sys_.addConstraintL2LAngle(sa->line, sb->line, ang, tag);
          } else if (sa->geo->type == GeomType::Line) {
            sys_.addConstraintP2PAngle(sa->line.p1, sa->line.p2, ang, 0.0, tag);
          } else if (sa->geo->type == GeomType::Arc) {
            // sweep angle of an arc (end - start), created by the arc tools' angle box
            sys_.addConstraintDifference(sa->arc.startAngle, sa->arc.endAngle, ang, tag);
          }
          break;
        }
      }
    }
  }

  GCS::Ellipse& ellipseOf(GeoSlot& s) { return s.geo->type == GeomType::Ellipse ? s.ellipse : s.arcEllipse; }

  /// GCS normal of a curve at a point of it (param version for splines).
  GCS::DeriVector2 normalAt(GeoSlot& s, GCS::Point& p, double* param) {
    GCS::Curve* cv = curve(s);
    if (!cv) return {};
    if (s.geo->isSpline() && param) return cv->CalculateNormal(param);
    return cv->CalculateNormal(p);
  }

  /// Angle-via-point tangency between two curves meeting at p (0 or pi chosen from the current
  /// configuration). Spline normals use their curve parameter.
  void tangentViaPoint(GeoSlot& sa, double* paramA, GeoSlot& sb, double* paramB, GCS::Point& p, int tag) {
    GCS::Curve* ca = curve(sa);
    GCS::Curve* cb = curve(sb);
    if (!ca || !cb) return;
    bool splA = sa.geo->isSpline() && paramA, splB = sb.geo->isSpline() && paramB;
    GCS::DeriVector2 n1 = normalAt(sa, p, paramA);
    GCS::DeriVector2 n2 = normalAt(sb, p, paramB);
    double ang = std::atan2(n1.x * n2.y - n1.y * n2.x, n1.x * n2.x + n1.y * n2.y);
    double* snapped = value(std::abs(ang) < kPi / 2 ? 0.0 : kPi);
    if (splA && splB) sys_.addConstraintAngleViaPointAndTwoParams(*ca, *cb, p, paramA, paramB, snapped, tag);
    else if (splA) sys_.addConstraintAngleViaPointAndParam(*ca, *cb, p, paramA, snapped, tag);
    else if (splB) sys_.addConstraintAngleViaPointAndParam(*cb, *ca, p, paramB, snapped, tag);
    else sys_.addConstraintAngleViaPoint(*ca, *cb, p, snapped, tag);
  }

  /// Parameter of a spline end point (first / last knot) for endpoint tangency.
  double* splineEndParam(GeoSlot& s, PointPos pos) {
    if (!s.geo->isSpline()) return nullptr;
    BSpline2 sp = s.geo->bspline();
    if (pos == PointPos::Start) return value(sp.first());
    if (pos == PointPos::End) return value(sp.last());
    if (pos == PointPos::Vertex) return nullptr;
    return nullptr;
  }

  void addTangent(const Constraint& c, GeoSlot& sa, GeoSlot* sb, int tag) {
    if (!sb) return;
    const Geometry& ga = *sa.geo;
    const Geometry& gb = *sb->geo;
    if (!ga.isCurve() || !gb.isCurve() || ga.type == GeomType::Text || gb.type == GeomType::Text) return;
    const bool general = !(ga.type == GeomType::Line || ga.isCircular()) || !(gb.type == GeomType::Line || gb.isCircular());
    if (general && c.a.isPoint() && c.b.isPoint()) {
      // tangent at a shared end point of an ellipse arc / spline
      auto pa = point(c.a), pb = point(c.b);
      if (!pa || !pb) return;
      sys_.addConstraintP2PCoincident(*pa, *pb, tag);
      double* ua = splineEndParam(sa, c.a.pos);
      double* ub = splineEndParam(*sb, c.b.pos);
      if ((ga.isSpline() && !ua) || (gb.isSpline() && !ub)) return;  // interior vertex: no tangency
      // the point must lie on both curves: use the end point of a non-spline curve
      GCS::Point& p = ga.isSpline() ? *pb : *pa;
      tangentViaPoint(sa, ua, *sb, ub, p, tag);
      return;
    }
    if (general) {
      if (ga.type == GeomType::Line && gb.isElliptical()) {
        sys_.addConstraintTangent(sa.line, ellipseOf(*sb), tag);
        return;
      }
      if (gb.type == GeomType::Line && ga.isElliptical()) {
        sys_.addConstraintTangent(sb->line, ellipseOf(sa), tag);
        return;
      }
      // tangency anywhere along the curves: an auxiliary touching point on both
      Pt2 best = ga.start();
      double bd = 1e300;
      for (const Pt2& q : ga.tessellate(128)) {
        double d = gb.distanceTo(q);
        if (d < bd) {
          bd = d;
          best = (q + gb.closestPoint(q)) * 0.5;
        }
      }
      double* x = aux(best.x);
      double* y = aux(best.y);
      unknowns_.push_back(x);
      unknowns_.push_back(y);
      GCS::Point& p = auxPoints_.emplace_back(x, y);
      double* ua = nullptr;
      double* ub = nullptr;
      if (!pointOnCurve(p, sa, tag, &ua) || !pointOnCurve(p, *sb, tag, &ub)) return;
      tangentViaPoint(sa, ua, *sb, ub, p, tag);
      return;
    }
    if (c.a.isPoint() && c.b.isPoint()) {
      // tangent at a shared endpoint: coincident + angle via point (0 or pi)
      auto pa = point(c.a), pb = point(c.b);
      GCS::Curve* ca = curve(sa);
      GCS::Curve* cb = curve(*sb);
      if (!pa || !pb || !ca || !cb) return;
      sys_.addConstraintP2PCoincident(*pa, *pb, tag);
      GCS::DeriVector2 n1 = ca->CalculateNormal(*pa);
      GCS::DeriVector2 n2 = cb->CalculateNormal(*pa);
      double ang = std::atan2(n1.x * n2.y - n1.y * n2.x, n1.x * n2.x + n1.y * n2.y);
      double snapped = std::abs(ang) < kPi / 2 ? 0.0 : kPi;
      sys_.addConstraintAngleViaPoint(*ca, *cb, *pa, value(snapped), tag);
      return;
    }
    if (ga.type == GeomType::Line && gb.isCircular()) {
      bool ccw = signedDistanceToLine(ga.start(), ga.direction(), gb.center()) >= 0;
      if (gb.type == GeomType::Circle) sys_.addConstraintTangent(sa.line, sb->circle, ccw, tag);
      else sys_.addConstraintTangent(sa.line, sb->arc, ccw, tag);
    } else if (gb.type == GeomType::Line && ga.isCircular()) {
      bool ccw = signedDistanceToLine(gb.start(), gb.direction(), ga.center()) >= 0;
      if (ga.type == GeomType::Circle) sys_.addConstraintTangent(sb->line, sa.circle, ccw, tag);
      else sys_.addConstraintTangent(sb->line, sa.arc, ccw, tag);
    } else if (ga.isCircular() && gb.isCircular()) {
      if (ga.type == GeomType::Circle && gb.type == GeomType::Circle) sys_.addConstraintTangent(sa.circle, sb->circle, tag);
      else if (ga.type == GeomType::Arc && gb.type == GeomType::Arc) sys_.addConstraintTangent(sa.arc, sb->arc, tag);
      else if (ga.type == GeomType::Circle) sys_.addConstraintTangent(sa.circle, sb->arc, tag);
      else sys_.addConstraintTangent(sb->circle, sa.arc, tag);
    }
  }

  /// Tag of the low-priority "keep this ellipse's size and orientation" preferences. Negative tags
  /// are soft in PlaneGCS (solved as an objective, not counted in DOF / conflict diagnosis).
  static constexpr int kKeepShapeTag = -2;

  /// Soft preferences that keep every movable ellipse's major radius, minor radius and axis
  /// direction, so a constraint that moves only the centre (a dimension to it, a coincidence)
  /// translates the ellipse instead of reshaping it, and a radius dimension changes only that
  /// radius. Skipped for the ellipse being dragged (addDrag() already says how it moves).
  /// Returns whether any were added.
  bool addKeepShape(const DragTarget* drag) {
    bool any = false;
    for (auto& [id, s] : slots_) {
      if (!s.geo->isElliptical() || unknownsOf_[id].empty()) continue;
      if (drag && drag->ref.geo == id) continue;
      GCS::Ellipse& e = ellipseOf(s);
      EllipseFrame f = s.geo->ellipse();
      if (f.a < 1e-9 || f.b < 1e-9) continue;
      // the major radius through an auxiliary point held at the major axis end (hard, DOF-neutral)
      Pt2 tip = f.center + f.major * f.a;
      double* x = aux(tip.x);
      double* y = aux(tip.y);
      unknowns_.push_back(x);
      unknowns_.push_back(y);
      GCS::Point& p = auxPoints_.emplace_back(x, y);
      sys_.addConstraintInternalAlignmentPoint2Ellipse(e, p, GCS::EllipsePositiveMajorX, 0);
      sys_.addConstraintInternalAlignmentPoint2Ellipse(e, p, GCS::EllipsePositiveMajorY, 0);
      sys_.addConstraintP2PDistance(e.center, p, value(f.a), kKeepShapeTag);
      sys_.addConstraintEqual(e.radmin, value(s.geo->p[4]), kKeepShapeTag);
      Pt2 cf{s.geo->p[2] - s.geo->p[0], s.geo->p[3] - s.geo->p[1]};
      if (cf.length() > 1e-9) sys_.addConstraintP2PAngle(e.center, e.focus1, value(cf.angle()), 0.0, kKeepShapeTag);
      if (!drag) {
        // and its position (a radius change grows it about the centre). Not while dragging
        // something else: that would hold back geometry tied to the centre.
        sys_.addConstraintCoordinateX(e.center, value(f.center.x), kKeepShapeTag);
        sys_.addConstraintCoordinateY(e.center, value(f.center.y), kKeepShapeTag);
      }
      any = true;
    }
    return any;
  }

  void addDrag(const DragTarget& d) {
    GeoSlot* s = slot(d.ref.geo);
    if (!s) return;
    const int tag = GCS::DefaultTemporaryConstraint;
    auto pull = [&](GCS::Point p, Pt2 target) {
      sys_.addConstraintCoordinateX(p, value(target.x), tag);
      sys_.addConstraintCoordinateY(p, value(target.y), tag);
    };
    if (d.ref.pos != PointPos::None) {
      if (d.ref.pos == PointPos::Mid && s->geo->type != GeomType::Point) {
        // dragging a midpoint translates the entity
        if (s->geo->type == GeomType::Line) {
          pull(s->line.p1, s->geo->start() + d.delta);
          pull(s->line.p2, s->geo->end() + d.delta);
        } else if (s->geo->type == GeomType::Arc) {
          pull(s->arc.center, s->geo->center() + d.delta);
          pull(s->arc.start, s->geo->start() + d.delta);
          pull(s->arc.end, s->geo->end() + d.delta);
        }
        return;
      }
      if (d.ref.pos == PointPos::Center && s->geo->isElliptical()) {
        // the ellipse is stored as centre + focus + minor radius: moving the centre alone would
        // reshape and rotate it. Carry the focus (and the arc ends) along with the centre.
        const Geometry& g = *s->geo;
        Pt2 delta = d.target - Pt2{g.p[0], g.p[1]};
        GCS::Ellipse& e = ellipseOf(*s);
        pull(e.center, d.target);
        pull(e.focus1, Pt2{g.p[2], g.p[3]} + delta);
        if (g.type == GeomType::EllipseArc) {
          pull(s->arcEllipse.start, g.start() + delta);
          pull(s->arcEllipse.end, g.end() + delta);
        }
        return;
      }
      auto p = point(d.ref);
      if (p) pull(*p, d.target);
      return;
    }
    switch (s->geo->type) {
      case GeomType::Point: pull(s->point, s->geo->center() + d.delta); break;
      case GeomType::Line:
        pull(s->line.p1, s->geo->start() + d.delta);
        pull(s->line.p2, s->geo->end() + d.delta);
        break;
      case GeomType::Circle: pull(s->circle.center, s->geo->center() + d.delta); break;
      case GeomType::Arc:
        pull(s->arc.center, s->geo->center() + d.delta);
        pull(s->arc.start, s->geo->start() + d.delta);
        pull(s->arc.end, s->geo->end() + d.delta);
        break;
      case GeomType::Ellipse:
        pull(s->ellipse.center, Pt2{s->geo->p[0], s->geo->p[1]} + d.delta);
        pull(s->ellipse.focus1, Pt2{s->geo->p[2], s->geo->p[3]} + d.delta);
        break;
      case GeomType::EllipseArc:
        pull(s->arcEllipse.center, Pt2{s->geo->p[0], s->geo->p[1]} + d.delta);
        pull(s->arcEllipse.focus1, Pt2{s->geo->p[2], s->geo->p[3]} + d.delta);
        break;
      case GeomType::Spline:
      case GeomType::FitSpline:
        for (size_t k = 0; k < s->verts.size(); ++k) pull(s->verts[k], s->geo->vertex(static_cast<int>(k)) + d.delta);
        break;
      case GeomType::Text: pull(s->point, s->geo->start() + d.delta); break;
    }
  }

  void copyBack() {
    for (Geometry& g : sketch_.geometries()) {
      auto it = slots_.find(g.id);
      if (it == slots_.end()) continue;
      const GeoSlot& s = it->second;
      for (int i = 0; i < s.count; ++i) g.p[static_cast<size_t>(i)] = buf_[static_cast<size_t>(s.offset + i)];
      g.normalize();
      if (g.type == GeomType::Circle) g.p[2] = std::abs(g.p[2]);
    }
  }

  Sketch& sketch_;
  GCS::System sys_;
  std::vector<double> buf_;
  std::deque<double> values_;
  std::deque<double> aux_;
  std::deque<GCS::Point> auxPoints_;
  int next_ = 0;
  std::map<int, GeoSlot> slots_;
  std::map<int, GCS::Point> midPoints_;
  std::map<int, std::vector<double*>> unknownsOf_;
  GCS::VEC_pD unknowns_;
};

}  // namespace

SolveResult SketchSolver::solve(Sketch& sketch, bool /*diagnose*/, const DragTarget* drag) {
  if (sketch.geometries().empty()) {
    SolveResult r;
    r.dofs = 0;
    return r;
  }
  Builder b(sketch);
  try {
    return b.run(drag);
  } catch (const std::exception& e) {
    SolveResult r;
    r.status = SolveResult::Status::Failed;
    r.message = e.what();
    r.dofs = -1;
    FC_WARN("sketch solver exception: {}", e.what());
    return r;
  }
}

}  // namespace fabricad::sketch
