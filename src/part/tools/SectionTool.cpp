// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/tools/SectionTool.h"

#include "core/AppHost.h"
#include "core/Document.h"
#include "core/GeometryRefs.h"
#include "core/Selection.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>

namespace fabricad::part {

using core::ShapeRef;
using core::ToolContext;

namespace {

std::string upper(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return s;
}

std::optional<double> number(const std::string& s) {
  std::string t = s;
  if (t.size() > 2 && t.ends_with("mm")) t.resize(t.size() - 2);
  double v = 0;
  const char* b = t.data();
  const char* e = b + t.size();
  if (b != e && *b == '+') ++b;
  auto [p, ec] = std::from_chars(b, e, v);
  if (ec != std::errc() || p != e || !std::isfinite(v)) return std::nullopt;
  return v;
}

bool samePlane(const gp_Pln& a, const gp_Pln& b) {
  return a.Axis().Direction().IsEqual(b.Axis().Direction(), 1e-9) && a.Distance(b.Location()) < 1e-7;
}

core::ToolInfo sectionInfo() {
  core::ToolInfo i;
  i.id = "inspect.section";
  i.name = "Section Analysis";
  i.tooltip = "Cut the model with a live clipping plane; drag the arrow to move the cut (SECTION OFF removes it)";
  i.icon = "inspect-section";
  i.aliases = {"SECTION", "SECTIONANALYSIS"};
  i.workspace = core::Workspace::Design;
  i.tab = "SOLID";
  i.group = "INSPECT";
  i.order = 2;
  i.promoted = true;
  i.repeatable = false;
  return i;
}

}  // namespace

gp_Pln sectionPlaneFor(const gp_Ax3& base, double offset, bool flip) {
  gp_Ax3 ax = base;
  ax.Translate(gp_Vec(base.Direction()) * offset);
  gp_Pln p(ax);
  if (flip) p.SetAxis(p.Axis().Reversed());
  return p;
}

SectionTool::SectionTool() : ToolBase(sectionInfo()) {}

void SectionTool::activate(ToolContext& ctx) {
  ctx_ = &ctx;
  base_.reset();
  offset_ = 0.0;
  flip_ = false;
  manip_ = core::kInvalidManipulator;
  before_ = ctx.viewer ? ctx.viewer->sectionPlane() : std::nullopt;
  // resume the section that is on (the last one this tool made, else the viewer's plane as is)
  if (before_) {
    if (lastBase_ && samePlane(sectionPlaneFor(*lastBase_, lastOffset_, lastFlip_), *before_)) {
      base_ = lastBase_;
      offset_ = lastOffset_;
      flip_ = lastFlip_;
    } else {
      base_ = before_->Position();
    }
  }
  for (const std::string& a : ctx.args) {
    if (upper(a) == "OFF" || upper(a) == "CLEAR" || upper(a) == "REMOVE") {
      if (ctx.viewer) ctx.viewer->setSectionPlane(std::nullopt);
      lastBase_.reset();
      ctx.showMessage("Section analysis removed");
      ctx.finish();
      return;
    }
  }
  for (const std::string& a : ctx.args) applyWord(ctx, a);
  if (!base_)
    for (const ShapeRef& r : ctx.preselection)
      if (pickPlane(ctx, r)) break;
  if (ctx.selection) {
    ctx.selection->clear();
    ctx.selection->setFilter(core::kPlaneKinds);
  }
  if (ctx.viewer) ctx.viewer->setSelectionFilter(core::kPlaneKinds);
  if (base_) apply(ctx);
  ctx.setPrompt(base_ ? "Drag the arrow or type an offset; FLIP keeps the other side; Enter keeps the section"
                      : "Select a plane or planar face to cut the model with (or type XY, XZ, YZ)");
}

void SectionTool::deactivate(ToolContext&) {
  ctx_ = nullptr;
  manip_ = core::kInvalidManipulator;  // ToolManager clears the gizmos
}

void SectionTool::accept(ToolContext& ctx) {
  if (!base_) {
    ctx.cancel();
    return;
  }
  lastBase_ = base_;
  lastOffset_ = offset_;
  lastFlip_ = flip_;
  ctx.showMessage(std::format("Section analysis on (offset {} mm); SECTION OFF removes it", offset_));
  ctx.finish();
}

void SectionTool::cancel(ToolContext& ctx) {
  if (ctx.viewer) ctx.viewer->setSectionPlane(before_);
  ctx.cancel();
}

void SectionTool::selectionChanged(ToolContext& ctx) {
  if (!ctx.selection) return;
  for (const ShapeRef& r : ctx.selection->items())
    if (pickPlane(ctx, r)) break;
  if (ctx.selection && !ctx.selection->isEmpty()) ctx.selection->clear();
}

bool SectionTool::mousePress(ToolContext& ctx, const core::ViewerMouseEvent& e) {
  if (e.button == core::MouseButton::Left && e.hasPick && core::hasKind(core::kPlaneKinds, e.pick.kind))
    return pickPlane(ctx, e.pick);
  return false;
}

bool SectionTool::commandLineInput(ToolContext& ctx, const std::string& text) {
  if (upper(text) == "OFF" || upper(text) == "REMOVE") {
    if (ctx.viewer) ctx.viewer->setSectionPlane(std::nullopt);
    before_.reset();
    lastBase_.reset();
    ctx.showMessage("Section analysis removed");
    ctx.finish();
    return true;
  }
  return applyWord(ctx, text);
}

bool SectionTool::applyWord(ToolContext& ctx, const std::string& word) {
  const std::string w = upper(word);
  if (w == "XY" || w == "XZ" || w == "YZ") return pickPlane(ctx, ShapeRef::originPlane(w));
  if (w == "FLIP") {
    setFlip(ctx, !flip_);
    return true;
  }
  std::string v = word;
  if (w.starts_with("OFFSET=")) v = word.substr(7);
  if (auto n = number(v)) {
    setOffset(ctx, *n);
    return true;
  }
  return false;
}

bool SectionTool::pickPlane(ToolContext& ctx, const ShapeRef& ref) {
  if (!ctx.document || !core::hasKind(core::kPlaneKinds, ref.kind)) return false;
  auto pl = core::resolvePlane(*ctx.document, ref);
  if (!pl) return false;
  gp_Ax3 ax = *pl;
  // a body face: in its component's world placement
  if (ref.isBrep() && ctx.document->body(ref.owner)) {
    const gp_Trsf world = ctx.document->worldPlacement(ctx.document->body(ref.owner)->component);
    ax.Transform(world);
  }
  offset_ = 0.0;
  setBase(ctx, ax);
  return true;
}

void SectionTool::setBase(ToolContext& ctx, const gp_Ax3& base) {
  base_ = base;
  apply(ctx);
  ctx.setPrompt("Drag the arrow or type an offset; FLIP keeps the other side; Enter keeps the section");
}

void SectionTool::setOffset(ToolContext& ctx, double mm) {
  offset_ = mm;
  if (base_) apply(ctx);
}

void SectionTool::setFlip(ToolContext& ctx, bool flip) {
  flip_ = flip;
  if (base_) apply(ctx);
}

void SectionTool::apply(ToolContext& ctx) {
  if (!base_) return;
  if (ctx.viewer) {
    ctx.viewer->setSectionPlane(sectionPlaneFor(*base_, offset_, flip_));
    core::ManipulatorSpec spec;
    spec.kind = core::ManipulatorKind::Linear;
    spec.frame = *base_;
    spec.value = offset_;
    spec.label = "Offset";
    if (manip_ == core::kInvalidManipulator) {
      manip_ = ctx.viewer->showManipulator(spec, [this](const core::ManipulatorEvent& ev) {
        if (!ctx_) return;
        if (ev.phase == core::ManipulatorPhase::Drag || ev.phase == core::ManipulatorPhase::DragEnd ||
            ev.phase == core::ManipulatorPhase::DragCancel) {
          offset_ = ev.value;
          if (base_ && ctx_->viewer) ctx_->viewer->setSectionPlane(sectionPlaneFor(*base_, offset_, flip_));
          syncDialog();
        }
      });
    } else {
      ctx.viewer->updateManipulator(manip_, spec);
      ctx.viewer->setManipulatorValue(manip_, offset_);
    }
    ctx.viewer->requestRedraw();
  }
  syncDialog();
}

void SectionTool::syncDialog() {
  if (syncing_) return;
  syncing_ = true;
  if (planeLabel_) {
    if (!base_) planeLabel_->setText("Select a plane or planar face");
    else {
      const gp_Dir n = base_->Direction();
      auto c = [](double v) { return std::abs(v) < 1e-12 ? 0.0 : v; };  // no "-0"
      planeLabel_->setText(QString::fromStdString(std::format("Normal ({:.3g}, {:.3g}, {:.3g})", c(n.X()), c(n.Y()), c(n.Z()))));
    }
  }
  if (offsetBox_) {
    QSignalBlocker b(offsetBox_);
    offsetBox_->setValue(offset_);
    offsetBox_->setEnabled(base_.has_value());
  }
  if (flipBox_) {
    QSignalBlocker b(flipBox_);
    flipBox_->setChecked(flip_);
    flipBox_->setEnabled(base_.has_value());
  }
  syncing_ = false;
}

QWidget* SectionTool::createDialog(ToolContext& ctx, QWidget* parent) {
  (void)ctx;
  auto* w = new QWidget(parent);
  auto* form = new QFormLayout(w);
  form->setContentsMargins(6, 6, 6, 6);
  planeLabel_ = new QLabel(w);
  planeLabel_->setObjectName("sectionPlane");
  form->addRow("Plane", planeLabel_);
  offsetBox_ = new QDoubleSpinBox(w);
  offsetBox_->setObjectName("sectionOffset");
  offsetBox_->setRange(-1e6, 1e6);
  offsetBox_->setDecimals(3);
  offsetBox_->setSuffix(" mm");
  offsetBox_->setFocusPolicy(Qt::StrongFocus);
  QObject::connect(offsetBox_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
    if (!syncing_ && ctx_) setOffset(*ctx_, v);
  });
  form->addRow("Offset", offsetBox_);
  flipBox_ = new QCheckBox("Flip side", w);
  flipBox_->setObjectName("sectionFlip");
  QObject::connect(flipBox_, &QCheckBox::toggled, this, [this](bool on) {
    if (!syncing_ && ctx_) setFlip(*ctx_, on);
  });
  form->addRow(QString(), flipBox_);
  auto* remove = new QPushButton("Remove section", w);
  remove->setObjectName("sectionRemove");
  remove->setToolTip("Turn the section analysis off");
  QObject::connect(remove, &QPushButton::clicked, this, [this] {
    if (ctx_) commandLineInput(*ctx_, "OFF");
  });
  form->addRow(QString(), remove);
  syncDialog();
  return w;
}

}  // namespace fabricad::part
