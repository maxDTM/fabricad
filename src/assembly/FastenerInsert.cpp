// SPDX-License-Identifier: LGPL-2.1-or-later
#include "assembly/FastenerInsert.h"

#include "assembly/AssemblySolver.h"
#include "assembly/ComponentOps.h"
#include "assembly/JointFeature.h"
#include "core/Commands.h"
#include "core/GeometryRefs.h"
#include "core/Materials.h"
#include "core/ShapeUtils.h"
#include "part/features/StandardPart.h"
#include "part/features/Thread.h"
#include "part/standards/Clearance.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <gp_Ax3.hxx>
#include <gp_Lin.hxx>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>

namespace fabricad::assembly {

using core::ObjectId;
namespace st = part::standards;

namespace {

struct Segment {
  double radius, t0, t1;
};

bool isAir(BRepClass3d_SolidClassifier& cls, const gp_Pnt& p) {
  cls.Perform(p, 1e-6);
  return cls.State() == TopAbs_OUT;
}

/// Internal full cylindrical faces of a body (the walls of its holes).
std::vector<part::ThreadCylinder> holeWalls(const TopoDS_Shape& body) {
  std::vector<part::ThreadCylinder> out;
  for (TopExp_Explorer ex(body, TopAbs_FACE); ex.More(); ex.Next()) {
    BRepAdaptor_Surface a(TopoDS::Face(ex.Current()));
    if (a.GetType() != GeomAbs_Cylinder) continue;
    auto c = part::analyseThreadCylinder(body, ex.Current());
    if (c && c->internal) out.push_back(*c);
  }
  return out;
}

/// Hole analysis in the body's local frame from one wall of the hole (`walls`: holeWalls(body)).
std::optional<HoleSite> analyseLocal(BRepClass3d_SolidClassifier& body, const std::vector<part::ThreadCylinder>& walls,
                                     const std::optional<part::ThreadCylinder>& c0, const std::optional<gp_Pnt>& pick,
                                     std::string* error) {
  auto fail = [&](const char* m) -> std::optional<HoleSite> {
    if (error) *error = m;
    return std::nullopt;
  };
  if (!c0 || !c0->internal) return fail("pick a hole: a circular edge or the cylindrical wall of a hole");
  const gp_Lin axis(c0->axis);
  const gp_Pnt O = c0->axis.Location();
  const gp_Vec D(c0->axis.Direction());
  std::vector<Segment> segs;
  for (const part::ThreadCylinder& cw : walls) {
    const part::ThreadCylinder* c = &cw;
    if (!c->axis.Direction().IsParallel(c0->axis.Direction(), 1e-6)) continue;
    if (axis.Distance(c->axis.Location()) > 1e-5) continue;
    const gp_Vec cd(c->axis.Direction());
    const double base = gp_Vec(O, c->axis.Location()).Dot(D);
    const double sgn = cd.Dot(D) > 0 ? 1.0 : -1.0;
    double a = base + sgn * c->v0, b = base + sgn * c->v1;
    if (a > b) std::swap(a, b);
    segs.push_back({c->radius, a, b});
  }
  // keep the contiguous stack containing the picked face
  const double pa = std::min(c0->v0, c0->v1), pb = std::max(c0->v0, c0->v1);
  std::sort(segs.begin(), segs.end(), [](const Segment& x, const Segment& y) { return x.t0 < y.t0; });
  double lo = pa, hi = pb;
  for (bool grew = true; grew;) {
    grew = false;
    for (const Segment& s : segs) {
      if (s.t1 < lo - 1e-6 || s.t0 > hi + 1e-6) continue;
      if (s.t0 < lo - 1e-9) lo = s.t0, grew = true;
      if (s.t1 > hi + 1e-9) hi = s.t1, grew = true;
    }
  }
  std::vector<Segment> stack;
  for (const Segment& s : segs)
    if (s.t1 >= lo - 1e-6 && s.t0 <= hi + 1e-6) stack.push_back(s);
  double rMin = 1e100, rMax = 0;
  for (const Segment& s : stack) rMin = std::min(rMin, s.radius), rMax = std::max(rMax, s.radius);
  const double probe = 2 * rMax + 0.5;
  const bool open0 = isAir(body, O.Translated(D * (lo - probe))), open1 = isAir(body, O.Translated(D * (hi + probe)));
  if (!open0 && !open1) return fail("the hole is closed at both ends");
  // entry: the end of the widest segment when the hole is stepped, else the open end (nearer the pick)
  bool entryAtHi = open1 && !open0;
  if (open0 && open1) {
    if (rMax - rMin > 1e-6) {
      for (const Segment& s : stack)
        if (std::abs(s.radius - rMax) < 1e-6) entryAtHi = std::abs(s.t1 - hi) < std::abs(s.t0 - lo);
    } else if (pick) {
      const double tp = gp_Vec(O, *pick).Dot(D);
      entryAtHi = std::abs(tp - hi) < std::abs(tp - lo);
    } else {
      entryAtHi = true;
    }
  }
  HoleSite h;
  h.diameter = 2 * rMin;
  h.length = hi - lo;
  h.through = open0 && open1;
  h.axisIn = gp_Dir(entryAtHi ? -D : D);
  h.entry = O.Translated(D * (entryAtHi ? hi : lo));
  h.axisPoint = h.entry;
  double seatDepth = 0;
  if (rMax - rMin > 1e-6) {
    // counterbore bottom: inner end of the widest segment
    for (const Segment& s : stack)
      if (std::abs(s.radius - rMax) < 1e-6) seatDepth = entryAtHi ? hi - s.t0 : s.t1 - lo;
  }
  h.seat = h.entry.Translated(gp_Vec(h.axisIn) * seatDepth);
  h.seatToFar = h.length - seatDepth;
  return h;
}

HoleSite toWorld(HoleSite h, const gp_Trsf& w) {
  h.axisPoint.Transform(w);
  h.entry.Transform(w);
  h.seat.Transform(w);
  h.axisIn.Transform(w);
  return h;
}

gp_Trsf frameTrsf(const gp_Pnt& origin, const gp_Dir& z) {
  gp_Dir x = std::abs(z.Z()) < 0.9 ? gp_Dir(gp_Vec(z).Crossed(gp_Vec(0, 0, 1))) : gp_Dir(gp_Vec(z).Crossed(gp_Vec(1, 0, 0)));
  gp_Trsf t;
  t.SetDisplacement(gp_Ax3(), gp_Ax3(origin, z, x));
  return t;
}

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

}  // namespace

std::optional<HoleSite> analyseHole(const core::Document& doc, const core::ShapeRef& ref, std::string* error) {
  if (!ref.isBrep() || !doc.body(ref.owner)) {
    if (error) *error = "pick a hole on a body";
    return std::nullopt;
  }
  const TopoDS_Shape& body = doc.bodyShape(ref.owner);
  TopoDS_Shape sub = core::resolveShape(doc, ref);
  if (sub.IsNull() || body.IsNull()) {
    if (error) *error = "the picked hole no longer exists";
    return std::nullopt;
  }
  TopoDS_Shape face;
  if (sub.ShapeType() == TopAbs_FACE) face = sub;
  else if (sub.ShapeType() == TopAbs_EDGE) {
    BRepAdaptor_Curve c(TopoDS::Edge(sub));
    if (c.GetType() != GeomAbs_Circle) {
      if (error) *error = "pick a circular edge of a hole";
      return std::nullopt;
    }
    TopTools_IndexedDataMapOfShapeListOfShape map;
    TopExp::MapShapesAndAncestors(body, TopAbs_EDGE, TopAbs_FACE, map);
    if (map.Contains(sub))
      for (const TopoDS_Shape& f : map.FindFromKey(sub)) {
        auto cyl = part::analyseThreadCylinder(body, f);
        if (cyl && cyl->internal && std::abs(cyl->radius - c.Circle().Radius()) < 1e-6) {
          face = f;
          break;
        }
      }
  }
  if (face.IsNull()) {
    if (error) *error = "pick a circular edge or the cylindrical wall of a hole";
    return std::nullopt;
  }
  std::optional<gp_Pnt> pick;
  if (ref.hasPoint) pick = ref.point.toPnt();
  BRepClass3d_SolidClassifier cls(body);
  auto h = analyseLocal(cls, holeWalls(body), part::analyseThreadCylinder(body, face), pick, error);
  if (!h) return std::nullopt;
  const core::Body* b = doc.body(ref.owner);
  h->body = ref.owner;
  h->component = b->component;
  return toWorld(*h, doc.worldPlacement(b->component));
}

std::vector<HoleSite> equalHoles(const core::Document& doc, const HoleSite& site) {
  std::vector<HoleSite> out;
  const core::Body* b = doc.body(site.body);
  if (!b) return out;
  const TopoDS_Shape& body = doc.bodyShape(site.body);
  const gp_Trsf w = doc.worldPlacement(b->component);
  std::vector<gp_Lin> seen;
  const std::vector<part::ThreadCylinder> walls = holeWalls(body);
  BRepClass3d_SolidClassifier cls(body);
  for (const part::ThreadCylinder& c : walls) {
    if (std::abs(2 * c.radius - site.diameter) > 1e-6) continue;
    const gp_Lin l(c.axis);
    if (std::any_of(seen.begin(), seen.end(), [&](const gp_Lin& s) { return s.Distance(l.Location()) < 1e-5 && s.Direction().IsParallel(l.Direction(), 1e-6); }))
      continue;
    seen.push_back(l);
    auto h = analyseLocal(cls, walls, c, std::nullopt, nullptr);
    if (!h) continue;
    h->body = site.body;
    h->component = b->component;
    HoleSite hw = toWorld(*h, w);
    if (std::abs(hw.length - site.length) > 1e-4 || std::abs(hw.seatToFar - site.seatToFar) > 1e-4) continue;
    if (!hw.axisIn.IsEqual(site.axisIn, 1e-6)) continue;
    out.push_back(hw);
  }
  // deterministic order: by seat coordinates
  std::sort(out.begin(), out.end(), [](const HoleSite& a, const HoleSite& b) {
    if (std::abs(a.seat.X() - b.seat.X()) > 1e-6) return a.seat.X() < b.seat.X();
    if (std::abs(a.seat.Y() - b.seat.Y()) > 1e-6) return a.seat.Y() < b.seat.Y();
    return a.seat.Z() < b.seat.Z();
  });
  return out;
}

st::FastenerSpec specForHole(const HoleSite& site, const InsertOptions& opt, std::string* error) {
  st::FastenerSpec s;
  s.family = opt.family;
  s.grade = opt.grade;
  s.size = opt.size;
  if (s.size.empty()) {
    if (s.family == st::Family::HeatSetInsert) {
      for (const std::string& sz : st::familySizes(s.family)) {
        st::FastenerSpec probe = s;
        probe.size = sz;
        auto d = st::fastenerDims(probe);
        if (d && std::abs(d->recommendedHole - site.diameter) < 0.1) s.size = sz;
      }
    } else if (s.family == st::Family::DowelPin) {
      for (const std::string& sz : st::familySizes(s.family)) {
        st::FastenerSpec probe = s;
        probe.size = sz;
        probe.length = 10;
        auto d = st::fastenerDims(probe);
        if (d && std::abs(d->d - site.diameter) < 0.05) s.size = sz;
      }
    } else {
      s.size = st::sizeForHoleDiameter(site.diameter, false);
      if (s.size.empty()) s.size = st::sizeForHoleDiameter(site.diameter, true);
    }
    if (s.size.empty()) {
      if (error) *error = std::format("no {} matches a hole of diameter {:.3f} mm: give size=", st::familyDescription(s.family), site.diameter);
      return s;
    }
  }
  if (!st::familyHasLength(s.family)) return s;
  if (opt.length > 0) {
    s.length = opt.length;
    return s;
  }
  const std::vector<double> ls = st::familyLengths(s.family, s.size);
  if (ls.empty()) {
    if (error) *error = std::format("no standard lengths for {} {}", st::familyDescription(s.family), s.size);
    return s;
  }
  auto largestBelow = [&](double v) {
    double best = ls.front();
    for (double l : ls)
      if (l <= v + 1e-6) best = l;
    return best;
  };
  if (s.family == st::Family::DowelPin) {
    s.length = largestBelow(2 * site.length);
  } else if (s.family == st::Family::SetScrew || !site.through) {
    s.length = largestBelow(s.family == st::Family::SetScrew ? site.length : site.seatToFar);
  } else {
    double need = site.seatToFar;
    const st::ThreadSpec* t = st::findThread(st::threadDesignationFor(s.size));
    st::FastenerSpec w{opt.washer ? st::Family::Washer : st::Family::Washer, s.size, 0, opt.grade};
    const double wh = st::stackHeight(w);
    if (opt.washer) need += wh;
    if (opt.nut) {
      st::FastenerSpec n{opt.nyloc ? st::Family::NylocNut : st::Family::HexNut, s.size, 0, opt.grade};
      need += st::stackHeight(n) + (opt.washer ? wh : 0) + 2 * (t ? t->pitch : 1.0);
    }
    s.length = st::nextStandardLength(s.family, s.size, need);
  }
  return s;
}

ObjectId standardPartDefinition(core::Document& doc, const st::FastenerSpec& spec) {
  const std::string pn = st::partNumber(spec);
  for (const core::Component* c : doc.components())
    if (!c->isOccurrence() && !c->isRoot() && c->partNumber == pn) return c->id;
  core::Component& comp = doc.addComponent(pn, core::kRootComponentId);
  comp.partNumber = pn;
  comp.description = st::familyDescription(spec.family);
  const st::Grade* g = st::findGrade(spec.grade);
  comp.material = g ? g->materialName : spec.grade;
  const ObjectId id = comp.id;
  auto f = std::make_unique<part::StandardPartFeature>();
  f->setSpec(spec);
  f->setComponent(id);
  f->setName(std::format("Fastener{}", id));
  doc.addFeature(std::move(f));
  return id;
}

bool applyInsertOption(const std::string& word, InsertOptions& opt) {
  const auto eq = word.find('=');
  if (eq == std::string::npos) return false;
  const std::string k = lower(word.substr(0, eq)), raw = word.substr(eq + 1), v = lower(raw);
  auto yes = [&] { return v == "yes" || v == "true" || v == "1" || v == "on"; };
  if (k == "family" || k == "type") {
    auto f = st::familyFromKey(v);
    if (!f) return false;
    opt.family = *f;
    return true;
  }
  if (k == "size") {
    opt.size = v == "auto" ? std::string() : st::sizeOf(raw);
    if (!opt.size.empty() && opt.size[0] == 'm') opt.size[0] = 'M';
    return true;
  }
  if (k == "length") {
    if (v == "auto") {
      opt.length = 0;
      return true;
    }
    try {
      opt.length = std::stod(v);
      return opt.length > 0;
    } catch (...) {
      return false;
    }
  }
  if (k == "material" || k == "grade") {
    const st::Grade* g = st::findGrade(raw);
    if (!g) return false;
    opt.grade = g->key;
    return true;
  }
  if (k == "nut") {
    opt.nut = yes() || v == "nyloc";
    opt.nyloc = v == "nyloc";
    return true;
  }
  if (k == "nyloc") {
    opt.nyloc = yes();
    if (opt.nyloc) opt.nut = true;
    return true;
  }
  if (k == "washer") {
    opt.washer = yes();
    return true;
  }
  if (k == "rotation" || k == "revolute") {
    opt.allowRotation = yes();
    return true;
  }
  if (k == "all") {
    opt.allEqual = yes();
    return true;
  }
  if (k == "joint") {
    if (v == "auto") opt.joint = JointMode::Auto;
    else if (v == "rigid") opt.joint = JointMode::Rigid;
    else if (v == "group") opt.joint = JointMode::Group;
    else if (v == "none" || v == "no") opt.joint = JointMode::None;
    else return false;
    return true;
  }
  return false;
}

InsertResult insertFasteners(core::Document& doc, core::UndoStack* undo, const std::vector<core::ShapeRef>& holeRefs,
                             const InsertOptions& opt) {
  InsertResult res;
  std::vector<HoleSite> sites;
  for (const core::ShapeRef& r : holeRefs) {
    std::string err;
    auto h = analyseHole(doc, r, &err);
    if (!h) {
      res.error = err;
      return res;
    }
    if (opt.allEqual) {
      for (const HoleSite& e : equalHoles(doc, *h)) sites.push_back(e);
    } else {
      sites.push_back(*h);
    }
  }
  // drop duplicates (the same hole picked twice / through all-equal)
  std::vector<HoleSite> unique;
  for (const HoleSite& s : sites)
    if (std::none_of(unique.begin(), unique.end(), [&](const HoleSite& u) { return u.seat.Distance(s.seat) < 1e-6; }))
      unique.push_back(s);
  sites = std::move(unique);
  if (sites.empty()) {
    res.error = "select holes to put fasteners in";
    return res;
  }
  // resolve the specs first (no document change on error)
  struct Item {
    st::FastenerSpec spec;
    gp_Trsf world;
    gp_Ax3 jointFrame;
    ObjectId plate;
  };
  std::vector<Item> items;
  for (const HoleSite& site : sites) {
    std::string err;
    st::FastenerSpec main = specForHole(site, opt, &err);
    if (!err.empty()) {
      res.error = err;
      return res;
    }
    if (res.partNumber.empty()) res.partNumber = st::partNumber(main);
    const gp_Dir out = site.axisIn.Reversed();
    const gp_Ax3 jf(site.seat, out);
    const bool headed = main.family == st::Family::SocketCap || main.family == st::Family::HexBolt;
    double lift = 0;
    if (headed && opt.washer) {
      st::FastenerSpec w{st::Family::Washer, main.size, 0, opt.grade};
      if (!st::fastenerDims(w)) {
        res.error = std::format("no washer for {}", main.size);
        return res;
      }
      items.push_back({w, frameTrsf(site.seat, out), jf, site.component});
      lift = st::stackHeight(w);
    }
    const gp_Pnt at = (headed || main.family == st::Family::DowelPin) ? site.seat : site.entry;
    items.push_back({main, frameTrsf(at.Translated(gp_Vec(out) * lift), out), jf, site.component});
    if (headed && opt.nut) {
      if (!site.through) {
        res.warnings.push_back("blind hole: no nut added");
      } else {
        const gp_Pnt far = site.entry.Translated(gp_Vec(site.axisIn) * site.length);
        double up = 0;
        if (opt.washer) {
          st::FastenerSpec w{st::Family::Washer, main.size, 0, opt.grade};
          items.push_back({w, frameTrsf(far, site.axisIn), jf, site.component});
          up = st::stackHeight(w);
        }
        st::FastenerSpec n{opt.nyloc ? st::Family::NylocNut : st::Family::HexNut, main.size, 0, opt.grade};
        if (!st::fastenerDims(n)) {
          res.error = std::format("no nut for {}", main.size);
          return res;
        }
        items.push_back({n, frameTrsf(far.Translated(gp_Vec(site.axisIn) * up), site.axisIn), jf, site.component});
      }
    }
  }
  res.holes = static_cast<int>(sites.size());
  const JointMode mode = opt.joint == JointMode::Auto ? (items.size() <= 8 ? JointMode::Rigid : JointMode::Group) : opt.joint;
  auto created = std::make_shared<std::vector<ObjectId>>();
  auto newDefs = std::make_shared<std::vector<ObjectId>>();
  const bool rotate = opt.allowRotation;
  auto edit = [&doc, items, mode, rotate, created, newDefs] {
    created->clear();
    newDefs->clear();
    std::map<std::string, bool> placedDef;  // definition already used as an instance
    std::map<ObjectId, std::vector<ObjectId>> byPlate;
    for (const Item& it : items) {
      const std::string pn = st::partNumber(it.spec);
      bool existed = false;
      for (const core::Component* c : doc.components())
        if (!c->isOccurrence() && !c->isRoot() && c->partNumber == pn) existed = true;
      const ObjectId def = standardPartDefinition(doc, it.spec);
      if (!existed) newDefs->push_back(def);
      ObjectId inst = def;
      if (existed || placedDef[pn]) {
        inst = createOccurrence(doc, nullptr, def, core::kRootComponentId, it.world);
      } else {
        doc.component(def)->placement = it.world;
        doc.notifyComponentChanged(def);
      }
      placedDef[pn] = true;
      created->push_back(inst);
      byPlate[it.plate].push_back(inst);
      if (mode == JointMode::Rigid && inst != core::kInvalidId) {
        auto j = std::make_unique<JointFeature>();
        j->setComponents(inst, it.plate);
        j->setJointType(rotate ? JointType::Revolute : JointType::Rigid);
        j->setAsBuilt(true);
        SolverJoint sj;
        sj.type = rotate ? JointType::Revolute : JointType::Rigid;
        AssemblySolver::asBuiltFrames(sj, it.world, doc.worldPlacement(it.plate), it.jointFrame);
        JointOrigin a, b;
        a.component = inst;
        a.frame = sj.frameA;
        a.valid = true;
        b.component = it.plate;
        b.frame = sj.frameB;
        b.valid = true;
        j->setOrigins(a, b);
        doc.addFeature(std::move(j));
      }
    }
    if (mode == JointMode::Group) {
      for (auto& [plate, members] : byPlate) {
        std::vector<ObjectId> ids = members;
        if (plate != core::kRootComponentId) ids.insert(ids.begin(), plate);
        if (ids.size() < 2) continue;
        auto g = std::make_unique<RigidGroupFeature>();
        g->setMembers(doc, ids);
        doc.addFeature(std::move(g));
      }
    }
    doc.recompute();
    // body appearance and density of new definitions (bodies exist after the recompute)
    for (ObjectId def : *newDefs) {
      const core::Component* c = doc.component(def);
      if (!c) continue;
      const core::Material* m = core::findMaterial(c->material);
      for (ObjectId bid : doc.liveBodies()) {
        core::Body* b = doc.body(bid);
        if (!b || b->component != def) continue;
        b->material = c->material;
        if (m) {
          b->density = m->density;
          b->color = m->color;
        }
        doc.notifyBodyChanged(bid);
      }
    }
  };
  if (undo) undo->push(std::make_unique<core::DocumentSnapshotCommand>(doc, "Insert fastener", edit));
  else edit();
  res.components = *created;
  return res;
}

}  // namespace fabricad::assembly
