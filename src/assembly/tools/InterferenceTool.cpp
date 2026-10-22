// FabriCAD assembly tools: Interference check implementation.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "assembly/tools/InterferenceTool.h"

#include "assembly/ComponentOps.h"
#include "assembly/tools/ToolCommon.h"
#include "core/AppHost.h"
#include "core/Json.h"
#include "core/Selection.h"

#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <format>

namespace fabricad::assembly {

using core::Document;
using core::ObjectId;
using core::PreviewStyle;
using core::ShapeKind;
using core::ShapeRef;
using core::ToolContext;
using core::ToolInfo;

InterferenceTool::InterferenceTool()
    : ToolBase(ToolInfo{.id = "assembly.interference",
                        .name = "Interference",
                        .tooltip = "Find overlapping bodies between components",
                        .icon = "inspect-interference",
                        .aliases = {"INTERFERENCE", "CLASH"},
                        .tab = "SOLID",
                        .group = "INSPECT",
                        .order = 2,
                        .promoted = true,
                        .repeatable = false}) {}

void InterferenceTool::activate(ToolContext& ctx) {
  ctx_ = &ctx;
  report_ = InterferenceReport{};
  previews_.clear();
  components_ = componentsOf(*ctx.document, ctx.preselection);
  ctx.selection->setFilter(ShapeKind::Component | ShapeKind::Body);
  if (ctx.viewer) ctx.viewer->setSelectionFilter(ctx.selection->filter());
  bool all = components_.empty();
  for (const std::string& a : ctx.args)
    if (wordIs(a, "ALL")) all = true;
  if (all) components_.clear();
  ctx.setPrompt(components_.empty() ? "Interference: checking all components" : std::format("Interference: {} selected components", components_.size()));
  compute(ctx);
}

void InterferenceTool::deactivate(ToolContext& ctx) {
  (void)ctx;
  ctx_ = nullptr;
  scopeLabel_ = summaryLabel_ = nullptr;
  table_ = nullptr;
}

void InterferenceTool::accept(ToolContext& ctx) { ctx.finish(); }

void InterferenceTool::selectionChanged(ToolContext& ctx) {
  std::vector<ObjectId> comps = componentsOf(*ctx.document, ctx.selection->items());
  if (comps.empty()) return;
  components_ = comps;
  compute(ctx);
}

bool InterferenceTool::commandLineInput(ToolContext& ctx, const std::string& text) {
  if (wordIs(text, "ALL")) {
    components_.clear();
    compute(ctx);
    return true;
  }
  return false;
}

const InterferenceReport& InterferenceTool::compute(ToolContext& ctx) {
  Document& doc = *ctx.document;
  InterferenceOptions opt;
  std::vector<BodyInstance> instances = bodyInstances(doc, components_, true);
  // Bodies of one component are normally not compared (a multi-body part may overlap on
  // purpose). When every body sits in the same component, e.g. a plain multi-body design, that
  // left nothing to test ("0 pairs tested", general retest APP-11): compare them then.
  opt.sameComponentPairs = allInOneComponent(instances);
  report_ = checkInterference(instances, &doc, opt);
  showResults(ctx);
  return report_;
}

void InterferenceTool::showResults(ToolContext& ctx) {
  Document& doc = *ctx.document;
  showPreviews(ctx, -1);
  std::string summary = report_.pairs.empty()
                            ? std::format("No interference ({} bodies, {} pairs tested)", report_.instances, report_.pairsTested)
                            : std::format("{} interfering pair(s), total {:.3f} mm3", report_.pairs.size(), report_.totalVolume());
  ctx.showMessage(summary, false);
  if (summaryLabel_) summaryLabel_->setText(core::qs(summary));
  if (scopeLabel_) scopeLabel_->setText(components_.empty() ? "All components" : QString::number(components_.size()) + " components");
  if (table_) {
    table_->setRowCount(static_cast<int>(report_.pairs.size()));
    for (int i = 0; i < static_cast<int>(report_.pairs.size()); ++i) {
      const auto& p = report_.pairs[static_cast<size_t>(i)];
      auto nameOf = [&](const BodyInstance& bi) {
        const core::Body* b = doc.body(bi.body);
        return std::format("{} ({})", b ? b->name : std::to_string(bi.body), componentLabel(doc, bi.component));
      };
      table_->setItem(i, 0, new QTableWidgetItem(core::qs(nameOf(p.a))));
      table_->setItem(i, 1, new QTableWidgetItem(core::qs(nameOf(p.b))));
      auto* vol = new QTableWidgetItem(core::qs(std::format("{:.2f}", p.volume)));
      vol->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      table_->setItem(i, 2, vol);
    }
  }
}

void InterferenceTool::showPreviews(ToolContext& ctx, int selectedRow) {
  if (!ctx.viewer) return;
  for (core::PreviewHandle h : previews_) ctx.viewer->removePreview(h);
  previews_.clear();
  // The common volumes lie inside both (opaque) bodies: draw them on top so they are visible
  // (APP-11). The selected pair is bright and opaque, the others dimmer.
  for (int i = 0; i < static_cast<int>(report_.pairs.size()); ++i) {
    PreviewStyle red = PreviewStyle::cut();
    red.onTop = true;
    const bool sel = i == selectedRow;
    red.color = sel ? std::array<float, 4>{1.0f, 0.1f, 0.1f, 0.95f}
                    : std::array<float, 4>{0.95f, 0.15f, 0.15f, selectedRow < 0 ? 0.8f : 0.35f};
    previews_.push_back(ctx.viewer->showPreviewShape(report_.pairs[static_cast<size_t>(i)].shape, red));
    if (sel) {
      PreviewStyle edges = red;
      edges.shaded = false;
      edges.lineWidth = 2.5f;
      edges.color = {0.6f, 0.0f, 0.0f, 1.0f};
      previews_.push_back(ctx.viewer->showPreviewShape(report_.pairs[static_cast<size_t>(i)].shape, edges));
    }
  }
  ctx.viewer->requestRedraw();
}

void InterferenceTool::highlightRow(ToolContext& ctx, int row) {
  if (!ctx.viewer || row < 0 || row >= static_cast<int>(report_.pairs.size())) return;
  const auto& p = report_.pairs[static_cast<size_t>(row)];
  ctx.viewer->setHighlighted({ShapeRef::body(p.a.body), ShapeRef::body(p.b.body)});
  showPreviews(ctx, row);
}

QWidget* InterferenceTool::createDialog(ToolContext& ctx, QWidget* parent) {
  auto* w = new QWidget(parent);
  auto* v = new QVBoxLayout(w);
  scopeLabel_ = new QLabel(w);
  v->addWidget(scopeLabel_);
  auto* compute = new QPushButton("Compute", w);
  QObject::connect(compute, &QPushButton::clicked, this, [this] {
    if (ctx_) this->compute(*ctx_);
  });
  v->addWidget(compute);
  summaryLabel_ = new QLabel(w);
  v->addWidget(summaryLabel_);
  table_ = new QTableWidget(0, 3, w);
  table_->setHorizontalHeaderLabels({"Body 1", "Body 2", QString::fromUtf8("Volume (mm\u00B3)")});
  // APP-11: names share the width (elided), the volume column always fits its numbers
  table_->verticalHeader()->hide();
  table_->setTextElideMode(Qt::ElideRight);
  table_->setWordWrap(false);
  table_->horizontalHeader()->setStretchLastSection(false);
  table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  QObject::connect(table_, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
    if (ctx_) highlightRow(*ctx_, row);
  });
  v->addWidget(table_, 1);
  showResults(ctx);
  return w;
}

}  // namespace fabricad::assembly
