// FabriCAD part: FeatureValueBox implementation (see FeatureValueBox.h).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/tools/FeatureValueBox.h"

#include "core/Document.h"
#include "core/Manipulator.h"
#include "core/Property.h"
#include "core/ViewerContext.h"
#include "part/tools/FeatureTool.h"
#include "sketch/ValueInputOverlay.h"
#include "sketch/tools/SketchToolBase.h"

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <QWidget>
#include <gp_Pnt.hxx>

namespace fabricad::part {

namespace {

/// The value box sits at the tip of the manipulator arrow that drives the same property (Fusion
/// shows the value next to the handle); without a gizmo, at the centre of the bounding box of the
/// preview shapes (or the edited feature's solid).
std::optional<core::Vec3> anchorFor(const FeatureTool& tool, core::ToolContext& ctx, const std::string& prop) {
  if (ctx.viewer) {
    if (const FeatureTool::ManipulatorLink* link = tool.manipulatorFor(prop); link && ctx.viewer->manipulators().has(link->id)) {
      if (const core::ManipulatorModel* m = ctx.viewer->manipulators().model(link->id)) {
        // a triad's centre is its free-move handle: anchor the box at the X arrow tip instead
        const core::ManipulatorHandle h =
            m->spec().kind == core::ManipulatorKind::Triad ? core::ManipulatorHandle::MoveX : core::ManipulatorHandle::None;
        const gp_Pnt tip = m->handlePoint(h, ctx.viewer->pixelSize(m->spec().frame.Location()));
        return core::Vec3{tip.X(), tip.Y(), tip.Z()};
      }
    }
  }
  Bnd_Box box;
  for (const auto& pv : tool.lastOutcome().previews)
    if (!pv.shape.IsNull()) BRepBndLib::Add(pv.shape, box, false);
  if (box.IsVoid() && tool.feature() && !tool.feature()->outputShape().IsNull())
    BRepBndLib::Add(tool.feature()->outputShape(), box, false);
  if (box.IsVoid()) return std::nullopt;
  double x0, y0, z0, x1, y1, z1;
  box.Get(x0, y0, z0, x1, y1, z1);
  return core::Vec3{(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2};
}

}  // namespace

FeatureValueBox::FeatureValueBox(core::ToolContext& ctx, FeatureTool& tool) : tool_(tool) {
  model_.setEvaluator(sketch::makeSketchEvaluator(ctx.document));
  model_.setFormatter(sketch::makeUnitFormatter(ctx.document));
  model_.setChangedCallback([this] {
    if (overlay_) overlay_->sync();
  });
  QWidget* parent = ctx.viewer ? ctx.viewer->widget() : nullptr;
  if (parent) {
    overlay_ = std::make_unique<sketch::ValueInputOverlay>(model_, ctx.viewer, parent);
    core::ToolManager* mgr = ctx.manager;
    overlay_->setKeyForwarder([mgr](int key, int mods, const std::string& text) {
      if (!mgr) return;
      core::ViewerKeyEvent ev;
      ev.key = key;
      ev.modifiers = mods ? core::ModShift : core::ModNone;
      ev.text = text;
      mgr->keyPress(ev);
    });
  }
  update(ctx);
}

FeatureValueBox::~FeatureValueBox() {
  model_.setChangedCallback({});
  overlay_.reset();
}

void FeatureValueBox::update(core::ToolContext& ctx) {
  PartFeature* f = tool_.feature();
  std::string name = f ? f->primaryNumeric() : std::string();
  const core::Property* p = (f && !name.empty()) ? f->property(name) : nullptr;
  if (!p || !p->isNumeric() || p->flags().hidden) {
    if (!prop_.empty()) {
      prop_.clear();
      model_.clear();
    }
    return;
  }
  sketch::ValueKind kind = p->dimension() == core::Dimension::Angle() ? sketch::ValueKind::Angle
                           : p->dimension() == core::Dimension::Length() ? sketch::ValueKind::Length
                                                                           : sketch::ValueKind::Scalar;
  if (prop_ != name || model_.empty() || model_.field(0).kind != kind) {
    prop_ = name;
    model_.clear();
    model_.addField(sketch::ValueField{.id = name, .label = p->label(), .kind = kind});
  }
  model_.setMeasured(prop_, p->toDouble());
  if (auto a = anchorFor(tool_, ctx, prop_)) model_.setAnchor(prop_, *a);
}

void FeatureValueBox::applyTyped(core::ToolContext& ctx) {
  const sketch::ValueField* f = model_.activeField();
  if (!f || f->text.empty()) return;
  std::string err;
  sketch::ValueInputModel::Evaluator ev = sketch::makeSketchEvaluator(ctx.document);
  if (!ev(f->text, f->kind, &err)) return;  // incomplete expression: wait for more keys
  tool_.setNumeric(ctx, prop_, f->text);
}

bool FeatureValueBox::keyPress(core::ToolContext& ctx, const core::ViewerKeyEvent& e) {
  if (!isActive()) return false;
  switch (model_.keyPress(e.key, e.modifiers, e.text)) {
    case sketch::ValueInputModel::KeyResult::Consumed:
      applyTyped(ctx);
      return true;
    case sketch::ValueInputModel::KeyResult::Commit:
      applyTyped(ctx);
      model_.resetInput();
      tool_.accept(ctx);
      return true;
    case sketch::ValueInputModel::KeyResult::Ignored: break;
  }
  return false;
}

}  // namespace fabricad::part
