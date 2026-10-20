// FabriCAD assembly: joint solver implementation (tree propagation + LM fallback).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "assembly/AssemblySolver.h"

#include "core/Log.h"

#include <Eigen/Dense>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Mat.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <deque>
#include <numbers>

namespace fabricad::assembly {

using core::kInvalidId;
using core::ObjectId;

namespace {

gp_XYZ axisOf(const gp_Trsf& t, int axis) {
  gp_XYZ v(axis == 0, axis == 1, axis == 2);
  return t.VectorialPart() * v;
}

void push3(std::vector<double>& out, const gp_XYZ& v) {
  out.push_back(v.X());
  out.push_back(v.Y());
  out.push_back(v.Z());
}

/// Rigid residual between two frames: origin difference + Z difference + X difference.
void rigidResidual(std::vector<double>& out, const gp_Trsf& fa, const gp_Trsf& fb) {
  push3(out, fa.TranslationPart() - fb.TranslationPart());
  push3(out, axisOf(fa, 2) - axisOf(fb, 2));
  push3(out, axisOf(fa, 0) - axisOf(fb, 0));
}

/// Apply the 6-vector (tx,ty,tz, wx,wy,wz) as an incremental rigid motion to W, rotating about
/// W's current origin so translation and rotation stay decoupled.
gp_Trsf applyDelta(const gp_Trsf& w, const double* d) {
  gp_Trsf out = w;
  gp_XYZ omega(d[3], d[4], d[5]);
  double angle = omega.Modulus();
  if (angle > 1e-15) {
    gp_Trsf rot;
    rot.SetRotation(gp_Ax1(gp_Pnt(w.TranslationPart()), gp_Dir(omega)), angle);
    out = rot.Multiplied(out);
  }
  gp_Trsf tr;
  tr.SetTranslation(gp_Vec(d[0], d[1], d[2]));
  return tr.Multiplied(out);
}

struct Group {
  std::set<ObjectId> components;
  std::vector<size_t> joints;  ///< indices into the joint list
  bool conflict = false;
};

}  // namespace

const char* jointSolveStatusName(JointSolveStatus s) {
  switch (s) {
    case JointSolveStatus::Ok: return "Ok";
    case JointSolveStatus::Numeric: return "Numeric";
    case JointSolveStatus::Conflict: return "Conflict";
    case JointSolveStatus::Skipped: return "Skipped";
  }
  return "?";
}

bool SolverResult::ok() const {
  for (const auto& [id, st] : jointStatus)
    if (st == JointSolveStatus::Conflict) return false;
  return true;
}

// ---------------------------------------------------------------------------
// building blocks
// ---------------------------------------------------------------------------

gp_Trsf AssemblySolver::frameTransform(const gp_Ax3& frame) {
  gp_Trsf t;
  t.SetTransformation(frame);  // world -> frame
  return t.Inverted();         // frame -> world (here: frame -> component)
}

gp_Trsf AssemblySolver::alignTransform(bool flip) {
  gp_Trsf t;
  if (!flip) t.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0)), std::numbers::pi);
  return t;
}

gp_Trsf AssemblySolver::jointWorldFrame(const gp_Trsf& worldB, const SolverJoint& j) {
  return worldB.Multiplied(frameTransform(j.frameB)).Multiplied(j.offset);
}

gp_Trsf AssemblySolver::placeA(const gp_Trsf& worldB, const SolverJoint& j) {
  // W_A = W_B * L_B * Offset * M(q) * Align * L_A^-1
  return jointWorldFrame(worldB, j)
      .Multiplied(jointMotion(j.type, j.q))
      .Multiplied(alignTransform(j.flip))
      .Multiplied(frameTransform(j.frameA).Inverted());
}

gp_Trsf AssemblySolver::placeB(const gp_Trsf& worldA, const SolverJoint& j) {
  // W_B = W_A * L_A * Align^-1 * M(q)^-1 * Offset^-1 * L_B^-1
  return worldA.Multiplied(frameTransform(j.frameA))
      .Multiplied(alignTransform(j.flip).Inverted())
      .Multiplied(jointMotion(j.type, j.q).Inverted())
      .Multiplied(j.offset.Inverted())
      .Multiplied(frameTransform(j.frameB).Inverted());
}

JointValues AssemblySolver::currentValues(const SolverJoint& j, const gp_Trsf& worldA, const gp_Trsf& worldB) {
  // M(q) = (W_B L_B Offset)^-1 * W_A * L_A * Align^-1
  gp_Trsf motion = jointWorldFrame(worldB, j).Inverted()
                       .Multiplied(worldA.Multiplied(frameTransform(j.frameA)))
                       .Multiplied(alignTransform(j.flip).Inverted());
  return jointValuesFromMotion(j.type, motion);
}

void AssemblySolver::asBuiltFrames(SolverJoint& j, const gp_Trsf& worldA, const gp_Trsf& worldB,
                                   const gp_Ax3& worldFrame) {
  gp_Trsf F = frameTransform(worldFrame);  // frame -> world
  // L_B = W_B^-1 * F  (offset identity, q = 0)
  gp_Trsf lb = worldB.Inverted().Multiplied(F);
  // L_A = W_A^-1 * F * Align
  gp_Trsf la = worldA.Inverted().Multiplied(F).Multiplied(alignTransform(j.flip));
  j.frameB = gp_Ax3().Transformed(lb);
  j.frameA = gp_Ax3().Transformed(la);
  j.offset = gp_Trsf();
  j.q = {0, 0, 0};
}

std::vector<double> AssemblySolver::residual(const SolverJoint& j, const gp_Trsf& worldA, const gp_Trsf& worldB) {
  std::vector<double> r;
  if (!j.enabled) return r;
  gp_Trsf fa = worldA.Multiplied(frameTransform(j.frameA)).Multiplied(alignTransform(j.flip).Inverted());
  gp_Trsf fb = jointWorldFrame(worldB, j);
  if (j.lockMotion || j.type == JointType::Rigid) {
    rigidResidual(r, fa, fb.Multiplied(jointMotion(j.type, j.q)));
    return r;
  }
  gp_XYZ oa = fa.TranslationPart(), ob = fb.TranslationPart();
  gp_XYZ za = axisOf(fa, 2), zb = axisOf(fb, 2);
  gp_XYZ xa = axisOf(fa, 0), xb = axisOf(fb, 0), yb = axisOf(fb, 1);
  gp_XYZ d = oa - ob;
  switch (j.type) {
    case JointType::Rigid: break;  // handled above
    case JointType::Revolute:
      push3(r, d);
      push3(r, za - zb);
      break;
    case JointType::Slider:
      push3(r, za - zb);
      push3(r, xa - xb);
      r.push_back(d.Dot(xb));
      r.push_back(d.Dot(yb));
      break;
    case JointType::Cylindrical:
      push3(r, za - zb);
      r.push_back(d.Dot(xb));
      r.push_back(d.Dot(yb));
      break;
    case JointType::PinSlot:
      push3(r, za - zb);
      r.push_back(d.Dot(yb));
      r.push_back(d.Dot(zb));
      break;
    case JointType::Planar:
      push3(r, za - zb);
      r.push_back(d.Dot(zb));
      break;
    case JointType::Ball: push3(r, d); break;
  }
  return r;
}

double AssemblySolver::residualNorm(const SolverJoint& j, const gp_Trsf& worldA, const gp_Trsf& worldB) {
  double m = 0;
  for (double v : residual(j, worldA, worldB)) m = std::max(m, std::abs(v));
  return m;
}

bool AssemblySolver::placementsEqual(const gp_Trsf& a, const gp_Trsf& b, double positionTol, double directionTol) {
  if ((a.TranslationPart() - b.TranslationPart()).Modulus() > positionTol) return false;
  for (int axis = 0; axis < 3; ++axis)
    if ((axisOf(a, axis) - axisOf(b, axis)).Modulus() > directionTol) return false;
  return true;
}

// ---------------------------------------------------------------------------
// solve
// ---------------------------------------------------------------------------

SolverResult AssemblySolver::solve(const std::vector<SolverComponent>& components, const std::vector<SolverJoint>& joints,
                                   const SolverOptions& opt) {
  SolverResult res;
  std::map<ObjectId, const SolverComponent*> comps;
  for (const auto& c : components) {
    comps[c.id] = &c;
    res.world[c.id] = c.world;
  }

  std::set<ObjectId> placed;  // components whose world placement is final for this pass
  for (const auto& c : components)
    if (c.grounded || c.parent == kInvalidId) placed.insert(c.id);

  // joints usable in the solve
  std::vector<bool> usable(joints.size(), false);
  for (size_t i = 0; i < joints.size(); ++i) {
    const SolverJoint& j = joints[i];
    if (!j.enabled || !comps.count(j.compA) || !comps.count(j.compB) || j.compA == j.compB) {
      res.jointStatus[j.id] = JointSolveStatus::Skipped;
      res.jointMessage[j.id] = !j.enabled ? "suppressed" : "component missing";
      continue;
    }
    usable[i] = true;
    res.jointStatus[j.id] = JointSolveStatus::Ok;
  }

  // connected groups (union of joints)
  std::map<ObjectId, ObjectId> uf;
  auto find = [&](ObjectId x) {
    if (!uf.count(x)) uf[x] = x;
    while (uf[x] != x) {
      uf[x] = uf[uf[x]];
      x = uf[x];
    }
    return x;
  };
  auto unite = [&](ObjectId a, ObjectId b) {
    a = find(a);
    b = find(b);
    if (a != b) uf[a] = b;
  };
  for (size_t i = 0; i < joints.size(); ++i)
    if (usable[i]) unite(joints[i].compA, joints[i].compB);
  std::map<ObjectId, Group> groups;
  for (size_t i = 0; i < joints.size(); ++i) {
    if (!usable[i]) continue;
    Group& g = groups[find(joints[i].compA)];
    g.components.insert(joints[i].compA);
    g.components.insert(joints[i].compB);
    g.joints.push_back(i);
  }

  // --- phase 1: tree propagation ------------------------------------------------
  for (auto& [root, g] : groups) {
    bool hasGround = false;
    for (ObjectId c : g.components)
      if (placed.count(c)) hasGround = true;
    if (!hasGround) {
      // floating group: the anchor side of its first joint stays where it is
      placed.insert(joints[g.joints.front()].compB);
    }
    std::vector<bool> done(g.joints.size(), false);
    bool progress = true;
    while (progress) {
      progress = false;
      for (size_t k = 0; k < g.joints.size(); ++k) {
        if (done[k]) continue;
        const SolverJoint& j = joints[g.joints[k]];
        bool aPlaced = placed.count(j.compA) > 0, bPlaced = placed.count(j.compB) > 0;
        if (aPlaced && bPlaced) {
          // loop closure: check consistency
          gp_Trsf predicted = placeA(res.world[j.compB], j);
          if (!placementsEqual(predicted, res.world[j.compA], opt.positionTol, opt.directionTol)) {
            // the joint may still be satisfied along its free DOF: check the residual
            if (residualNorm(j, res.world[j.compA], res.world[j.compB]) > opt.positionTol) g.conflict = true;
          }
          done[k] = true;
          progress = true;
        } else if (bPlaced) {
          res.world[j.compA] = placeA(res.world[j.compB], j);
          placed.insert(j.compA);
          res.moved.insert(j.compA);
          done[k] = true;
          progress = true;
        } else if (aPlaced) {
          res.world[j.compB] = placeB(res.world[j.compA], j);
          placed.insert(j.compB);
          res.moved.insert(j.compB);
          done[k] = true;
          progress = true;
        }
      }
      if (!progress) {
        // disconnected remainder inside the group cannot happen (union-find), but guard anyway
        for (size_t k = 0; k < g.joints.size(); ++k)
          if (!done[k]) {
            placed.insert(joints[g.joints[k]].compB);
            progress = true;
            break;
          }
      }
      if (std::all_of(done.begin(), done.end(), [](bool b) { return b; })) break;
    }
  }

  // --- phase 2: numeric fallback for inconsistent loops ----------------------------
  for (auto& [root, g] : groups) {
    if (!g.conflict) continue;
    if (!opt.allowNumeric) {
      for (size_t k : g.joints) {
        res.jointStatus[joints[k].id] = JointSolveStatus::Conflict;
        res.jointMessage[joints[k].id] = "closed loop is inconsistent";
      }
      continue;
    }
    res.usedNumeric = true;
    // unknowns: every member that is not grounded (root/grounded flag)
    std::vector<ObjectId> free;
    for (ObjectId c : g.components) {
      const SolverComponent* sc = comps[c];
      if (!(sc->grounded || sc->parent == kInvalidId)) free.push_back(c);
    }
    if (free.empty()) {
      for (size_t k : g.joints) {
        res.jointStatus[joints[k].id] = JointSolveStatus::Conflict;
        res.jointMessage[joints[k].id] = "all components grounded";
      }
      continue;
    }
    std::map<ObjectId, int> index;
    for (size_t i = 0; i < free.size(); ++i) index[free[i]] = static_cast<int>(i);
    const int n = static_cast<int>(free.size()) * 6;

    auto evaluate = [&](const std::map<ObjectId, gp_Trsf>& world, Eigen::VectorXd& r) {
      std::vector<double> all;
      for (size_t k : g.joints) {
        const SolverJoint& j = joints[k];
        std::vector<double> rj = residual(j, world.at(j.compA), world.at(j.compB));
        all.insert(all.end(), rj.begin(), rj.end());
      }
      r = Eigen::Map<Eigen::VectorXd>(all.data(), static_cast<Eigen::Index>(all.size()));
    };

    struct Attempt {
      std::map<ObjectId, gp_Trsf> world;
      Eigen::VectorXd r;
      int iterations = 0;
      int rank = 0;
    };
    auto runNumeric = [&](std::map<ObjectId, gp_Trsf> world) {
      Attempt a;
      evaluate(world, a.r);
      double lambda = 1e-3;
      int iter = 0;
      for (; iter < opt.maxIterations; ++iter) {
        // polish well below the acceptance tolerance: LM converges quadratically near the solution
        if (a.r.lpNorm<Eigen::Infinity>() < opt.positionTol * 1e-3) break;
        // numeric Jacobian (central differences)
        Eigen::MatrixXd J(a.r.size(), n);
        for (int col = 0; col < n; ++col) {
          double h = (col % 6 < 3) ? 1e-4 : 1e-6;
          std::map<ObjectId, gp_Trsf> wp = world, wm = world;
          ObjectId cid = free[static_cast<size_t>(col / 6)];
          double dp[6] = {0, 0, 0, 0, 0, 0}, dm[6] = {0, 0, 0, 0, 0, 0};
          dp[col % 6] = h;
          dm[col % 6] = -h;
          wp[cid] = applyDelta(world[cid], dp);
          wm[cid] = applyDelta(world[cid], dm);
          Eigen::VectorXd rp, rm;
          evaluate(wp, rp);
          evaluate(wm, rm);
          J.col(col) = (rp - rm) / (2 * h);
        }
        Eigen::MatrixXd JtJ = J.transpose() * J;
        Eigen::VectorXd grad = J.transpose() * a.r;
        {
          // rank of the latest Jacobian: the final one describes the converged configuration
          // (the start configuration of a loop is often singular, e.g. collinear links)
          Eigen::ColPivHouseholderQR<Eigen::MatrixXd> qr(J);
          qr.setThreshold(1e-8);
          a.rank = static_cast<int>(qr.rank());
        }
        // LM step with simple damping adaptation
        bool accepted = false;
        for (int attempt = 0; attempt < 8 && !accepted; ++attempt) {
          Eigen::MatrixXd A = JtJ + lambda * Eigen::MatrixXd::Identity(n, n);
          Eigen::VectorXd step = A.ldlt().solve(-grad);
          std::map<ObjectId, gp_Trsf> trial = world;
          for (size_t i = 0; i < free.size(); ++i) trial[free[i]] = applyDelta(world[free[i]], step.data() + 6 * i);
          Eigen::VectorXd rt;
          evaluate(trial, rt);
          if (rt.squaredNorm() < a.r.squaredNorm()) {
            world = trial;
            a.r = rt;
            lambda = std::max(lambda * 0.3, 1e-9);
            accepted = true;
          } else {
            lambda *= 10.0;
          }
        }
        if (!accepted) break;
      }
      a.iterations = iter;
      a.world = std::move(world);
      return a;
    };
    auto converged = [&](const Attempt& a) { return a.r.size() == 0 || a.r.lpNorm<Eigen::Infinity>() < opt.positionTol * 10; };

    // Attempt 1: the caller's current placements (continuity while dragging/driving).
    std::map<ObjectId, gp_Trsf> start = res.world;
    for (ObjectId c : free) start[c] = comps[c]->world;
    Attempt best = runNumeric(start);
    int iter = best.iterations;
    if (!converged(best)) {
      // Attempt 2: the tree placements.
      Attempt a2 = runNumeric(res.world);
      iter += a2.iterations;
      if (a2.r.squaredNorm() < best.r.squaredNorm()) best = a2;
    }
    if (!converged(best)) {
      // Attempt 3: tree placements nudged off a singular (collinear) configuration.
      std::map<ObjectId, gp_Trsf> nudged = res.world;
      int k = 0;
      for (ObjectId c : free) {
        double d[6] = {0, 0, 0, 0.01 * ((k % 3) == 0), 0.01 * ((k % 3) == 1), 0.01 * ((k % 3) == 2)};
        d[3 + (k % 3)] = 0.02 + 0.005 * k;
        nudged[c] = applyDelta(nudged[c], d);
        ++k;
      }
      Attempt a3 = runNumeric(nudged);
      iter += a3.iterations;
      if (a3.r.squaredNorm() < best.r.squaredNorm()) best = a3;
    }
    std::map<ObjectId, gp_Trsf>& world = best.world;
    Eigen::VectorXd& r = best.r;
    int rank = best.rank;
    res.numericIterations += iter;
    double maxRes = r.size() ? r.lpNorm<Eigen::Infinity>() : 0.0;
    res.maxResidual = std::max(res.maxResidual, maxRes);
    res.freeDof += std::max(0, n - rank);
    bool solved = maxRes < opt.positionTol * 10;
    if (solved) {
      for (ObjectId c : free) {
        res.world[c] = world[c];
        res.moved.insert(c);
      }
    }
    for (size_t k : g.joints) {
      const SolverJoint& j = joints[k];
      if (solved) {
        res.jointStatus[j.id] = JointSolveStatus::Numeric;
      } else {
        double rn = residualNorm(j, res.world[j.compA], res.world[j.compB]);
        res.jointStatus[j.id] = rn > opt.positionTol * 10 ? JointSolveStatus::Conflict : JointSolveStatus::Ok;
        if (rn > opt.positionTol * 10) res.jointMessage[j.id] = "closed loop could not be solved (" + std::to_string(rn) + ")";
      }
    }
  }

  // motion values as realised
  for (size_t i = 0; i < joints.size(); ++i) {
    if (!usable[i]) continue;
    const SolverJoint& j = joints[i];
    res.jointValues[j.id] = currentValues(j, res.world[j.compA], res.world[j.compB]);
  }
  return res;
}

}  // namespace fabricad::assembly
