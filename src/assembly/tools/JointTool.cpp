// FabriCAD assembly tools: Joint / As-built Joint implementation.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "assembly/tools/JointTool.h"

#include "assembly/ComponentOps.h"
#include "assembly/tools/ToolCommon.h"
#include "core/AppHost.h"
#include "core/Commands.h"
#include "core/Json.h"
#include "core/Selection.h"
#include "core/ShapeUtils.h"
#include "core/UndoStack.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QWidget>
#include <gp_Quaternion.hxx>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <numbers>

namespace fabricad::assembly {

using core::Document;
using core::kInvalidId;
using core::ObjectId;
using core::PreviewStyle;
using core::ShapeKind;
using core::ShapeRef;
using core::ToolContext;
using core::ToolInfo;
using core::Vec3;

namespace {

constexpr double kSnapPixels = 14.0;

ToolInfo infoFor(bool asBuilt) {
  if (asBuilt)
    return ToolInfo{.id = "assembly.as-built-joint",
                    .name = "As-built Joint",
                    .tooltip = "Joint two components where they are",
                    .icon = "assembly-as-built-joint",
                    .shortcut = "Shift+J",
                    .aliases = {"ASBUILTJOINT", "ASBUILT"},
                    .tab = "SOLID",
                    .group = "ASSEMBLE",
                    .order = 3,
                    .promoted = true};
  return ToolInfo{.id = "assembly.joint",
                  .name = "Joint",
                  .tooltip = "Join two components with a rigid, revolute, slider, cylindrical, pin-slot, planar or ball joint",
                  .icon = "assembly-joint",
                  .shortcut = "J",
                  .aliases = {"JOINT", "J"},
                  .tab = "SOLID",
                  .group = "ASSEMBLE",
                  .order = 2,
                  .promoted = true};
}

PreviewStyle snapStyle(bool best) {
  PreviewStyle s;
  s.color = best ? std::array<float, 4>{1.0f, 1.0f, 1.0f, 1.0f} : std::array<float, 4>{0.85f, 0.85f, 0.85f, 0.9f};
  s.pointSize = best ? 10.0f : 6.0f;
  s.onTop = true;
  return s;
}

PreviewStyle frameStyle(int axis) {
  PreviewStyle s;
  s.color = axis == 0 ? std::array<float, 4>{0.9f, 0.2f, 0.2f, 1.0f}
          : axis == 1 ? std::array<float, 4>{0.2f, 0.75f, 0.2f, 1.0f}
                      : std::array<float, 4>{0.2f, 0.4f, 0.95f, 1.0f};
  s.lineWidth = 2.5f;
  s.shaded = false;
  s.onTop = true;
  return s;
}

std::string describeOrigin(const Document& doc, const JointOrigin& o) {
  if (o.component == kInvalidId) return "not set";
  if (o.mode == SnapMode::ComponentOrigin) return std::format("Component origin of {}", componentLabel(doc, o.component));
  return std::format("{} ({})", componentLabel(doc, o.component), snapModeName(o.mode));
}

/// Middle-elided label text with the full text as tooltip (labels have an Ignored size policy).
void setElided(QLabel* label, const std::string& text) {
  if (!label) return;
  const QString full = core::qs(text);
  const int width = std::max(label->width(), 120);
  label->setText(label->fontMetrics().elidedText(full, Qt::ElideMiddle, width));
  label->setToolTip(full);
}

}  // namespace

JointTool::JointTool(bool asBuilt) : ToolBase(infoFor(asBuilt)), asBuiltTool_(asBuilt), asBuilt_(asBuilt) {
  timer_.setInterval(33);
  connect(&timer_, &QTimer::timeout, this, &JointTool::onAnimationTick);
}

// ---------------------------------------------------------------------------
// lifecycle
// ---------------------------------------------------------------------------

void JointTool::reset(ToolContext& ctx) {
  asBuilt_ = asBuiltTool_;
  editing_ = kInvalidId;
  before_ = {};
  editAccepted_ = false;
  stage_ = Stage::PickA;
  originA_ = originB_ = JointOrigin{};
  compA_ = compB_ = kInvalidId;
  asBuiltAxis_.reset();
  type_ = JointType::Rigid;
  flip_ = false;
  offset_[0] = offset_[1] = offset_[2] = 0.0;
  rotation_ = 0.0;
  values_ = {0, 0, 0};
  limitsEnabled_ = false;
  limitMin_ = limitMax_ = limitRest_ = 0.0;
  placementsBefore_ = snapshotPlacements(*ctx.document);
  placementsChanged_ = false;
  created_ = kInvalidId;
  hoverRef_ = ShapeRef{};
  hoverCandidates_.clear();
  hoverBest_ = -1;
  glyphs_.clear();
  framePreviews_.clear();
  animPhase_ = 0.0;
  typedValue_ = 0.0;
  pickErrorShown_ = false;
}

void JointTool::activate(ToolContext& ctx) {
  ctx_ = &ctx;
  reset(ctx);
  core::ShapeKindMask filter = core::kBrepKinds | ShapeKind::Component | core::kPlaneKinds | core::kAxisKinds | core::kPointKinds;
  // ASM-06: origin geometry stays an explicit pick (browser Origin folder) but is neither
  // auto-shown nor pickable in the viewport, so body faces win over the origin planes.
  const core::ShapeKindMask viewerFilter =
      filter & ~(ShapeKind::OriginPlane | ShapeKind::OriginAxis | ShapeKind::OriginPoint);
  // Edit mode: {"edit", id} (Feature::editToolId dispatch) or a Joint feature in the preselection.
  ObjectId editId = kInvalidId;
  if (ctx.args.size() >= 2 && ctx.args[0] == "edit") {
    unsigned long long v = 0;
    auto [p, ec] = std::from_chars(ctx.args[1].data(), ctx.args[1].data() + ctx.args[1].size(), v);
    if (ec == std::errc() && p == ctx.args[1].data() + ctx.args[1].size()) editId = static_cast<ObjectId>(v);
  }
  if (editId == kInvalidId && ctx.preselection.size() == 1 && ctx.preselection[0].kind == ShapeKind::Feature) {
    if (const core::Feature* f = ctx.document->feature(ctx.preselection[0].owner); f && f->type() == JointFeature::kType)
      editId = f->id();
  }
  if (editId != kInvalidId) {
    auto* f = ctx.document->featureAs<JointFeature>(editId);
    if (!f) {
      ctx.showMessage("select a joint to edit", true);
      ctx.cancel();
      return;
    }
    editing_ = editId;
    before_ = f->toJson();
    loadFromFeature(*ctx.document, *f);
    ctx.selection->setFilter(filter);
    if (ctx.viewer) ctx.viewer->setSelectionFilter(viewerFilter);
    ctx.selection->clear();
    setStage(ctx, Stage::Ready);
    ctx.setPrompt(std::format("Editing {}: change the motion type, offsets or re-pick a snap, then OK", f->name()));
    updatePreview(ctx);
    return;
  }
  // command-line type argument: JOINT REVOLUTE
  for (const std::string& a : ctx.args)
    for (int i = 0; i < kJointTypeCount; ++i)
      if (wordIs(a, jointTypeName(static_cast<JointType>(i)))) type_ = static_cast<JointType>(i);
  ctx.selection->setFilter(filter);
  if (ctx.viewer) ctx.viewer->setSelectionFilter(viewerFilter);
  // preselection: up to two picked entities become the snaps
  std::vector<ShapeRef> pre;
  // ASM-10: a joint snaps to B-rep geometry; a preselected component would silently become its
  // component origin, so only the as-built flavour (which picks whole components) takes those
  for (const ShapeRef& r : ctx.preselection)
    if (r.isBrep() || (asBuilt_ && r.kind == ShapeKind::Component)) pre.push_back(r);
  ctx.selection->clear();
  setStage(ctx, Stage::PickA);
  for (size_t i = 0; i < pre.size() && i < 2; ++i) {
    gp_Pnt p = pre[i].hasPoint ? pre[i].point.toPnt() : gp_Pnt(0, 0, 0);
    pick(ctx, pre[i], p);
  }
}

void JointTool::deactivate(ToolContext& ctx) {
  stopAnimation(nullptr);
  if (editing_ != kInvalidId && !editAccepted_) restoreEdited(ctx);
  else if (placementsChanged_ && created_ == kInvalidId) restorePlacements(ctx);
  editing_ = kInvalidId;
  asBuilt_ = asBuiltTool_;
  ctx_ = nullptr;
  labelA_ = labelB_ = nullptr;
  typeCombo_ = nullptr;
  flipButton_ = nullptr;
  for (auto& e : offsetEdits_) e = nullptr;
  rotationEdit_ = valueEdit_ = nullptr;
  valueLabel_ = nullptr;
  animateButton_ = nullptr;
  limitsCheck_ = nullptr;
  for (auto& e : limitEdits_) e = nullptr;
}

void JointTool::cancel(ToolContext& ctx) {
  stopAnimation(nullptr);
  if (editing_ != kInvalidId) restoreEdited(ctx);
  else if (placementsChanged_) restorePlacements(ctx);
  ctx.cancel();
}

void JointTool::accept(ToolContext& ctx) {
  if (stage_ != Stage::Ready) {
    ctx.showMessage("Select both joint positions first", true);
    return;
  }
  // the animation's leftover value must not be committed: restore the typed one (ASM-05)
  stopAnimation(nullptr);
  Document& doc = *ctx.document;
  if (editing_ != kInvalidId) {
    auto* f = doc.featureAs<JointFeature>(editing_);
    if (!f) {
      ctx.cancel();
      return;
    }
    fillFeature(*f, doc);
    doc.notifyFeatureChanged(editing_);
    doc.recompute();
    QJsonObject after = f->toJson();
    if (after != before_ && ctx.undo)
      ctx.undo->push(std::make_unique<core::ModifyFeatureCommand>(doc, editing_, before_, after, std::format("Edit {}", f->name())));
    editAccepted_ = true;
    created_ = editing_;
    if (f->status() == core::FeatureStatus::Error) ctx.showMessage("Joint has errors: " + f->statusMessage(), true);
    ctx.finish();
    return;
  }
  std::unique_ptr<JointFeature> f = buildFeature(doc);
  // the live preview already moved component 1; the macro records that move plus the feature
  std::map<ObjectId, gp_Trsf> after = snapshotPlacements(doc);
  auto placements = makePlacementsCommand(doc, placementsBefore_, after, "Joint placement");
  auto add = std::make_unique<core::AddFeatureCommand>(doc, std::move(f));
  core::AddFeatureCommand* addRaw = add.get();
  ctx.undo->beginMacro(asBuilt_ ? "As-built joint" : "Joint");
  ctx.undo->push(std::move(placements));
  ctx.undo->push(std::move(add));
  ctx.undo->endMacro();
  created_ = addRaw->featureId();
  placementsChanged_ = false;  // owned by the undo stack now
  const core::Feature* jf = doc.feature(created_);
  if (jf && jf->status() == core::FeatureStatus::Error) ctx.showMessage("Joint has errors: " + jf->statusMessage(), true);
  ctx.finish();
}

void JointTool::clearPickError(ToolContext& ctx) {
  if (!pickErrorShown_) return;
  pickErrorShown_ = false;
  // replaces the red "must differ" line in the status bar
  ctx.showMessage(std::format("Component 2: {}", asBuilt_ ? componentLabel(*ctx.document, compB_)
                                                        : describeOrigin(*ctx.document, originB_)));
}

void JointTool::setStage(ToolContext& ctx, Stage s) {
  stage_ = s;
  clearSnapGlyphs(ctx);
  // a stale hover from the previous stage must not decide the next click (ASM-10)
  hoverRef_ = ShapeRef{};
  hoverCandidates_.clear();
  hoverBest_ = -1;
  switch (s) {
    case Stage::PickA:
      ctx.setPrompt(asBuilt_ ? "As-built joint: select component 1" : "Joint: select a snap position on component 1");
      break;
    case Stage::PickB:
      ctx.setPrompt(asBuilt_ ? "As-built joint: select component 2" : "Joint: select a snap position on component 2");
      break;
    case Stage::PickAxis: ctx.setPrompt("As-built joint: select the joint position (axis / point)"); break;
    case Stage::Ready: ctx.setPrompt("Joint: adjust motion type, offsets and flip, then OK"); break;
  }
  if (ctx.host) ctx.host->setToolAcceptEnabled(s == Stage::Ready);
  syncDialog();
}

// ---------------------------------------------------------------------------
// picking
// ---------------------------------------------------------------------------

void JointTool::pick(ToolContext& ctx, const ShapeRef& ref, const gp_Pnt& worldPoint, SnapMode mode) {
  Document& doc = *ctx.document;
  ObjectId comp = componentOfRef(doc, ref);
  if (comp == kInvalidId) {
    ctx.showMessage("Pick geometry that belongs to a component", true);
    return;
  }
  if (const core::Component* c = doc.component(comp); c && c->children.empty() && displayedBodies(doc, comp).empty()) {
    // ASM-03: an empty component has nothing to joint (and nothing would visibly move)
    ctx.showMessage(std::format("'{}' is empty: it has no bodies to joint", componentLabel(doc, comp)), true);
    pickErrorShown_ = true;
    return;
  }
  if (asBuilt_) {
    if (stage_ == Stage::PickA) {
      compA_ = comp;
      setStage(ctx, Stage::PickB);
    } else if (stage_ == Stage::PickB) {
      if (comp == compA_) {
        ctx.showMessage("Component 2 must differ from component 1", true);
        pickErrorShown_ = true;
        return;
      }
      compB_ = comp;
      clearPickError(ctx);
      if (type_ == JointType::Rigid) {
        asBuiltAxis_ = gp_Ax3().Transformed(doc.worldPlacement(compB_));
        setStage(ctx, Stage::Ready);
        updatePreview(ctx);
      } else {
        setStage(ctx, Stage::PickAxis);
      }
    } else if (stage_ == Stage::PickAxis || stage_ == Stage::Ready) {
      JointOrigin o = makeJointOrigin(doc, ref, worldPoint, mode);
      asBuiltAxis_ = o.toWorld(doc);
      setStage(ctx, Stage::Ready);
      updatePreview(ctx);
    }
    return;
  }
  JointOrigin origin = makeJointOrigin(doc, ref, worldPoint, mode);
  if (!origin.valid && !ref.hasPoint) {
    ctx.showMessage("Cannot derive a joint origin from this pick", true);
    return;
  }
  placeOrigin(ctx, origin);
}

void JointTool::placeOrigin(ToolContext& ctx, const JointOrigin& origin) {
  if (stage_ == Stage::PickA) {
    originA_ = origin;
    setStage(ctx, Stage::PickB);
  } else {
    if (origin.component == originA_.component) {
      ctx.showMessage("Component 2 must differ from component 1", true);
      pickErrorShown_ = true;
      return;
    }
    originB_ = origin;
    clearPickError(ctx);
    setStage(ctx, Stage::Ready);
  }
  updatePreview(ctx);
}

void JointTool::selectionChanged(ToolContext& ctx) {
  if (stage_ == Stage::Ready && !asBuilt_) {
    // a new pick while ready re-picks component 2 (Fusion: click the snap again)
    auto first = ctx.selection->first();
    if (!first) return;
    gp_Pnt p = first->hasPoint ? first->point.toPnt() : gp_Pnt(0, 0, 0);
    stage_ = Stage::PickB;
    pick(ctx, *first, p);
    ctx.selection->clear();
    return;
  }
  auto first = ctx.selection->first();
  if (!first) return;
  gp_Pnt p = first->hasPoint ? first->point.toPnt() : gp_Pnt(0, 0, 0);
  pick(ctx, *first, p);
  ctx.selection->clear();
}

bool JointTool::mouseMove(ToolContext& ctx, const core::ViewerMouseEvent& e) {
  if (asBuilt_ && stage_ != Stage::PickAxis) return false;
  if (stage_ == Stage::Ready && !asBuilt_) return false;
  ShapeRef ref;
  gp_Pnt hit;
  bool have = false;
  if (e.hasPick && (e.pick.isBrep() || e.pick.kind == ShapeKind::Component)) {
    ref = e.pick;
    hit = e.pickPoint.toPnt();
    have = true;
  } else if (ctx.viewer && ctx.viewer->pick(e.x, e.y, core::kBrepKinds, ref, &hit)) {
    have = true;
  }
  if (!have) {
    clearSnapGlyphs(ctx);
    return false;
  }
  showSnapGlyphs(ctx, ref, hit);
  return true;
}

bool JointTool::mousePress(ToolContext& ctx, const core::ViewerMouseEvent& e) {
  if (e.button != core::MouseButton::Left) return false;
  const bool snapStage = !asBuilt_ || stage_ == Stage::PickAxis;
  if (snapStage && hoverBest_ >= 0 && hoverBest_ < static_cast<int>(hoverCandidates_.size()) && hoverRef_.isValid()) {
    const SnapCandidate& c = hoverCandidates_[static_cast<size_t>(hoverBest_)];
    ObjectId comp = componentOfRef(*ctx.document, hoverRef_);
    gp_Pnt world = c.frame.Location().Transformed(ctx.document->worldPlacement(comp));
    pick(ctx, hoverRef_, world, c.mode);
    clearSnapGlyphs(ctx);
    return true;
  }
  if (e.hasPick && (e.pick.isBrep() || e.pick.kind == ShapeKind::Component)) {
    pick(ctx, e.pick, e.pickPoint.toPnt());
    return true;
  }
  return false;  // let the viewer select; selectionChanged() picks it up
}

void JointTool::showSnapGlyphs(ToolContext& ctx, const ShapeRef& ref, const gp_Pnt& worldHit) {
  Document& doc = *ctx.document;
  if (!(ref == hoverRef_)) {
    hoverRef_ = ref;
    hoverCandidates_ = snapCandidates(doc, ref);
  }
  ObjectId comp = componentOfRef(doc, ref);
  gp_Trsf world = comp == kInvalidId ? gp_Trsf() : doc.worldPlacement(comp);
  double tol = ctx.viewer ? kSnapPixels * ctx.viewer->pixelSize(worldHit) : 2.0;
  hoverBest_ = -1;
  double bestD = tol;
  std::vector<Vec3> pts;
  for (size_t i = 0; i < hoverCandidates_.size(); ++i) {
    gp_Pnt p = hoverCandidates_[i].frame.Location().Transformed(world);
    pts.push_back(Vec3::from(p));
    double d = p.Distance(worldHit);
    if (d < bestD) {
      bestD = d;
      hoverBest_ = static_cast<int>(i);
    }
  }
  if (hoverBest_ < 0 && ref.kind == ShapeKind::Face) {
    // no candidate near the cursor: offer the point under the cursor itself
    gp_Pnt local = worldHit.Transformed(world.Inverted());
    if (auto f = snapFrame(doc, ref, SnapMode::PickPoint, local)) {
      hoverCandidates_.push_back({SnapMode::PickPoint, *f, "Point on face"});
      pts.push_back(Vec3::from(f->Location().Transformed(world)));
      hoverBest_ = static_cast<int>(hoverCandidates_.size()) - 1;
      hoverRef_ = ShapeRef{};  // force re-enumeration next move (the pick point changes)
    }
  }
  if (!ctx.viewer) return;
  for (core::PreviewHandle h : glyphs_) ctx.viewer->removePreview(h);
  glyphs_.clear();
  if (!pts.empty()) glyphs_.push_back(ctx.viewer->showPreviewPoints(pts, snapStyle(false)));
  if (hoverBest_ >= 0) {
    const SnapCandidate& c = hoverCandidates_[static_cast<size_t>(hoverBest_)];
    gp_Ax3 wf = c.frame.Transformed(world);
    glyphs_.push_back(ctx.viewer->showPreviewPoints({pts[static_cast<size_t>(hoverBest_)]}, snapStyle(true)));
    double len = 10.0 * (ctx.viewer ? ctx.viewer->pixelSize(worldHit) : 1.0);
    gp_Pnt o = wf.Location();
    glyphs_.push_back(ctx.viewer->showPreviewLines({Vec3::from(o), Vec3::from(o.Translated(gp_Vec(wf.Direction()) * len * 2))}, frameStyle(2)));
    glyphs_.push_back(ctx.viewer->showPreviewText(Vec3::from(o), c.label, PreviewStyle::inference()));
    ctx.viewer->setCursor(core::CursorShape::Pick);
  }
  if (ctx.viewer) ctx.viewer->requestRedraw();
}

void JointTool::clearSnapGlyphs(ToolContext& ctx) {
  if (ctx.viewer)
    for (core::PreviewHandle h : glyphs_) ctx.viewer->removePreview(h);
  glyphs_.clear();
  hoverBest_ = -1;
  hoverRef_ = ShapeRef{};
  hoverCandidates_.clear();
}

// ---------------------------------------------------------------------------
// preview / feature
// ---------------------------------------------------------------------------

std::optional<SolverJoint> JointTool::currentSolverJoint(const Document& doc) const {
  SolverJoint j;
  j.id = kInvalidId;
  j.type = type_;
  j.flip = flip_;
  j.q = values_;
  if (limitsEnabled_ && jointDofCount(type_) > 0) j.q[0] = std::clamp(j.q[0], std::min(limitMin_, limitMax_), std::max(limitMin_, limitMax_));
  j.lockMotion = true;
  gp_Trsf tr;
  tr.SetTranslation(gp_Vec(offset_[0], offset_[1], offset_[2]));
  gp_Trsf rot;
  if (rotation_ != 0.0) rot.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), rotation_ * std::numbers::pi / 180.0);
  j.offset = tr.Multiplied(rot);
  if (asBuilt_) {
    if (compA_ == kInvalidId || compB_ == kInvalidId || !asBuiltAxis_) return std::nullopt;
    j.compA = compA_;
    j.compB = compB_;
    AssemblySolver::asBuiltFrames(j, doc.worldPlacement(compA_), doc.worldPlacement(compB_), *asBuiltAxis_);
    j.offset = gp_Trsf();
    return j;
  }
  if (originA_.component == kInvalidId || originB_.component == kInvalidId) return std::nullopt;
  j.compA = originA_.component;
  j.compB = originB_.component;
  j.frameA = originA_.frame;
  j.frameB = originB_.frame;
  return j;
}

void JointTool::fillFeature(JointFeature& f, const Document& doc) const {
  f.setJointType(type_);
  f.setFlip(flip_);
  f.setOffset(offset_[0], offset_[1], offset_[2], rotation_);
  f.setLimits(limitsEnabled_, limitMin_, limitMax_, limitRest_);
  if (asBuilt_) {
    std::optional<SolverJoint> sj = currentSolverJoint(doc);
    JointOrigin a, b;
    a.component = compA_;
    b.component = compB_;
    if (sj) {
      a.frame = sj->frameA;
      b.frame = sj->frameB;
    }
    a.valid = b.valid = true;
    a.ref = ShapeRef::component(compA_);
    b.ref = ShapeRef::component(compB_);
    f.setOrigins(a, b);
    f.setAsBuilt(true);
    f.setOffset(0, 0, 0, 0);
  } else {
    f.setAsBuilt(false);
    f.setOrigins(originA_, originB_);
  }
  f.setValues(values_);
}

std::unique_ptr<JointFeature> JointTool::buildFeature(const Document& doc) const {
  auto f = std::make_unique<JointFeature>();
  fillFeature(*f, doc);
  return f;
}

void JointTool::loadFromFeature(const Document& doc, const JointFeature& f) {
  asBuilt_ = f.isAsBuilt();
  type_ = f.jointType();
  flip_ = f.flip();
  offset_[0] = f.number("offsetX");
  offset_[1] = f.number("offsetY");
  offset_[2] = f.number("offsetZ");
  rotation_ = f.number("rotation");
  values_ = f.values();
  typedValue_ = values_[0];
  limitsEnabled_ = f.limitsEnabled();
  limitMin_ = f.limitMin();
  limitMax_ = f.limitMax();
  limitRest_ = f.limitRest();
  originA_ = f.originA();
  originB_ = f.originB();
  compA_ = f.componentA();
  compB_ = f.componentB();
  if (asBuilt_) asBuiltAxis_ = originB_.toWorld(doc);
}

void JointTool::previewEdit(ToolContext& ctx) {
  Document& doc = *ctx.document;
  auto* f = doc.featureAs<JointFeature>(editing_);
  if (!f) return;
  if (ctx.viewer) {
    for (core::PreviewHandle h : framePreviews_) ctx.viewer->removePreview(h);
    framePreviews_.clear();
  }
  if (stage_ == Stage::Ready) {
    fillFeature(*f, doc);
    doc.notifyFeatureChanged(editing_);
    doc.recompute();
    if (f->status() == core::FeatureStatus::Error) ctx.showMessage(f->statusMessage(), true);
    if (ctx.viewer) {
      gp_Ax3 wf = gp_Ax3().Transformed(f->worldFrame(doc));
      gp_Pnt o = wf.Location();
      const double len = 8.0;
      framePreviews_.push_back(ctx.viewer->showPreviewLines({Vec3::from(o), Vec3::from(o.Translated(gp_Vec(wf.XDirection()) * len))}, frameStyle(0)));
      framePreviews_.push_back(ctx.viewer->showPreviewLines({Vec3::from(o), Vec3::from(o.Translated(gp_Vec(wf.YDirection()) * len))}, frameStyle(1)));
      framePreviews_.push_back(ctx.viewer->showPreviewLines({Vec3::from(o), Vec3::from(o.Translated(gp_Vec(wf.Direction()) * len * 1.5))}, frameStyle(2)));
    }
  }
  if (ctx.viewer) ctx.viewer->requestRedraw();
  syncDialog();
}

void JointTool::restoreEdited(ToolContext& ctx) {
  Document& doc = *ctx.document;
  if (auto* f = doc.featureAs<JointFeature>(editing_); f && f->toJson() != before_) {
    f->fromJson(before_);
    doc.notifyFeatureChanged(editing_);
    doc.recompute();
  }
  placementsChanged_ = false;
}

void JointTool::updatePreview(ToolContext& ctx) {
  if (editing_ != kInvalidId) {
    previewEdit(ctx);
    return;
  }
  Document& doc = *ctx.document;
  if (ctx.viewer) {
    for (core::PreviewHandle h : framePreviews_) ctx.viewer->removePreview(h);
    framePreviews_.clear();
  }
  // frame glyph at the snap(s)
  auto drawFrame = [&](const gp_Ax3& wf, double len) {
    if (!ctx.viewer) return;
    gp_Pnt o = wf.Location();
    framePreviews_.push_back(ctx.viewer->showPreviewLines({Vec3::from(o), Vec3::from(o.Translated(gp_Vec(wf.XDirection()) * len))}, frameStyle(0)));
    framePreviews_.push_back(ctx.viewer->showPreviewLines({Vec3::from(o), Vec3::from(o.Translated(gp_Vec(wf.YDirection()) * len))}, frameStyle(1)));
    framePreviews_.push_back(ctx.viewer->showPreviewLines({Vec3::from(o), Vec3::from(o.Translated(gp_Vec(wf.Direction()) * len * 1.5))}, frameStyle(2)));
  };
  double len = 8.0;
  if (!asBuilt_ && originA_.component != kInvalidId && stage_ == Stage::PickB) drawFrame(originA_.toWorld(doc), len);
  if (stage_ != Stage::Ready) {
    if (ctx.viewer) ctx.viewer->requestRedraw();
    return;
  }
  std::optional<SolverJoint> sj = currentSolverJoint(doc);
  if (!sj) return;
  if (!asBuilt_) {
    // live placement: component 1 (and everything jointed to it) jumps onto component 2
    for (const auto& [id, p] : placementsBefore_)
      if (core::Component* c = doc.component(id); c && !(c->placement.TranslationPart().IsEqual(p.TranslationPart(), 1e-12) &&
                                                      c->placement.GetRotation().IsEqual(p.GetRotation()))) {
        c->placement = p;
        doc.notifyComponentChanged(id);
      }
    SolverResult r = solveAssembly(doc, true, {*sj});
    placementsChanged_ = true;
    if (!r.ok()) ctx.showMessage("Joint conflicts with existing joints", true);
  }
  gp_Trsf jw = AssemblySolver::jointWorldFrame(doc.worldPlacement(sj->compB), *sj);
  drawFrame(gp_Ax3().Transformed(jw), len);
  if (ctx.viewer) {
    double size = std::clamp(core::bboxDiagonal(componentWorldShape(doc, sj->compA)) * 0.15, 2.0, 40.0);
    PreviewStyle gs = PreviewStyle::inference();
    gs.color = {1.0f, 1.0f, 1.0f, 1.0f};
    gs.dashed = false;
    gs.lineWidth = 2.0f;
    framePreviews_.push_back(ctx.viewer->showPreviewShape(JointFeature::makeGlyph(type_, jw, size), gs));
    ctx.viewer->requestRedraw();
  }
  syncDialog();
}

void JointTool::restorePlacements(ToolContext& ctx) {
  Document& doc = *ctx.document;
  for (const auto& [id, p] : placementsBefore_)
    if (core::Component* c = doc.component(id)) {
      c->placement = p;
      doc.notifyComponentChanged(id);
    }
  placementsChanged_ = false;
}

void JointTool::documentChanged(ToolContext& ctx) {
  (void)ctx;  // placements are ours; nothing to do
}

// ---------------------------------------------------------------------------
// parameters
// ---------------------------------------------------------------------------

void JointTool::setJointType(ToolContext& ctx, JointType t) {
  stopAnimation(nullptr);
  type_ = t;
  values_ = {0, 0, 0};
  typedValue_ = 0.0;
  pickErrorShown_ = false;
  if (asBuilt_ && stage_ == Stage::Ready && t != JointType::Rigid && asBuiltAxis_ && compB_ != kInvalidId) {
    // a rigid as-built joint had no axis pick: ask for one now
    setStage(ctx, Stage::PickAxis);
    return;
  }
  updatePreview(ctx);
  syncDialog();
}

void JointTool::setFlip(ToolContext& ctx, bool f) {
  flip_ = f;
  updatePreview(ctx);
}

void JointTool::setOffset(ToolContext& ctx, double x, double y, double z, double rotationDeg) {
  offset_[0] = x;
  offset_[1] = y;
  offset_[2] = z;
  rotation_ = rotationDeg;
  updatePreview(ctx);
}

void JointTool::setValue(ToolContext& ctx, double primary) {
  const auto& dofs = jointDofs(type_);
  if (!dofs.empty() && dofs.front().kind == DofKind::Rotation && !limitsEnabled_) primary = wrapAngle180(primary);
  values_[0] = primary;
  typedValue_ = primary;
  updatePreview(ctx);
}

void JointTool::setLimits(bool enabled, double min, double max, double rest) {
  limitsEnabled_ = enabled;
  limitMin_ = min;
  limitMax_ = max;
  limitRest_ = rest;
  if (ctx_) updatePreview(*ctx_);
}

void JointTool::setAnimating(ToolContext& ctx, bool on) {
  if (on && stage_ == Stage::Ready && jointDofCount(type_) > 0) {
    if (!timer_.isActive()) typedValue_ = values_[0];
    animPhase_ = 0.0;
    timer_.start();
  } else {
    stopAnimation(&ctx);
  }
  if (animateButton_) {
    const QSignalBlocker block(animateButton_);
    animateButton_->setChecked(timer_.isActive());
  }
}

void JointTool::stopAnimation(ToolContext* ctx) {
  if (!timer_.isActive()) return;
  timer_.stop();
  values_[0] = typedValue_;
  if (!ctx) ctx = ctx_;
  if (ctx) updatePreview(*ctx);
  if (animateButton_) {
    const QSignalBlocker block(animateButton_);
    animateButton_->setChecked(false);
  }
}

void JointTool::onAnimationTick() {
  if (!ctx_) {
    timer_.stop();
    return;
  }
  animPhase_ += 0.02;
  const auto& dofs = jointDofs(type_);
  if (dofs.empty()) return;
  double v;
  if (dofs.front().kind == DofKind::Rotation) {
    v = limitsEnabled_ ? (limitMin_ + (limitMax_ - limitMin_) * (0.5 - 0.5 * std::cos(animPhase_ * std::numbers::pi)))
                       : wrapAngle180(animPhase_ * 180.0);
  } else {
    double amp = 20.0;
    v = limitsEnabled_ ? (limitMin_ + (limitMax_ - limitMin_) * (0.5 - 0.5 * std::cos(animPhase_ * std::numbers::pi)))
                       : amp * std::sin(animPhase_ * std::numbers::pi);
  }
  values_[0] = v;
  updatePreview(*ctx_);
}

bool JointTool::commandLineInput(ToolContext& ctx, const std::string& text) {
  if (text.empty()) return false;
  for (int i = 0; i < kJointTypeCount; ++i)
    if (wordIs(text, jointTypeName(static_cast<JointType>(i)))) {
      setJointType(ctx, static_cast<JointType>(i));
      return true;
    }
  if (wordIs(text, "FLIP")) {
    setFlip(ctx, !flip_);
    return true;
  }
  if (wordIs(text, "ANIMATE")) {
    setAnimating(ctx, !timer_.isActive());
    return true;
  }
  double v;
  if (parseNumber(text, v) && stage_ == Stage::Ready) {
    if (jointDofCount(type_) > 0) setValue(ctx, v);
    else setOffset(ctx, offset_[0], offset_[1], v, rotation_);
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// dialog
// ---------------------------------------------------------------------------

QWidget* JointTool::createDialog(ToolContext& ctx, QWidget* parent) {
  Document* doc = ctx.document;
  auto* w = new QWidget(parent);
  auto* form = new QFormLayout(w);

  auto makeSelectRow = [&](const char* title, QLabel*& label, Stage stage) {
    auto* row = new QWidget(w);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    label = new QLabel("not set", row);
    // ASM-10: a long label must not push the Select button out of the dialog
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    label->setMinimumWidth(40);
    label->setTextFormat(Qt::PlainText);
    auto* btn = new QPushButton("Select", row);
    btn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    QObject::connect(btn, &QPushButton::clicked, this, [this, stage] {
      if (ctx_) setStage(*ctx_, stage);
    });
    h->addWidget(label, 1);
    h->addWidget(btn);
    form->addRow(title, row);
  };
  makeSelectRow("Component 1", labelA_, Stage::PickA);
  makeSelectRow("Component 2", labelB_, Stage::PickB);

  typeCombo_ = new QComboBox(w);
  for (const std::string& n : jointTypeNames()) typeCombo_->addItem(core::qs(n));
  typeCombo_->setCurrentIndex(static_cast<int>(type_));
  QObject::connect(typeCombo_, &QComboBox::currentIndexChanged, this, [this](int i) {
    if (ctx_ && !syncing_) setJointType(*ctx_, static_cast<JointType>(i));
  });
  form->addRow("Motion", typeCombo_);

  if (!asBuilt_) {
    flipButton_ = new QPushButton("Flip", w);
    flipButton_->setCheckable(true);
    QObject::connect(flipButton_, &QPushButton::toggled, this, [this](bool on) {
      if (ctx_ && !syncing_) setFlip(*ctx_, on);
    });
    form->addRow("Align", flipButton_);
    const char* names[3] = {"Offset X", "Offset Y", "Offset Z"};
    for (int i = 0; i < 3; ++i) {
      offsetEdits_[i] = new ExpressionEdit(doc, core::Dimension::Length(), w);
      offsetEdits_[i]->setValue(offset_[i]);
      QObject::connect(offsetEdits_[i], &ExpressionEdit::valueChanged, this, [this](double) {
        if (ctx_ && !syncing_) setOffset(*ctx_, offsetEdits_[0]->value(), offsetEdits_[1]->value(), offsetEdits_[2]->value(), rotationEdit_->value());
      });
      form->addRow(names[i], offsetEdits_[i]);
    }
    rotationEdit_ = new ExpressionEdit(doc, core::Dimension::Angle(), w);
    rotationEdit_->setValue(rotation_);
    QObject::connect(rotationEdit_, &ExpressionEdit::valueChanged, this, [this](double) {
      if (ctx_ && !syncing_) setOffset(*ctx_, offsetEdits_[0]->value(), offsetEdits_[1]->value(), offsetEdits_[2]->value(), rotationEdit_->value());
    });
    form->addRow("Rotation", rotationEdit_);
  }

  valueLabel_ = new QLabel("Angle", w);
  valueEdit_ = new ExpressionEdit(doc, core::Dimension::Angle(), w);
  valueEdit_->setValue(0.0);
  QObject::connect(valueEdit_, &ExpressionEdit::valueChanged, this, [this](double v) {
    if (ctx_ && !syncing_) setValue(*ctx_, v);
  });
  form->addRow(valueLabel_, valueEdit_);

  animateButton_ = new QPushButton("Animate", w);
  animateButton_->setCheckable(true);
  QObject::connect(animateButton_, &QPushButton::toggled, this, [this](bool on) {
    if (ctx_ && !syncing_) setAnimating(*ctx_, on);
  });
  form->addRow("Preview", animateButton_);

  auto* limits = new QGroupBox("Joint limits", w);
  auto* lf = new QFormLayout(limits);
  limitsCheck_ = new QCheckBox("Enable", limits);
  lf->addRow(limitsCheck_);
  const char* lnames[3] = {"Minimum", "Maximum", "Rest"};
  for (int i = 0; i < 3; ++i) {
    limitEdits_[i] = new ExpressionEdit(doc, core::Dimension::Scalar(), limits);
    limitEdits_[i]->setValue(0.0);
    lf->addRow(lnames[i], limitEdits_[i]);
    QObject::connect(limitEdits_[i], &ExpressionEdit::valueChanged, this, [this](double) {
      if (!syncing_) setLimits(limitsCheck_->isChecked(), limitEdits_[0]->value(), limitEdits_[1]->value(), limitEdits_[2]->value());
    });
  }
  QObject::connect(limitsCheck_, &QCheckBox::toggled, this, [this](bool on) {
    if (!syncing_) setLimits(on, limitEdits_[0]->value(), limitEdits_[1]->value(), limitEdits_[2]->value());
  });
  form->addRow(limits);
  syncDialog();
  return w;
}

void JointTool::syncDialog() {
  if (!typeCombo_ || !ctx_) return;
  syncing_ = true;
  const Document& doc = *ctx_->document;
  if (asBuilt_) {
    setElided(labelA_, compA_ == kInvalidId ? std::string("not set") : componentLabel(doc, compA_));
    setElided(labelB_, compB_ == kInvalidId ? std::string("not set") : componentLabel(doc, compB_));
  } else {
    setElided(labelA_, describeOrigin(doc, originA_));
    setElided(labelB_, describeOrigin(doc, originB_));
  }
  typeCombo_->setCurrentIndex(static_cast<int>(type_));
  if (flipButton_) flipButton_->setChecked(flip_);
  const auto& dofs = jointDofs(type_);
  bool hasDof = !dofs.empty();
  valueLabel_->setVisible(hasDof);
  valueEdit_->setVisible(hasDof);
  animateButton_->setEnabled(hasDof && stage_ == Stage::Ready);
  if (hasDof) {
    valueLabel_->setText(core::qs(dofs.front().label));
    // while animating the field keeps the typed value (the animation is preview only)
    double shown = timer_.isActive() ? typedValue_ : values_[0];
    if (std::abs(shown - valueEdit_->value()) > 1e-9) valueEdit_->setValue(shown);
  }
  limitsCheck_->setChecked(limitsEnabled_);
  limitsCheck_->parentWidget()->setEnabled(hasDof);
  syncing_ = false;
}

}  // namespace fabricad::assembly
