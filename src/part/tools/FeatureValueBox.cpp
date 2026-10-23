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
#include <format>
#include <cctype>
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
        const double px = ctx.viewer->gizmoPixelSize(m->spec().frame.Location());
        gp_Pnt tip = m->handlePoint(h, px);
        // a linear handle's point is the arrow base: the box sits at the arrow tip, like the
        // mockup's DIST box next to the arrow head (P5-MOCKUP-4)
        if (m->spec().kind == core::ManipulatorKind::Linear)
          tip.Translate(gp_Vec(m->spec().frame.Direction()) * m->effectiveSize(px));
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

/// Short caption for the box tag (5.3: `DIST`): long labels are abbreviated.
std::string tagFor(const std::string& label) {
  std::string l;
  for (char c : label) l += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (l == "distance") return "Dist";
  if (l == "thickness") return "Thk";
  if (l == "radius") return "R";
  if (l == "diameter") return "Dia";
  return label.size() <= 8 ? label : label.substr(0, 4);
}

}  // namespace

FeatureValueBox::FeatureValueBox(core::ToolContext& ctx, FeatureTool& tool) : tool_(tool) {
  model_.setEvaluator(sketch::makeSketchEvaluator(ctx.document));
  // the bare number: the unit is the box's own suffix label (5.3)
  model_.setFormatter([doc = ctx.document](double v, sketch::ValueKind kind) -> std::string {
    const core::UnitSystem u = doc ? doc->units() : core::UnitSystem{};
    switch (kind) {
      case sketch::ValueKind::Length: return core::formatQuantity(core::Quantity::mm(v), u, false);
      case sketch::ValueKind::Angle: return core::formatQuantity(core::Quantity::deg(v), u, false);
      default: return std::format("{:.6g}", v);
    }
  });
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
    // the ▴▾ stepper (5.3): one unit per click, ten with Shift
    overlay_->setStepper([this, mgr](int fieldIndex, int steps) {
      core::ToolContext* c = mgr ? mgr->activeContext() : nullptr;
      if (!c || fieldIndex != 0) return;
      step(*c, steps);
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
    model_.addField(sketch::ValueField{.id = name, .label = tagFor(p->label()), .kind = kind});
  }
  // unit suffix and stepper (5.3, P5-MOCKUP-4)
  std::string unit;
  if (kind == sketch::ValueKind::Angle)
    unit = ctx.document && ctx.document->units().angle == core::AngleUnit::Radian ? "rad" : "\u00B0";
  else if (kind == sketch::ValueKind::Length)
    unit = ctx.document ? core::lengthUnitSymbol(ctx.document->units().length) : "mm";
  model_.setPresentation(prop_, unit, true);
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

void FeatureValueBox::step(core::ToolContext& ctx, int steps) {
  PartFeature* f = tool_.feature();
  const core::Property* p = f && !prop_.empty() ? f->property(prop_) : nullptr;
  if (!p || steps == 0 || model_.empty()) return;
  // one display unit (1 mm / 1 in / 1 deg) per step, from the current value
  const sketch::ValueKind kind = model_.field(0).kind;
  double next = p->toDouble() + steps;
  std::string unit;
  if (kind == sketch::ValueKind::Length) {
    next = p->toDouble() + steps * (ctx.document ? core::lengthUnitToMm(ctx.document->units().length) : 1.0);
    unit = " mm";
  } else if (kind == sketch::ValueKind::Angle) {
    unit = " deg";
  }
  model_.resetInput();
  tool_.setNumeric(ctx, prop_, std::format("{:.6g}{}", next, unit));
}

bool FeatureValueBox::keyPress(core::ToolContext& ctx, const core::ViewerKeyEvent& e) {
  if (!isActive()) return false;
  // one field: Tab has nowhere to go inside the box, so the shell moves the keyboard into the
  // command card instead (P5-KEYBOARD-2); the typed value is already applied
  if ((e.key == Qt::Key_Tab || e.key == Qt::Key_Backtab) && model_.fieldCount() <= 1) {
    model_.resetInput();
    return false;
  }
  switch (model_.keyPress(e.key, e.modifiers, e.text)) {
    case sketch::ValueInputModel::KeyResult::Consumed:
      applyTyped(ctx);
      return true;
    case sketch::ValueInputModel::KeyResult::Commit: {
      // PART-02: Enter on text that does not evaluate reports the error and keeps the
      // command open (it used to accept with the last good value).
      if (const sketch::ValueField* f = model_.activeField(); f && !f->text.empty()) {
        std::string err;
        sketch::ValueInputModel::Evaluator ev = sketch::makeSketchEvaluator(ctx.document);
        if (!ev(f->text, f->kind, &err)) {
          ctx.showMessage(err.empty() ? std::string("invalid expression") : err, true);
          return true;
        }
      }
      applyTyped(ctx);
      model_.resetInput();
      tool_.accept(ctx);
      return true;
    }
    case sketch::ValueInputModel::KeyResult::Ignored: break;
  }
  return false;
}

}  // namespace fabricad::part
