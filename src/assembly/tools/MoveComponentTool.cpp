// FabriCAD assembly tools: Move/Rotate component implementation.
//
// The triad is a viewer manipulator (core::ManipulatorKind::Triad): the viewer draws it, picks
// it ahead of selection and reports drags through onTriadEvent(); the tool turns the delta
// transform into component placements or a joint value and keeps the dialog in sync.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "assembly/tools/MoveComponentTool.h"

#include "assembly/ComponentOps.h"
#include "assembly/JointFeature.h"
#include "assembly/tools/ToolCommon.h"
#include "core/AppHost.h"
#include "core/Commands.h"
#include "core/Json.h"
#include "core/Selection.h"
#include "core/ShapeUtils.h"
#include "core/UndoStack.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QWidget>
#include <gp_Ax1.hxx>
#include <gp_Quaternion.hxx>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <numbers>

namespace fabricad::assembly {

using core::Document;
using core::kInvalidId;
using core::ObjectId;
using core::ShapeKind;
using core::ShapeRef;
using core::ToolContext;
using core::ToolInfo;
using core::Vec3;

namespace {

unsigned handlesForJoint(JointType t) {
  switch (t) {
    case JointType::Rigid: return 0;
    case JointType::Revolute: return Triad::kRotZ;
    case JointType::Slider: return Triad::kMoveZ;
    case JointType::Cylindrical: return Triad::kRotZ | Triad::kMoveZ;
    case JointType::PinSlot: return Triad::kRotZ | Triad::kMoveX;
    case JointType::Planar: return Triad::kMoveX | Triad::kMoveY | Triad::kRotZ | Triad::kPlaneXY;
    case JointType::Ball: return Triad::kRotations;
  }
  return Triad::kAll;
}

}  // namespace

MoveComponentTool::MoveComponentTool()
    : ToolBase(ToolInfo{.id = "assembly.move",
                        .name = "Move Component",
                        .tooltip = "Move or rotate components with the triad (jointed components move along their joint)",
                        .icon = "assembly-move",
                        .aliases = {"MOVECOMPONENT", "MOVECOMP", "MC"},
                        .tab = "SOLID",
                        .group = "ASSEMBLE",
                        .order = 6}) {}

void MoveComponentTool::activate(ToolContext& ctx) {
  ctx_ = &ctx;
  components_.clear();
  startWorld_.clear();
  placementsBefore_ = snapshotPlacements(*ctx.document);
  delta_ = gp_Trsf();
  dragging_ = false;
  activeHandle_ = core::ManipulatorHandle::None;
  triadId_ = core::kInvalidManipulator;
  joint_ = nullptr;
  for (double& v : values_) v = 0.0;
  ctx.selection->setFilter(core::kBrepKinds | ShapeKind::Component);
  if (ctx.viewer) ctx.viewer->setSelectionFilter(ctx.selection->filter());
  std::vector<ObjectId> pre = componentsOf(*ctx.document, ctx.preselection);
  if (!pre.empty()) setComponents(ctx, pre);
  else ctx.setPrompt("Move: select a component");
  if (ctx.host) ctx.host->setToolAcceptEnabled(!components_.empty());
}

void MoveComponentTool::deactivate(ToolContext& ctx) {
  if (ctx.viewer && triadId_ != core::kInvalidManipulator) ctx.viewer->removeManipulator(triadId_);
  triadId_ = core::kInvalidManipulator;
  ctx_ = nullptr;
  for (auto& e : edits_) e = nullptr;
  for (auto& e : pivotEdits_) e = nullptr;
  targetLabel_ = nullptr;
}

void MoveComponentTool::setComponents(ToolContext& ctx, const std::vector<ObjectId>& ids) {
  Document& doc = *ctx.document;
  components_.clear();
  // ASM-09: a grounded component stays put (Fusion): warn and leave it out
  std::vector<ObjectId> grounded;
  for (ObjectId id : ids) {
    const core::Component* c = doc.component(id);
    if (!c || id == core::kRootComponentId) continue;
    if (c->grounded) grounded.push_back(id);
    else components_.push_back(id);
  }
  if (!grounded.empty()) {
    // ASM-09: warn before moving a grounded component (scripts take the default: move it)
    std::string names;
    for (ObjectId id : grounded) names += (names.empty() ? "'" : ", '") + componentLabel(doc, id) + "'";
    const bool one = grounded.size() == 1;
    const bool move = !ctx.host || ctx.host->askYesNo("Move grounded component",
                                                      std::format("{} {} grounded. Move {} anyway?", names,
                                                                  one ? "is" : "are", one ? "it" : "them"),
                                                      true);
    const std::string msg = move ? std::format("Moving grounded {}", names)
                                 : std::format("{} {} grounded: not moved (Unground first)", names, one ? "is" : "are");
    if (ctx.host) ctx.host->showMessage(msg, core::MessageLevel::Warning);
    else ctx.showMessage(msg, !move);
    if (move) components_.insert(components_.end(), grounded.begin(), grounded.end());
  }
  // drop components whose ancestor is also selected (they move with it)
  std::vector<ObjectId> roots;
  for (ObjectId c : components_) {
    bool nested = false;
    for (ObjectId o : components_)
      if (o != c && isAncestorOf(doc, o, c)) nested = true;
    if (!nested) roots.push_back(c);
  }
  components_ = roots;
  startWorld_.clear();
  for (ObjectId c : components_) startWorld_[c] = doc.worldPlacement(c);
  joint_ = nullptr;
  if (components_.size() == 1) {
    // a jointed component moves along its joint's DOF
    for (const auto& f : doc.features()) {
      auto* jf = dynamic_cast<JointFeature*>(f.get());
      if (!jf || !doc.isFeatureActive(jf->id()) || jf->isSuppressed()) continue;
      if (jf->componentA() == components_.front()) {
        joint_ = jf;
        break;
      }
    }
    if (!joint_)
      for (const auto& f : doc.features()) {
        auto* jf = dynamic_cast<JointFeature*>(f.get());
        if (jf && doc.isFeatureActive(jf->id()) && !jf->isSuppressed() && jf->componentB() == components_.front() &&
            !doc.component(jf->componentA())->grounded) {
          // component B of a joint: moving it drags A along (whole chain) - treat as free move
          break;
        }
      }
  }
  if (joint_) {
    jointBefore_ = joint_->toJson();
    jointValueStart_ = joint_->primaryValue();
  }
  setupTriad(ctx);
  if (ctx.host) ctx.host->setToolAcceptEnabled(!components_.empty());
  if (components_.empty()) {
    ctx.setPrompt("Move: select a component");
  } else if (joint_) {
    if (jointDofCount(joint_->jointType()) == 0) {
      ctx.setPrompt(std::format("'{}' is rigidly jointed: free move will be undone by the joint", componentLabel(doc, components_.front())));
    } else {
      ctx.setPrompt(std::format("Drag the {} handle to drive '{}'", jointDofs(joint_->jointType()).front().label, joint_->name()));
    }
  } else {
    ctx.setPrompt("Drag the triad to move/rotate, or type distances; OK to keep");
  }
  syncDialog();
}

void MoveComponentTool::setupTriad(ToolContext& ctx) {
  Document& doc = *ctx.document;
  if (components_.empty()) {
    if (ctx.viewer && triadId_ != core::kInvalidManipulator) ctx.viewer->removeManipulator(triadId_);
    triadId_ = core::kInvalidManipulator;
    return;
  }
  gp_Ax3 frame;
  unsigned handles = Triad::kAll;
  double size = 20.0;
  if (joint_) {
    gp_Trsf jw = joint_->worldFrame(doc);
    frame = gp_Ax3().Transformed(jw);
    handles = handlesForJoint(joint_->jointType());
    if (handles == 0) handles = Triad::kAll;  // rigid: allow a free move (user was warned)
  } else {
    auto c = componentCenter(doc, components_.front());
    gp_Pnt pivot = c ? *c : gp_Pnt(0, 0, 0).Transformed(doc.worldPlacement(components_.front()));
    frame = gp_Ax3(pivot, gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
  }
  TopoDS_Shape shape = componentWorldShape(doc, components_.front());
  if (!shape.IsNull()) size = std::clamp(core::bboxDiagonal(shape) * 0.35, 5.0, 500.0);
  if (ctx.viewer) size = std::max(size, 60.0 * ctx.viewer->pixelSize(frame.Location()));
  baseFrame_ = frame;
  dragBase_ = delta_;
  triad_ = Triad(frame, size);
  triad_.setEnabledHandles(handles);
  moveTriadToCurrent(ctx);
}

core::ManipulatorSpec MoveComponentTool::triadSpec() const {
  core::ManipulatorSpec s;
  s.kind = core::ManipulatorKind::Triad;
  s.frame = triad_.frame();
  s.size = triad_.size();
  s.handles = triad_.enabledHandles();  // Triad::HandleMask and core::ManipulatorHandleMask share the bit layout
  s.label = joint_ ? joint_->name() : std::string("Move");
  s.tag = 1;
  return s;
}

void MoveComponentTool::showTriad(ToolContext& ctx) {
  if (!ctx.viewer) return;
  core::ManipulatorSpec spec = triadSpec();
  if (triadId_ != core::kInvalidManipulator && ctx.viewer->manipulators().has(triadId_)) {
    ctx.viewer->updateManipulator(triadId_, spec);
    return;
  }
  core::ToolContext* pctx = ctx.manager ? ctx.manager->activeContext() : nullptr;
  if (!pctx) pctx = &ctx;
  triadId_ = ctx.viewer->showManipulator(spec, [this, pctx](const core::ManipulatorEvent& e) { onTriadEvent(*pctx, e); });
}

void MoveComponentTool::moveTriadToCurrent(ToolContext& ctx) {
  if (components_.empty()) return;
  if (joint_) triad_.setFrame(gp_Ax3().Transformed(joint_->worldFrame(*ctx.document)));
  else triad_.setFrame(baseFrame_.Transformed(delta_));
  showTriad(ctx);
}

void MoveComponentTool::selectionChanged(ToolContext& ctx) {
  if (dragging_) return;
  std::vector<ObjectId> comps = componentsOf(*ctx.document, ctx.selection->items());
  if (!comps.empty()) {
    // switching the target: keep what was moved so far (already applied), restart the delta
    delta_ = gp_Trsf();
    for (double& v : values_) v = 0.0;
    setComponents(ctx, comps);
  }
}

void MoveComponentTool::onTriadEvent(ToolContext& ctx, const core::ManipulatorEvent& e) {
  using core::ManipulatorPhase;
  if (components_.empty()) return;
  switch (e.phase) {
    case ManipulatorPhase::Hover:
    case ManipulatorPhase::Leave: return;
    case ManipulatorPhase::DragBegin:
      dragging_ = true;
      activeHandle_ = e.handle;
      dragBase_ = delta_;
      ctx.selection->setEnabled(false);
      return;
    case ManipulatorPhase::Drag:
      // the triad sits at the start-of-drag frame, so the transform is relative to dragBase_
      activeHandle_ = e.handle;
      applyDelta(ctx, e.transform.Multiplied(dragBase_));
      return;
    case ManipulatorPhase::DragEnd:
    case ManipulatorPhase::DragCancel:
      if (e.phase == ManipulatorPhase::DragEnd) applyDelta(ctx, e.transform.Multiplied(dragBase_));
      else applyDelta(ctx, dragBase_);
      dragging_ = false;
      activeHandle_ = core::ManipulatorHandle::None;
      ctx.selection->setEnabled(true);
      // move the triad with the component and remember the accumulated delta
      dragBase_ = delta_;
      moveTriadToCurrent(ctx);
      return;
  }
}

void MoveComponentTool::applyDelta(ToolContext& ctx, const gp_Trsf& delta, const double* fields) {
  Document& doc = *ctx.document;
  delta_ = delta;
  if (joint_ && jointDofCount(joint_->jointType()) > 0) {
    ObjectId a = components_.front();
    applyJointDrive(ctx, delta.Multiplied(startWorld_.at(a)));
  } else {
    for (ObjectId c : components_) setWorldPlacement(doc, c, delta.Multiplied(startWorld_.at(c)));
  }
  if (fields) {
    // typed values are the source of truth (ASM-04): never re-derive them from the transform
    for (int i = 0; i < 6; ++i) values_[i] = fields[i];
    syncDialog();
    return;
  }
  // dialog numbers: translation + Euler angles of the delta. The delta is T(t)*T(p)*R*T(-p) about
  // the pivot p (see setTransform), so the translation field is TranslationPart - (p - R*p).
  const gp_XYZ p = baseFrame_.Location().XYZ();
  const gp_XYZ rp = delta.GetRotation().Multiply(gp_Vec(p)).XYZ();
  gp_XYZ t = delta.TranslationPart() - (p - rp);
  values_[0] = t.X();
  values_[1] = t.Y();
  values_[2] = t.Z();
  double a, b, c;
  delta.GetRotation().GetEulerAngles(gp_Extrinsic_XYZ, a, b, c);
  values_[3] = a * 180.0 / std::numbers::pi;
  values_[4] = b * 180.0 / std::numbers::pi;
  values_[5] = c * 180.0 / std::numbers::pi;
  syncDialog();
}

void MoveComponentTool::applyJointDrive(ToolContext& ctx, const gp_Trsf& newWorldA) {
  Document& doc = *ctx.document;
  SolverJoint sj = joint_->toSolverJoint(doc, nullptr, nullptr);
  JointValues q = AssemblySolver::currentValues(sj, newWorldA, doc.worldPlacement(sj.compB));
  JointValues cur = joint_->values();
  // only the DOF the handle drives changes; keep the others
  const core::ManipulatorHandle h = activeHandle_;
  const auto& dofs = jointDofs(joint_->jointType());
  for (size_t i = 0; i < dofs.size(); ++i) {
    bool rot = dofs[i].kind == DofKind::Rotation;
    bool matches = h == core::ManipulatorHandle::None || (rot && core::isRotationHandle(h)) ||
                   (!rot && (core::isMoveHandle(h) || core::isPlaneHandle(h)));
    if (matches) cur[i] = q[i];
  }
  if (joint_->limitsEnabled()) cur[0] = joint_->clampToLimits(cur[0]);
  joint_->setValues(cur);
  doc.notifyFeatureChanged(joint_->id());
  doc.recompute();
}

void MoveComponentTool::setTransform(ToolContext& ctx, double x, double y, double z, double rx, double ry, double rz) {
  // The dialog values are absolute (relative to the placements at activation): rotate about the pivot where the
  // triad started, then translate. Rotating about the moved triad after translating gives the same composite.
  gp_Pnt pivot = baseFrame_.Location();
  gp_Trsf rot;
  gp_Quaternion q;
  q.SetEulerAngles(gp_Extrinsic_XYZ, rx * std::numbers::pi / 180.0, ry * std::numbers::pi / 180.0, rz * std::numbers::pi / 180.0);
  rot.SetRotation(q);
  // rotate about the pivot: T(p) R T(-p)
  gp_Trsf toPivot, fromPivot;
  toPivot.SetTranslation(gp_Vec(gp_Pnt(0, 0, 0), pivot));
  fromPivot.SetTranslation(gp_Vec(pivot, gp_Pnt(0, 0, 0)));
  gp_Trsf tr;
  tr.SetTranslation(gp_Vec(x, y, z));
  const double fields[6] = {x, y, z, rx, ry, rz};
  applyDelta(ctx, tr.Multiplied(toPivot).Multiplied(rot).Multiplied(fromPivot), fields);
  dragBase_ = delta_;
  moveTriadToCurrent(ctx);
}

bool MoveComponentTool::setPivot(ToolContext& ctx, const gp_Pnt& p) {
  if (components_.empty()) return false;
  baseFrame_.SetLocation(p);
  // the accumulated rotation now turns about the new pivot
  setTransform(ctx, values_[0], values_[1], values_[2], values_[3], values_[4], values_[5]);
  return true;
}

bool MoveComponentTool::commandLineInput(ToolContext& ctx, const std::string& text) {
  // "10,0,5" -> translate; "@30" -> rotate about Z (free) or drive (jointed);
  // "x=10", "rx=-5" (one of x y z rx ry rz) -> that dialog field; "pivot=0,200,430" -> rotation pivot
  if (text.empty() || components_.empty()) return false;
  if (auto eq = text.find('='); eq != std::string::npos && text[0] != '@') {
    std::string key = text.substr(0, eq), value = text.substr(eq + 1);
    while (!key.empty() && std::isspace(static_cast<unsigned char>(key.back()))) key.pop_back();
    for (char& ch : key) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    static const char* kNames[6] = {"x", "y", "z", "rx", "ry", "rz"};
    static const char* kAliases[6] = {"dx", "dy", "dz", "anglex", "angley", "anglez"};
    if (key == "pivot" || key == "about") {
      double p[3] = {0, 0, 0};
      size_t start = 0;
      int n = 0;
      while (n < 3 && start <= value.size()) {
        size_t comma = value.find(',', start);
        std::string part = value.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!parseNumber(part, p[n])) return false;
        ++n;
        if (comma == std::string::npos) break;
        start = comma + 1;
      }
      if (n != 3) return false;
      if (joint_ && jointDofCount(joint_->jointType()) > 0) return false;  // a joint has its own axis
      setPivot(ctx, gp_Pnt(p[0], p[1], p[2]));
      ctx.showMessage(std::format("pivot ({}, {}, {})", p[0], p[1], p[2]));
      return true;
    }
    for (int i = 0; i < 6; ++i) {
      if (key != kNames[i] && key != kAliases[i]) continue;
      double v;
      if (!parseNumber(value, v)) {
        // expressions / units through the document evaluator
        core::EvalResult r = ctx.document->evaluate(value, i < 3 ? core::Dimension::Length() : core::Dimension::Angle());
        if (!r.ok) return false;
        v = r.value.value;
      }
      double vals[6];
      for (int k = 0; k < 6; ++k) vals[k] = values_[k];
      vals[i] = v;
      if (joint_ && jointDofCount(joint_->jointType()) > 0) return false;
      setTransform(ctx, vals[0], vals[1], vals[2], vals[3], vals[4], vals[5]);
      return true;
    }
    return false;
  }
  if (text[0] == '@') {
    double a;
    if (!parseNumber(text.substr(1), a)) return false;
    if (joint_ && jointDofCount(joint_->jointType()) > 0) {
      joint_->setPrimaryValue(a);
      ctx.document->notifyFeatureChanged(joint_->id());
      ctx.document->recompute();
      moveTriadToCurrent(ctx);
      syncDialog();
    } else {
      setTransform(ctx, values_[0], values_[1], values_[2], values_[3], values_[4], a);
    }
    return true;
  }
  double v[3] = {0, 0, 0};
  size_t start = 0;
  int n = 0;
  while (n < 3 && start <= text.size()) {
    size_t comma = text.find(',', start);
    std::string part = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
    if (!parseNumber(part, v[n])) return false;
    ++n;
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  if (n == 0) return false;
  if (joint_ && jointDofCount(joint_->jointType()) > 0 && n == 1) {
    joint_->setPrimaryValue(v[0]);
    ctx.document->notifyFeatureChanged(joint_->id());
    ctx.document->recompute();
    moveTriadToCurrent(ctx);
    syncDialog();
    return true;
  }
  setTransform(ctx, v[0], v[1], v[2], values_[3], values_[4], values_[5]);
  return true;
}

void MoveComponentTool::accept(ToolContext& ctx) {
  Document& doc = *ctx.document;
  if (components_.empty()) {
    ctx.cancel();
    return;
  }
  if (joint_) {
    QJsonObject after = joint_->toJson();
    ctx.undo->push(std::make_unique<core::ModifyFeatureCommand>(doc, joint_->id(), jointBefore_, after, "Drive joint"));
  }
  std::map<ObjectId, gp_Trsf> after = snapshotPlacements(doc);
  bool changed = false;
  for (const auto& [id, p] : after)
    if (placementsBefore_.count(id) && !(placementsBefore_.at(id).TranslationPart().IsEqual(p.TranslationPart(), 1e-12) &&
                                         placementsBefore_.at(id).GetRotation().IsEqual(p.GetRotation())))
      changed = true;
  if (changed) {
    // one undo step: the placements plus a "Position" timeline record (Fusion records moves in
    // the timeline); a driven joint is its own record
    ctx.undo->beginMacro("Move component");
    ctx.undo->push(makePlacementsCommand(doc, placementsBefore_, after, "Move component"));
    if (!joint_) {
      std::map<ObjectId, gp_Trsf> world;
      for (ObjectId c : components_)
        if (doc.component(c)) world[c] = doc.worldPlacement(c);
      auto pos = std::make_unique<PositionFeature>();
      pos->setName(doc.uniqueFeatureName("Position"));
      pos->setPlacements(world);
      ctx.undo->push(std::make_unique<core::AddFeatureCommand>(doc, std::move(pos)));
    }
    ctx.undo->endMacro();
    // joints referencing moved free components are re-solved (body features do not depend on placements)
    recomputeJoints(doc);
  }
  ctx.finish();
}

void MoveComponentTool::cancel(ToolContext& ctx) {
  Document& doc = *ctx.document;
  if (joint_) {
    joint_->fromJson(jointBefore_);
    doc.notifyFeatureChanged(joint_->id());
  }
  for (const auto& [id, p] : placementsBefore_)
    if (core::Component* c = doc.component(id)) {
      c->placement = p;
      doc.notifyComponentChanged(id);
    }
  if (joint_) doc.recompute();
  ctx.cancel();
}

QWidget* MoveComponentTool::createDialog(ToolContext& ctx, QWidget* parent) {
  auto* w = new QWidget(parent);
  auto* form = new QFormLayout(w);
  targetLabel_ = new QLabel(w);
  form->addRow("Components", targetLabel_);
  const char* names[6] = {"X Distance", "Y Distance", "Z Distance", "X Angle", "Y Angle", "Z Angle"};
  for (int i = 0; i < 6; ++i) {
    edits_[i] = new ExpressionEdit(ctx.document, i < 3 ? core::Dimension::Length() : core::Dimension::Angle(), w);
    edits_[i]->setValue(values_[i]);
    QObject::connect(edits_[i], &ExpressionEdit::valueChanged, this, [this](double) {
      if (!ctx_ || syncing_ || components_.empty() || dragging_) return;
      setTransform(*ctx_, edits_[0]->value(), edits_[1]->value(), edits_[2]->value(), edits_[3]->value(), edits_[4]->value(),
                   edits_[5]->value());
    });
    form->addRow(names[i], edits_[i]);
  }
  // rotation pivot (world point the angles turn about; the triad origin)
  auto* pivotRow = new QWidget(w);
  auto* ph = new QHBoxLayout(pivotRow);
  ph->setContentsMargins(0, 0, 0, 0);
  const gp_Pnt p0 = baseFrame_.Location();
  const double pv[3] = {p0.X(), p0.Y(), p0.Z()};
  for (int i = 0; i < 3; ++i) {
    pivotEdits_[i] = new ExpressionEdit(ctx.document, core::Dimension::Length(), pivotRow);
    pivotEdits_[i]->setValue(pv[i]);
    pivotEdits_[i]->setToolTip("World point the rotation angles turn about");
    QObject::connect(pivotEdits_[i], &ExpressionEdit::valueChanged, this, [this](double) {
      if (!ctx_ || syncing_ || components_.empty() || dragging_) return;
      if (!pivotEdits_[0] || !pivotEdits_[1] || !pivotEdits_[2]) return;
      setPivot(*ctx_, gp_Pnt(pivotEdits_[0]->value(), pivotEdits_[1]->value(), pivotEdits_[2]->value()));
    });
    ph->addWidget(pivotEdits_[i]);
  }
  form->addRow("Pivot X / Y / Z", pivotRow);
  syncDialog();
  return w;
}

void MoveComponentTool::syncDialog() {
  if (!targetLabel_ || !ctx_) return;
  syncing_ = true;
  std::string names;
  for (ObjectId c : components_) names += (names.empty() ? "" : ", ") + componentLabel(*ctx_->document, c);
  if (joint_) names += std::format(" [{}]", joint_->name());
  targetLabel_->setText(core::qs(names.empty() ? std::string("none") : names));
  for (int i = 0; i < 6; ++i)
    if (edits_[i] && std::abs(edits_[i]->value() - values_[i]) > 1e-9) edits_[i]->setValue(values_[i]);
  const gp_Pnt p0 = baseFrame_.Location();
  const double pv[3] = {p0.X(), p0.Y(), p0.Z()};
  for (int i = 0; i < 3; ++i)
    if (pivotEdits_[i] && std::abs(pivotEdits_[i]->value() - pv[i]) > 1e-9) pivotEdits_[i]->setValue(pv[i]);
  syncing_ = false;
}

}  // namespace fabricad::assembly
