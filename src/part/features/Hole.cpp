// SPDX-License-Identifier: LGPL-2.1-or-later
// Hole profile construction adapted from FreeCAD src/Mod/PartDesign/App/FeatureHole.cpp
// (LGPL-2.1-or-later, (c) 2011 Juergen Riegel and FreeCAD contributors), see THIRD_PARTY.md.
#include "part/features/Hole.h"

#include "core/Document.h"
#include "core/Feature.h"
#include "core/ShapeUtils.h"
#include "core/Tool.h"
#include "part/PartUtils.h"
#include "part/standards/Clearance.h"
#include "part/standards/Threads.h"

#include <QSettings>

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <gp_Ax2.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS.hxx>
#include <gp_Ax3.hxx>
#include <gp_Lin.hxx>

#include <algorithm>
#include <cmath>
#include <format>

namespace fabricad::part {

HoleFeature::HoleFeature() : PartFeature(kType) {
  addRefs("points", core::ShapeKind::SketchPoint | core::ShapeKind::Vertex | core::ShapeKind::ConstructionPoint |
                        core::ShapeKind::Edge | core::ShapeKind::Face)
      .setLabel("Placement points")
      .setTooltip("Sketch points, vertices, circular edges (centre) or clicks on a planar face where holes start");
  addRef("face", core::mask(core::ShapeKind::Face)).setLabel("Start face").setTooltip("Optional: face defining the hole direction");
  addEnum("holeType", {"Simple", "Counterbore", "Countersink"}, Simple).setLabel("Hole type");
  addLength("diameter", 6.6).setLabel("Diameter");
  addEnum("extentType", {"Distance", "All"}, Distance).setLabel("Extent");
  addLength("depth", 10.0).setLabel("Depth");
  addEnum("drillPoint", {"Flat", "Angle"}, Flat).setLabel("Drill point");
  addAngle("drillAngle", 118.0).setLabel("Drill point angle");
  addLength("cboreDiameter", 11.0).setLabel("Counterbore diameter");
  addLength("cboreDepth", 6.0).setLabel("Counterbore depth");
  addLength("csinkDiameter", 12.0).setLabel("Countersink diameter");
  addAngle("csinkAngle", 90.0).setLabel("Countersink angle");
  addBool("flip", false).setLabel("Flip direction");
  addEnum("tapType", {"Simple", "Clearance", "Tapped", "Tap drill only"}, Simple)
      .setLabel("Type")
      .setTooltip("Simple: typed diameter. Clearance / Tapped / Tap drill only take the diameter from the size table");
  addEnum("standard", {"ISO metric", "ANSI inch"}, Iso)
      .setLabel("Standard")
      .setTooltip("ISO metric (ISO 261 / 273) or ANSI unified inch (UNC / UNF, ASME B18.2.8)");
  addEnum("threadSize", isoMetricDesignations(), 0).setLabel("Size").setEnum("M6x1");
  addEnum("ansiSize", standards::inchDesignations(), 0).setLabel("Size").setEnum("1/4-20");
  addEnum("clearanceFit", {"Close", "Normal", "Loose"}, 1).setLabel("Fit").setTooltip("ISO 273 / ASME B18.2.8 clearance hole series");
  addBool("threadModeled", false).setLabel("Modeled").setTooltip("Cut the real thread (slow); off = cosmetic thread");
  addBool("threadFullDepth", true).setLabel("Full tap depth");
  addLength("threadDepth", 8.0).setLabel("Thread depth");
  addString("threadClass", "").setLabel("Thread class").setTooltip("Tolerance class; empty = 6H (ISO) / 2B (ANSI)");
  addEnum("fastener", standards::headTypeNames(), 0)
      .setLabel("Fastener")
      .setTooltip("Size the counterbore / countersink from the screw head table; Custom = typed values");
  addLength("headClearance", 0.0).setLabel("Head clearance").setTooltip("Extra counterbore depth above the head (0 / 0.5 / 1 mm)");
  addLength("headOffset", 0.0).setLabel("Head below surface").setTooltip("Sink the head this far below the surface");
  addString("threadInfo", "").setLabel("Thread").flags().readOnly = true;
  addRefs("targets", core::mask(core::ShapeKind::Body))
      .setLabel("Bodies to cut")
      .setTooltip("empty = the body the hole is placed on (else every body of the component the hole touches)");
  updatePropertyFlags();
}

void HoleFeature::updatePropertyFlags() {
  int t = enumIndex("holeType");
  bool all = enumIndex("extentType") == All;
  prop("depth").flags().hidden = all;
  prop("drillPoint").flags().hidden = all;
  prop("drillAngle").flags().hidden = all || enumIndex("drillPoint") != Angled;
  prop("cboreDiameter").flags().hidden = t != Counterbore;
  prop("cboreDepth").flags().hidden = t != Counterbore;
  prop("csinkDiameter").flags().hidden = t != Countersink;
  prop("csinkAngle").flags().hidden = t != Countersink;
  const int tap = enumIndex("tapType");
  const bool ansi = enumIndex("standard") == Ansi;
  const bool headSized = (t == Counterbore || t == Countersink) && enumIndex("fastener") != 0;
  const bool sized = tap != SimpleTap || headSized;
  prop("diameter").flags().hidden = tap != SimpleTap;
  prop("threadSize").flags().hidden = !sized || ansi;
  prop("ansiSize").flags().hidden = !sized || !ansi;
  prop("clearanceFit").flags().hidden = tap != Clearance;
  prop("threadModeled").flags().hidden = tap != Tapped;
  prop("threadFullDepth").flags().hidden = tap != Tapped;
  prop("threadDepth").flags().hidden = tap != Tapped || flag("threadFullDepth");
  prop("threadClass").flags().hidden = tap != Tapped;
  prop("fastener").flags().hidden = t != Counterbore && t != Countersink;
  prop("headClearance").flags().hidden = !headSized;
  prop("headOffset").flags().hidden = !headSized;
  syncHeadValues();
  prop("threadInfo").setString(threadDepthText());
  prop("threadInfo").flags().hidden = tap != Tapped || all;
}

double HoleFeature::clearanceDiameter(const IsoThread& t, int fit) {
  return standards::clearanceDiameter(t, static_cast<standards::Fit>(std::clamp(fit, 0, 2)));
}

const IsoThread* HoleFeature::threadSpec() const {
  if (enumIndex("standard") == Ansi) return standards::findInchThread(enumName("ansiSize"));
  return findIsoThread(enumName("threadSize"));
}

std::optional<standards::HeadRecess> HoleFeature::tableRecess() const {
  const int fast = enumIndex("fastener");
  if (fast <= 0) return std::nullopt;
  const IsoThread* t = threadSpec();
  if (!t) return std::nullopt;
  auto rec = standards::headRecess(static_cast<standards::HeadType>(fast), *t, prop("headClearance").toDouble(),
                                   prop("headOffset").toDouble());
  // the head must suit the recess: countersinks take countersunk heads, counterbores the others
  const int holeType = enumIndex("holeType");
  if (rec && (holeType == Countersink) != rec->countersink) return std::nullopt;
  return rec;
}

double HoleFeature::effectiveDiameter() const {
  const int tap = enumIndex("tapType");
  const IsoThread* t = tap == SimpleTap ? nullptr : threadSpec();
  if (!t) return prop("diameter").toDouble();
  return tap == Clearance ? clearanceDiameter(*t, enumIndex("clearanceFit")) : t->tapDrill();
}

double HoleFeature::usableThreadDepth() const {
  if (!flag("threadFullDepth")) return prop("threadDepth").toDouble();
  return prop("depth").toDouble();
}

std::string HoleFeature::threadDepthText() const {
  if (enumIndex("tapType") != Tapped) return {};
  const bool inch = enumIndex("standard") == Ansi;
  auto len = [inch](double mm) {
    return inch ? std::format("{:.3f}\"", mm / standards::kInch) : std::format("{:.4g} mm", mm);
  };
  std::string out = std::format("thread {}, drill {}", len(usableThreadDepth()), len(prop("depth").toDouble()));
  if (enumIndex("drillPoint") == Angled) {
    const double a = prop("drillAngle").toDouble();
    if (a > 0 && a < 180) out += std::format(" + {} point", len(effectiveDiameter() / 2.0 / std::tan(a * M_PI / 360.0)));
  }
  return out;
}

void HoleFeature::syncHeadValues() {
  const int t = enumIndex("holeType");
  const int fast = enumIndex("fastener");
  const std::string key = std::format("{}|{}|{}|{}|{}|{}|{}", t, fast, enumIndex("standard"), enumName("threadSize"),
                                      enumName("ansiSize"), prop("headClearance").toDouble(), prop("headOffset").toDouble());
  const char* names[4] = {"cboreDiameter", "cboreDepth", "csinkDiameter", "csinkAngle"};
  double cur[4];
  for (int i = 0; i < 4; ++i) cur[i] = prop(names[i]).toDouble();
  auto rec = (t == Counterbore || t == Countersink) ? tableRecess() : std::nullopt;
  if (!rec) {
    headSynced_ = false;
    lastHeadKey_ = key;
    return;
  }
  if (headSynced_ && key == lastHeadKey_) {
    for (int i = 0; i < 4; ++i)
      if (std::abs(cur[i] - lastFill_[i]) > 1e-9) {
        prop("fastener").setEnum(0);  // a hand-edited recess is a Custom head
        headSynced_ = false;
        prop("headClearance").flags().hidden = true;
        prop("headOffset").flags().hidden = true;
        return;
      }
    return;
  }
  prop("cboreDiameter").setDouble(rec->diameter);
  prop("cboreDepth").setDouble(rec->depth);
  prop("csinkDiameter").setDouble(rec->diameter);
  if (rec->angle > 0) prop("csinkAngle").setDouble(rec->angle);
  for (int i = 0; i < 4; ++i) lastFill_[i] = prop(names[i]).toDouble();
  headSynced_ = true;
  lastHeadKey_ = key;
}

void HoleFeature::fromJson(const QJsonObject& o) {
  PartFeature::fromJson(o);
  headSynced_ = false;
  lastHeadKey_.clear();
  updatePropertyFlags();
}

void HoleFeature::applyDefaultsForNewFeature(const core::Document& doc) {
  PartFeature::applyDefaultsForNewFeature(doc);
  // Interactive holes size counterbores for a socket head cap screw; scripts keep Custom (typed
  // recess values, as before P2) unless they say fastener=...
  if (!core::isBatchMode()) prop("fastener").setEnum(static_cast<int>(standards::HeadType::SocketCap));
  // preference hole/defaultStandard: auto (follows the document unit), iso or ansi
  const std::string pref = QSettings().value("hole/defaultStandard", "auto").toString().toStdString();
  const bool inchDoc = doc.units().length == core::LengthUnit::Inch || doc.units().length == core::LengthUnit::Foot;
  const bool ansi = pref == "ansi" || (pref != "iso" && inchDoc);
  prop("standard").setEnum(ansi ? Ansi : Iso);
  if (ansi) {
    prop("diameter").setDouble(0.281 * standards::kInch);
    prop("depth").setDouble(0.5 * standards::kInch);
  }
  updatePropertyFlags();
}

namespace {
std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
}  // namespace

bool HoleFeature::applyScriptOption(const std::string& nameIn, const std::string& valueIn) {
  const std::string name = lower(nameIn), v = lower(valueIn);
  if (name == "standard") {
    if (v == "iso" || v == "metric" || v == "iso metric") prop("standard").setEnum(Iso);
    else if (v == "ansi" || v == "inch" || v == "ansi inch" || v == "unc" || v == "unf") prop("standard").setEnum(Ansi);
    else return false;
    return true;
  }
  if (name == "type") {
    static const std::pair<const char*, int> kTypes[] = {{"simple", SimpleTap}, {"clearance", Clearance}, {"tapped", Tapped},
                                                         {"tapdrill", TapDrillOnly}, {"tap drill only", TapDrillOnly}};
    for (auto [k, i] : kTypes)
      if (v == k) {
        prop("tapType").setEnum(i);
        return true;
      }
    return false;
  }
  if (name == "size") {
    if (const IsoThread* iso = findIsoThread(valueIn)) {
      prop("threadSize").setEnum(iso->designation);
      prop("standard").setEnum(Iso);
      return true;
    }
    if (const IsoThread* in = standards::findInchThread(valueIn)) {
      prop("ansiSize").setEnum(in->designation);
      prop("standard").setEnum(Ansi);
      return true;
    }
    return false;
  }
  if (name == "fit") {
    const int i = v == "close" ? 0 : v == "normal" ? 1 : v == "loose" ? 2 : -1;
    if (i < 0) return false;
    prop("clearanceFit").setEnum(i);
    return true;
  }
  if (name == "fastener" || name == "head") {
    const auto& keys = standards::headTypeKeys();
    for (size_t i = 0; i < keys.size(); ++i)
      if (v == keys[i] || v == lower(standards::headTypeNames()[i])) {
        prop("fastener").setEnum(static_cast<int>(i));
        return true;
      }
    return false;
  }
  if (name == "point") {
    if (v == "flat") {
      prop("drillPoint").setEnum(Flat);
      return true;
    }
    try {
      const double a = std::stod(v);
      if (a <= 0 || a >= 180) return false;
      prop("drillPoint").setEnum(Angled);
      prop("drillAngle").setDouble(a);
      return true;
    } catch (...) {
      return false;
    }
  }
  if (name == "class") {
    prop("threadClass").setString(valueIn);
    return true;
  }
  return false;
}

TopoDS_Shape HoleFeature::makeProtoHole(const Params& p, std::string* error, std::vector<TopoDS_Shape>* namedFaces) {
  auto fail = [&](const char* msg) {
    if (error) *error = msg;
    return TopoDS_Shape();
  };
  const double radius = p.diameter / 2.0;
  if (radius <= 1e-9) return fail("diameter must be positive");
  if (p.depth <= 1e-9) return fail("depth must be positive");
  try {
    // Half cross-section in the XZ plane: x = radius, z downwards (hole axis = -Z through origin).
    // Points are listed clockwise starting on the axis at the top.
    std::vector<gp_Pnt> pts;
    const double ext = std::max(0.0, p.topExtension);
    const gp_Pnt top(0, 0, ext);
    pts.push_back(top);
    double depth = p.depth;
    if (p.type == Counterbore) {
      double cr = p.cboreDiameter / 2.0;
      if (cr <= radius) return fail("counterbore diameter must exceed the hole diameter");
      if (p.cboreDepth <= 0 || p.cboreDepth >= depth) return fail("counterbore depth must be between 0 and the hole depth");
      pts.emplace_back(cr, 0, ext);
      pts.emplace_back(cr, 0, -p.cboreDepth);
      pts.emplace_back(radius, 0, -p.cboreDepth);
    } else if (p.type == Countersink) {
      double cr = p.csinkDiameter / 2.0;
      if (cr <= radius) return fail("countersink diameter must exceed the hole diameter");
      if (p.csinkAngle <= 0 || p.csinkAngle >= 180) return fail("countersink angle must be between 0 and 180 degrees");
      double tanHalf = std::tan(p.csinkAngle * M_PI / 360.0);
      double csDepth = (cr - radius) / tanHalf;
      if (csDepth >= depth) return fail("countersink is deeper than the hole");
      pts.emplace_back(cr + ext * tanHalf, 0, ext);  // cone continued above the surface
      pts.emplace_back(radius, 0, -csDepth);
    } else {
      pts.emplace_back(radius, 0, ext);
    }
    if (p.drillPoint == Angled) {
      if (p.drillAngle <= 0 || p.drillAngle >= 180) return fail("drill point angle must be between 0 and 180 degrees");
      double tipH = radius / std::tan(p.drillAngle * M_PI / 360.0);
      pts.emplace_back(radius, 0, -depth);
      pts.emplace_back(0, 0, -depth - tipH);
    } else {
      pts.emplace_back(radius, 0, -depth);
      pts.emplace_back(0, 0, -depth);
    }
    BRepBuilderAPI_MakeWire mw;
    for (size_t i = 0; i + 1 < pts.size(); ++i) mw.Add(BRepBuilderAPI_MakeEdge(pts[i], pts[i + 1]).Edge());
    mw.Add(BRepBuilderAPI_MakeEdge(pts.back(), pts.front()).Edge());
    if (!mw.IsDone()) return fail("hole profile is not closed");
    BRepBuilderAPI_MakeFace mf(mw.Wire(), Standard_True);
    if (!mf.IsDone()) return fail("hole profile is not planar");
    BRepPrimAPI_MakeRevol rev(mf.Face(), gp_Ax1(top, gp_Dir(0, 0, 1)), 2 * M_PI);
    rev.Build();
    if (!rev.IsDone()) return fail("could not revolve the hole profile");
    TopoDS_Shape s = rev.Shape();
    if (!core::isValidShape(s)) return fail("hole profile produced an invalid solid");
    const std::vector<TopoDS_Shape> plainFaces = facesOf(s);
    if (namedFaces) *namedFaces = plainFaces;
    if (ext > 0 && (p.type == Counterbore || p.type == Countersink)) {
      // DRW-VIEWS-ANNOT-13: above the placement face only the drill radius cuts; remove the
      // ring radius..outer x 0..ext (+ margin) from the tool
      const double outer = std::max(p.cboreDiameter, p.csinkDiameter) / 2.0 + ext * 4.0 + 1.0;
      TopoDS_Shape big = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), outer, ext + 1.0).Shape();
      TopoDS_Shape core = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, -1.0), gp_Dir(0, 0, 1)), radius, ext + 3.0).Shape();
      BRepAlgoAPI_Cut tube(big, core);
      if (!tube.IsDone()) return fail("could not trim the hole above the face");
      BRepAlgoAPI_Cut trim(s, tube.Shape());
      if (!trim.IsDone()) return fail("could not trim the hole above the face");
      TopoDS_Shape t = trim.Shape();
      if (!core::isValidShape(t)) return fail("trimmed hole is not valid");
      if (namedFaces) {
        // old faces first (their image in the trimmed tool), then the new ones
        std::vector<TopoDS_Shape> order;
        const std::vector<TopoDS_Shape> now = facesOf(t);
        auto present = [&](const TopoDS_Shape& f) {
          return std::any_of(now.begin(), now.end(), [&](const TopoDS_Shape& g) { return g.IsSame(f); });
        };
        auto taken = [&](const TopoDS_Shape& f) {
          return std::any_of(order.begin(), order.end(), [&](const TopoDS_Shape& g) { return !g.IsNull() && g.IsSame(f); });
        };
        for (const TopoDS_Shape& f : plainFaces) {
          TopoDS_Shape img;
          if (present(f)) img = f;
          else
            for (const TopoDS_Shape& m : trim.Modified(f))
              if (m.ShapeType() == TopAbs_FACE && present(m) && !taken(m)) {
                img = m;
                break;
              }
          // a face that vanished keeps its (empty) slot, so the later numbers do not shift
          order.push_back(!img.IsNull() && !taken(img) ? img : TopoDS_Shape());
        }
        for (const TopoDS_Shape& f : now)
          if (!taken(f)) order.push_back(f);
        *namedFaces = std::move(order);
      }
      return t;
    }
    return s;
  } catch (const Standard_Failure& e) {
    if (error) *error = e.GetMessageString() ? e.GetMessageString() : "hole construction failed";
    return {};
  }
}

void HoleFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  holeThreads_.clear();
  Params p;
  p.type = enumIndex("holeType");
  p.diameter = ctx.length("diameter");
  const int tap = enumIndex("tapType");
  const IsoThread* iso = nullptr;
  if (tap != SimpleTap || ((p.type == Counterbore || p.type == Countersink) && enumIndex("fastener") > 0)) {
    iso = threadSpec();
    if (!iso) {
      ctx.setError("unknown thread size");
      return;
    }
    if (tap != SimpleTap) p.diameter = tap == Clearance ? clearanceDiameter(*iso, enumIndex("clearanceFit")) : iso->tapDrill();
  }
  p.depth = ctx.length("depth");
  p.drillPoint = enumIndex("drillPoint");
  p.drillAngle = ctx.angle("drillAngle");
  p.cboreDiameter = ctx.length("cboreDiameter");
  p.cboreDepth = ctx.length("cboreDepth");
  p.csinkDiameter = ctx.length("csinkDiameter");
  p.csinkAngle = ctx.angle("csinkAngle");
  if (p.type == Counterbore || p.type == Countersink) {
    if (auto rec = tableRecess()) {
      p.cboreDiameter = rec->diameter;
      p.cboreDepth = rec->depth;
      p.csinkDiameter = rec->diameter;
      if (rec->angle > 0) p.csinkAngle = rec->angle;
    } else if (enumIndex("fastener") > 0 && iso) {
      const auto any = standards::headRecess(static_cast<standards::HeadType>(enumIndex("fastener")), *iso, 0, 0);
      if (!any)
        ctx.setWarning(std::format("no {} head for {} in the table: typed recess used",
                                   standards::headTypeKeys()[static_cast<size_t>(enumIndex("fastener"))], iso->designation));
    }
  }
  const bool all = enumIndex("extentType") == All;
  if (all) {
    p.depth = throughAllLength(ctx);
    p.drillPoint = Flat;
    // Start a little above the surface so a coplanar top face does not confuse the boolean;
    // the extension keeps the counterbore/countersink depth measured from z = 0.
    p.topExtension = 1.0;
  }

  // Direction: from the start face (into the material = -normal), else the sketch plane of
  // the first sketch point, else -Z.
  std::optional<gp_Dir> normal;
  if (ref("face").isValid()) {
    TopoDS_Shape f = resolveRefProperty(ctx, prop("face"));
    normal = faceNormal(f);
    if (!normal) {
      ctx.setError("start face must be planar");
      return;
    }
  }
  std::vector<gp_Pnt> centres;
  for (auto& r : prop("points").refs()) {
    if (r.kind == core::ShapeKind::Face) {
      // click on a planar face: the hole starts at the pick point projected onto the face
      TopoDS_Shape f = resolveRef(ctx, r);
      if (f.IsNull()) continue;
      auto pl = core::facePlane(f);
      if (!pl || !r.hasPoint) {
        ctx.setWarning("hole placement on a face needs a planar face and a pick point");
        continue;
      }
      if (!normal) normal = faceNormal(f);
      gp_Pnt pick = r.point.toPnt();
      gp_Vec n(pl->Direction());
      double d = gp_Vec(pl->Location(), pick).Dot(n);
      centres.push_back(pick.Translated(-n * d));
      continue;
    }
    if (!normal && r.kind == core::ShapeKind::SketchPoint) {
      if (const core::IProfileSource* ps = ctx.profileSource(r)) normal = ps->sketchPlane().Direction();
    }
    if (r.isBrep()) {
      TopoDS_Shape s = resolveRef(ctx, r);
      if (s.IsNull()) continue;
    }
    auto pt = ctx.point(r);
    if (!pt) {
      ctx.setWarning(std::format("placement point not found: {}", r.toString()));
      continue;
    }
    centres.push_back(*pt);
  }
  if (centres.empty()) {
    ctx.setError("select placement points (sketch points, vertices or circle centres)");
    return;
  }
  if (!normal) normal = gp_Dir(0, 0, 1);
  gp_Dir axisDir = normal->Reversed();  // into the material
  if (flag("flip")) axisDir.Reverse();

  std::string err;
  std::vector<TopoDS_Shape> protoNamed;
  TopoDS_Shape proto = makeProtoHole(p, &err, &protoNamed);
  if (proto.IsNull()) {
    ctx.setError(err);
    return;
  }
  const std::vector<TopoDS_Shape> protoFaces = facesOf(proto);
  std::vector<TopoDS_Shape> holes;
  NameMap names;
  const std::string base = name().empty() ? std::string(kType) : name();
  int k = 0;
  for (const gp_Pnt& c : centres) {
    ++k;
    gp_Ax3 target(c, axisDir.Reversed(), anyPerpendicular(axisDir));
    gp_Trsf t;
    t.SetDisplacement(gp_Ax3(), target);  // moves the proto frame (Z up at origin) onto the target frame
    TopoDS_Shape h = copyTransformed(proto, t);
    holes.push_back(h);
    // name the faces of each hole in the proto's naming order (copies keep the face order)
    const std::vector<TopoDS_Shape> hf = facesOf(h);
    int fi = 0;
    for (const TopoDS_Shape& f : protoNamed) {
      ++fi;
      if (f.IsNull()) continue;
      for (size_t i = 0; i < protoFaces.size() && i < hf.size(); ++i)
        if (protoFaces[i].IsSame(f)) {
          names[std::format("{}/H{}/F{}", base, k, fi)] = hf[i];
          break;
        }
    }
  }
  TopoDS_Shape tool = holes.size() == 1 ? holes.front() : compoundOf(holes);
  // Fusion: with no explicit "Bodies to cut" a hole cuts the body it was placed on (start face /
  // picked face, vertex or edge), not every body on its axis (a through-all hole would otherwise
  // drill parts of other components behind the rail).
  std::vector<core::ShapeRef> defaultTargets;
  if (prop("targets").refs().empty()) {
    auto addOwner = [&](const core::ShapeRef& r) {
      if (!r.isBrep() || !ctx.document().body(r.owner)) return;
      for (const auto& t : defaultTargets)
        if (t.owner == r.owner) return;
      defaultTargets.push_back(core::ShapeRef::body(r.owner));
    };
    if (ref("face").isValid()) addOwner(ref("face"));
    for (const auto& r : prop("points").refs()) addOwner(r);
  }
  std::vector<core::ShapeRef>& targets = defaultTargets.empty() ? prop("targets").refs() : defaultTargets;
  if (!applyTool(ctx, tool, Operation::Cut, targets, names, out)) return;
  if (tap == Tapped) applyThreads(ctx, *iso, centres, axisDir, p.diameter / 2, out);
}

void HoleFeature::applyThreads(core::RecomputeContext& ctx, const IsoThread& iso, const std::vector<gp_Pnt>& centres,
                               const gp_Dir& axisDir, double radius, Outcome& out) {
  const bool modeled = flag("threadModeled"), full = flag("threadFullDepth");
  const double depth = ctx.length("threadDepth");
  for (auto& change : out.bodies) {
    if (change.shape.IsNull()) continue;
    std::vector<TopoDS_Shape> cutters;
    for (const gp_Pnt& c : centres) {
      // the hole wall: a full cylindrical face of the tap-drill radius on this hole's axis
      for (const auto& f : facesOf(change.shape)) {
        auto cyl = analyseThreadCylinder(change.shape, f);
        if (!cyl || !cyl->internal || std::abs(cyl->radius - radius) > 1e-4) continue;
        if (cyl->axis.Direction().IsParallel(axisDir, 1e-6) == Standard_False) continue;
        if (gp_Lin(cyl->axis).Distance(c) > 1e-4) continue;
        // start at the end nearest the hole's start point
        gp_Vec oc(cyl->axis.Location(), c);
        double vc = oc.Dot(gp_Vec(cyl->axis.Direction()));
        bool flip = std::abs(vc - cyl->v1) < std::abs(vc - cyl->v0);
        ThreadInfo info;
        TopoDS_Shape cutter;
        std::string err;
        if (!placeThread(*cyl, iso, full, 0.0, depth, flip, false, modeled, info, modeled ? &cutter : nullptr, &err)) {
          ctx.setWarning("thread: " + err);
          continue;
        }
        info.threadClass = prop("threadClass").toString();
        if (info.threadClass.empty() && iso.isInch()) info.threadClass = standards::defaultThreadClass(iso, true);
        holeThreads_.push_back(info);
        if (!cutter.IsNull()) cutters.push_back(cutter);
      }
    }
    if (cutters.empty()) continue;
    BoolResult r = threadCut(change.shape, cutters);
    if (!r) {
      ctx.setError("modelled thread failed: " + (r.error.empty() ? std::string("empty result") : r.error));
      return;
    }
    NameMap mapped = mapNames(out.names, History::of(r.history), r.shape);
    addEdgeNames(mapped, r.shape);
    out.names = mapped;
    change.shape = r.shape;
  }
  if (holeThreads_.empty()) ctx.setWarning("no hole wall found for the thread");
}

}  // namespace fabricad::part
