// FabriCAD drawing: tool registration and export tools.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "drawing/tools/DrawingTools.h"

#include "core/AppHost.h"
#include "core/Document.h"
#include "core/Tool.h"
#include "drawing/DrawingExport.h"
#include "drawing/Sheet.h"
#include "drawing/tools/AnnotationTools.h"
#include "drawing/tools/CalloutTools.h"
#include "drawing/tools/ViewTools.h"

#include <QDir>
#include <QFileDialog>
#include "drawing/tools/ExportFaceDxfTool.h"

#include <QFileInfo>
#include <QString>

#include <memory>

namespace fabricad::drawing {

namespace {

const char* formatName(ExportTool::Format f) {
  switch (f) {
    case ExportTool::Format::Pdf: return "PDF";
    case ExportTool::Format::Svg: return "SVG";
    case ExportTool::Format::Dxf: return "DXF";
    case ExportTool::Format::Csv: return "CSV";
  }
  return "";
}

const char* formatSuffix(ExportTool::Format f) {
  switch (f) {
    case ExportTool::Format::Pdf: return ".pdf";
    case ExportTool::Format::Svg: return ".svg";
    case ExportTool::Format::Dxf: return ".dxf";
    case ExportTool::Format::Csv: return ".csv";
  }
  return "";
}

core::ToolInfo exportInfo(ExportTool::Format f) {
  switch (f) {
    case ExportTool::Format::Pdf:
      return drawingToolInfo("drawing.export-pdf", "Output PDF", "Export all sheets as a vector PDF", "EXPORT", 10, true, {},
                             {"EXPORTPDF", "PDF"});
    case ExportTool::Format::Svg:
      return drawingToolInfo("drawing.export-svg", "Output SVG", "Export the active sheet as SVG", "EXPORT", 20, false, {},
                             {"EXPORTSVG", "SVG"});
    case ExportTool::Format::Dxf:
      return drawingToolInfo("drawing.export-dxf", "Output DXF", "Export the active sheet as DXF R12", "EXPORT", 30, true, {},
                             {"EXPORTDXF", "DXF"});
    case ExportTool::Format::Csv:
      return drawingToolInfo("drawing.export-csv", "Output CSV", "Export the parts list as CSV", "EXPORT", 40, false, {},
                             {"EXPORTCSV", "CSV"});
  }
  return {};
}

/// Instant tool: centre marks on every circle of the selected views.
class AutoCenterMarksTool : public DrawingToolBase {
 public:
  AutoCenterMarksTool()
      : DrawingToolBase(drawingToolInfo("drawing.auto-center-marks", "Center Mark Pattern",
                                        "Centre marks on all circles of the selected views", "GEOMETRY", 30, false, {},
                                        {"AUTOCENTERMARKS"})) {}
  bool isInstant() const override { return true; }
  void activate(core::ToolContext& ctx) override {
    Sheet* s = activeSheet(ctx);
    if (!s) return;
    CenterMarkTool helper;
    std::vector<core::ObjectId> ids;
    if (DrawingView* v = pickedView(ctx, *s)) ids.push_back(v->id);
    if (ids.empty())
      for (const auto& v : s->views) ids.push_back(v.id);
    int n = 0;
    for (core::ObjectId id : ids) n += helper.markAllCircles(ctx, id);
    ctx.showMessage(std::to_string(n) + " centre mark(s) added");
  }
};

/// Instant tool: balloon every BOM item of the selected view.
class BalloonAllTool : public DrawingToolBase {
 public:
  BalloonAllTool()
      : DrawingToolBase(drawingToolInfo("drawing.balloon-all", "Balloon All", "Balloons for every parts-list item of a view",
                                        "TABLES", 30, false, {}, {"BALLOONALL"})) {}
  bool isInstant() const override { return true; }
  void activate(core::ToolContext& ctx) override {
    Sheet* s = activeSheet(ctx);
    if (!s) return;
    BalloonTool helper;
    std::vector<core::ObjectId> ids;
    if (DrawingView* v = pickedView(ctx, *s)) ids.push_back(v->id);
    if (ids.empty())
      for (const auto& v : s->views) ids.push_back(v.id);
    int n = 0;
    for (core::ObjectId id : ids) n += helper.balloonAll(ctx, id);
    ctx.showMessage(std::to_string(n) + " balloon(s) added");
  }
};

}  // namespace

// ---------------------------------------------------------------------------------------
// ExportTool
// ---------------------------------------------------------------------------------------

ExportTool::ExportTool(Format format) : DrawingToolBase(exportInfo(format)), format_(format) {}

bool ExportTool::exportTo(core::ToolContext& ctx, const std::string& path, std::string* error) {
  DrawingModel* m = model(ctx);
  Sheet* s = activeSheet(ctx);
  if (!m || !s) {
    if (error) *error = "no drawing sheet";
    return false;
  }
  // make sure all views are projected before writing
  if (!m->waitForIdle(120000)) {
    if (error) *error = "views are still updating";
    return false;
  }
  ExportOptions opts;
  switch (format_) {
    case Format::Pdf: return exportPdf(*m, path, opts, error);
    case Format::Svg: return exportSvg(*m, *s, path, opts, error);
    case Format::Dxf: return exportDxf(*m, *s, path, opts, error);
    case Format::Csv: return exportPartsListCsv(*m, *s, path, error);
  }
  return false;
}

void ExportTool::activate(core::ToolContext& ctx) {
  if (!model(ctx) || !activeSheet(ctx)) {
    ctx.showMessage("Nothing to export: the drawing has no sheet", true);
    return;
  }
  std::string path;
  if (!ctx.args.empty()) path = ctx.args[0];
  if (path.empty()) {
    QWidget* parent = ctx.host ? ctx.host->mainWindow() : nullptr;
    if (!parent) {
      ctx.showMessage(std::string("Export ") + formatName(format_) + ": give the output path as argument", true);
      return;
    }
    QString filter = QStringLiteral("%1 files (*%2)").arg(formatName(format_), formatSuffix(format_));
    QString suggested = QString::fromStdString(suggestedExportName(ctx)) + formatSuffix(format_);
    // DRW2-ASM-OUTPUT-8: start in the last export folder, else the document's folder, else
    // home; never the process working directory (a quick Enter wrote into the source tree)
    static QString lastDir;
    QString dir = lastDir;
    if (dir.isEmpty() && ctx.document && !ctx.document->filePath().empty())
      dir = QFileInfo(QString::fromStdString(ctx.document->filePath())).absolutePath();
    if (dir.isEmpty() || !QFileInfo(dir).isDir()) dir = QDir::homePath();
    suggested = QDir(dir).filePath(suggested);
    QString chosen = QFileDialog::getSaveFileName(parent, tr("Output %1").arg(formatName(format_)), suggested, filter);
    if (chosen.isEmpty()) return;
    if (QFileInfo(chosen).suffix().isEmpty()) chosen += formatSuffix(format_);
    lastDir = QFileInfo(chosen).absolutePath();
    path = chosen.toStdString();
  }
  std::string error;
  if (exportTo(ctx, path, &error)) {
    std::string msg = std::string("Exported ") + formatName(format_) + " to " + path;
    // SVG and DXF hold one sheet: say which, when there are more (DRW-ASM-OUTPUT-8)
    const DrawingModel* m = model(ctx);
    if ((format_ == Format::Svg || format_ == Format::Dxf) && m && m->sheets().size() > 1)
      msg += " (sheet " + activeSheet(ctx)->name + " only; activate another sheet to export it)";
    ctx.showMessage(msg);
  } else {
    ctx.showMessage(std::string("Export failed: ") + error, true);
  }
}

std::string ExportTool::suggestedExportName(core::ToolContext& ctx) {
  DrawingModel* m = model(ctx);
  const Sheet* s = activeSheet(ctx);
  std::string base = ctx.document ? ctx.document->name() : std::string();
  if ((base.empty() || base == "Untitled") && m && s) base = documentTitle(*m, *s);
  if (base.empty()) base = "drawing";
  // file-name safe
  for (char& c : base)
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
  if ((format_ == Format::Svg || format_ == Format::Dxf) && m && s && m->sheets().size() > 1) base += "-" + s->name;
  return base;
}

// ---------------------------------------------------------------------------------------
// registration
// ---------------------------------------------------------------------------------------

void registerDrawingTools(core::ToolRegistry& tools) {
  // CREATE
  tools.registerTool(std::make_unique<NewDrawingTool>());
  tools.registerTool(std::make_unique<NewSheetTool>());
  tools.registerTool(std::make_unique<BaseViewTool>());
  tools.registerTool(std::make_unique<ProjectedViewTool>());
  tools.registerTool(std::make_unique<SectionViewTool>());
  tools.registerTool(std::make_unique<DetailViewTool>());
  tools.registerTool(std::make_unique<MoveTool>());
  tools.registerTool(std::make_unique<DeleteTool>());
  // MODIFY
  tools.registerTool(std::make_unique<EditViewTool>());
  tools.registerTool(std::make_unique<EditSheetTool>());
  tools.registerTool(std::make_unique<EditTitleBlockTool>());
  tools.registerTool(std::make_unique<ToggleViewOptionTool>(ToggleViewOptionTool::Option::HiddenLines));
  tools.registerTool(std::make_unique<ToggleViewOptionTool>(ToggleViewOptionTool::Option::TangentEdges));
  tools.registerTool(std::make_unique<DeleteSheetTool>());
  // GEOMETRY
  tools.registerTool(std::make_unique<CenterMarkTool>());
  tools.registerTool(std::make_unique<CenterLineTool>());
  tools.registerTool(std::make_unique<AutoCenterMarksTool>());
  // DIMENSIONS
  tools.registerTool(std::make_unique<DimensionTool>());
  tools.registerTool(std::make_unique<EditDimensionTool>());
  // TEXT
  tools.registerTool(std::make_unique<TextTool>(false));
  tools.registerTool(std::make_unique<TextTool>(true));
  // TABLES
  tools.registerTool(std::make_unique<PartsListTool>());
  tools.registerTool(std::make_unique<BalloonTool>());
  tools.registerTool(std::make_unique<BalloonAllTool>());
  // EXPORT
  registerCalloutTools(tools);  // round 2: callouts, GD&T, ordinate/baseline/chain, sections, breaks

  tools.registerTool(std::make_unique<ExportTool>(ExportTool::Format::Pdf));
  tools.registerTool(std::make_unique<ExportTool>(ExportTool::Format::Svg));
  tools.registerTool(std::make_unique<ExportTool>(ExportTool::Format::Dxf));
  tools.registerTool(std::make_unique<ExportTool>(ExportTool::Format::Csv));
  // P3: 1:1 DXF of a face / sketch (Design workspace). Registered after the sheet export so that
  // EXPORTDXF keeps meaning the sheet export wherever that one is enabled (drawing workspace).
  tools.registerTool(std::make_unique<ExportFaceDxfTool>());
}

}  // namespace fabricad::drawing
