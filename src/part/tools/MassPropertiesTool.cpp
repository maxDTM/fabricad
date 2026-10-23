// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/tools/MassPropertiesTool.h"

#include "core/AppHost.h"
#include "core/Document.h"
#include "core/Selection.h"
#include "core/ShapeRef.h"
#include "core/Units.h"

#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSaveFile>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <format>

namespace fabricad::part {

using core::ObjectId;
using core::ShapeKind;
using core::ToolContext;
using core::Vec3;

MassPropertiesTool::MassPropertiesTool(core::ToolInfo info) : ToolBase(std::move(info)) {}

bool MassPropertiesTool::inch(const ToolContext& ctx) const {
  if (!ctx.document) return false;
  const auto l = ctx.document->units().length;
  return l == core::LengthUnit::Inch || l == core::LengthUnit::Foot;
}

void MassPropertiesTool::recompute(ToolContext& ctx) {
  components_.clear();
  bodies_.clear();
  std::vector<core::ShapeRef> refs = ctx.selection ? ctx.selection->items() : ctx.preselection;
  if (refs.empty()) refs = ctx.preselection;
  for (const core::ShapeRef& r : refs) {
    if (r.kind == ShapeKind::Component && ctx.document->component(r.owner)) {
      if (std::find(components_.begin(), components_.end(), r.owner) == components_.end()) components_.push_back(r.owner);
    } else if (ctx.document->body(r.owner)) {
      if (std::find(bodies_.begin(), bodies_.end(), r.owner) == bodies_.end()) bodies_.push_back(r.owner);
    }
  }
  if (components_.empty() && bodies_.empty()) components_.push_back(ctx.document->activeComponent());
  result_ = computeMassProperties(massItemsFor(*ctx.document, components_, bodies_));
}

void MassPropertiesTool::activate(ToolContext& ctx) {
  ctx_ = &ctx;
  if (!ctx.document) {
    ctx.cancel();
    return;
  }
  std::string report;
  for (const std::string& a : ctx.args) {
    if (a.rfind("report=", 0) == 0) report = a.substr(7);
    else if (a.find('.') != std::string::npos) report = a;
  }
  recompute(ctx);
  const bool gui = ctx.host && ctx.host->mainWindow();
  if (!report.empty() || !ctx.args.empty() || !gui) {
    const bool in = inch(ctx);
    const double L = in ? 1 / 25.4 : 1.0;
    ctx.showMessage(std::format("Mass properties: {} bodies, volume {:.3f} {}3, mass {:.3f} {}, CoG ({:.3f}, {:.3f}, {:.3f}) {}{}",
                                result_.bodies, result_.volume * L * L * L, in ? "in" : "mm",
                                in ? result_.mass / 453.59237 : result_.mass, in ? "lb" : "g", result_.cog.x * L,
                                result_.cog.y * L, result_.cog.z * L, in ? "in" : "mm",
                                result_.bodiesWithoutDensity ? std::format(" ({} without density)", result_.bodiesWithoutDensity)
                                                             : std::string()));
    if (!report.empty()) {
      std::string err;
      if (!writeReport(ctx, report, &err)) {
        ctx.showMessage(err, true);
        ctx.cancel();
        return;
      }
      ctx.showMessage("Mass properties report written to " + report);
    }
    ctx.finish();
    return;
  }
  ctx.setPrompt("Mass Properties: select bodies or components (none: the active component), OK to close");
  showResult(ctx);
}

void MassPropertiesTool::deactivate(ToolContext& ctx) {
  if (ctx.viewer) {
    for (core::PreviewHandle h : previews_) ctx.viewer->removePreview(h);
    ctx.viewer->requestRedraw();
  }
  previews_.clear();
  table_ = nullptr;
  scopeLabel_ = nullptr;
  ctx_ = nullptr;
}

void MassPropertiesTool::selectionChanged(ToolContext& ctx) {
  if (!ctx.document) return;
  recompute(ctx);
  showResult(ctx);
}

void MassPropertiesTool::documentChanged(ToolContext& ctx) { selectionChanged(ctx); }

bool MassPropertiesTool::commandLineInput(ToolContext& ctx, const std::string& text) {
  std::string t = text;
  while (!t.empty() && t.back() == ' ') t.pop_back();
  if (t.empty()) return false;
  std::string up = t;
  for (char& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  if (up == "COPY") {
    if (QClipboard* cb = QApplication::clipboard()) cb->setText(QString::fromStdString(massPropertiesText(result_, inch(ctx), ctx.document->units().precision)));
    ctx.showMessage("Mass properties copied");
    return true;
  }
  if (t.rfind("report=", 0) == 0) t = t.substr(7);
  std::string err;
  if (writeReport(ctx, t, &err)) ctx.showMessage("Mass properties report written to " + t);
  else ctx.showMessage(err, true);
  return true;
}

bool MassPropertiesTool::writeReport(ToolContext& ctx, const std::string& path, std::string* err) const {
  std::string lower = path;
  for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  const bool md = lower.ends_with(".md") || lower.ends_with(".markdown");
  const std::string text = md ? massPropertiesMarkdown(result_, inch(ctx), ctx.document ? ctx.document->units().precision : 3)
                              : massPropertiesCsv(result_, inch(ctx));
  QSaveFile f(QString::fromStdString(path));
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text) || f.write(text.data(), static_cast<qint64>(text.size())) < 0 || !f.commit()) {
    if (err) *err = "Cannot write " + path;
    return false;
  }
  return true;
}

void MassPropertiesTool::showResult(ToolContext& ctx) {
  if (ctx.viewer) {
    for (core::PreviewHandle h : previews_) ctx.viewer->removePreview(h);
    previews_.clear();
    if (result_.bodies > 0) {
      // CoG marker plus the principal axes as a small triad scaled to the part
      double size = 10.0;
      if (result_.hasBox) {
        const Vec3 d = result_.bboxMax - result_.bboxMin;
        size = std::max(1.0, 0.25 * std::max({d.x, d.y, d.z}));
      }
      core::PreviewStyle pt = core::PreviewStyle::inference();
      pt.dashed = false;
      pt.pointSize = 9.0f;
      previews_.push_back(ctx.viewer->showPreviewPoints({result_.cog}, pt));
      core::PreviewStyle axes = core::PreviewStyle::inference();
      axes.dashed = false;
      axes.lineWidth = 2.0f;
      std::vector<Vec3> seg;
      for (const Vec3& a : result_.principalAxes) {
        seg.push_back(result_.cog - a * size);
        seg.push_back(result_.cog + a * size);
      }
      previews_.push_back(ctx.viewer->showPreviewLines(seg, axes));
      previews_.push_back(ctx.viewer->showPreviewText(result_.cog, result_.massBased ? "CoG" : "Centroid", pt));
    }
    ctx.viewer->requestRedraw();
  }
  if (scopeLabel_) {
    std::string scope;
    if (!bodies_.empty() || components_.size() != 1 || components_.front() != core::kRootComponentId)
      scope = std::format("{} bod{} in scope", result_.bodies, result_.bodies == 1 ? "y" : "ies");
    else scope = std::format("Whole design: {} bod{}", result_.bodies, result_.bodies == 1 ? "y" : "ies");
    if (result_.bodiesWithoutDensity) scope += std::format(", {} without density (set a Physical Material)", result_.bodiesWithoutDensity);
    scopeLabel_->setText(QString::fromStdString(scope));
  }
  if (table_) {
    const std::string tsv = massPropertiesText(result_, inch(ctx), ctx.document ? ctx.document->units().precision : 3);
    std::vector<std::array<std::string, 2>> rows;
    size_t pos = 0;
    while (pos < tsv.size()) {
      size_t nl = tsv.find('\n', pos);
      if (nl == std::string::npos) nl = tsv.size();
      const std::string line = tsv.substr(pos, nl - pos);
      pos = nl + 1;
      const size_t t1 = line.find('\t');
      if (t1 == std::string::npos) continue;
      std::string value = line.substr(t1 + 1);
      if (const size_t t2 = value.find('\t'); t2 != std::string::npos) value = value.substr(0, t2) + " " + value.substr(t2 + 1);
      rows.push_back({line.substr(0, t1), value});
    }
    table_->setRowCount(static_cast<int>(rows.size()));
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
      auto* label = new QTableWidgetItem(QString::fromStdString(rows[static_cast<size_t>(i)][0]));
      auto* value = new QTableWidgetItem(QString::fromStdString(rows[static_cast<size_t>(i)][1]));
      label->setFlags(label->flags() & ~Qt::ItemIsEditable);
      value->setFlags(value->flags() & ~Qt::ItemIsEditable);
      table_->setItem(i, 0, label);
      table_->setItem(i, 1, value);
    }
    table_->resizeRowsToContents();
  }
}

QWidget* MassPropertiesTool::createDialog(ToolContext& ctx, QWidget* parent) {
  auto* w = new QWidget(parent);
  auto* v = new QVBoxLayout(w);
  v->setContentsMargins(6, 6, 6, 6);
  scopeLabel_ = new QLabel(w);
  scopeLabel_->setObjectName("massPropsScope");
  scopeLabel_->setWordWrap(true);
  v->addWidget(scopeLabel_);
  table_ = new QTableWidget(0, 2, w);
  table_->setObjectName("massPropsTable");
  table_->setHorizontalHeaderLabels({"Property", "Value"});
  table_->horizontalHeader()->setStretchLastSection(true);
  table_->verticalHeader()->setVisible(false);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setMinimumHeight(240);
  v->addWidget(table_);
  auto* bottom = new QHBoxLayout();
  auto* copy = new QPushButton("Copy", w);
  copy->setObjectName("massPropsCopy");
  copy->setToolTip("Copy all values as tab-separated text");
  QObject::connect(copy, &QPushButton::clicked, this, [this] {
    if (!ctx_) return;
    if (QClipboard* cb = QApplication::clipboard())
      cb->setText(QString::fromStdString(massPropertiesText(result_, inch(*ctx_), ctx_->document->units().precision)));
    ctx_->showMessage("Mass properties copied");
  });
  auto* report = new QPushButton("Write report...", w);
  report->setObjectName("massPropsReport");
  QObject::connect(report, &QPushButton::clicked, this, [this, w] {
    if (!ctx_) return;
    QString p = QFileDialog::getSaveFileName(w, "Mass properties report", QString(), "CSV (*.csv);;Markdown (*.md)");
    if (p.isEmpty()) return;
    if (!p.contains('.')) p += ".csv";
    std::string err;
    if (writeReport(*ctx_, p.toStdString(), &err)) ctx_->showMessage("Mass properties report written to " + p.toStdString());
    else ctx_->showMessage(err, true);
  });
  bottom->addStretch(1);
  bottom->addWidget(copy);
  bottom->addWidget(report);
  v->addLayout(bottom);
  showResult(ctx);
  return w;
}

}  // namespace fabricad::part
