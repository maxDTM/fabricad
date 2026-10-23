// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/features/StandardPart.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/Feature.h"

#include <TopExp_Explorer.hxx>

#include <algorithm>
#include <format>

namespace fabricad::part {

StandardPartFeature::StandardPartFeature() : PartFeature(kType) {
  addEnum("family", standards::familyNames(), 0).setLabel("Family");
  addString("size", "M6").setLabel("Size");
  addLength("length", 20.0).setLabel("Length");
  addString("grade", "8.8").setLabel("Material");
  addBool("shortInsert", false).setLabel("Short insert");
  addString("bodyName", "").setLabel("Body name");
  updatePropertyFlags();
}

void StandardPartFeature::updatePropertyFlags() {
  const auto fam = static_cast<standards::Family>(std::clamp(enumIndex("family"), 0, standards::kFamilyCount - 1));
  prop("length").flags().hidden = !standards::familyHasLength(fam);
  prop("shortInsert").flags().hidden = fam != standards::Family::HeatSetInsert;
}

void StandardPartFeature::fromJson(const QJsonObject& o) {
  PartFeature::fromJson(o);
  updatePropertyFlags();
}

standards::FastenerSpec StandardPartFeature::spec() const {
  standards::FastenerSpec s;
  s.family = static_cast<standards::Family>(std::clamp(enumIndex("family"), 0, standards::kFamilyCount - 1));
  s.size = prop("size").toString();
  s.length = prop("length").toDouble();
  s.grade = prop("grade").toString();
  s.shortInsert = flag("shortInsert");
  return s;
}

void StandardPartFeature::setSpec(const standards::FastenerSpec& s) {
  prop("family").setEnum(static_cast<int>(s.family));
  prop("size").setString(s.size);
  prop("length").setDouble(s.length);
  prop("grade").setString(s.grade);
  prop("shortInsert").setBool(s.shortInsert);
  updatePropertyFlags();
}

void StandardPartFeature::prepareCommit(core::Document& doc, core::UndoStack* undo) {
  const core::Component* c = doc.component(component());
  if (!c || c->isRoot()) return;
  const standards::FastenerSpec s = spec();
  const std::string pn = standards::partNumber(s), desc = standards::familyDescription(s.family);
  const standards::Grade* g = standards::findGrade(s.grade);
  const std::string mat = g ? g->materialName : s.grade;
  if (c->partNumber == pn && c->description == desc && c->material == mat) return;
  auto edit = [pn, desc, mat](core::Component& comp) {
    comp.partNumber = pn;
    comp.description = desc;
    comp.material = mat;
    comp.name = pn;
  };
  if (undo) undo->push(core::makeComponentEditCommand(doc, component(), edit, "Standard part"));
  else if (core::Component* m = doc.component(component())) edit(*m);
}

void StandardPartFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  standards::FastenerSpec s = spec();
  s.length = ctx.length("length");
  std::string err;
  TopoDS_Shape shape = standards::makeFastener(s, &err);
  if (shape.IsNull()) {
    ctx.setError(err.empty() ? std::string("standard part could not be generated") : err);
    return;
  }
  std::string bodyName = prop("bodyName").toString();
  if (bodyName.empty()) bodyName = standards::partNumber(s);
  out.addNew(shape, bodyName);
  const std::string base = name().empty() ? type() : name();
  int fi = 0;
  for (TopExp_Explorer ex(shape, TopAbs_FACE); ex.More(); ex.Next()) out.names[std::format("{}/F{}", base, ++fi)] = ex.Current();
  out.addPreview(shape, Operation::NewBody);
}

}  // namespace fabricad::part
