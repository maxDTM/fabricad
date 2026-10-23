// SPDX-License-Identifier: LGPL-2.1-or-later
#include "assembly/tools/InsertFastenerTool.h"

#include "assembly/tools/ToolCommon.h"
#include "core/Json.h"
#include "core/Selection.h"
#include "core/ViewerContext.h"
#include "part/standards/Threads.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QWidget>

#include <format>

namespace fabricad::assembly {

using core::ShapeKind;
using core::ShapeRef;
using core::ToolContext;
using core::ToolInfo;
namespace st = part::standards;

InsertFastenerTool::InsertFastenerTool()
    : ToolBase(ToolInfo{.id = "assembly.insert-fastener",
                        .name = "Insert Fastener",
                        .tooltip = "Put a standard screw, bolt, nut, washer, insert or pin into a hole (sized from the hole)",
                        .icon = "assembly.insert-fastener",
                        .aliases = {"FASTENER", "INSERTFASTENER"},
                        .tab = "SOLID",
                        .group = "ASSEMBLE",
                        .order = 40,
                        .promoted = false,
                        .repeatable = true}) {}

void InsertFastenerTool::activate(ToolContext& ctx) {
  ctx_ = &ctx;
  opt_ = InsertOptions{};
  holes_.clear();
  ctx.selection->setFilter(ShapeKind::Edge | ShapeKind::Face);
  if (ctx.viewer) ctx.viewer->setSelectionFilter(ctx.selection->filter());
  for (const std::string& a : ctx.args)
    if (!applyInsertOption(a, opt_)) ctx.showMessage(std::format("FASTENER: unknown option '{}'", a), true);
  takeHoles(ctx, ctx.preselection);
  updateSummary(ctx);
}

void InsertFastenerTool::deactivate(ToolContext& ctx) {
  (void)ctx;
  ctx_ = nullptr;
  family_ = size_ = length_ = grade_ = nullptr;
  nut_ = washer_ = rotation_ = all_ = nullptr;
  summary_ = nullptr;
}

void InsertFastenerTool::takeHoles(ToolContext& ctx, const std::vector<ShapeRef>& refs) {
  for (const ShapeRef& r : refs) {
    if (!r.isBrep() || (r.kind != ShapeKind::Edge && r.kind != ShapeKind::Face)) continue;
    if (std::find(holes_.begin(), holes_.end(), r) != holes_.end()) continue;
    std::string err;
    if (!analyseHole(*ctx.document, r, &err)) {
      ctx.showMessage(err, true);
      continue;
    }
    holes_.push_back(r);
  }
}

void InsertFastenerTool::selectionChanged(ToolContext& ctx) {
  const auto items = ctx.selection->items();
  if (items.empty()) return;
  takeHoles(ctx, items);
  updateSummary(ctx);
}

void InsertFastenerTool::updateSummary(ToolContext& ctx) {
  std::string text;
  if (holes_.empty()) {
    text = "Insert Fastener: pick a hole (circular edge or hole wall)";
  } else if (auto h = analyseHole(*ctx.document, holes_.front())) {
    std::string err;
    st::FastenerSpec s = specForHole(*h, opt_, &err);
    int n = static_cast<int>(holes_.size());
    if (opt_.allEqual) n = static_cast<int>(equalHoles(*ctx.document, *h).size());
    text = err.empty() ? std::format("{} x {} (hole ⌀{:.2f}, grip {:.2f} mm)", n, st::partNumber(s), h->diameter, h->seatToFar)
                       : err;
  }
  ctx.setPrompt(text);
  if (summary_) summary_->setText(core::qs(text));
}

bool InsertFastenerTool::commandLineInput(ToolContext& ctx, const std::string& text) {
  if (!applyInsertOption(text, opt_)) return false;
  updateSummary(ctx);
  return true;
}

void InsertFastenerTool::accept(ToolContext& ctx) {
  if (holes_.empty()) takeHoles(ctx, ctx.selection->items());
  if (holes_.empty()) {
    ctx.showMessage("Insert Fastener: select a hole first", true);
    return;
  }
  InsertResult r = insertFasteners(*ctx.document, ctx.undo, holes_, opt_);
  if (!r.ok()) {
    ctx.showMessage("Insert Fastener: " + r.error, true);
    return;
  }
  for (const std::string& w : r.warnings) ctx.showMessage(w, false);
  ctx.showMessage(std::format("Inserted {} part(s) in {} hole(s): {}", r.components.size(), r.holes, r.partNumber), false);
  ctx.selection->clear();
  ctx.finish();
}

void InsertFastenerTool::fillSizeLists() {
  if (!size_ || !length_) return;
  const QSignalBlocker b1(size_), b2(length_);
  size_->clear();
  size_->addItem("Auto (from the hole)");
  for (const std::string& s : st::familySizes(opt_.family)) size_->addItem(core::qs(s));
  const int si = size_->findText(core::qs(opt_.size));
  size_->setCurrentIndex(si < 0 ? 0 : si);
  length_->clear();
  length_->addItem("Auto (from the grip)");
  if (st::familyHasLength(opt_.family) && !opt_.size.empty())
    for (double l : st::familyLengths(opt_.family, opt_.size)) length_->addItem(core::qs(std::format("{:g}", l)), l);
  length_->setEnabled(st::familyHasLength(opt_.family));
}

QWidget* InsertFastenerTool::createDialog(ToolContext& ctx, QWidget* parent) {
  auto* w = new QWidget(parent);
  auto* form = new QFormLayout(w);
  family_ = new QComboBox(w);
  for (const std::string& n : st::familyNames()) family_->addItem(core::qs(n));
  family_->setCurrentIndex(static_cast<int>(opt_.family));
  size_ = new QComboBox(w);
  length_ = new QComboBox(w);
  grade_ = new QComboBox(w);
  for (const st::Grade& g : st::grades()) grade_->addItem(core::qs(std::format("{} ({})", g.key, g.materialName)), QString(g.key));
  grade_->setCurrentIndex(std::max(0, grade_->findData(core::qs(opt_.grade))));
  washer_ = new QCheckBox("Washer under the head (and the nut)", w);
  nut_ = new QCheckBox("Add nut on the far face", w);
  rotation_ = new QCheckBox("Allow rotation (revolute joint)", w);
  all_ = new QCheckBox("All equal holes of this body", w);
  washer_->setChecked(opt_.washer);
  nut_->setChecked(opt_.nut);
  rotation_->setChecked(opt_.allowRotation);
  all_->setChecked(opt_.allEqual);
  summary_ = new QLabel(w);
  summary_->setWordWrap(true);
  form->addRow("Family", family_);
  form->addRow("Size", size_);
  form->addRow("Length", length_);
  form->addRow("Material", grade_);
  form->addRow(washer_);
  form->addRow(nut_);
  form->addRow(rotation_);
  form->addRow(all_);
  form->addRow(summary_);
  fillSizeLists();
  ToolContext* c = &ctx;
  QObject::connect(family_, &QComboBox::currentIndexChanged, w, [this, c](int i) {
    opt_.family = static_cast<st::Family>(std::max(0, i));
    opt_.size.clear();
    opt_.length = 0;
    fillSizeLists();
    updateSummary(*c);
  });
  QObject::connect(size_, &QComboBox::currentIndexChanged, w, [this, c](int i) {
    opt_.size = i <= 0 ? std::string() : size_->itemText(i).toStdString();
    opt_.length = 0;
    fillSizeLists();
    updateSummary(*c);
  });
  QObject::connect(length_, &QComboBox::currentIndexChanged, w, [this, c](int i) {
    opt_.length = i <= 0 ? 0.0 : length_->itemData(i).toDouble();
    updateSummary(*c);
  });
  QObject::connect(grade_, &QComboBox::currentIndexChanged, w, [this, c](int i) {
    opt_.grade = grade_->itemData(i).toString().toStdString();
    updateSummary(*c);
  });
  auto check = [this, c, w](QCheckBox* b, bool InsertOptions::*field) {
    QObject::connect(b, &QCheckBox::toggled, w, [this, c, field](bool on) {
      opt_.*field = on;
      updateSummary(*c);
    });
  };
  check(washer_, &InsertOptions::washer);
  check(nut_, &InsertOptions::nut);
  check(rotation_, &InsertOptions::allowRotation);
  check(all_, &InsertOptions::allEqual);
  updateSummary(ctx);
  return w;
}

}  // namespace fabricad::assembly
