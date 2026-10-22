// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/tools/FeatureTool.h"

#include "core/AppHost.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Log.h"
#include "core/Selection.h"
#include "core/ShapeUtils.h"
#include "core/UndoStack.h"
#include "core/ViewerContext.h"
#include "part/Naming.h"
#include "part/features/Extrude.h"
#include "part/features/Fillet.h"
#include "part/features/Primitives.h"
#include "part/features/Shell.h"
#include "part/PartUtils.h"
#include "part/tools/FeatureManipulators.h"
#include "part/tools/FeatureValueBox.h"
#include "part/tools/PropertyPanel.h"

#include <BRepAlgoAPI_Common.hxx>
#include <TopLoc_Location.hxx>
#include <QApplication>
#include <QWidget>
#include <Standard_Failure.hxx>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <map>

namespace fabricad::part {

namespace {

std::map<std::string, std::string>& typeToolMap() {
  static std::map<std::string, std::string> m;
  return m;
}

core::PreviewStyle styleFor(Operation op) {
  switch (op) {
    case Operation::Join: return core::PreviewStyle::join();
    case Operation::Cut: return core::PreviewStyle::cut();
    case Operation::Intersect: {
      core::PreviewStyle s = core::PreviewStyle::join();
      s.color = {0.9f, 0.75f, 0.2f, 0.5f};
      return s;
    }
    default: return core::PreviewStyle::newBody();
  }
}

bool parseId(const std::string& s, core::ObjectId& out) {
  if (s.empty()) return false;
  unsigned long long v = 0;
  auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
  if (ec != std::errc() || p != s.data() + s.size()) return false;
  out = static_cast<core::ObjectId>(v);
  return true;
}

}  // namespace

std::string toolIdForFeatureType(const std::string& type) {
  auto it = typeToolMap().find(type);
  return it == typeToolMap().end() ? std::string() : it->second;
}

void registerFeatureToolId(const std::string& type, const std::string& toolId) { typeToolMap()[type] = toolId; }

// ---------------------------------------------------------------------------
// FeatureTool
// ---------------------------------------------------------------------------

FeatureTool::FeatureTool(core::ToolInfo info, std::string featureType, Creator creator)
    : ToolBase(std::move(info)), featureType_(std::move(featureType)), creator_(std::move(creator)) {
  registerFeatureToolId(featureType_, info_.id);
}

FeatureTool::~FeatureTool() = default;

PartFeature* FeatureTool::feature() const { return editing_ ? editing_ : working_.get(); }

std::unique_ptr<PartFeature> FeatureTool::createFeature(core::ToolContext& ctx) {
  (void)ctx;
  return creator_ ? creator_() : nullptr;
}

std::string FeatureTool::primaryNumeric() const {
  const PartFeature* f = feature();
  return f ? f->primaryNumeric() : std::string();
}

void FeatureTool::activate(core::ToolContext& ctx) {
  working_.reset();
  editing_ = nullptr;
  before_ = {};
  activeInput_.clear();
  previews_.clear();
  valueBox_.reset();
  manipulators_.clear();
  manipulatorDragging_ = false;
  operationAuto_ = true;
  flipAuto_ = false;
  flipUser_ = false;
  outcome_ = {};
  valid_ = false;
  message_.clear();
  if (!ctx.document) {
    ctx.cancel();
    return;
  }
  // Edit mode: args {"edit", id} or a Feature ref of our type in the preselection.
  core::ObjectId editId = core::kInvalidId;
  if (ctx.args.size() >= 2 && ctx.args[0] == "edit") parseId(ctx.args[1], editId);
  if (editId == core::kInvalidId && ctx.preselection.size() == 1 && ctx.preselection[0].kind == core::ShapeKind::Feature) {
    if (const core::Feature* f = ctx.document->feature(ctx.preselection[0].owner); f && f->type() == featureType_)
      editId = f->id();
  }
  if (editId != core::kInvalidId) {
    auto* f = ctx.document->featureAs<PartFeature>(editId);
    if (!f || f->type() != featureType_) {
      ctx.showMessage("feature cannot be edited with this tool", true);
      ctx.cancel();
      return;
    }
    editing_ = f;
    before_ = f->toJson();
    f->updatePropertyFlags();
    operationAuto_ = false;  // editing: the operation was chosen when the feature was made
    activeInput_ = f->primaryInput();
    // Roll the timeline marker just after the feature so every keystroke recomputes this
    // feature only; the downstream history is replayed once on OK / Cancel.
    rollbackToFeature(ctx);
    applyFilter(ctx);
    highlightRefs(ctx);
    ctx.setPrompt(std::format("Editing {}", f->name()));
    updatePreview(ctx);
    return;
  }
  std::unique_ptr<PartFeature> f = createFeature(ctx);
  if (!f) {
    // subclasses may wait for a selection (Press Pull)
    ctx.setPrompt("Select geometry");
    return;
  }
  startWith(ctx, std::move(f));
}

void FeatureTool::startWith(core::ToolContext& ctx, std::unique_ptr<PartFeature> f) {
  working_ = std::move(f);
  working_->setName(ctx.document->uniqueFeatureName(working_->type()));
  // the component the feature will belong to (Document::addFeature sets the same): previews and
  // automatic booleans are scoped to it exactly as the committed feature will be
  working_->setComponent(ctx.document->activeComponent());
  working_->applyDefaultsForNewFeature(*ctx.document);
  working_->updatePropertyFlags();
  activeInput_ = working_->primaryInput();
  applyPreselection(ctx, ctx.preselection);
  // command-line argument: the primary numeric ("EXTRUDE 10") or the primary text ("IMPORT part.step")
  if (!ctx.args.empty() && ctx.args[0] != "edit") {
    std::string num = primaryNumeric();
    if (!num.empty()) setNumeric(ctx, num, ctx.args[0]);
    else if (std::string text = working_->primaryText(); !text.empty()) working_->prop(text).setString(ctx.args[0]);
  }
  if (ctx.selection) ctx.selection->clear();
  applyFilter(ctx);
  highlightRefs(ctx);
  const core::Property* p = working_->property(activeInput_);
  ctx.setPrompt(p ? std::format("Select {}", p->label()) : std::string("Set the parameters"));
  updatePreview(ctx);
}

void FeatureTool::applyPreselection(core::ToolContext& ctx, const std::vector<core::ShapeRef>& refs) {
  PartFeature* f = feature();
  if (!f || refs.empty()) return;
  for (core::ShapeRef r : refs) {
    // the primary input first, then any other ref property accepting the kind
    bool placed = false;
    if (core::Property* p = f->property(activeInput_); p && core::hasKind(p->refFilter(), r.kind))
      placed = addRefToInput(ctx, p->name(), r);
    if (!placed) {
      for (auto& p : f->properties()) {
        if (p.type() != core::PropertyType::Ref && p.type() != core::PropertyType::RefList) continue;
        if (p.flags().hidden || !core::hasKind(p.refFilter(), r.kind)) continue;
        if (p.type() == core::PropertyType::Ref && p.ref().isValid()) continue;
        if (addRefToInput(ctx, p.name(), r)) break;
      }
    }
  }
  advanceActiveInput(ctx);
}

bool FeatureTool::addRefToInput(core::ToolContext& ctx, const std::string& propName, core::ShapeRef ref) {
  PartFeature* f = feature();
  core::Property* p = f ? f->property(propName) : nullptr;
  if (!p || !core::hasKind(p->refFilter(), ref.kind)) return false;
  if (ref.isBrep() && ref.kind != core::ShapeKind::Body && ctx.document) annotateRef(*ctx.document, ref);
  if (p->type() == core::PropertyType::Ref) {
    p->setRef(ref);
    return true;
  }
  if (p->type() != core::PropertyType::RefList) return false;
  auto& refs = p->refs();
  if (f->refsDistinguishedByPoint(propName) && ref.hasPoint && ref.isBrep() && ctx.document) {
    // pick points arrive in world coordinates; features work in the body's local frame
    if (const core::Body* b = ctx.document->body(ref.owner))
      ref.point = core::Vec3::from(ref.point.toPnt().Transformed(ctx.document->worldPlacement(b->component).Inverted()));
    auto same = std::find_if(refs.begin(), refs.end(), [&](const core::ShapeRef& r) {
      return r == ref && r.hasPoint && r.point.toPnt().Distance(ref.point.toPnt()) < 1e-6;
    });
    if (same != refs.end()) refs.erase(same);
    else refs.push_back(ref);
    return true;
  }
  auto it = std::find(refs.begin(), refs.end(), ref);
  if (it != refs.end()) {
    refs.erase(it);  // picking a selected item again deselects it (Fusion)
    return true;
  }
  if (p->maxRefs() > 0 && static_cast<int>(refs.size()) >= p->maxRefs()) refs.erase(refs.begin());
  refs.push_back(ref);
  return true;
}

void FeatureTool::advanceActiveInput(core::ToolContext& ctx) {
  PartFeature* f = feature();
  if (!f) return;
  core::Property* cur = f->property(activeInput_);
  if (!cur || cur->type() != core::PropertyType::Ref || !cur->ref().isValid()) return;
  // a single-ref input is filled: move to the next empty, visible ref input
  for (auto& p : f->properties()) {
    if (p.flags().hidden) continue;
    if (p.type() == core::PropertyType::Ref && !p.ref().isValid()) {
      setActiveInput(ctx, p.name());
      return;
    }
    if (p.type() == core::PropertyType::RefList && p.refs().empty() && p.name() != cur->name()) {
      setActiveInput(ctx, p.name());
      return;
    }
  }
}

void FeatureTool::setActiveInput(core::ToolContext& ctx, const std::string& propName) {
  activeInput_ = propName;
  if (panel_) panel_->setActiveInput(propName);
  applyFilter(ctx);
  if (PartFeature* f = feature()) {
    const core::Property* p = f->property(propName);
    if (p) ctx.setPrompt(std::format("Select {}", p->label()));
  }
}

void FeatureTool::applyFilter(core::ToolContext& ctx) {
  PartFeature* f = feature();
  core::ShapeKindMask m = core::kAllKinds;
  if (f) {
    if (const core::Property* p = f->property(activeInput_)) m = p->refFilter();
  }
  if (ctx.selection) ctx.selection->setFilter(m);
  if (ctx.viewer) ctx.viewer->setSelectionFilter(m);
}

void FeatureTool::highlightRefs(core::ToolContext& ctx) {
  if (!ctx.viewer) return;
  std::vector<core::ShapeRef> refs;
  if (PartFeature* f = feature()) {
    for (const auto& p : f->properties()) {
      if (p.type() == core::PropertyType::Ref && p.ref().isValid()) refs.push_back(p.ref());
      if (p.type() == core::PropertyType::RefList) refs.insert(refs.end(), p.refs().begin(), p.refs().end());
    }
  }
  ctx.viewer->setHighlighted(refs);
}

void FeatureTool::rollbackToFeature(core::ToolContext& ctx) {
  savedRollback_ = -1;
  if (!editing_ || !ctx.document) return;
  core::Document& doc = *ctx.document;
  int idx = doc.indexOf(editing_->id());
  if (idx < 0 || idx + 1 >= doc.rollbackIndex()) return;
  savedRollback_ = doc.rollbackIndex();
  doc.setRollbackIndex(idx + 1);
}

void FeatureTool::restoreRollback(core::ToolContext& ctx) {
  if (savedRollback_ < 0) return;
  int target = savedRollback_;
  savedRollback_ = -1;
  if (ctx.document) ctx.document->setRollbackIndex(target);  // forward: recomputes the rest
}

void FeatureTool::clearPreviews(core::ToolContext& ctx) {
  if (ctx.viewer)
    for (auto h : previews_) ctx.viewer->removePreview(h);
  previews_.clear();
}

// ---------------------------------------------------------------------------
// manipulators
// ---------------------------------------------------------------------------

const FeatureTool::ManipulatorLink* FeatureTool::manipulatorFor(const std::string& property) const {
  for (const auto& l : manipulators_)
    if (l.property == property) return &l;
  return nullptr;
}

void FeatureTool::clearManipulators(core::ToolContext& ctx) {
  if (ctx.viewer)
    for (const auto& l : manipulators_) ctx.viewer->removeManipulator(l.id);
  manipulators_.clear();
  manipulatorDragging_ = false;
}

void FeatureTool::syncManipulators(core::ToolContext& ctx, core::RecomputeContext& rctx) {
  PartFeature* f = feature();
  if (!f || !ctx.viewer) return;
  core::IViewerContext& viewer = *ctx.viewer;
  PixelSizeFn pixelSize = [&viewer](const gp_Pnt& at) { return viewer.pixelSize(at); };
  std::vector<ManipulatorBinding> bindings = manipulatorsFor(*f, rctx, pixelSize);
  core::ToolContext* pctx = ctx.manager ? ctx.manager->activeContext() : nullptr;
  if (!pctx) pctx = &ctx;
  std::vector<ManipulatorLink> next;
  for (ManipulatorBinding& b : bindings) {
    ManipulatorLink link;
    link.property = b.property;
    link.scale = b.scale;
    link.triad = b.spec.kind == core::ManipulatorKind::Triad;
    if (const ManipulatorLink* old = manipulatorFor(b.property); old && viewer.manipulators().has(old->id)) {
      link.id = old->id;
      // a triad reports its drag relative to the frame where the drag began: keep it there
      // until the drag ends (it then follows the moved bodies)
      if (!(link.triad && manipulatorDragging_)) viewer.updateManipulator(link.id, b.spec);
    } else {
      link.id = viewer.showManipulator(b.spec, [this, pctx](const core::ManipulatorEvent& e) { onManipulatorEvent(*pctx, e); });
    }
    next.push_back(link);
  }
  for (const auto& old : manipulators_) {
    bool kept = false;
    for (const auto& n : next)
      if (n.id == old.id) kept = true;
    if (!kept) viewer.removeManipulator(old.id);
  }
  manipulators_ = std::move(next);
}

void FeatureTool::onManipulatorEvent(core::ToolContext& ctx, const core::ManipulatorEvent& e) {
  using core::ManipulatorPhase;
  PartFeature* f = feature();
  if (!f) return;
  const ManipulatorLink* link = nullptr;
  for (const auto& l : manipulators_)
    if (l.id == e.id) link = &l;
  if (!link) return;
  static constexpr const char* kTriadProps[6] = {"dx", "dy", "dz", "rx", "ry", "rz"};
  switch (e.phase) {
    case ManipulatorPhase::Hover:
    case ManipulatorPhase::Leave: return;
    case ManipulatorPhase::DragBegin:
      manipulatorDragging_ = true;
      if (ctx.selection) ctx.selection->setEnabled(false);
      if (link->triad)
        for (int i = 0; i < 6; ++i) {
          const core::Property* tp = f->property(kTriadProps[i]);
          triadStart_[i] = tp && tp->isNumeric() ? tp->toDouble() : 0.0;
        }
      return;
    case ManipulatorPhase::Drag:
    case ManipulatorPhase::DragEnd:
    case ManipulatorPhase::DragCancel: break;
  }
  bool changed = false;
  auto setValue = [&](core::Property* p, double value) {
    if (p && p->isNumeric() && (std::abs(p->toDouble() - value) > 1e-9 || !p->expression().empty())) {
      p->setDouble(value);  // a drag replaces an expression with the dragged number (Fusion)
      changed = true;
    }
  };
  if (link->triad) {
    // Move triad (UX-32): the handle picks the property; values are start-of-drag + amount
    std::array<double, 6> v = triadStart_;
    std::array<bool, 6> touched{};
    if (e.phase != ManipulatorPhase::DragCancel) {
      using core::ManipulatorHandle;
      auto add = [&](int i, double d) {
        v[i] += d;
        touched[i] = true;
      };
      switch (e.handle) {
        case ManipulatorHandle::MoveX: add(0, e.value); break;
        case ManipulatorHandle::MoveY: add(1, e.value); break;
        case ManipulatorHandle::MoveZ: add(2, e.value); break;
        case ManipulatorHandle::RotX: add(3, e.value); break;
        case ManipulatorHandle::RotY: add(4, e.value); break;
        case ManipulatorHandle::RotZ: add(5, e.value); break;
        case ManipulatorHandle::PlaneXY:
        case ManipulatorHandle::PlaneYZ:
        case ManipulatorHandle::PlaneZX: {
          const gp_XYZ t = e.transform.TranslationPart();
          add(0, t.X());
          add(1, t.Y());
          add(2, t.Z());
          break;
        }
        default: break;
      }
    } else {
      touched.fill(true);
    }
    for (int i = 0; i < 6; ++i)
      if (touched[i]) setValue(f->property(kTriadProps[i]), v[i]);
  } else {
    const double scale = std::abs(link->scale) > 1e-12 ? link->scale : 1.0;
    setValue(f->property(link->property), e.value / scale);
  }
  const bool ending = e.phase != ManipulatorPhase::Drag;
  if (ending) {
    manipulatorDragging_ = false;
    if (ctx.selection) ctx.selection->setEnabled(true);
  }
  if (changed || ending) {
    if (ending && !editing_) autoOperation(ctx);
    updatePreview(ctx);
    if (ending && panel_) panel_->refresh();
  }
}

void FeatureTool::autoOperation(core::ToolContext& ctx) {
  PartFeature* f = feature();
  if (!f || !operationAuto_ || !ctx.document) return;
  // Only Extrude: its arrow may be dragged into existing material (Offset Face reads Join/Cut
  // from the sign of its distance itself).
  if (f->type() != ExtrudeFeature::kType) return;
  core::Property* op = f->property("operation");
  if (!op || op->type() != core::PropertyType::Enum) return;
  const Operation cur = operationFromIndex(op->toInt());
  if (cur != Operation::Join && cur != Operation::Cut) return;
  if (outcome_.tool.IsNull()) return;
  // "Into material" = more than half of the tool solid lies inside existing bodies.
  const double fraction = materialFraction(ctx, outcome_.tool);
  if (fraction < 0) return;
  const bool intoMaterial = fraction > 0.5;
  if (intoMaterial && cur == Operation::Join) {
    op->setEnum("Cut");
    if (panel_) panel_->rebuild();
  } else if (!intoMaterial && cur == Operation::Cut) {
    op->setEnum("Join");
    if (panel_) panel_->rebuild();
  }
}

double FeatureTool::materialFraction(core::ToolContext& ctx, const TopoDS_Shape& tool) const {
  const PartFeature* f = feature();
  if (!f || !ctx.document || tool.IsNull()) return -1.0;
  const double total = core::volume(tool);
  if (!(total > 1e-9)) return -1.0;
  double inside = 0.0;
  for (core::ObjectId id : bodiesInComponentTree(*ctx.document, f->component(), ctx.document->liveBodies(true))) {
    const TopoDS_Shape& body = ctx.document->bodyShape(id);
    if (body.IsNull() || !shapesTouch(tool, body)) continue;
    try {
      BRepAlgoAPI_Common common(tool, body);
      if (common.IsDone()) inside += core::volume(common.Shape());
    } catch (const Standard_Failure&) {
      // ignore: the body simply does not count as material
    }
  }
  return inside / total;
}

bool FeatureTool::autoPrimitiveOperation(core::RecomputeContext& rctx) {
  PartFeature* f = feature();
  if (!f || editing_ || !operationAuto_ || !dynamic_cast<PrimitiveFeature*>(f)) return false;
  core::Property* op = f->property("operation");
  if (!op || op->type() != core::PropertyType::Enum) return false;
  const Operation cur = operationFromIndex(op->toInt());
  if (cur != Operation::Join && cur != Operation::NewBody) return false;
  if (const core::Property* t = f->property("targets"); t && !t->refs().empty()) return false;
  if (outcome_.tool.IsNull()) return false;
  bool touches = false;
  for (core::ObjectId id : bodiesInScope(rctx)) {
    const TopoDS_Shape& body = rctx.bodyShape(id);
    if (!body.IsNull() && shapesTouch(body, outcome_.tool)) {
      touches = true;
      break;
    }
  }
  const Operation want = touches ? Operation::Join : Operation::NewBody;
  if (want == cur) return false;
  op->setEnum(static_cast<int>(want));
  return true;
}

void FeatureTool::autoFlipForCut(core::ToolContext& ctx) {
  PartFeature* f = feature();
  if (!f || editing_ || flipUser_ || f->type() != ExtrudeFeature::kType) return;
  if (f->operation() != Operation::Cut) return;
  if (f->enumIndex("direction") != ExtrudeFeature::OneSide || f->enumIndex("extentType") != ExtrudeFeature::Distance) return;
  core::Property* flip = f->property("flip");
  if (!flip || flip->toBool()) return;
  const double before = materialFraction(ctx, outcome_.tool);
  if (before < 0 || before > 0.01) return;  // already (partly) in material, or no tool yet
  flip->setBool(true);
  updatePreview(ctx);
  const double after = materialFraction(ctx, outcome_.tool);
  if (after > 0.01) {
    flipAuto_ = true;
    return;
  }
  flip->setBool(false);  // flipping does not help either: keep the user's direction
  updatePreview(ctx);
}

void FeatureTool::undoAutoFlip(core::ToolContext& ctx) {
  PartFeature* f = feature();
  if (!f || !flipAuto_ || flipUser_ || f->operation() == Operation::Cut) return;
  flipAuto_ = false;
  if (core::Property* flip = f->property("flip"); flip && flip->toBool()) {
    flip->setBool(false);
    updatePreview(ctx);
  }
}

void FeatureTool::updatePreview(core::ToolContext& ctx) {
  PartFeature* f = feature();
  if (!f || !ctx.document) return;
  f->updatePropertyFlags();
  clearPreviews(ctx);
  valid_ = false;
  message_.clear();
  outcome_ = {};
  core::Document& doc = *ctx.document;
  if (editing_) {
    doc.notifyFeatureChanged(editing_->id());
    doc.recompute();
    valid_ = editing_->status() != core::FeatureStatus::Error;
    message_ = editing_->statusMessage();
    if (ctx.viewer && valid_) {
      // UX-34: live preview of the bodies the edited feature produces with the current values
      // (the recompute changed them in place; draw them as a preview so the result follows the
      // dialog / arrow even before the viewer resyncs its body presentations)
      const int idx = doc.indexOf(editing_->id());
      const auto& before = doc.bodyStatesAfter(idx - 1);
      const core::PreviewStyle style = styleFor(Operation::Join);
      for (const auto& [id, shape] : doc.bodyStates()) {
        if (shape.IsNull()) continue;
        auto it = before.find(id);
        if (it != before.end() && it->second.IsEqual(shape)) continue;  // untouched by this feature
        const core::Body* b = doc.body(id);
        const gp_Trsf world = b ? doc.worldPlacement(b->component) : gp_Trsf();
        const TopoDS_Shape shown = world.Form() == gp_Identity ? shape : shape.Moved(TopLoc_Location(world));
        previews_.push_back(ctx.viewer->showPreviewShape(shown, style));
      }
    }
    if (ctx.viewer) {
      // anchor the gizmos on the state before the edited feature (what its inputs refer to)
      const int idx = doc.indexOf(editing_->id());
      core::RecomputeContext rctx(doc, *editing_, doc.bodyStatesAfter(idx - 1));
      syncManipulators(ctx, rctx);
    }
  } else {
    auto run = [&]() {
      auto c = std::make_unique<core::RecomputeContext>(doc, *working_, doc.bodyStatesAfter(doc.rollbackIndex() - 1));
      try {
        working_->compute(*c, outcome_);
      } catch (const Standard_Failure& e) {
        c->setError(std::format("OCCT: {}", e.GetMessageString() ? e.GetMessageString() : "failure"));
      } catch (const std::exception& e) {
        c->setError(e.what());
      }
      return c;
    };
    std::unique_ptr<core::RecomputeContext> rc = run();
    if (autoPrimitiveOperation(*rc)) {
      // ASM-CORE-13: New Body <-> Join switched: recompute with the new operation
      outcome_ = {};
      rc = run();
    }
    core::RecomputeContext& rctx = *rc;
    valid_ = !rctx.hasError();
    message_ = rctx.hasError() ? rctx.errorMessage() : (rctx.hasWarning() ? rctx.warnings().front() : std::string());
    if (valid_ && ctx.viewer) {
      for (const auto& pv : outcome_.previews) previews_.push_back(ctx.viewer->showPreviewShape(pv.shape, styleFor(pv.operation)));
    }
    if (ctx.viewer) syncManipulators(ctx, rctx);
  }
  // PART-02: a dialog field holding an expression that does not evaluate blocks OK
  if (panel_ && panel_->hasInvalid()) {
    valid_ = false;
    message_ = panel_->invalidMessage();
  }
  if (ctx.host) ctx.host->setToolAcceptEnabled(valid_);
  if (panel_) {
    inPreview_ = true;
    rebuildPanelIfNeeded();
    inPreview_ = false;
    panel_->refresh();
    panel_->setMessage(message_, !valid_);
  }
  if (!valueBox_) valueBox_ = std::make_unique<FeatureValueBox>(ctx, *this);
  else valueBox_->update(ctx);
  if (ctx.viewer) ctx.viewer->requestRedraw();
}

void FeatureTool::rebuildPanelIfNeeded() {
  if (!panel_ || !panel_->needsRebuild()) return;
  QString focusName;
  if (QWidget* fw = QApplication::focusWidget(); fw && panel_->isAncestorOf(fw)) focusName = fw->objectName();
  panel_->rebuild();
  if (!focusName.isEmpty())
    if (QWidget* w = panel_->findChild<QWidget*>(focusName)) w->setFocus(Qt::OtherFocusReason);
}

bool FeatureTool::keyPress(core::ToolContext& ctx, const core::ViewerKeyEvent& e) {
  // UX-33: keys typed into a field of the dialog belong to that field, not to the on-canvas
  // value box of the primary numeric (Enter / Esc still reach the router: OK / Cancel).
  if (panel_ && e.key != Qt::Key_Return && e.key != Qt::Key_Enter && e.key != Qt::Key_Escape) {
    QWidget* focus = QApplication::focusWidget();
    if (!focus && panel_->window()) focus = panel_->window()->focusWidget();
    if (focus && (focus == panel_ || panel_->isAncestorOf(focus))) return false;
  }
  return valueBox_ && valueBox_->keyPress(ctx, e);
}

bool FeatureTool::setNumeric(core::ToolContext& ctx, const std::string& propName, const std::string& text) {
  PartFeature* f = feature();
  core::Property* p = f ? f->property(propName) : nullptr;
  if (!p || !p->isNumeric() || !ctx.document) return false;
  core::EvalResult r = ctx.document->evaluate(text, p->dimension());
  if (!r.ok) {
    ctx.showMessage(r.error, true);
    return false;
  }
  bool plain = !text.empty();
  for (char c : text)
    if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '+')) plain = false;
  if (plain) p->setDouble(r.value.value);
  else {
    p->setExpression(text);
    p->setEvaluatedValue(r.value.value);
  }
  updatePreview(ctx);
  return true;
}

void FeatureTool::selectionChanged(core::ToolContext& ctx) {
  if (inSelectionUpdate_ || !ctx.selection) return;
  PartFeature* f = feature();
  if (!f) return;
  std::vector<core::ShapeRef> items = ctx.selection->items();
  if (items.empty()) return;
  inSelectionUpdate_ = true;
  bool changed = false;
  for (const auto& r : items) {
    if (!activeInput_.empty() && addRefToInput(ctx, activeInput_, r)) changed = true;
  }
  ctx.selection->clear();
  inSelectionUpdate_ = false;
  if (!changed) return;
  if (PartFeature* ff = feature(); ff) {
    core::Property* p = ff->property(activeInput_);
    if (p && p->type() == core::PropertyType::Ref) advanceActiveInput(ctx);
  }
  highlightRefs(ctx);
  updatePreview(ctx);
}

void FeatureTool::documentChanged(core::ToolContext& ctx) { (void)ctx; }

bool FeatureTool::commandLineInput(core::ToolContext& ctx, const std::string& text) {
  auto f_primaryText = [this] {
    PartFeature* f = feature();
    return f ? f->primaryText() : std::string();
  };
  std::string t = text;
  while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.pop_back();
  while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front()))) t.erase(t.begin());
  if (t.empty()) return false;
  // "name=value" sets a property, a bare value goes to the primary numeric input
  auto eq = t.find('=');
  if (eq != std::string::npos) {
    std::string name = t.substr(0, eq), value = t.substr(eq + 1);
    PartFeature* f = feature();
    core::Property* p = f ? f->property(name) : nullptr;
    if (!p) return false;
    if (p->isNumeric()) return setNumeric(ctx, name, value);
    if (p->type() == core::PropertyType::Enum) {
      p->setEnum(value);
      if (name == "operation") operationAuto_ = false;  // the user chose: stop switching Join/Cut automatically
      updatePreview(ctx);
      if (panel_) panel_->rebuild();
      return true;
    }
    if (p->type() == core::PropertyType::Bool) {
      p->setBool(value == "1" || value == "true" || value == "yes");
      updatePreview(ctx);
      return true;
    }
    if (p->type() == core::PropertyType::String) {
      p->setString(value);
      updatePreview(ctx);
      if (panel_) panel_->rebuild();
      return true;
    }
    return false;
  }
  std::string num = primaryNumeric();
  if (!num.empty()) return setNumeric(ctx, num, t);
  // no numeric input: a bare value is the primary text (IMPORT: the file path)
  std::string textProp = f_primaryText();
  if (textProp.empty()) return false;
  feature()->prop(textProp).setString(t);
  updatePreview(ctx);
  if (panel_) panel_->rebuild();
  return true;
}

QWidget* FeatureTool::createDialog(core::ToolContext& ctx, QWidget* parent) {
  PartFeature* f = feature();
  if (!f || !ctx.document) return nullptr;
  auto* panel = new PropertyPanel(*ctx.document, *f, parent);
  panel_ = panel;
  panel->setValueChangeDelay(editing_ ? 150 : 0);
  panel->setActiveInput(activeInput_);
  core::ToolContext* pctx = ctx.manager ? ctx.manager->activeContext() : nullptr;
  if (!pctx) pctx = &ctx;
  QObject::connect(panel, &PropertyPanel::valueChanged, panel, [this, pctx](const std::string& name) {
    PartFeature* ff = feature();
    if (!ff) return;
    const core::Property* p = ff->property(name);
    bool structural = p && p->type() == core::PropertyType::Enum;
    if (name == "operation") operationAuto_ = false;  // the user chose: stop switching Join/Cut automatically
    if (name == "flip") {
      flipUser_ = true;  // a manual Flip wins over the automatic one
      flipAuto_ = false;
    }
    updatePreview(*pctx);
    if (name == "operation") {
      autoFlipForCut(*pctx);  // PART-04: Cut on a face sketch goes into the body
      undoAutoFlip(*pctx);
    }
    if (structural && panel_) panel_->rebuild();
  });
  QObject::connect(panel, &PropertyPanel::activeInputChanged, panel, [this, pctx](const std::string& name) {
    activeInput_ = name;
    applyFilter(*pctx);
    if (PartFeature* ff = feature()) {
      if (const core::Property* p = ff->property(name)) pctx->setPrompt(std::format("Select {}", p->label()));
    }
  });
  QObject::connect(panel, &PropertyPanel::refsCleared, panel, [this, pctx](const std::string&) { highlightRefs(*pctx); });
  QObject::connect(panel, &PropertyPanel::validityChanged, panel, [this, pctx] {
    if (!inPreview_) updatePreview(*pctx);
  });
  panel->setMessage(message_, !valid_);
  return panel;
}

void FeatureTool::accept(core::ToolContext& ctx) {
  if (!ctx.document) {
    ctx.cancel();
    return;
  }
  if (panel_ && panel_->hasInvalid()) {
    ctx.showMessage(panel_->invalidMessage(), true);
    return;
  }
  if (!valid_) {
    ctx.showMessage(message_.empty() ? "the feature is not valid yet" : message_, true);
    return;
  }
  clearPreviews(ctx);
  // Features owning extra document objects (component pattern occurrences) sync them first,
  // in the same undo step as the feature command.
  PartFeature* committing = editing_ ? editing_ : working_.get();
  const bool prepare = committing && committing->wantsCommitPreparation();
  if (editing_) {
    std::string name = editing_->name();
    if (prepare && ctx.undo) ctx.undo->beginMacro(std::format("Edit {}", name));
    if (prepare) editing_->prepareCommit(*ctx.document, ctx.undo);
    QJsonObject after = editing_->toJson();
    core::ObjectId id = editing_->id();
    editing_ = nullptr;
    restoreRollback(ctx);  // replay the downstream features with the new definition
    if (ctx.undo && after != before_)
      ctx.undo->push(std::make_unique<core::ModifyFeatureCommand>(*ctx.document, id, before_, after, std::format("Edit {}", name)));
    else if (prepare) {
      ctx.document->notifyFeatureChanged(id);
      ctx.document->recompute();
    }
    if (prepare && ctx.undo) ctx.undo->endMacro();
    ctx.finish();
    return;
  }
  if (!working_) {
    ctx.cancel();
    return;
  }
  std::string text = std::format("{}", working_->name());
  // UX-42: a sketch consumed by a new Extrude / Revolve / Sweep / Loft is hidden afterwards
  // (Fusion). This is the browser-eye state, so the user can show it again; not on Edit.
  std::vector<core::ObjectId> consumedSketches;
  if (const std::string& t = working_->type(); t == "Extrude" || t == "Revolve" || t == "Sweep" || t == "Loft") {
    for (const auto& p : working_->properties()) {
      std::vector<core::ShapeRef> refs;
      if (p.type() == core::PropertyType::Ref) refs.push_back(p.ref());
      else if (p.type() == core::PropertyType::RefList) refs = p.refs();
      for (const core::ShapeRef& r : refs)
        if (r.kind == core::ShapeKind::Profile && ctx.document->feature(r.owner) &&
            std::find(consumedSketches.begin(), consumedSketches.end(), r.owner) == consumedSketches.end())
          consumedSketches.push_back(r.owner);
    }
  }
  if (prepare && ctx.undo) ctx.undo->beginMacro(text);
  if (prepare) working_->prepareCommit(*ctx.document, ctx.undo);
  auto cmd = std::make_unique<core::AddFeatureCommand>(*ctx.document, std::move(working_));
  cmd->setText(text);
  if (ctx.undo) ctx.undo->push(std::move(cmd));
  else {
    // no undo stack (scripting): apply directly
    cmd->redo();
  }
  if (prepare && ctx.undo) ctx.undo->endMacro();
  if (ctx.viewer)
    for (core::ObjectId sk : consumedSketches) ctx.viewer->setFeatureHidden(sk, true);
  ctx.finish();
}

void FeatureTool::cancel(core::ToolContext& ctx) {
  clearPreviews(ctx);
  if (editing_ && ctx.document) {
    editing_->fromJson(before_);
    ctx.document->notifyFeatureChanged(editing_->id());
    ctx.document->recompute();
    editing_ = nullptr;
    restoreRollback(ctx);
  }
  working_.reset();
  ctx.cancel();
}

void FeatureTool::deactivate(core::ToolContext& ctx) {
  valueBox_.reset();
  clearPreviews(ctx);
  clearManipulators(ctx);
  if (editing_ && ctx.document) {
    // deactivated without accept (another tool started): behave like cancel
    editing_->fromJson(before_);
    ctx.document->notifyFeatureChanged(editing_->id());
    ctx.document->recompute();
    editing_ = nullptr;
    restoreRollback(ctx);
  }
  editing_ = nullptr;
  working_.reset();
  panel_ = nullptr;
  if (ctx.viewer) ctx.viewer->setHighlighted({});
}

// ---------------------------------------------------------------------------
// PressPullTool
// ---------------------------------------------------------------------------

PressPullTool::PressPullTool(core::ToolInfo info) : FeatureTool(std::move(info), "PressPull", nullptr) {}

std::unique_ptr<PartFeature> PressPullTool::featureFor(const std::vector<core::ShapeRef>& refs) const {
  for (const auto& r : refs) {
    switch (r.kind) {
      case core::ShapeKind::Face: return std::make_unique<OffsetFaceFeature>();
      case core::ShapeKind::Edge: return std::make_unique<FilletFeature>();
      case core::ShapeKind::Profile: return std::make_unique<ExtrudeFeature>();
      default: break;
    }
  }
  return nullptr;
}

void PressPullTool::activate(core::ToolContext& ctx) {
  working_.reset();
  editing_ = nullptr;
  if (!ctx.document) {
    ctx.cancel();
    return;
  }
  std::unique_ptr<PartFeature> f = featureFor(ctx.preselection);
  if (f) {
    featureType_ = f->type();
    startWith(ctx, std::move(f));
    return;
  }
  core::ShapeKindMask m = core::ShapeKind::Face | core::ShapeKind::Edge | core::ShapeKind::Profile;
  if (ctx.selection) ctx.selection->setFilter(m);
  if (ctx.viewer) ctx.viewer->setSelectionFilter(m);
  ctx.setPrompt("Select a face to offset, an edge to fillet or a profile to extrude");
}

void PressPullTool::selectionChanged(core::ToolContext& ctx) {
  if (feature()) {
    FeatureTool::selectionChanged(ctx);
    return;
  }
  if (!ctx.selection || ctx.selection->isEmpty()) return;
  std::vector<core::ShapeRef> items = ctx.selection->items();
  std::unique_ptr<PartFeature> f = featureFor(items);
  if (!f) return;
  featureType_ = f->type();
  ctx.preselection = items;
  startWith(ctx, std::move(f));
  if (ctx.host) ctx.host->refreshToolDialog();
}

// ---------------------------------------------------------------------------
// EditFeatureTool
// ---------------------------------------------------------------------------

void EditFeatureTool::activate(core::ToolContext& ctx) {
  if (!ctx.document || !ctx.manager) {
    ctx.cancel();
    return;
  }
  core::ObjectId id = core::kInvalidId;
  if (!ctx.args.empty()) parseId(ctx.args[0], id);
  if (id == core::kInvalidId && ctx.preselection.size() == 1 && ctx.preselection[0].kind == core::ShapeKind::Feature)
    id = ctx.preselection[0].owner;
  const core::Feature* f = ctx.document->feature(id);
  if (!f) {
    ctx.showMessage("select a feature to edit", true);
    ctx.cancel();
    return;
  }
  std::string toolId = toolIdForFeatureType(f->type());
  if (toolId.empty()) {
    ctx.showMessage(std::format("'{}' has no edit dialog", f->name()), true);
    ctx.cancel();
    return;
  }
  // Running another tool finishes this one and activates the editor.
  ctx.manager->run(toolId, {"edit", std::to_string(id)});
}

}  // namespace fabricad::part
