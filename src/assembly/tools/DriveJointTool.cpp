// FabriCAD assembly tools: Drive Joints implementation.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "assembly/tools/DriveJointTool.h"

#include "assembly/JointFeature.h"
#include "assembly/tools/ToolCommon.h"
#include "core/AppHost.h"
#include "core/Commands.h"
#include "core/Json.h"
#include "core/Selection.h"
#include "core/UndoStack.h"
#include "core/ViewerContext.h"

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <format>

#include <gp_Ax3.hxx>

namespace fabricad::assembly {

using core::Document;
using core::kInvalidId;
using core::ObjectId;
using core::ToolContext;
using core::ToolInfo;

DriveJointTool::DriveJointTool()
    : ToolBase(ToolInfo{.id = "assembly.drive-joint",
                        .name = "Drive Joints",
                        .tooltip = "Set or animate a joint's angle / distance",
                        .icon = "assembly-drive-joint",
                        .aliases = {"DRIVEJOINT", "DRIVE"},
                        .tab = "SOLID",
                        .group = "ASSEMBLE",
                        .order = 7}) {
  timer_.setInterval(33);
  connect(&timer_, &QTimer::timeout, this, [this] {
    if (ctx_) step(*ctx_);
  });
}

std::vector<ObjectId> DriveJointTool::candidates(const Document& doc) const {
  std::vector<ObjectId> out;
  for (const auto& f : doc.features()) {
    const auto* jf = dynamic_cast<const JointFeature*>(f.get());
    // ASM-02: a joint in error (e.g. its component was deleted) cannot be driven
    if (jf && doc.isFeatureActive(jf->id()) && jf->status() != core::FeatureStatus::Error &&
        jointDofCount(jf->jointType()) > 0)
      out.push_back(jf->id());
  }
  return out;
}

void DriveJointTool::activate(ToolContext& ctx) {
  ctx_ = &ctx;
  joint_ = nullptr;
  Document& doc = *ctx.document;
  std::vector<ObjectId> picked = jointsOf(doc, ctx.preselection);
  ObjectId target = kInvalidId;
  for (ObjectId id : picked)
    if (jointDofCount(doc.featureAs<JointFeature>(id)->jointType()) > 0) target = id;
  if (target == kInvalidId && !ctx.args.empty())
    for (const auto& f : doc.features())
      if (f->name() == ctx.args.front() && dynamic_cast<JointFeature*>(f.get())) target = f->id();
  if (target == kInvalidId) {
    std::vector<ObjectId> all = candidates(doc);
    if (!all.empty()) target = all.back();
  }
  ctx.selection->setFilter(core::ShapeKind::Joint | core::ShapeKind::Feature);
  if (ctx.viewer) ctx.viewer->setSelectionFilter(ctx.selection->filter());
  if (target == kInvalidId) {
    ctx.setPrompt("Drive joint: no joints with motion in this design");
    if (ctx.host) ctx.host->setToolAcceptEnabled(false);
    return;
  }
  setJoint(ctx, target);
  if (ctx.args.size() >= 2) {
    double v;
    if (parseNumber(ctx.args[1], v)) setValue(ctx, v);
  }
}

void DriveJointTool::deactivate(ToolContext& ctx) {
  timer_.stop();
  if (ctx.viewer && manipId_ != core::kInvalidManipulator) ctx.viewer->removeManipulator(manipId_);
  manipId_ = core::kInvalidManipulator;
  dragging_ = false;
  ctx_ = nullptr;
  jointCombo_ = nullptr;
  slider_ = nullptr;
  valueEdit_ = nullptr;
  rangeLabel_ = nullptr;
  animateButton_ = nullptr;
}

void DriveJointTool::setJoint(ToolContext& ctx, ObjectId jointId) {
  Document& doc = *ctx.document;
  // restore the previous joint if we switch mid-way
  if (joint_ && joint_->id() != jointId) restore(ctx);
  joint_ = doc.featureAs<JointFeature>(jointId);
  if (!joint_) return;
  before_ = joint_->toJson();
  const auto& dofs = jointDofs(joint_->jointType());
  bool rot = !dofs.empty() && dofs.front().kind == DofKind::Rotation;
  if (joint_->limitsEnabled()) {
    rangeMin_ = std::min(joint_->limitMin(), joint_->limitMax());
    rangeMax_ = std::max(joint_->limitMin(), joint_->limitMax());
  } else {
    rangeMin_ = rot ? -180.0 : -100.0;
    rangeMax_ = rot ? 180.0 : 100.0;
  }
  speed_ = (rangeMax_ - rangeMin_) / 120.0;
  ctx.setPrompt(std::format("Drive '{}': drag the handle or the slider, or type a value ({})", joint_->name(),
                            dofs.empty() ? "" : dofs.front().label));
  if (ctx.host) ctx.host->setToolAcceptEnabled(true);
  showManipulator(ctx);
  syncDialog();
}

void DriveJointTool::showManipulator(ToolContext& ctx) {
  if (!ctx.viewer || !joint_) return;
  const auto& dofs = jointDofs(joint_->jointType());
  if (dofs.empty()) return;
  const DofInfo& dof = dofs.front();
  const gp_Ax3 jf = gp_Ax3().Transformed(joint_->worldFrame(*ctx.document));
  const gp_Dir axes[3] = {jf.XDirection(), jf.YDirection(), jf.Direction()};
  const gp_Dir axis = axes[std::clamp(dof.axis, 0, 2)];
  core::ManipulatorSpec s;
  s.kind = dof.kind == DofKind::Rotation ? core::ManipulatorKind::Angular : core::ManipulatorKind::Linear;
  // X direction of the gizmo frame: the joint axis that is not the DOF axis (angle 0 = joint rest pose)
  const gp_Dir zero = dof.axis == 0 ? jf.YDirection() : jf.XDirection();
  s.frame = gp_Ax3(jf.Location(), axis, zero);
  s.value = value();
  s.label = dof.label;
  if (joint_->limitsEnabled()) {
    s.minValue = rangeMin_;
    s.maxValue = rangeMax_;
  }
  s.tag = 1;
  if (manipId_ != core::kInvalidManipulator && ctx.viewer->manipulators().has(manipId_)) {
    ctx.viewer->updateManipulator(manipId_, s);
    return;
  }
  core::ToolContext* pctx = ctx.manager ? ctx.manager->activeContext() : nullptr;
  if (!pctx) pctx = &ctx;
  manipId_ = ctx.viewer->showManipulator(s, [this, pctx](const core::ManipulatorEvent& e) { onManipulatorEvent(*pctx, e); });
}

void DriveJointTool::onManipulatorEvent(ToolContext& ctx, const core::ManipulatorEvent& e) {
  using core::ManipulatorPhase;
  if (!joint_) return;
  switch (e.phase) {
    case ManipulatorPhase::Hover:
    case ManipulatorPhase::Leave: return;
    case ManipulatorPhase::DragBegin:
      dragging_ = true;
      timer_.stop();
      if (animateButton_) animateButton_->setChecked(false);
      return;
    case ManipulatorPhase::Drag:
      setValue(ctx, e.value);
      return;
    case ManipulatorPhase::DragEnd:
    case ManipulatorPhase::DragCancel:
      dragging_ = false;
      setValue(ctx, e.value);
      return;
  }
}

double DriveJointTool::value() const { return joint_ ? joint_->primaryValue() : 0.0; }

void DriveJointTool::setValue(ToolContext& ctx, double v) {
  if (!joint_) return;
  joint_->setPrimaryValue(v);
  ctx.document->notifyFeatureChanged(joint_->id());
  ctx.document->recompute();
  if (ctx.viewer && manipId_ != core::kInvalidManipulator) ctx.viewer->setManipulatorValue(manipId_, value());
  syncDialog();
}

void DriveJointTool::step(ToolContext& ctx) {
  if (!joint_) return;
  double v = value() + (forward_ ? speed_ : -speed_);
  if (joint_->limitsEnabled()) {
    if (v >= rangeMax_) {
      v = rangeMax_;
      forward_ = false;
    } else if (v <= rangeMin_) {
      v = rangeMin_;
      forward_ = true;
    }
  } else {
    const auto& dofs = jointDofs(joint_->jointType());
    bool rot = !dofs.empty() && dofs.front().kind == DofKind::Rotation;
    if (rot) {
      if (v > 180.0) v -= 360.0;
    } else if (v >= rangeMax_ || v <= rangeMin_) {
      forward_ = !forward_;
    }
  }
  setValue(ctx, v);
}

void DriveJointTool::setAnimating(ToolContext& ctx, bool on) {
  (void)ctx;
  if (on && joint_) timer_.start();
  else timer_.stop();
  if (animateButton_) animateButton_->setChecked(timer_.isActive());
}

void DriveJointTool::selectionChanged(ToolContext& ctx) {
  std::vector<ObjectId> picked = jointsOf(*ctx.document, ctx.selection->items());
  if (!picked.empty()) setJoint(ctx, picked.front());
}

bool DriveJointTool::commandLineInput(ToolContext& ctx, const std::string& text) {
  if (wordIs(text, "ANIMATE")) {
    setAnimating(ctx, !timer_.isActive());
    return true;
  }
  double v;
  if (parseNumber(text, v)) {
    setValue(ctx, v);
    return true;
  }
  return false;
}

void DriveJointTool::restore(ToolContext& ctx) {
  if (!joint_) return;
  joint_->fromJson(before_);
  ctx.document->notifyFeatureChanged(joint_->id());
  ctx.document->recompute();
}

void DriveJointTool::accept(ToolContext& ctx) {
  timer_.stop();
  if (joint_) {
    QJsonObject after = joint_->toJson();
    if (after != before_)
      ctx.undo->push(std::make_unique<core::ModifyFeatureCommand>(*ctx.document, joint_->id(), before_, after, "Drive joint"));
  }
  ctx.finish();
}

void DriveJointTool::cancel(ToolContext& ctx) {
  timer_.stop();
  restore(ctx);
  ctx.cancel();
}

QWidget* DriveJointTool::createDialog(ToolContext& ctx, QWidget* parent) {
  Document& doc = *ctx.document;
  auto* w = new QWidget(parent);
  auto* form = new QFormLayout(w);
  jointCombo_ = new QComboBox(w);
  for (ObjectId id : candidates(doc)) jointCombo_->addItem(core::qs(doc.feature(id)->name()), QVariant::fromValue<qulonglong>(id));
  QObject::connect(jointCombo_, &QComboBox::currentIndexChanged, this, [this](int i) {
    if (ctx_ && !syncing_ && i >= 0) setJoint(*ctx_, static_cast<ObjectId>(jointCombo_->itemData(i).toULongLong()));
  });
  form->addRow("Joint", jointCombo_);
  slider_ = new QSlider(Qt::Horizontal, w);
  slider_->setRange(0, 1000);
  QObject::connect(slider_, &QSlider::valueChanged, this, [this](int s) {
    if (ctx_ && !syncing_) setValue(*ctx_, rangeMin_ + (rangeMax_ - rangeMin_) * s / 1000.0);
  });
  form->addRow("Position", slider_);
  valueEdit_ = new ExpressionEdit(&doc, core::Dimension::Scalar(), w);
  QObject::connect(valueEdit_, &ExpressionEdit::valueChanged, this, [this](double v) {
    if (ctx_ && !syncing_) setValue(*ctx_, v);
  });
  form->addRow("Value", valueEdit_);
  rangeLabel_ = new QLabel(w);
  form->addRow("Range", rangeLabel_);
  animateButton_ = new QPushButton("Animate", w);
  animateButton_->setCheckable(true);
  QObject::connect(animateButton_, &QPushButton::toggled, this, [this](bool on) {
    if (ctx_) setAnimating(*ctx_, on);
  });
  form->addRow(animateButton_);
  syncDialog();
  return w;
}

void DriveJointTool::syncDialog() {
  if (!slider_ || !joint_) return;
  syncing_ = true;
  for (int i = 0; i < jointCombo_->count(); ++i)
    if (static_cast<ObjectId>(jointCombo_->itemData(i).toULongLong()) == joint_->id()) jointCombo_->setCurrentIndex(i);
  double v = value();
  double t = (rangeMax_ > rangeMin_) ? (v - rangeMin_) / (rangeMax_ - rangeMin_) : 0.0;
  slider_->setValue(static_cast<int>(std::lround(std::clamp(t, 0.0, 1.0) * 1000)));
  if (std::abs(valueEdit_->value() - v) > 1e-9) valueEdit_->setValue(v);
  const auto& dofs = jointDofs(joint_->jointType());
  std::string unit = dofs.empty() ? "" : (dofs.front().kind == DofKind::Rotation ? " deg" : " mm");
  rangeLabel_->setText(core::qs(std::format("{:g}{} .. {:g}{}{}", rangeMin_, unit, rangeMax_, unit,
                                            joint_->limitsEnabled() ? " (limits)" : "")));
  syncing_ = false;
}

}  // namespace fabricad::assembly
