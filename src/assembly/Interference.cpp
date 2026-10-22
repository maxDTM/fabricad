// FabriCAD assembly: interference check implementation.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "assembly/Interference.h"

#include "core/ShapeUtils.h"

#include <BRepAlgoAPI_Common.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <Standard_Failure.hxx>

#include <format>

namespace fabricad::assembly {

using core::Document;
using core::ObjectId;

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
          rep.pairs.push_back(std::move(p));
        }
      } catch (const Standard_Failure& e) {
        rep.errors.push_back(std::format("OCCT: {}", e.GetMessageString() ? e.GetMessageString() : "failure"));
      } catch (const std::exception& e) {
        rep.errors.push_back(e.what());
      }
    }
  }
  if (doc) {
    for (auto& p : rep.pairs) (void)p;  // labels are resolved by the UI through the document
  }
  return rep;
}

}  // namespace fabricad::assembly
