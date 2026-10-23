// FabriCAD assembly: interference check implementation.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "assembly/Interference.h"

#include "core/ShapeUtils.h"
#include "part/features/StandardPart.h"
#include "part/standards/Fasteners.h"

#include <BRepAlgoAPI_Common.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Lin.hxx>

#include <cmath>
#include <format>
#include <map>
#include <optional>

namespace fabricad::assembly {

using core::Document;
using core::ObjectId;

namespace {

namespace st = part::standards;

/// Radial band (about the part's axis) in which a standard part overlaps its mate by design.
struct ThreadBand {
  double rIn = 0, rOut = 0;
};

std::optional<ThreadBand> threadBand(const st::FastenerSpec& s) {
  auto d = st::fastenerDims(s);
  if (!d) return std::nullopt;
  switch (s.family) {
    case st::Family::SocketCap:
    case st::Family::HexBolt:
    case st::Family::SetScrew:
    case st::Family::HexNut:
    case st::Family::NylocNut: {
      const st::ThreadSpec* t = st::findThread(st::threadDesignationFor(s.size));
      const double pitch = t ? t->pitch : 0.1 * d->d;
      return ThreadBand{d->d / 2 - pitch, d->d / 2};  // tap drill (d - P) to the major diameter
    }
    case st::Family::HeatSetInsert:
      return ThreadBand{std::min(d->recommendedHole, d->headDia) / 2 - 0.05, d->headDia / 2};
    default:
      return std::nullopt;
  }
}

/// Every edge of the intersection lies within the band about the axis: a thread / insert overlap.
bool withinBand(const TopoDS_Shape& common, const gp_Ax1& axis, const ThreadBand& b) {
  const gp_Lin line(axis);
  const double tol = 1e-3;
  bool any = false;
  for (TopExp_Explorer ex(common, TopAbs_EDGE); ex.More(); ex.Next()) {
    BRepAdaptor_Curve c(TopoDS::Edge(ex.Current()));
    const double a = c.FirstParameter(), z = c.LastParameter();
    if (std::isinf(a) || std::isinf(z)) return false;
    for (int i = 0; i <= 4; ++i) {
      const double r = line.Distance(c.Value(a + (z - a) * i / 4.0));
      if (r < b.rIn - tol || r > b.rOut + tol) return false;
    }
    any = true;
  }
  return any;
}

}  // namespace

std::string InterferencePair::label() const { return std::format("{} x {}", a.body, b.body); }

double InterferenceReport::totalVolume() const {
  double v = 0;
  for (const auto& p : pairs) v += p.volume;
  return v;
}

bool allInOneComponent(const std::vector<BodyInstance>& instances) {
  if (instances.size() < 2) return false;
  for (const BodyInstance& bi : instances)
    if (bi.component != instances.front().component) return false;
  return true;
}

InterferenceReport checkInterference(const Document& doc, const std::vector<ObjectId>& components,
                                     const InterferenceOptions& options) {
  return checkInterference(bodyInstances(doc, components, true), &doc, options);
}

InterferenceReport checkInterference(const std::vector<BodyInstance>& instances, const Document* doc,
                                     const InterferenceOptions& options) {
  InterferenceReport rep;
  rep.instances = static_cast<int>(instances.size());
  // standard parts: body -> spec (thread engagement is not a clash)
  std::map<ObjectId, st::FastenerSpec> standard;
  if (doc)
    for (const auto& f : doc->features())
      if (const auto* sp = dynamic_cast<const part::StandardPartFeature*>(f.get()); sp && !sp->isSuppressed())
        for (ObjectId b : sp->createdBodies()) standard[b] = sp->spec();
  auto isThread = [&](const BodyInstance& x, const TopoDS_Shape& common) {
    auto it = standard.find(x.body);
    if (it == standard.end() || !doc) return false;
    auto band = threadBand(it->second);
    if (!band) return false;
    const gp_Ax1 axis = gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)).Transformed(doc->worldPlacement(x.component));
    return withinBand(common, axis, *band);
  };
  std::vector<Bnd_Box> boxes(instances.size());
  for (size_t i = 0; i < instances.size(); ++i) {
    BRepBndLib::Add(instances[i].worldShape, boxes[i], false);
    boxes[i].Enlarge(options.bboxGap);
  }
  for (size_t i = 0; i < instances.size(); ++i) {
    for (size_t j = i + 1; j < instances.size(); ++j) {
      if (options.cancelled && options.cancelled()) {
        rep.cancelled = true;
        return rep;
      }
      const BodyInstance& a = instances[i];
      const BodyInstance& b = instances[j];
      if (!options.sameComponentPairs && a.component == b.component) continue;
      if (boxes[i].IsVoid() || boxes[j].IsVoid() || boxes[i].IsOut(boxes[j])) {
        ++rep.pairsSkipped;
        continue;
      }
      ++rep.pairsTested;
      try {
        BRepAlgoAPI_Common common(a.worldShape, b.worldShape);
        if (!common.IsDone()) {
          rep.errors.push_back(std::format("common failed for bodies {} and {}", a.body, b.body));
          continue;
        }
        TopoDS_Shape s = common.Shape();
        if (s.IsNull()) continue;
        double v = core::volume(s);
        if (v > options.minVolume) {
          InterferencePair p;
          p.a = a;
          p.b = b;
          p.volume = v;
          p.shape = s;
          p.thread = isThread(a, s) || isThread(b, s);
          if (p.thread) ++rep.threadPairs;
          if (!p.thread || options.includeThreads) rep.pairs.push_back(std::move(p));
        }
      } catch (const Standard_Failure& e) {
        rep.errors.push_back(std::format("OCCT: {}", e.GetMessageString() ? e.GetMessageString() : "failure"));
      } catch (const std::exception& e) {
        rep.errors.push_back(e.what());
      }
    }
  }
  return rep;
}

}  // namespace fabricad::assembly
