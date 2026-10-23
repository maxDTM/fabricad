// FabriCAD part: Export STL command implementation.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/tools/ExportStlTool.h"

#include "core/AppHost.h"
#include "core/Document.h"
#include "core/Selection.h"
#include "part/features/Import.h"
#include "part/tools/PartTools.h"

#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QWidget>

#include <cmath>
#include <cstdlib>
#include <format>

namespace fabricad::part {

namespace {

constexpr double kPi = 3.14159265358979323846;

bool parseDouble(const std::string& text, double& out) {
  if (text.empty()) return false;
  char* end = nullptr;
  out = std::strtod(text.c_str(), &end);
  return end && *end == '\0' && std::isfinite(out);
}

}  // namespace

StlExportSettings stlPreset(StlQuality q) {
  StlExportSettings s;
  switch (q) {
    case StlQuality::Coarse: s.linearDeflection = 0.1; s.angularDeflectionDeg = 20.0; break;
    case StlQuality::Fine: s.linearDeflection = 0.01; s.angularDeflectionDeg = 5.0; break;
    case StlQuality::Medium:
    case StlQuality::Custom: break;
  }
  return s;
}

StlQuality stlQualityOf(const StlExportSettings& s) {
  for (StlQuality q : {StlQuality::Coarse, StlQuality::Medium, StlQuality::Fine}) {
    const StlExportSettings p = stlPreset(q);
    if (std::abs(p.linearDeflection - s.linearDeflection) < 1e-9 && std::abs(p.angularDeflectionDeg - s.angularDeflectionDeg) < 1e-6)
      return q;
  }
  return StlQuality::Custom;
}

const char* stlQualityName(StlQuality q) {
  switch (q) {
    case StlQuality::Coarse: return "coarse";
    case StlQuality::Medium: return "medium";
    case StlQuality::Fine: return "fine";
    case StlQuality::Custom: break;
  }
  return "custom";
}

std::optional<StlQuality> parseStlQuality(const std::string& name) {
  std::string n = name;
  for (char& ch : n) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  if (n == "coarse" || n == "low") return StlQuality::Coarse;
  if (n == "medium" || n == "default" || n == "normal") return StlQuality::Medium;
  if (n == "fine" || n == "high") return StlQuality::Fine;
  return std::nullopt;
}

ExportStlTool::ExportStlTool(core::ToolInfo info) : ToolBase(std::move(info)) {}

bool ExportStlTool::isEnabled(const core::ToolContext& ctx) const {
  return ctx.document && !ctx.document->liveBodies().empty();
}

StlExportSettings ExportStlTool::loadDefaults() {
  StlExportSettings s;
  QSettings q;
  s.linearDeflection = q.value("export/stl/linear", s.linearDeflection).toDouble();
  s.angularDeflectionDeg = q.value("export/stl/angular", s.angularDeflectionDeg).toDouble();
  s.binary = q.value("export/stl/binary", s.binary).toBool();
  if (!(s.linearDeflection > 0)) s.linearDeflection = 0.05;
  if (!(s.angularDeflectionDeg > 0)) s.angularDeflectionDeg = kStlDefaultAngleDeg;
  return s;
}

void ExportStlTool::saveDefaults(const StlExportSettings& s) {
  QSettings q;
  q.setValue("export/stl/linear", s.linearDeflection);
  q.setValue("export/stl/angular", s.angularDeflectionDeg);
  q.setValue("export/stl/binary", s.binary);
}

bool ExportStlTool::applyOption(const std::string& a, StlExportSettings& s) {
  auto value = [&](size_t eq) { return a.substr(eq + 1); };
  const size_t eq = a.find('=');
  std::string key = eq == std::string::npos ? a : a.substr(0, eq);
  for (char& ch : key) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  if (key == "quality" || key == "preset" || key == "refinement") {
    if (eq == std::string::npos) return false;
    if (auto q = parseStlQuality(value(eq))) {
      const StlExportSettings p = stlPreset(*q);
      s.linearDeflection = p.linearDeflection;
      s.angularDeflectionDeg = p.angularDeflectionDeg;
    }
    return true;
  }
  if (eq == std::string::npos && (key == "coarse" || key == "medium" || key == "fine")) {
    const StlExportSettings p = stlPreset(*parseStlQuality(key));
    s.linearDeflection = p.linearDeflection;
    s.angularDeflectionDeg = p.angularDeflectionDeg;
    return true;
  }
  if (key == "deflection" || key == "linear" || key == "lineardeflection") {
    double v;
    std::string t = eq == std::string::npos ? std::string() : value(eq);
    if (t.size() > 2 && t.compare(t.size() - 2, 2, "mm") == 0) t.resize(t.size() - 2);
    if (parseDouble(t, v) && v > 0) s.linearDeflection = v;
    return true;
  }
  if (key == "angle" || key == "angular" || key == "angulardeflection") {
    std::string v = eq == std::string::npos ? std::string() : value(eq);
    const bool rad = v.size() > 3 && v.compare(v.size() - 3, 3, "rad") == 0;
    if (rad) v.resize(v.size() - 3);
    else if (v.size() > 3 && v.compare(v.size() - 3, 3, "deg") == 0) v.resize(v.size() - 3);
    double d;
    if (parseDouble(v, d) && d > 0) s.angularDeflectionDeg = d * (rad ? 180.0 / kPi : 1.0);
    return true;
  }
  if (key == "ascii") {
    s.binary = false;
    return true;
  }
  if (key == "binary") {
    s.binary = true;
    return true;
  }
  if (key == "format" && eq != std::string::npos) {
    std::string v = value(eq);
    for (char& ch : v) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    s.binary = v != "ascii";
    return true;
  }
  return false;
}

void ExportStlTool::activate(core::ToolContext& ctx) {
  // Command line / script: the built-in defaults (Medium: 0.05 mm, 0.2 rad, binary) unless options are typed,
  // so "EXPORTSTL a.stl" always means the same mesh. The dialog starts from the last used settings.
  settings_ = ctx.args.empty() ? loadDefaults() : StlExportSettings{};
  path_.clear();
  if (!ctx.document) {
    ctx.cancel();
    return;
  }
  for (const std::string& a : ctx.args)
    if (!applyOption(a, settings_) && path_.empty()) path_ = a;
  if (!path_.empty()) {
    // command line / script: export at once, like the instant tool did
    if (exportNow(ctx)) ctx.finish();
    else ctx.cancel();
    return;
  }
  if (!(ctx.host && ctx.host->mainWindow())) {
    ctx.showMessage("EXPORTSTL needs a file name: EXPORTSTL out.stl [quality=coarse|medium|fine] [deflection=0.05] [angle=11.5|0.2rad] [ascii]", true);
    ctx.cancel();
    return;
  }
  ctx.setPrompt("Export STL: choose the file and the tessellation, then OK");
  if (ctx.host) ctx.host->setToolAcceptEnabled(true);
}

void ExportStlTool::deactivate(core::ToolContext&) {
  qualityCombo_ = nullptr;
  pathEdit_ = nullptr;
  linearEdit_ = nullptr;
  angularEdit_ = nullptr;
  formatCombo_ = nullptr;
  bodiesLabel_ = nullptr;
}

bool ExportStlTool::commandLineInput(core::ToolContext& ctx, const std::string& text) {
  std::string t = text;
  while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.pop_back();
  while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front()))) t.erase(t.begin());
  if (t.empty()) return false;
  if (applyOption(t, settings_)) {
    if (linearEdit_) linearEdit_->setText(QString::number(settings_.linearDeflection));
    if (angularEdit_) angularEdit_->setText(QString::number(settings_.angularDeflectionDeg, 'g', 6));
    if (formatCombo_) formatCombo_->setCurrentIndex(settings_.binary ? 0 : 1);
    syncQualityCombo();
    ctx.showMessage(std::format("STL settings: {} (linear {} mm, angular {:.4g} deg), {}", stlQualityName(stlQualityOf(settings_)),
                                settings_.linearDeflection, settings_.angularDeflectionDeg, settings_.binary ? "binary" : "ascii"));
    return true;
  }
  if (t.rfind("file=", 0) == 0 || t.rfind("path=", 0) == 0) t = t.substr(5);
  path_ = t;
  if (pathEdit_) pathEdit_->setText(QString::fromStdString(path_));
  ctx.showMessage("file: " + path_);
  return true;
}

void ExportStlTool::readDialog() {
  if (pathEdit_) path_ = pathEdit_->text().trimmed().toStdString();
  if (linearEdit_) {
    double v;
    if (parseDouble(linearEdit_->text().trimmed().toStdString(), v) && v > 0) settings_.linearDeflection = v;
  }
  if (angularEdit_) {
    double v;
    // the field shows 6 digits: an unchanged text keeps the exact value (0.2 rad = 11.4592 deg)
    const QString t = angularEdit_->text().trimmed();
    if (t != QString::number(settings_.angularDeflectionDeg, 'g', 6) && parseDouble(t.toStdString(), v) && v > 0)
      settings_.angularDeflectionDeg = v;
  }
  if (formatCombo_) settings_.binary = formatCombo_->currentIndex() == 0;
}

void ExportStlTool::accept(core::ToolContext& ctx) {
  readDialog();
  if (path_.empty()) {
    QWidget* parent = ctx.host ? ctx.host->mainWindow() : nullptr;
    if (!parent) {
      ctx.showMessage("Export STL: type the file name (or EXPORTSTL out.stl)", true);
      return;
    }
    QString p = QFileDialog::getSaveFileName(parent, "Export STL", QString(), "STL files (*.stl)");
    if (p.isEmpty()) return;
    if (!p.contains('.')) p += ".stl";
    path_ = p.toStdString();
  }
  if (exportNow(ctx)) ctx.finish();
}

void ExportStlTool::syncQualityCombo() {
  if (qualityCombo_) qualityCombo_->setCurrentIndex(static_cast<int>(stlQualityOf(settings_)));
}

bool ExportStlTool::exportNow(core::ToolContext& ctx) {
  std::vector<TopoDS_Shape> shapes;
  std::vector<std::string> names;
  if (!collectExportShapes(ctx, shapes, names)) return false;
  std::string err;
  if (!exportStl(shapes, path_, settings_.binary, settings_.linearDeflection, settings_.angularDeflectionDeg, &err)) {
    ctx.showMessage("export failed: " + err, true);
    return false;
  }
  if (ctx.args.empty()) saveDefaults(settings_);  // remembered for the next dialog, not for typed exports
  ctx.showMessage(std::format("STL mesh ({}): linear deflection {} mm, angular deflection {:.4g} deg ({:.4g} rad), {}",
                              stlQualityName(stlQualityOf(settings_)), settings_.linearDeflection, settings_.angularDeflectionDeg,
                              settings_.angularDeflectionDeg * kPi / 180.0, settings_.binary ? "binary" : "ascii"));
  ctx.showMessage(std::format("exported {} bod{} to {}", shapes.size(), shapes.size() == 1 ? "y" : "ies", path_));
  return true;
}

QWidget* ExportStlTool::createDialog(core::ToolContext& ctx, QWidget* parent) {
  auto* w = new QWidget(parent);
  auto* form = new QFormLayout(w);
  auto* fileRow = new QWidget(w);
  auto* h = new QHBoxLayout(fileRow);
  h->setContentsMargins(0, 0, 0, 0);
  pathEdit_ = new QLineEdit(QString::fromStdString(path_), fileRow);
  pathEdit_->setObjectName("stl_path");  // stable names: tours and GUI tests (docs/plans/tutorial.md 6)
  pathEdit_->setPlaceholderText("out.stl (empty: ask on OK)");
  auto* browse = new QPushButton("...", fileRow);
  browse->setFixedWidth(28);
  h->addWidget(pathEdit_);
  h->addWidget(browse);
  QObject::connect(browse, &QPushButton::clicked, w, [this, w] {
    QString p = QFileDialog::getSaveFileName(w, "Export STL", pathEdit_ ? pathEdit_->text() : QString(), "STL files (*.stl)");
    if (p.isEmpty()) return;
    if (!p.contains('.')) p += ".stl";
    if (pathEdit_) pathEdit_->setText(p);
  });
  form->addRow("File", fileRow);
  qualityCombo_ = new QComboBox(w);
  qualityCombo_->setObjectName("stlQuality");
  qualityCombo_->addItems({"Coarse", "Medium", "Fine", "Custom"});
  qualityCombo_->setToolTip("Coarse 0.1 mm / 20 deg, Medium 0.05 mm / 11.5 deg, Fine 0.01 mm / 5 deg");
  form->addRow("Quality", qualityCombo_);
  linearEdit_ = new QLineEdit(QString::number(settings_.linearDeflection), w);
  linearEdit_->setObjectName("stl_linear");
  linearEdit_->setToolTip("Maximum distance between the mesh and the surface (mm, absolute)");
  form->addRow("Linear deflection (mm)", linearEdit_);
  angularEdit_ = new QLineEdit(QString::number(settings_.angularDeflectionDeg, 'g', 6), w);
  angularEdit_->setObjectName("stl_angular");
  angularEdit_->setToolTip("Maximum angle between neighbouring facet normals (degrees)");
  form->addRow("Angular deflection (deg)", angularEdit_);
  syncQualityCombo();
  QObject::connect(qualityCombo_, &QComboBox::activated, w, [this](int index) {
    const auto q = static_cast<StlQuality>(index);
    if (q == StlQuality::Custom) return;
    const StlExportSettings p = stlPreset(q);
    settings_.linearDeflection = p.linearDeflection;
    settings_.angularDeflectionDeg = p.angularDeflectionDeg;
    if (linearEdit_) linearEdit_->setText(QString::number(settings_.linearDeflection));
    if (angularEdit_) angularEdit_->setText(QString::number(settings_.angularDeflectionDeg, 'g', 6));
  });
  auto edited = [this] {
    readDialog();
    syncQualityCombo();
  };
  QObject::connect(linearEdit_, &QLineEdit::textEdited, w, edited);
  QObject::connect(angularEdit_, &QLineEdit::textEdited, w, edited);
  formatCombo_ = new QComboBox(w);
  formatCombo_->setObjectName("stl_format");
  formatCombo_->addItems({"Binary", "ASCII"});
  formatCombo_->setCurrentIndex(settings_.binary ? 0 : 1);
  form->addRow("Format", formatCombo_);
  size_t n = 0;
  if (ctx.document) {
    if (ctx.selection) n = ctx.selection->itemsOfKind(core::mask(core::ShapeKind::Body)).size();
    if (n == 0) n = ctx.document->liveBodies(true).size();
  }
  bodiesLabel_ = new QLabel(QString::fromStdString(std::format("{} bod{} (selected bodies, or all visible), world placement", n,
                                                               n == 1 ? "y" : "ies")),
                            w);
  form->addRow("Export", bodiesLabel_);
  return w;
}

}  // namespace fabricad::part
