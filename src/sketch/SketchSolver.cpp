// FabriCAD sketch: PlaneGCS wrapper (build system, solve, diagnose, copy back).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sketch/SketchSolver.h"

#include "core/Log.h"

#include "GCS.h"  // PlaneGCS (third_party/planegcs)

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>

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
};

class Builder {
 public:
  explicit Builder(Sketch& sk) : sketch_(sk) {}

  SolveResult run(const DragTarget* drag) {
    sys_.debugMode = GCS::NoDebug;
    allocate();
    buildGeometry();
    declareUnknowns();
    buildConstraints();
    if (drag) addDrag(*drag);

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
    switch (st) {
      case GCS::SolveStatus::Success: res.status = SolveResult::Status::Success; break;
      case GCS::SolveStatus::Converged: res.status = SolveResult::Status::Converged; break;
      case GCS::SolveStatus::SuccessfulSolutionInvalid: res.status = SolveResult::Status::Converged; break;
      case GCS::SolveStatus::Failed: res.status = SolveResult::Status::Failed; break;
    }
    if (res.ok()) {
      sys_.applySolution();
      if (collapsed()) {
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
      }
      next_ += s.count;
      slots_[g.id] = s;
    }
    for (auto& [id, s] : slots_)
      if (s.geo->type == GeomType::Arc) sys_.addConstraintArcRules(s.arc, 0);
  }

  void declareUnknowns() {
    // Fix constraints: whole geometry or single point
    std::set<int> fixedGeos;
    std::set<std::pair<int, PointPos>> fixedPoints;
    for (const Constraint& c : sketch_.constraints()) {
      if (c.type != ConstraintType::Fix || !c.a.isValid()) continue;
      if (c.a.pos == PointPos::None) fixedGeos.insert(c.a.geo);
      else fixedPoints.insert({c.a.geo, c.a.pos});
    }
    for (auto& [id, s] : slots_) {
      std::vector<double*> list;
      const Geometry& g = *s.geo;
      if (g.isMovable() && !fixedGeos.count(id)) {
        double* b = &buf_[static_cast<size_t>(s.offset)];
        auto isFixed = [&](PointPos pos) { return fixedPoints.count({id, pos}) > 0; };
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

  GeoSlot* slot(int geoId) {
    auto it = slots_.find(geoId);
    return it == slots_.end() ? nullptr : &it->second;
  }

  GCS::Curve* curve(GeoSlot& s) {
    switch (s.geo->type) {
      case GeomType::Line: return &s.line;
      case GeomType::Circle: return &s.circle;
      case GeomType::Arc: return &s.arc;
      case GeomType::Point: return nullptr;
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

  bool pointOnCurve(GCS::Point& p, GeoSlot& s, int tag) {
    switch (s.geo->type) {
      case GeomType::Line: sys_.addConstraintPointOnLine(p, s.line, tag); return true;
      case GeomType::Circle: sys_.addConstraintPointOnCircle(p, s.circle, tag); return true;
      case GeomType::Arc: sys_.addConstraintPointOnArc(p, s.arc, tag); return true;
      case GeomType::Point: return false;
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
          break;
        }
        case ConstraintType::Concentric: {
          if (!sb || !sa->geo->isCircular() || !sb->geo->isCircular()) break;
          GCS::Point ca = sa->geo->type == GeomType::Circle ? sa->circle.center : sa->arc.center;
          GCS::Point cb = sb->geo->type == GeomType::Circle ? sb->circle.center : sb->arc.center;
          sys_.addConstraintP2PCoincident(ca, cb, tag);
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

  void addTangent(const Constraint& c, GeoSlot& sa, GeoSlot* sb, int tag) {
    if (!sb) return;
    const Geometry& ga = *sa.geo;
    const Geometry& gb = *sb->geo;
    if (!ga.isCurve() || !gb.isCurve()) return;
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
    }
  }

  void copyBack() {
    for (Geometry& g : sketch_.geometries()) {
      auto it = slots_.find(g.id);
      if (it == slots_.end()) continue;
      const GeoSlot& s = it->second;
      for (int i = 0; i < s.count; ++i) g.p[static_cast<size_t>(i)] = buf_[static_cast<size_t>(s.offset + i)];
      if (g.type == GeomType::Arc) g.normalize();
      if (g.type == GeomType::Circle) g.p[2] = std::abs(g.p[2]);
    }
  }

  Sketch& sketch_;
  GCS::System sys_;
  std::vector<double> buf_;
  std::deque<double> values_;
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
