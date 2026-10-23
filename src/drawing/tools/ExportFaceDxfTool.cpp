// FabriCAD drawing: Export Face DXF tool.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "drawing/tools/ExportFaceDxfTool.h"

#include "core/AppHost.h"
#include "core/Document.h"
#include "core/Feature.h"
#include "core/Selection.h"
#include "core/ShapeRef.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRep_Builder.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QWidget>

#include <format>

namespace fabricad::drawing {

namespace {

core::ToolInfo faceDxfInfo() {
  core::ToolInfo i;
  i.id = "part.export-face-dxf";
  i.name = "Export Face DXF";
  i.tooltip = "1:1 DXF of a planar face, coplanar faces or a sketch for laser / waterjet / router (OUTER, INNER, ETCH layers)";
  i.icon = i.id;
  i.aliases = {"EXPORTDXF", "FACEDXF"};
  i.workspace = core::Workspace::Design;
  i.tab = "TOOLS";
  i.group = "EXPORT";
  i.order = 30;
  i.promoted = false;
  i.repeatable = false;
  return i;
}

/// Construction edges: in allEdges(true) but not in allEdges(false) (matched by midpoint).
TopoDS_Shape constructionOnly(const TopoDS_Shape& all, const TopoDS_Shape& real) {
  std::vector<gp_Pnt> mids;
  for (TopExp_Explorer ex(real, TopAbs_EDGE); ex.More(); ex.Next()) {
    BRepAdaptor_Curve c(TopoDS::Edge(ex.Current()));
    mids.push_back(c.Value(0.5 * (c.FirstParameter() + c.LastParameter())));
  }
  BRep_Builder b;
  TopoDS_Compound out;
  b.MakeCompound(out);
  for (TopExp_Explorer ex(all, TopAbs_EDGE); ex.More(); ex.Next()) {
    BRepAdaptor_Curve c(TopoDS::Edge(ex.Current()));
    const gp_Pnt m = c.Value(0.5 * (c.FirstParameter() + c.LastParameter()));
    bool found = false;
    for (const gp_Pnt& p : mids) found = found || p.Distance(m) < 1e-7;
    if (!found) b.Add(out, ex.Current());
  }
  return out;
}

}  // namespace

ExportFaceDxfTool::ExportFaceDxfTool() : ToolBase(faceDxfInfo()) {}

bool ExportFaceDxfTool::isEnabled(const core::ToolContext& ctx) const {
  if (!ctx.document) return false;
  // EXPORTDXF in the drawing workspace is the sheet export
  return !(ctx.host && ctx.host->workspace() == core::Workspace::Drawing);
}

FaceDxfResult ExportFaceDxfTool::build(const core::Document& doc, const std::vector<core::ShapeRef>& refs, const FaceDxfOptions& o) {
  std::vector<TopoDS_Face> faces;
  for (const core::ShapeRef& r : refs) {
    if (r.kind == core::ShapeKind::Face) {
      const TopoDS_Shape& body = doc.bodyShape(r.owner);
      if (body.IsNull()) continue;
      TopoDS_Shape f = core::subShapeAt(body, TopAbs_FACE, r.index);
      if (!f.IsNull() && f.ShapeType() == TopAbs_FACE) faces.push_back(TopoDS::Face(f));
      continue;
    }
    if (r.kind == core::ShapeKind::Sketch || r.kind == core::ShapeKind::SketchCurve || r.kind == core::ShapeKind::Profile ||
        r.kind == core::ShapeKind::SketchPoint) {
      const core::Feature* f = doc.feature(r.owner);
      const core::IProfileSource* src = f ? f->profileSource() : nullptr;
      if (!src) continue;
      const TopoDS_Shape real = src->allEdges(false);
      const TopoDS_Shape all = o.etch ? src->allEdges(true) : TopoDS_Shape();
      return sketchDxf(src->sketchPlane(), real, all.IsNull() ? all : constructionOnly(all, real), o);
    }
  }
  return faceDxf(faces, o);
}

void ExportFaceDxfTool::activate(core::ToolContext& ctx) {
  options_ = FaceDxfOptions{};
  options_.version = faceDxfVersionDefault();
  if (ctx.document) options_.inches = ctx.document->units().length == core::LengthUnit::Inch;
  path_.clear();
  refs_ = ctx.preselection;
  if (!ctx.document) {
    ctx.cancel();
    return;
  }
  for (const std::string& a : ctx.args)
    if (!applyFaceDxfOption(a, options_) && path_.empty()) path_ = a;
  if (!path_.empty()) {
    if (exportNow(ctx)) ctx.finish();
    else ctx.cancel();
    return;
  }
  if (!(ctx.host && ctx.host->mainWindow())) {
    ctx.showMessage("EXPORTDXF needs a file name: EXPORTDXF out.dxf [r12|r2000] [units=mm|in] [arcs=0.01] with a planar face or a sketch selected", true);
    ctx.cancel();
    return;
  }
  ctx.setPrompt("Export Face DXF: select a planar face (or coplanar faces) or a sketch, choose the file, then OK");
  if (ctx.host) ctx.host->setToolAcceptEnabled(true);
}

void ExportFaceDxfTool::deactivate(core::ToolContext&) {
  pathEdit_ = nullptr;
  versionCombo_ = nullptr;
  unitsCombo_ = nullptr;
  arcsEdit_ = nullptr;
  etchCheck_ = nullptr;
}

bool ExportFaceDxfTool::commandLineInput(core::ToolContext& ctx, const std::string& text) {
  std::string t = text;
  while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.pop_back();
  while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front()))) t.erase(t.begin());
  if (t.empty()) return false;
  if (applyFaceDxfOption(t, options_)) {
    if (versionCombo_) versionCombo_->setCurrentIndex(options_.version == DxfWriter::Version::R12 ? 1 : 0);
    if (unitsCombo_) unitsCombo_->setCurrentIndex(options_.inches ? 1 : 0);
    if (arcsEdit_) arcsEdit_->setText(options_.arcTolerance > 0 ? QString::number(options_.arcTolerance) : QString());
    if (etchCheck_) etchCheck_->setChecked(options_.etch);
    return true;
  }
  if (t.rfind("file=", 0) == 0 || t.rfind("path=", 0) == 0) t = t.substr(5);
  path_ = t;
  if (pathEdit_) pathEdit_->setText(QString::fromStdString(path_));
  ctx.showMessage("file: " + path_);
  return true;
}

void ExportFaceDxfTool::readDialog() {
  if (pathEdit_) path_ = pathEdit_->text().trimmed().toStdString();
  if (versionCombo_) options_.version = versionCombo_->currentIndex() == 1 ? DxfWriter::Version::R12 : DxfWriter::Version::R2000;
  if (unitsCombo_) options_.inches = unitsCombo_->currentIndex() == 1;
  if (arcsEdit_) {
    bool ok = false;
    const double v = arcsEdit_->text().trimmed().toDouble(&ok);
    options_.arcTolerance = ok && v > 0 ? v : 0.0;
  }
  if (etchCheck_) options_.etch = etchCheck_->isChecked();
}

void ExportFaceDxfTool::accept(core::ToolContext& ctx) {
  readDialog();
  if (ctx.selection && !ctx.selection->items().empty()) refs_ = ctx.selection->items();
  if (path_.empty()) {
    QWidget* parent = ctx.host ? ctx.host->mainWindow() : nullptr;
    if (!parent) {
      ctx.showMessage("Export Face DXF: type the file name", true);
      return;
    }
    QString p = QFileDialog::getSaveFileName(parent, "Export Face DXF", QString(), "DXF files (*.dxf)");
    if (p.isEmpty()) return;
    if (!p.contains('.')) p += ".dxf";
    path_ = p.toStdString();
  }
  if (exportNow(ctx)) ctx.finish();
}

bool ExportFaceDxfTool::exportNow(core::ToolContext& ctx) {
  if (refs_.empty() && ctx.selection) refs_ = ctx.selection->items();
  if (QFileInfo(QString::fromStdString(path_)).suffix().isEmpty()) path_ += ".dxf";
  FaceDxfResult r = build(*ctx.document, refs_, options_);
  if (!r.ok) {
    ctx.showMessage("export failed: " + r.error, true);
    return false;
  }
  QFile f(QString::fromStdString(path_));
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(QByteArray::fromStdString(r.dxf)) < 0) {
    ctx.showMessage("export failed: could not write " + path_, true);
    return false;
  }
  if (ctx.args.empty()) setFaceDxfVersionDefault(options_.version);
  const char* u = options_.inches ? "in" : "mm";
  ctx.showMessage(std::format("exported {} ({}, {}): {:.2f} x {:.2f} {}, {} outer / {} inner contour(s){}", path_,
                              options_.version == DxfWriter::Version::R12 ? "R12" : "R2000", u, r.width, r.height, u, r.outer, r.inner,
                              r.etch ? std::format(", {} etch", r.etch) : std::string()));
  return true;
}

QWidget* ExportFaceDxfTool::createDialog(core::ToolContext& ctx, QWidget* parent) {
  (void)ctx;
  auto* w = new QWidget(parent);
  auto* form = new QFormLayout(w);
  auto* fileRow = new QWidget(w);
  auto* h = new QHBoxLayout(fileRow);
  h->setContentsMargins(0, 0, 0, 0);
  pathEdit_ = new QLineEdit(QString::fromStdString(path_), fileRow);
  pathEdit_->setObjectName("facedxf_path");
  pathEdit_->setPlaceholderText("out.dxf");
  pathEdit_->setToolTip("Empty: a file dialog opens on OK");
  auto* browse = new QPushButton("Browse", fileRow);
  browse->setToolTip("Choose the file in a dialog");
  h->addWidget(pathEdit_);
  h->addWidget(browse);
  QObject::connect(browse, &QPushButton::clicked, w, [this, w] {
    QString p = QFileDialog::getSaveFileName(w, "Export Face DXF", pathEdit_ ? pathEdit_->text() : QString(), "DXF files (*.dxf)");
    if (p.isEmpty()) return;
    if (!p.contains('.')) p += ".dxf";
    if (pathEdit_) pathEdit_->setText(p);
  });
  form->addRow("File", fileRow);
  versionCombo_ = new QComboBox(w);
  versionCombo_->setObjectName("facedxf_version");
  versionCombo_->addItems({"R2000", "R12"});
  versionCombo_->setCurrentIndex(options_.version == DxfWriter::Version::R12 ? 1 : 0);
  versionCombo_->setToolTip("R2000: LWPOLYLINE, SPLINE and $INSUNITS; R12 for old controllers (polylines and arcs only)");
  form->addRow("Version", versionCombo_);
  unitsCombo_ = new QComboBox(w);
  unitsCombo_->setObjectName("facedxf_units");
  unitsCombo_->addItems({"mm", "in"});
  unitsCombo_->setCurrentIndex(options_.inches ? 1 : 0);
  form->addRow("Units", unitsCombo_);
  arcsEdit_ = new QLineEdit(options_.arcTolerance > 0 ? QString::number(options_.arcTolerance) : QString(), w);
  arcsEdit_->setObjectName("facedxf_arcs");
  arcsEdit_->setPlaceholderText("off: splines");
  arcsEdit_->setToolTip("Approximate splines as arcs within this tolerance (mm or in), for laser controllers without SPLINE");
  form->addRow("Arc tolerance", arcsEdit_);
  etchCheck_ = new QCheckBox("Etch layer", w);
  etchCheck_->setToolTip("Sketch construction lines on the ETCH layer");
  etchCheck_->setObjectName("facedxf_etch");
  etchCheck_->setChecked(options_.etch);
  form->addRow("", etchCheck_);
  auto* note = new QLabel("1:1 from outside the face, origin at its lower-left corner (sketch: the sketch origin). Layers OUTER, INNER, ETCH.", w);
  note->setWordWrap(true);
  note->setObjectName("StatusDim");
  form->addRow(note);
  return w;
}

}  // namespace fabricad::drawing
