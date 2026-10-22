// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/PartFeature.h"

#include "core/Document.h"
#include "part/PartUtils.h"

#include <Standard_Failure.hxx>

#include <format>

namespace fabricad::part {

const std::vector<std::string>& operationNames() {
  static const std::vector<std::string> kNames{"Join", "Cut", "Intersect", "New Body"};
  return kNames;
}

Operation operationFromIndex(int i) {
  if (i < 0 || i > 3) return Operation::NewBody;
  return static_cast<Operation>(i);
}

void addOperationProperties(core::Feature& f, Operation def) {
  f.addEnum("operation", operationNames(), static_cast<int>(def)).setLabel("Operation");
  f.addRefs("targets", core::mask(core::ShapeKind::Body))
      .setLabel("Objects to Cut/Join")
      .setTooltip("Bodies that take part in the operation (empty = every body the tool touches)");
}

void PartFeature::adoptPrecomputed(Outcome out, std::vector<std::string> warnings,
                                   std::map<core::ObjectId, TopoDS_Shape> inputState) {
  precomputed_ = Precomputed{std::move(out), std::move(warnings), std::move(inputState), scopeProbes_, scopeOpaque_};
}

namespace {
bool sameState(const std::map<core::ObjectId, TopoDS_Shape>& a, const std::map<core::ObjectId, TopoDS_Shape>& b) {
  if (a.size() != b.size()) return false;
  for (auto i = a.begin(), j = b.begin(); i != a.end(); ++i, ++j)
    if (i->first != j->first || !i->second.IsEqual(j->second)) return false;
  return true;
}
}  // namespace

void PartFeature::recompute(core::RecomputeContext& ctx) {
  placementsWritten_ = false;
  if (precomputed_) {
    Precomputed pc = std::move(*precomputed_);
    precomputed_.reset();
    if (sameState(pc.input, ctx.state())) {
      // the same computation already ran for the preview: commit its result
      for (const std::string& w : pc.warnings) ctx.setWarning(w);
      scopeProbes_ = std::move(pc.scopeProbes);
      scopeOpaque_ = pc.scopeOpaque;
      commit(ctx, pc.out);
      return;
    }
  }
  Outcome out;
  try {
    compute(ctx, out);
  } catch (const Standard_Failure& e) {
    ctx.setError(std::format("OCCT: {}", e.GetMessageString() ? e.GetMessageString() : "failure"));
    return;
  }
  if (ctx.hasError()) return;
  commit(ctx, out);
}

void PartFeature::commit(core::RecomputeContext& ctx, const Outcome& out) {
  for (const auto& change : out.bodies) {
    if (change.body == core::kInvalidId) {
      ctx.createBody(change.newName, change.shape, change.component);
    } else {
      ctx.setBodyShape(change.body, change.shape);
    }
  }
  for (const auto& [comp, placement] : out.placements) {
    core::Component* c = ctx.document().component(comp);
    if (!c) continue;
    placementsWritten_ = true;
    c->placement = placement;
    ctx.document().notifyComponentChanged(comp);
  }
  setOutputShape(!out.tool.IsNull() ? out.tool : out.display);
  namedShapes() = out.names;
}

std::string PartFeature::primaryInput() const {
  for (const auto& p : properties())
    if (p.type() == core::PropertyType::RefList && !p.flags().hidden) return p.name();
  for (const auto& p : properties())
    if (p.type() == core::PropertyType::Ref && !p.flags().hidden) return p.name();
  return {};
}

std::string PartFeature::primaryNumeric() const {
  for (const char* n : {"distance", "radius", "thickness", "depth", "diameter", "angle", "distance1", "dx"}) {
    const core::Property* p = property(n);
    if (p && p->isNumeric() && !p->flags().hidden) return n;
  }
  for (const auto& p : properties())
    if ((p.type() == core::PropertyType::Length || p.type() == core::PropertyType::Angle) && !p.flags().hidden) return p.name();
  return {};
}

Operation PartFeature::operation() const {
  const core::Property* p = property("operation");
  if (!p || p->type() != core::PropertyType::Enum) return Operation::NewBody;
  return operationFromIndex(p->enumIndex());
}

void PartFeature::applyDefaultsForNewFeature(const core::Document& doc) {
  core::Property* p = property("operation");
  if (!p || p->type() != core::PropertyType::Enum || p->options().size() != 4) return;
  // Fusion: Join when the active component (or one of its children) already has a body, else New
  // Body. Bodies of other components never take part in an automatic boolean (bodiesInScope()).
  const core::ObjectId scope = component() != core::kInvalidId && doc.component(component()) ? component() : doc.activeComponent();
  bool hasBody = !bodiesInComponentTree(doc, scope, doc.liveBodies()).empty();
  p->setEnum(static_cast<int>(hasBody ? Operation::Join : Operation::NewBody));
}

}  // namespace fabricad::part
