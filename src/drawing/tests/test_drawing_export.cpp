// Tests for the renderer display list and the PDF / SVG / DXF / CSV exporters (offscreen).
#include "core/Document.h"
#include "core/tests/support/TestFeatures.h"
#include "drawing/DrawingExport.h"
#include "drawing/DrawingModel.h"
#include "drawing/DxfWriter.h"
#include "drawing/SheetRenderer.h"

#include <QFile>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <cmath>

using namespace fabricad;
using namespace fabricad::drawing;

namespace {
bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }

struct Fixture {
  core::Document doc;
  DrawingModel* model = nullptr;
  core::ObjectId bodyId = core::kInvalidId;
  Sheet* sheet = nullptr;
  core::ObjectId viewId = core::kInvalidId;

  Fixture() {
    core::test::registerTestFeatures();
    doc.setName("Bracket");
    doc.addFeature(std::make_unique<core::test::BoxFeature>());
    auto hole = std::make_unique<core::test::HoleFeature>();
    doc.recompute();
    bodyId = doc.liveBodies().front();
    hole->prop("body").setRef(core::ShapeRef::body(bodyId));
    doc.addFeature(std::move(hole));
    doc.recompute();
    model = DrawingModel::forDocument(doc);
    model->setSynchronous(true);
    sheet = &model->addSheet(*sheetFormatByName("A3"), ProjectionStandard::ThirdAngle);
    DrawingView& v = model->addBaseView(*sheet, {}, ViewOrientation::Top, 2.0, {120, 150});
    viewId = v.id;
    model->addProjectedView(*sheet, viewId, ProjectionDirection::Below, {120, 230});
    model->updateGeometry();
    const DrawingView* pv = model->view(viewId);
    for (const auto& c : pv->curves)
      if (c.isCircle()) {
        Dimension d;
        inferDimension({DimPick{false, {}, c}}, d);
        d.viewId = viewId;
        d.textPos = pv->toSheet({14, 14});
        model->addDimension(*sheet, d);
        CenterMark m;
        m.viewId = viewId;
        m.ref = DimRef::toCurve(c);
        model->addCenterMark(*sheet, m);
        break;
      }
    TextNote n;
    n.text = "ALL DIMENSIONS IN MM";
    n.position = {40, 40};
    model->addNote(*sheet, n);
    PartsList pl;
    pl.position = {230, 200};
    model->addPartsList(*sheet, pl);
  }
};

int countOccurrences(const std::string& text, const std::string& needle) {
  int n = 0;
  for (size_t pos = text.find(needle); pos != std::string::npos; pos = text.find(needle, pos + needle.size())) ++n;
  return n;
}
}  // namespace

class ExportTest : public QObject {
  Q_OBJECT
 private slots:
  void displayListCoversSheet() {
    Fixture fx;
    SheetRenderer r(fx.model->style(), fx.doc.units());
    DisplayList list;
    r.addSheet(list, *fx.sheet, titleBlockValues(*fx.model, *fx.sheet));
    QVERIFY(list.curves.size() > 20);
    QVERIFY(!list.texts.empty());
    QVERIFY(!list.fills.empty());  // dimension arrows
    BBox2 b = list.bbox();
    QVERIFY(b.min.x >= -1 && b.max.x <= fx.sheet->format.width + 6);
    // title block auto fill
    auto tv = titleBlockValues(*fx.model, *fx.sheet);
    QCOMPARE(tv["Title"], std::string("Bracket"));
    QCOMPARE(tv["Size"], std::string("A3"));
    QCOMPARE(tv["Sheet"], std::string("1 / 1"));
    QCOMPARE(tv["Scale"], std::string("1:1"));
    // view scale maps geometry: top view box 10x20 at 2:1 -> 20 x 40 mm on the sheet
    const DrawingView* v = fx.model->view(fx.viewId);
    BBox2 sb = v->sheetBBox();
    QVERIFY(near(sb.width(), 20.0, 1e-6) && near(sb.height(), 40.0, 1e-6));
    // circle stays a circle with scaled radius
    DisplayList vl;
    r.addViewCurves(vl, *v);
    bool circle = false;
    for (const auto& c : vl.curves)
      if (c.isCircle() && near(c.radius, 4.0, 1e-6)) circle = true;
    QVERIFY(circle);
  }

  void paintsToImage() {
    Fixture fx;
    QImage img(840, 594, QImage::Format_ARGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    p.scale(2.0, 2.0);
    paintSheet(p, *fx.model, *fx.sheet, true);
    p.end();
    // something dark was drawn inside the view area
    int dark = 0;
    for (int y = 0; y < img.height(); y += 2)
      for (int x = 0; x < img.width(); x += 2)
        if (qGray(img.pixel(x, y)) < 128) ++dark;
    QVERIFY(dark > 200);
  }

  void pdfExport() {
    Fixture fx;
    core::ObjectId firstSheet = fx.sheet->id;
    fx.model->addSheet(*sheetFormatByName("A4"), ProjectionStandard::FirstAngle);  // may reallocate fx.sheet
    QTemporaryDir dir;
    std::string path = (dir.path() + "/out.pdf").toStdString();
    std::string err;
    ExportOptions opts;
    QVERIFY2(exportPdf(*fx.model, path, opts, &err), err.c_str());
    QFile f(QString::fromStdString(path));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QByteArray data = f.readAll();
    QVERIFY(data.startsWith("%PDF"));
    QVERIFY(data.size() > 2000);
    QVERIFY(data.contains("/Count 2"));  // two pages
    // single sheet selection
    opts.sheets = {firstSheet};
    std::string path1 = (dir.path() + "/one.pdf").toStdString();
    QVERIFY(exportPdf(*fx.model, path1, opts, &err));
    QFile f1(QString::fromStdString(path1));
    QVERIFY(f1.open(QIODevice::ReadOnly));
    QVERIFY(f1.readAll().contains("/Count 1"));
  }

  void svgExport() {
    Fixture fx;
    QTemporaryDir dir;
    std::string path = (dir.path() + "/out.svg").toStdString();
    std::string err;
    QVERIFY2(exportSvg(*fx.model, *fx.sheet, path, ExportOptions{}, &err), err.c_str());
    QFile f(QString::fromStdString(path));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QByteArray data = f.readAll();
    QVERIFY(data.contains("<svg"));
    QVERIFY(data.contains("<path") || data.contains("<polyline") || data.contains("<line"));
    QVERIFY(data.contains("ALL DIMENSIONS IN MM"));
  }

  void dxfExport() {
    Fixture fx;
    ExportOptions opts;
    std::string text = dxfText(*fx.model, *fx.sheet, opts);
    QVERIFY(text.find("AC1009") != std::string::npos);
    QVERIFY(text.find("ENTITIES") != std::string::npos);
    QVERIFY(text.rfind("EOF") != std::string::npos);
    QVERIFY(countOccurrences(text, "\nLINE\n") >= 8);
    QVERIFY(countOccurrences(text, "\nCIRCLE\n") >= 1);
    QVERIFY(countOccurrences(text, "\nTEXT\n") >= 3);
    QVERIFY(countOccurrences(text, "\nSOLID\n") >= 2);  // dimension arrows
    QVERIFY(text.find("\nVISIBLE\n") != std::string::npos);
    QVERIFY(text.find("\nCENTER\n") != std::string::npos);
    QVERIFY(text.find("\nDIM\n") != std::string::npos);
    // no locale commas
    QVERIFY(text.find(',') == std::string::npos);
    QTemporaryDir dir;
    std::string path = (dir.path() + "/out.dxf").toStdString();
    std::string err;
    QVERIFY2(exportDxf(*fx.model, *fx.sheet, path, opts, &err), err.c_str());
    QVERIFY(QFile::exists(QString::fromStdString(path)));
  }

  void dxfArcFlip() {
    // An arc in sheet space (y down) from 0 to 90 degrees (sweep +90) must map to a CCW DXF
    // arc from 270 to 360 after the Y flip.
    DxfWriter w(100.0);
    w.addCurve(Curve2D::arc({10, 10}, 5, 0, 90));
    std::string text = w.finish();
    QVERIFY(text.find("\nARC\n") != std::string::npos);
    QVERIFY(text.find(" 50\n270.000000\n") != std::string::npos);
    QVERIFY(text.find(" 51\n0.000000\n") != std::string::npos || text.find(" 51\n360.000000\n") != std::string::npos);
    QVERIFY(text.find(" 20\n90.000000\n") != std::string::npos);  // centre y flipped: 100 - 10
    // negative sweep
    DxfWriter w2(100.0);
    w2.addCurve(Curve2D::arc({0, 0}, 5, 90, -90));
    std::string t2 = w2.finish();
    QVERIFY(t2.find(" 50\n270.000000\n") != std::string::npos);
  }

  void csvExport() {
    Fixture fx;
    std::string csv = partsListCsv(*fx.model, *fx.sheet);
    QVERIFY(csv.rfind("Item,Qty,Part Number", 0) == 0);
    QCOMPARE(countOccurrences(csv, "\n"), 2);  // header + 1 body
    QTemporaryDir dir;
    std::string path = (dir.path() + "/bom.csv").toStdString();
    QVERIFY(exportPartsListCsv(*fx.model, *fx.sheet, path));
    QVERIFY(QFile::exists(QString::fromStdString(path)));
  }
};

QTEST_MAIN(ExportTest)
#include "test_drawing_export.moc"
