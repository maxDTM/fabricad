// FabriCAD assembly: joint / rigid group / component marker / external design features.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "assembly/JointFeature.h"

#include "assembly/ComponentOps.h"
#include "core/Json.h"
#include "core/Log.h"
#include "core/ShapeUtils.h"

#include <QFileInfo>
#include <QJsonArray>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>

#include <algorithm>
#include <format>
#include <mutex>
#include <numbers>

namespace fabricad::assembly {

using core::Document;
using core::kInvalidId;
using core::ObjectId;
using core::ShapeKind;
using core::Property;
using core::ShapeRef;

// ---------------------------------------------------------------------------
// free functions
// ---------------------------------------------------------------------------

AssemblyInput gatherAssembly(const Document& doc, int beforeIndex, const core::BodyStateMap* state) {
  AssemblyInput in;
  for (const core::Component* c : doc.components()) {
    SolverComponent sc;
    sc.id = c->id;
    sc.parent = c->parent;
    sc.world = doc.worldPlacement(c->id);
    sc.grounded = c->grounded;
    in.components.push_back(sc);
  }
  const core::BodyStateMap& st = state ? *state : doc.bodyStates();
  int limit = beforeIndex < 0 ? doc.rollbackIndex() : std::min(beforeIndex, doc.rollbackIndex());
  for (int i = 0; i < limit && i < doc.featureCount(); ++i) {
    const core::Feature* f = doc.featureAt(i);
    if (!f || f->isSuppressed()) continue;
    if (const auto* jp = dynamic_cast<const IJointProvider*>(f)) jp->appendSolverJoints(doc, st, in.joints, &in.warnings);
  }
  return in;
}

std::vector<ObjectId> applySolverResult(Document& doc, const SolverResult& result) {
  std::vector<ObjectId> changed;
  // parents before children so that local placements are computed against final parent placements
  std::vector<ObjectId> order(result.moved.begin(), result.moved.end());
  std::sort(order.begin(), order.end(), [&](ObjectId a, ObjectId b) {
    bool aAnc = isAncestorOf(doc, a, b), bAnc = isAncestorOf(doc, b, a);
    if (aAnc != bAnc) return aAnc;
    return a < b;
  });
  for (ObjectId id : order) {
    auto it = result.world.find(id);
    if (it == result.world.end()) continue;
    if (setWorldPlacement(doc, id, it->second, 1e-9)) changed.push_back(id);
  }
  return changed;
}

SolverResult solveAssembly(Document& doc, bool apply, const std::vector<SolverJoint>& extraJoints) {
  AssemblyInput in = gatherAssembly(doc, -1, nullptr);
  in.joints.insert(in.joints.end(), extraJoints.begin(), extraJoints.end());
  SolverResult r = AssemblySolver::solve(in.components, in.joints);
  if (apply) applySolverResult(doc, r);
  return r;
}

// ---------------------------------------------------------------------------
// JointFeature
// ---------------------------------------------------------------------------

JointFeature::JointFeature() : Feature(kType) {
  addRef("componentA", core::mask(ShapeKind::Component)).setLabel("Component 1");
  addRef("componentB", core::mask(ShapeKind::Component)).setLabel("Component 2");
  Property& oa = addProperty(Property("originA", core::PropertyType::Json));
  oa.setLabel("Snap 1");
  oa.flags().hidden = true;
  Property& ob = addProperty(Property("originB", core::PropertyType::Json));
  ob.setLabel("Snap 2");
  ob.flags().hidden = true;
  addEnum("type", jointTypeNames(), 0).setLabel("Motion");
  addBool("flip", false).setLabel("Flip");
  addLength("offsetX", 0).setLabel("Offset X");
  addLength("offsetY", 0).setLabel("Offset Y");
  addLength("offsetZ", 0).setLabel("Offset Z");
  addAngle("rotation", 0).setLabel("Rotation");
  addAngle("angle", 0).setLabel("Angle");
  addLength("distance", 0).setLabel("Distance");
  addLength("distance2", 0).setLabel("Distance Y");
  addAngle("angle2", 0).setLabel("Pitch");
  addAngle("angle3", 0).setLabel("Roll");
  addBool("limitsEnabled", false).setLabel("Joint limits");
  addDouble("limitMin", 0).setLabel("Minimum");
  addDouble("limitMax", 0).setLabel("Maximum");
  addDouble("limitRest", 0).setLabel("Rest");
  addBool("lockMotion", false).setLabel("Lock motion");
  Property& ab = addBool("asBuilt", false);
  ab.setLabel("As-built");
  ab.flags().hidden = true;
  updateValueVisibility();
}

std::string JointFeature::iconName() const {
  switch (jointType()) {
    case JointType::Rigid: return "joint-rigid";
    case JointType::Revolute: return "joint-revolute";
    case JointType::Slider: return "joint-slider";
    case JointType::Cylindrical: return "joint-cylindrical";
    case JointType::PinSlot: return "joint-pin-slot";
    case JointType::Planar: return "joint-planar";
    case JointType::Ball: return "joint-ball";
  }
  return "joint";
}

std::vector<ObjectId> JointFeature::extraDependencies() const {
  std::vector<ObjectId> out;
  for (const JointOrigin& o : {originA(), originB()}) {
    if (o.ref.owner == kInvalidId || o.ref.owner == core::kOriginId) continue;
    if (o.ref.kind == ShapeKind::Component) continue;
    out.push_back(o.ref.owner);
  }
  return out;
}

JointType JointFeature::jointType() const {
  int i = enumIndex("type");
  return (i >= 0 && i < kJointTypeCount) ? static_cast<JointType>(i) : JointType::Rigid;
}

void JointFeature::setJointType(JointType t) {
  prop("type").setEnum(static_cast<int>(t));
  updateValueVisibility();
}

void JointFeature::updateValueVisibility() {
  const char* names[] = {"angle", "distance", "distance2", "angle2", "angle3"};
  for (const char* n : names) prop(n).flags().hidden = true;
  switch (jointType()) {
    case JointType::Rigid: break;
    case JointType::Revolute: prop("angle").flags().hidden = false; break;
    case JointType::Slider: prop("distance").flags().hidden = false; break;
    case JointType::Cylindrical:
    case JointType::PinSlot:
      prop("angle").flags().hidden = false;
      prop("distance").flags().hidden = false;
      break;
    case JointType::Planar:
      prop("distance").flags().hidden = false;
      prop("distance2").flags().hidden = false;
      prop("angle").flags().hidden = false;
      break;
    case JointType::Ball:
      prop("angle").flags().hidden = false;
      prop("angle2").flags().hidden = false;
      prop("angle3").flags().hidden = false;
      break;
  }
  bool limits = jointDofCount(jointType()) > 0;
  for (const char* n : {"limitsEnabled", "limitMin", "limitMax", "limitRest", "lockMotion"}) prop(n).flags().hidden = !limits;
}

void JointFeature::setComponents(ObjectId a, ObjectId b) {
  prop("componentA").setRef(ShapeRef::component(a));
  prop("componentB").setRef(ShapeRef::component(b));
}

JointOrigin JointFeature::originA() const { return JointOrigin::fromJson(prop("originA").json()); }
JointOrigin JointFeature::originB() const { return JointOrigin::fromJson(prop("originB").json()); }

void JointFeature::setOrigins(const JointOrigin& a, const JointOrigin& b) {
  prop("originA").setJson(a.toJson());
  prop("originB").setJson(b.toJson());
  if (a.component != kInvalidId && b.component != kInvalidId) setComponents(a.component, b.component);
}

void JointFeature::setFlip(bool f) { prop("flip").setBool(f); }
void JointFeature::setAsBuilt(bool b) { prop("asBuilt").setBool(b); }
void JointFeature::setLockMotion(bool l) { prop("lockMotion").setBool(l); }

JointValues JointFeature::values() const {
  JointValues q{0, 0, 0};
  switch (jointType()) {
    case JointType::Rigid: break;
    case JointType::Revolute: q[0] = angle("angle"); break;
    case JointType::Slider: q[0] = length("distance"); break;
    case JointType::Cylindrical:
    case JointType::PinSlot:
      q[0] = angle("angle");
      q[1] = length("distance");
      break;
    case JointType::Planar:
      q[0] = length("distance");
      q[1] = length("distance2");
      q[2] = angle("angle");
      break;
    case JointType::Ball:
      q[0] = angle("angle");
      q[1] = angle("angle2");
      q[2] = angle("angle3");
      break;
  }
  if (limitsEnabled() && jointDofCount(jointType()) > 0) q[0] = clampToLimits(q[0]);
  return q;
}

void JointFeature::setValues(const JointValues& q) {
  switch (jointType()) {
    case JointType::Rigid: break;
    case JointType::Revolute: prop("angle").setDouble(q[0]); break;
    case JointType::Slider: prop("distance").setDouble(q[0]); break;
    case JointType::Cylindrical:
    case JointType::PinSlot:
      prop("angle").setDouble(q[0]);
      prop("distance").setDouble(q[1]);
      break;
    case JointType::Planar:
      prop("distance").setDouble(q[0]);
      prop("distance2").setDouble(q[1]);
      prop("angle").setDouble(q[2]);
      break;
    case JointType::Ball:
      prop("angle").setDouble(q[0]);
      prop("angle2").setDouble(q[1]);
      prop("angle3").setDouble(q[2]);
      break;
  }
}

void JointFeature::setPrimaryValue(double v) {
  JointValues q = values();
  q[0] = limitsEnabled() ? clampToLimits(v) : v;
  setValues(q);
}

gp_Trsf JointFeature::offsetTransform() const {
  gp_Trsf tr;
  tr.SetTranslation(gp_Vec(length("offsetX"), length("offsetY"), length("offsetZ")));
  gp_Trsf rot;
  double deg = angle("rotation");
  if (deg != 0.0) rot.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), deg * std::numbers::pi / 180.0);
  return tr.Multiplied(rot);
}

void JointFeature::setOffset(double x, double y, double z, double rotationDeg) {
  prop("offsetX").setDouble(x);
  prop("offsetY").setDouble(y);
  prop("offsetZ").setDouble(z);
  prop("rotation").setDouble(rotationDeg);
}

void JointFeature::setLimits(bool enabled, double min, double max, double rest) {
  prop("limitsEnabled").setBool(enabled);
  prop("limitMin").setDouble(min);
  prop("limitMax").setDouble(max);
  prop("limitRest").setDouble(rest);
}

double JointFeature::clampToLimits(double v) const {
  double lo = limitMin(), hi = limitMax();
  if (hi < lo) std::swap(lo, hi);
  return std::clamp(v, lo, hi);
}

SolverJoint JointFeature::toSolverJoint(const Document& doc, const core::BodyStateMap* state,
                                        std::vector<std::string>* warnings) const {
  SolverJoint j;
  j.id = id();
  j.compA = componentA();
  j.compB = componentB();
  j.type = jointType();
  j.flip = flip();
  j.offset = offsetTransform();
  j.q = values();
  j.lockMotion = lockMotion();
  j.enabled = !isSuppressed();
  JointOrigin a = originA(), b = originB();
  if (!isAsBuilt()) {
    std::string w;
    if (!refreshJointOrigin(doc, a, state, &w) && warnings && !w.empty()) warnings->push_back(name() + ": " + w);
    w.clear();
    if (!refreshJointOrigin(doc, b, state, &w) && warnings && !w.empty()) warnings->push_back(name() + ": " + w);
  }
  j.frameA = a.frame;
  j.frameB = b.frame;
  return j;
}

void JointFeature::appendSolverJoints(const Document& doc, const core::BodyStateMap& state, std::vector<SolverJoint>& out,
                                      std::vector<std::string>* warnings) const {
  out.push_back(toSolverJoint(doc, &state, warnings));
}

gp_Trsf JointFeature::worldFrame(const Document& doc) const {
  SolverJoint j = toSolverJoint(doc, nullptr, nullptr);
  return AssemblySolver::jointWorldFrame(doc.worldPlacement(j.compB), j);
}

void JointFeature::recompute(core::RecomputeContext& ctx) {
  Document& doc = ctx.document();
  if (componentA() == kInvalidId || componentB() == kInvalidId) {
    ctx.setError("joint needs two components");
    return;
  }
  if (!doc.component(componentA()) || !doc.component(componentB())) {
    ctx.setError("a jointed component no longer exists");
    return;
  }
  if (componentA() == componentB()) {
    ctx.setError("both snaps are on the same component");
    return;
  }
  int myIndex = doc.indexOf(id());
  AssemblyInput in = gatherAssembly(doc, myIndex, &ctx.state());
  std::vector<std::string> warnings;
  SolverJoint mine = toSolverJoint(doc, &ctx.state(), &warnings);
  in.joints.push_back(mine);
  for (const std::string& w : warnings) ctx.setWarning(w);

  SolverResult res = AssemblySolver::solve(in.components, in.joints);
  applySolverResult(doc, res);
  auto st = res.jointStatus.find(id());
  solveStatus_ = st == res.jointStatus.end() ? JointSolveStatus::Skipped : st->second;
  if (solveStatus_ == JointSolveStatus::Conflict) {
    std::string msg = res.jointMessage.count(id()) ? res.jointMessage.at(id()) : "joint conflict";
    ctx.setError(msg);
  } else if (solveStatus_ == JointSolveStatus::Numeric) {
    ctx.setWarning(std::format("solved numerically (loop), residual {:.3g} mm", res.maxResidual));
  }
  // glyph at the joint frame
  double size = std::clamp(ctx.modelSize() * 0.04, 1.0, 50.0);
  gp_Trsf frame = AssemblySolver::jointWorldFrame(doc.worldPlacement(componentB()), mine);
  setOutputShape(makeGlyph(jointType(), frame, size));
}

TopoDS_Shape JointFeature::makeGlyph(JointType type, const gp_Trsf& frame, double size) {
  std::vector<TopoDS_Shape> parts;
  gp_Pnt o = gp_Pnt(0, 0, 0).Transformed(frame);
  gp_Dir z = gp_Dir(0, 0, 1).Transformed(frame);
  gp_Dir x = gp_Dir(1, 0, 0).Transformed(frame);
  gp_Dir y = gp_Dir(0, 1, 0).Transformed(frame);
  auto seg = [&](const gp_Pnt& a, const gp_Pnt& b) {
    if (a.Distance(b) > 1e-9) parts.push_back(BRepBuilderAPI_MakeEdge(a, b).Edge());
  };
  double r = size * 0.5;
  // common: small frame triad
  seg(o, o.Translated(gp_Vec(x) * r * 0.6));
  seg(o, o.Translated(gp_Vec(y) * r * 0.6));
  switch (type) {
    case JointType::Rigid: {
      // a small square in XY (lock)
      BRepBuilderAPI_MakePolygon poly;
      for (int i = 0; i < 4; ++i) {
        double sx = (i == 0 || i == 3) ? -1 : 1, sy = i < 2 ? -1 : 1;
        poly.Add(o.Translated(gp_Vec(x) * (sx * r * 0.5)).Translated(gp_Vec(y) * (sy * r * 0.5)));
      }
      poly.Close();
      parts.push_back(poly.Wire());
      seg(o.Translated(gp_Vec(z) * (-r * 0.5)), o.Translated(gp_Vec(z) * (r * 0.5)));
      break;
    }
    case JointType::Revolute:
    case JointType::Cylindrical:
    case JointType::PinSlot: {
      parts.push_back(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(o, z, x), r)).Edge());
      seg(o.Translated(gp_Vec(z) * (-r)), o.Translated(gp_Vec(z) * r));
      if (type != JointType::Revolute) seg(o.Translated(gp_Vec(z) * r), o.Translated(gp_Vec(z) * (r * 1.6)));
      if (type == JointType::PinSlot) seg(o.Translated(gp_Vec(x) * (-r * 1.4)), o.Translated(gp_Vec(x) * (r * 1.4)));
      break;
    }
    case JointType::Slider: {
      gp_Pnt a = o.Translated(gp_Vec(z) * (-r * 1.4)), b = o.Translated(gp_Vec(z) * (r * 1.4));
      seg(a, b);
      // arrow heads
      for (const gp_Pnt& tip : {a, b}) {
        double dir = tip.IsEqual(a, 1e-9) ? 1.0 : -1.0;
        gp_Pnt base = tip.Translated(gp_Vec(z) * (dir * r * 0.4));
        seg(tip, base.Translated(gp_Vec(x) * (r * 0.25)));
        seg(tip, base.Translated(gp_Vec(x) * (-r * 0.25)));
      }
      break;
    }
    case JointType::Planar: {
      BRepBuilderAPI_MakePolygon poly;
      for (int i = 0; i < 4; ++i) {
        double sx = (i == 0 || i == 3) ? -1 : 1, sy = i < 2 ? -1 : 1;
        poly.Add(o.Translated(gp_Vec(x) * (sx * r)).Translated(gp_Vec(y) * (sy * r)));
      }
      poly.Close();
      parts.push_back(poly.Wire());
      seg(o, o.Translated(gp_Vec(z) * r));
      break;
    }
    case JointType::Ball: {
      parts.push_back(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(o, z, x), r)).Edge());
      parts.push_back(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(o, x, y), r)).Edge());
      parts.push_back(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(o, y, z), r)).Edge());
      break;
    }
  }
  return core::makeCompound(parts);
}

// ---------------------------------------------------------------------------
// RigidGroupFeature
// ---------------------------------------------------------------------------

RigidGroupFeature::RigidGroupFeature() : Feature(kType) {
  addRefs("components", core::mask(ShapeKind::Component)).setLabel("Components");
}

std::vector<ObjectId> RigidGroupFeature::members() const {
  std::vector<ObjectId> out;
  for (const ShapeRef& r : refs("components")) out.push_back(r.owner);
  return out;
}

void RigidGroupFeature::setMembers(const Document& doc, const std::vector<ObjectId>& ids) {
  std::vector<ShapeRef> rs;
  relative_.clear();
  gp_Trsf w0;
  bool first = true;
  for (ObjectId id : ids) {
    if (!doc.component(id)) continue;
    rs.push_back(ShapeRef::component(id));
    gp_Trsf w = doc.worldPlacement(id);
    if (first) {
      w0 = w;
      first = false;
    }
    relative_.push_back(w0.Inverted().Multiplied(w));
  }
  prop("components").setRefs(rs);
}

bool RigidGroupFeature::involvesComponent(ObjectId c) const {
  for (ObjectId m : members())
    if (m == c) return true;
  return false;
}

void RigidGroupFeature::appendSolverJoints(const Document& doc, const core::BodyStateMap&, std::vector<SolverJoint>& out,
                                           std::vector<std::string>* warnings) const {
  std::vector<ObjectId> ms = members();
  if (ms.size() < 2) return;
  ObjectId anchor = ms.front();
  for (size_t i = 1; i < ms.size(); ++i) {
    SolverJoint j;
    j.id = id();
    j.compA = ms[i];
    j.compB = anchor;
    j.type = JointType::Rigid;
    j.flip = true;
    j.lockMotion = true;
    j.enabled = !isSuppressed();
    if (i >= relative_.size()) {
      if (warnings) warnings->push_back(name() + ": member placement not captured");
      continue;
    }
    // W_i = W_0 * R_i  <=>  W_A * I = W_B * L_B with L_B = R_i
    j.frameB = gp_Ax3().Transformed(relative_[i]);
    j.frameA = gp_Ax3();
    out.push_back(j);
  }
  (void)doc;
}

void RigidGroupFeature::recompute(core::RecomputeContext& ctx) {
  Document& doc = ctx.document();
  std::vector<ObjectId> ms = members();
  if (ms.size() < 2) {
    ctx.setError("a rigid group needs at least two components");
    return;
  }
  for (ObjectId m : ms)
    if (!doc.component(m)) {
      ctx.setError("a grouped component no longer exists");
      return;
    }
  AssemblyInput in = gatherAssembly(doc, doc.indexOf(id()), &ctx.state());
  std::vector<std::string> warnings;
  appendSolverJoints(doc, ctx.state(), in.joints, &warnings);
  for (const auto& w : warnings) ctx.setWarning(w);
  SolverResult res = AssemblySolver::solve(in.components, in.joints);
  applySolverResult(doc, res);
  if (res.jointStatus.count(id()) && res.jointStatus.at(id()) == JointSolveStatus::Conflict)
    ctx.setError("rigid group conflicts with other joints");
}

QJsonObject RigidGroupFeature::toJson() const {
  QJsonObject o = Feature::toJson();
  QJsonArray rel;
  for (const gp_Trsf& t : relative_) rel.append(core::trsfToJson(t));
  o["relative"] = rel;
  return o;
}

void RigidGroupFeature::fromJson(const QJsonObject& o) {
  Feature::fromJson(o);
  relative_.clear();
  for (const auto& v : o["relative"].toArray()) {
    QJsonObject e = v.toObject();
    // current form: a transform per member; early files stored {"id", "t"} objects
    relative_.push_back(core::trsfFromJson(e.contains("id") ? e["t"] : v));
  }
}

// ---------------------------------------------------------------------------
// NewComponentFeature
// ---------------------------------------------------------------------------

NewComponentFeature::NewComponentFeature() : Feature(kType) {
  Property& p = addRef("component", core::mask(ShapeKind::Component));
  p.setLabel("Component");
  p.flags().readOnly = true;
}

void NewComponentFeature::setComponent(ObjectId id) { prop("component").setRef(ShapeRef::component(id)); }

void NewComponentFeature::recompute(core::RecomputeContext& ctx) {
  if (component() == kInvalidId || !ctx.document().component(component())) ctx.setWarning("component was deleted");
}

// ---------------------------------------------------------------------------
// InsertDesignFeature
// ---------------------------------------------------------------------------

namespace {

struct ExternalEntry {
  qint64 mtime = 0;
  std::vector<std::pair<std::string, TopoDS_Shape>> bodies;  ///< name + world shape
  std::vector<std::string> materials;
  std::string error;
};
std::mutex gExternalMutex;
std::map<std::string, ExternalEntry> gExternalCache;

const ExternalEntry& loadExternal(const std::string& path) {
  std::lock_guard<std::mutex> lock(gExternalMutex);
  QFileInfo fi(core::qs(path));
  qint64 mtime = fi.exists() ? fi.lastModified().toMSecsSinceEpoch() : -1;
  auto it = gExternalCache.find(path);
  if (it != gExternalCache.end() && it->second.mtime == mtime) return it->second;
  ExternalEntry e;
  e.mtime = mtime;
  if (!fi.exists()) {
    e.error = "file not found: " + path;
  } else {
    Document ext;
    std::string err;
    if (!ext.load(path, &err)) {
      e.error = "cannot load " + path + ": " + err;
    } else {
      for (const BodyInstance& bi : bodyInstances(ext, {}, true)) {
        const core::Body* b = ext.body(bi.body);
        e.bodies.emplace_back(b ? b->name : "Body", bi.worldShape);
        e.materials.push_back(b ? b->material : "");
      }
      if (e.bodies.empty()) e.error = "external design has no visible bodies";
    }
  }
  return gExternalCache[path] = std::move(e);
}

}  // namespace

InsertDesignFeature::InsertDesignFeature() : Feature(kType) {
  addString("path", "").setLabel("File");
  Property& p = addRef("component", core::mask(ShapeKind::Component));
  p.setLabel("Component");
  p.flags().readOnly = true;
}

void InsertDesignFeature::setComponent(ObjectId id) { prop("component").setRef(ShapeRef::component(id)); }
void InsertDesignFeature::setPath(const std::string& p) { prop("path").setString(p); }
void InsertDesignFeature::clearCache() {
  std::lock_guard<std::mutex> lock(gExternalMutex);
  gExternalCache.clear();
}

void InsertDesignFeature::recompute(core::RecomputeContext& ctx) {
  Document& doc = ctx.document();
  ObjectId comp = component();
  if (comp == kInvalidId || !doc.component(comp)) {
    ctx.setError("target component no longer exists");
    return;
  }
  const std::string& p = path();
  if (p.empty()) {
    ctx.setError("no file");
    return;
  }
  const ExternalEntry& e = loadExternal(p);
  if (!e.error.empty()) {
    ctx.setError(e.error);
    return;
  }
  for (size_t i = 0; i < e.bodies.size(); ++i) {
    ObjectId id = ctx.createBody(e.bodies[i].first, e.bodies[i].second, comp);
    if (core::Body* b = doc.body(id); b && b->material.empty() && !e.materials[i].empty()) b->material = e.materials[i];
  }
}

}  // namespace fabricad::assembly
