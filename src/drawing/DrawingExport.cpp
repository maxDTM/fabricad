// FabriCAD drawing: export implementation.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "drawing/DrawingExport.h"

#include "core/Document.h"
#include "drawing/DrawingModel.h"
#include "drawing/DxfWriter.h"
#include "drawing/SheetRenderer.h"

#include <QFile>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <QRegularExpression>
#include <QSvgGenerator>

#include <sstream>

namespace fabricad::drawing {

namespace {
std::vector<const Sheet*> selectSheets(const DrawingModel& model, const ExportOptions& opts) {
  std::vector<const Sheet*> out;
  for (const auto& s : model.sheets()) {
    if (opts.sheets.empty()) out.push_back(&s);
    else
      for (core::ObjectId id : opts.sheets)
        if (id == s.id) out.push_back(&s);
  }
  return out;
}

void setError(std::string* error, const std::string& msg) {
  if (error) *error = msg;
}

std::string csvEscape(const std::string& s) {
  bool quote = s.find_first_of(",\"\n") != std::string::npos;
  if (!quote) return s;
  std::string out = "\"";
  for (char c : s) {
    if (c == '"') out += '"';
    out += c;
  }
  return out + "\"";
}
}  // namespace

std::string documentTitle(const DrawingModel& model, const Sheet& sheet) {
  auto values = titleBlockValues(model, sheet);
  auto it = values.find("Title");
  if (it != values.end() && !it->second.empty()) return it->second;
  return model.document().name();
}

void paintSheet(QPainter& painter, DrawingModel& model, const Sheet& sheet, bool frame) {
  SheetRenderer r(model.style(), model.document().units());
  DisplayList list;
  r.addSheet(list, sheet, titleBlockValues(model, sheet), frame);
  SheetRenderer::paint(painter, list, SheetRenderer::ink(), model.style().fontFamily);
}

bool exportPdf(DrawingModel& model, const std::string& path, const ExportOptions& opts, std::string* error) {
  auto sheets = selectSheets(model, opts);
  if (sheets.empty()) {
    setError(error, "no sheets to export");
    return false;
  }
  model.waitForIdle();
  QPdfWriter writer(QString::fromStdString(path));
  writer.setResolution(opts.pdfResolution);
  // document title = the title block's Title (DRW-ASM-OUTPUT-10), falling back to the file name
  writer.setTitle(QString::fromStdString(documentTitle(model, *sheets.front())));
  writer.setCreator("FabriCAD");
  auto pageLayout = [&](const Sheet& s) {
    QPageSize size(QSizeF(s.format.width, s.format.height), QPageSize::Millimeter, QString(), QPageSize::ExactMatch);
    return QPageLayout(size, QPageLayout::Portrait, QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);
  };
  writer.setPageLayout(pageLayout(*sheets.front()));
  QPainter painter;
  if (!painter.begin(&writer)) {
    setError(error, "cannot write " + path);
    return false;
  }
  const double unit = opts.pdfResolution / 25.4;  // device pixels per mm
  for (size_t i = 0; i < sheets.size(); ++i) {
    if (i > 0) {
      writer.setPageLayout(pageLayout(*sheets[i]));
      writer.newPage();
    }
    painter.save();
    painter.scale(unit, unit);
    paintSheet(painter, model, *sheets[i], opts.frame);
    painter.restore();
  }
  painter.end();
  return true;
}

bool exportSvg(DrawingModel& model, const Sheet& sheet, const std::string& path, const ExportOptions& opts,
               std::string* error) {
  model.waitForIdle();
  QSvgGenerator gen;
  gen.setFileName(QString::fromStdString(path));
  gen.setTitle(QString::fromStdString(documentTitle(model, sheet) + " - " + sheet.name));
  gen.setDescription("FabriCAD drawing");
  // Size in mm: QSvgGenerator takes the size in pixels at `resolution` dpi; viewBox in those units.
  const int dpi = 254;  // 10 px per mm
  gen.setResolution(dpi);
  const double k = dpi / 25.4;
  gen.setSize(QSize(static_cast<int>(sheet.format.width * k + 0.5), static_cast<int>(sheet.format.height * k + 0.5)));
  gen.setViewBox(QRectF(0, 0, sheet.format.width * k, sheet.format.height * k));
  QPainter painter;
  if (!painter.begin(&gen)) {
    setError(error, "cannot write " + path);
    return false;
  }
  painter.scale(k, k);
  paintSheet(painter, model, sheet, opts.frame);
  painter.end();
  if (!QFile::exists(QString::fromStdString(path))) return false;
  fixSvgDashes(QString::fromStdString(path));
  return true;
}

bool fixSvgDashes(const QString& path) {
  // QSvgGenerator writes a QPen dash pattern as-is, in units of the pen width, while SVG wants
  // user units: a 0.25 mm hidden line came out as 12 mm dashes (DRW-ASM-OUTPUT-1). Scale every
  // dasharray by the stroke-width of the same element.
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return false;
  QString svg = QString::fromUtf8(f.readAll());
  f.close();
  static const QRegularExpression tag(QStringLiteral("<[^<>]*stroke-dasharray=\"[^\"]*\"[^<>]*>"));
  static const QRegularExpression dash(QStringLiteral("stroke-dasharray=\"([^\"]*)\""));
  static const QRegularExpression width(QStringLiteral("stroke-width=\"([0-9.eE+-]+)\""));
  QString out;
  qsizetype last = 0;
  bool changed = false;
  for (auto it = tag.globalMatch(svg); it.hasNext();) {
    auto m = it.next();
    QString t = m.captured(0);
    auto wm = width.match(t);
    auto dm = dash.match(t);
    if (wm.hasMatch() && dm.hasMatch() && dm.captured(1) != QLatin1String("none")) {
      const double w = wm.captured(1).toDouble();
      if (w > 0) {
        QStringList parts;
        for (const QString& v : dm.captured(1).split(',', Qt::SkipEmptyParts)) parts << QString::number(v.trimmed().toDouble() * w, 'g', 6);
        t.replace(dm.capturedStart(0), dm.capturedLength(0), QStringLiteral("stroke-dasharray=\"%1\"").arg(parts.join(',')));
        changed = true;
      }
    }
    out += QStringView(svg).mid(last, m.capturedStart(0) - last);
    out += t;
    last = m.capturedEnd(0);
  }
  if (!changed) return true;
  out += QStringView(svg).mid(last);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
  f.write(out.toUtf8());
  return true;
}

std::string dxfText(DrawingModel& model, const Sheet& sheet, const ExportOptions& opts) {
  model.waitForIdle();
  SheetRenderer r(model.style(), model.document().units());
  DisplayList list;
  r.addSheet(list, sheet, titleBlockValues(model, sheet), opts.frame);
  DxfWriter dxf(sheet.format.width, sheet.format.height);
  dxf.addDisplayList(list);
  return dxf.finish();
}

bool exportDxf(DrawingModel& model, const Sheet& sheet, const std::string& path, const ExportOptions& opts,
               std::string* error) {
  std::string text = dxfText(model, sheet, opts);
  QSaveFile f(QString::fromStdString(path));
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
    setError(error, "cannot write " + path);
    return false;
  }
  f.write(text.data(), static_cast<qint64>(text.size()));
  if (!f.commit()) {
    setError(error, "cannot write " + path);
    return false;
  }
  return true;
}

std::string partsListCsv(DrawingModel& model, const Sheet& sheet) {
  std::vector<BomRow> rows;
  if (!sheet.partsLists.empty()) rows = model.bom(sheet.view(sheet.partsLists.front().viewId));
  else rows = model.bom(nullptr);
  std::ostringstream out;
  out << "Item,Qty,Part Number,Description,Material,Mass\n";
  for (const auto& r : rows) {
    out << r.item << ',' << r.quantity << ',' << csvEscape(r.partNumber) << ',' << csvEscape(r.description) << ','
        << csvEscape(r.material) << ',';
    if (r.mass > 0) out << r.mass;
    out << '\n';
  }
  return out.str();
}

bool exportPartsListCsv(DrawingModel& model, const Sheet& sheet, const std::string& path, std::string* error) {
  std::string text = partsListCsv(model, sheet);
  QSaveFile f(QString::fromStdString(path));
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
    setError(error, "cannot write " + path);
    return false;
  }
  f.write(text.data(), static_cast<qint64>(text.size()));
  return f.commit();
}

}  // namespace fabricad::drawing
