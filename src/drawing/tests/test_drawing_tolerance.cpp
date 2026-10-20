// Tests for dimension tolerances: ISO 286 tables and fits, tolerance text composition, the
// rendered tolerance block and its PDF / SVG / DXF export.
#include "core/Document.h"
#include "core/tests/support/TestFeatures.h"
#include "drawing/DrawingExport.h"
#include "drawing/DrawingModel.h"
#include "drawing/DxfWriter.h"
#include "drawing/SheetRenderer.h"
#include "drawing/Symbols.h"
#include "drawing/Tolerance.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <cmath>

using namespace fabricad;
using namespace fabricad::drawing;

namespace {
bool near(double a, double b, double tol = 1e-9) { return std::abs(a - b) <= tol; }

void checkZone(double nominal, const char* zone, double upper, double lower) {
  auto d = isoZoneDeviations(nominal, zone);
  QVERIFY2(d.has_value(), zone);
  QVERIFY2(near(d->upper, upper, 1e-9) && near(d->lower, lower, 1e-9),
           qPrintable(QString("%1 @ %2: got %3 / %4").arg(zone).arg(nominal).arg(d->upper).arg(d->lower)));
}

bool hasText(const DisplayList& l, const std::string& t) {
  for (const auto& x : l.texts)
    if (x.text == t) return true;
  return false;
}

/// Box drawing with a single horizontal dimension across the top edge (10 mm).
struct Fixture {
  core::Document doc;
  DrawingModel* model = nullptr;
  Sheet* sheet = nullptr;
  core::ObjectId viewId = core::kInvalidId;
  core::ObjectId dimId = core::kInvalidId;
  core::ObjectId diaId = core::kInvalidId;

  Fixture() {
    core::test::registerTestFeatures();
    doc.addFeature(std::make_unique<core::test::BoxFeature>());
    doc.recompute();
    auto hole = std::make_unique<core::test::HoleFeature>();
    hole->prop("body").setRef(core::ShapeRef::body(doc.liveBodies().front()));
    doc.addFeature(std::move(hole));
    doc.recompute();
    model = DrawingModel::forDocument(doc);
    model->setSynchronous(true);
    sheet = &model->addSheet(*sheetFormatByName("A3"), ProjectionStandard::FirstAngle);
    viewId = model->addBaseView(*sheet, {}, ViewOrientation::Top, 2.0, {150, 140}).id;
    model->updateGeometry();
    const DrawingView* v = model->view(viewId);
    for (const auto& c : v->curves) {
      if (c.isLine() && near(c.length(), 10.0, 1e-6) && dimId == core::kInvalidId) {
        Dimension d;
        inferDimension({DimPick{false, {}, c}}, d);
        d.viewId = viewId;
        d.type = DimensionType::Horizontal;
        d.textPos = v->toSheet(c.midpoint()) + Point2{0, -15};
        dimId = model->addDimension(*sheet, d).id;
      }
      if (c.isCircle() && diaId == core::kInvalidId) {
        Dimension d;
        inferDimension({DimPick{false, {}, c}}, d);
        d.viewId = viewId;
        d.textPos = v->toSheet(c.center) + Point2{20, -20};
        diaId = model->addDimension(*sheet, d).id;
      }
    }
  }
  Dimension& dim() { return *sheet->dimension(dimId); }
  Dimension& dia() { return *sheet->dimension(diaId); }
  DisplayList render(const Dimension& d) {
    SheetRenderer r(model->style(), doc.units());
    DisplayList l;
    r.addDimension(l, *model->view(viewId), d);
    return l;
  }
};
}  // namespace

class ToleranceTest : public QObject {
  Q_OBJECT
 private slots:
  void standardTolerances() {
    QCOMPARE(*isoStandardTolerance(20, 7), 21.0);
    QCOMPARE(*isoStandardTolerance(20, 6), 13.0);
    QCOMPARE(*isoStandardTolerance(30, 7), 21.0);     // 30 belongs to "over 18 up to 30"
    QCOMPARE(*isoStandardTolerance(30.01, 7), 25.0);
    QCOMPARE(*isoStandardTolerance(3, 11), 60.0);
    QCOMPARE(*isoStandardTolerance(500, 16), 4000.0);
    QVERIFY(!isoStandardTolerance(0, 7));
    QVERIFY(!isoStandardTolerance(501, 7));
    QVERIFY(!isoStandardTolerance(20, 17));
  }

  void zoneDeviations() {
    checkZone(20, "H7", 0.021, 0);
    checkZone(20, "g6", -0.007, -0.020);
    checkZone(25, "f7", -0.020, -0.041);
    checkZone(20, "h6", 0, -0.013);
    checkZone(20, "k6", 0.015, 0.002);
    checkZone(20, "js6", 0.0065, -0.0065);
    checkZone(20, "K7", 0.006, -0.015);
    checkZone(20, "N7", -0.007, -0.028);
    checkZone(20, "P7", -0.014, -0.035);
    checkZone(10, "M7", 0, -0.015);
    checkZone(60, "s6", 0.072, 0.053);
    checkZone(50, "H8", 0.039, 0);
    checkZone(40, "e8", -0.050, -0.089);
    checkZone(100, "h9", 0, -0.087);
    checkZone(20, "G7", 0.028, 0.007);
    checkZone(2, "K7", 0, -0.010);
    QVERIFY(!isoZoneDeviations(20, "Q7"));
    QVERIFY(!isoZoneDeviations(20, "H"));
    QVERIFY(!isoZoneDeviations(20, "H17"));
    QVERIFY(!isoZoneDeviations(600, "H7"));
  }

  void fits() {
    auto clearance = analyzeFit(20, "H7/g6");
    QVERIFY(clearance);
    QCOMPARE(clearance->kind, FitKind::Clearance);
    QVERIFY(near(clearance->minClearance, 0.007, 1e-9));
    QVERIFY(near(clearance->maxClearance, 0.041, 1e-9));
    auto interference = analyzeFit(20, "H7/p6");
    QVERIFY(interference);
    QCOMPARE(interference->kind, FitKind::Interference);
    QVERIFY(near(interference->maxClearance, -0.001, 1e-9));
    auto transition = analyzeFit(20, "H7 / k6");
    QVERIFY(transition);
    QCOMPARE(transition->kind, FitKind::Transition);
    QVERIFY(!analyzeFit(20, "H7"));
    std::string h, s;
    QVERIFY(splitFit("g6", h, s));
    QVERIFY(h.empty() && s == "g6");
    QVERIFY(!splitFit("H7/", h, s));
  }

  void formatting() {
    QCOMPARE(formatDeviation(0.1, -1), std::string("+0.1"));
    QCOMPARE(formatDeviation(-0.05, -1), std::string("-0.05"));
    QCOMPARE(formatDeviation(0.0, 2), std::string("0"));
    QCOMPARE(formatDeviation(0.021, 3), std::string("+0.021"));
    QCOMPARE(formatValue(20.0, 3), std::string("20"));
    QCOMPARE(formatValue(19.95, 3), std::string("19.95"));
    core::UnitSystem mm;
    DimTolerance t;
    t.mode = ToleranceMode::Symmetric;
    t.upper = 0.1;
    QCOMPARE(composeTolerance(t, 20, "20", "", "", true, 3, mm).after, std::string(" \xC2\xB1" "0.1"));
    t.mode = ToleranceMode::Deviation;
    t.upper = 0.1;
    t.lower = -0.05;
    ToleranceText dev = composeTolerance(t, 20, "20", "", "", true, 3, mm);
    QCOMPARE(dev.upper, std::string("+0.10"));  // same number of decimals as the lower deviation
    QCOMPARE(dev.lower, std::string("-0.05"));
    QVERIFY(dev.stacked() && !dev.stackedMain);
    t.lower = -0.1;
    QCOMPARE(composeTolerance(t, 20, "20", "", "", true, 3, mm).after, std::string(" \xC2\xB1" "0.1"));
    t.mode = ToleranceMode::Limits;
    t.upper = 0.1;
    t.lower = -0.05;
    ToleranceText lim = composeTolerance(t, 20, "\xC3\x98" "20", "\xC3\x98", "", true, 2, mm);
    QVERIFY(lim.stackedMain && lim.main.empty());
    QCOMPARE(lim.upper, std::string("\xC3\x98" "20.10"));
    QCOMPARE(lim.lower, std::string("\xC3\x98" "19.95"));
    t = DimTolerance{};
    t.mode = ToleranceMode::Fit;
    t.fit = "H7/g6";
    QCOMPARE(composeTolerance(t, 20, "\xC3\x98" "20", "\xC3\x98", "", true, 3, mm).after, std::string(" H7/g6"));
    t.fit = "H7";
    t.fitDisplay = FitDisplay::Deviations;
    ToleranceText fd = composeTolerance(t, 20, "20", "", "", true, 3, mm);
    QCOMPARE(fd.upper, std::string("+0.021"));
    QCOMPARE(fd.lower, std::string("0"));
    t.fitDisplay = FitDisplay::DesignationDeviations;
    ToleranceText fdd = composeTolerance(t, 20, "20", "", "", true, 3, mm);
    QCOMPARE(fdd.after, std::string(" H7"));
    QCOMPARE(fdd.upper, std::string("(+0.021"));
    QCOMPARE(fdd.lower, std::string("0)"));
    t.fitDisplay = FitDisplay::Limits;
    ToleranceText fl = composeTolerance(t, 20, "20", "", "", true, 3, mm);
    QCOMPARE(fl.upper, std::string("20.021"));
    QCOMPARE(fl.lower, std::string("20.000"));
    t = DimTolerance{};
    t.mode = ToleranceMode::Reference;
    QCOMPARE(composeTolerance(t, 20, "20", "", "", true, 3, mm).main, std::string("(20)"));
    t.mode = ToleranceMode::Basic;
    QVERIFY(composeTolerance(t, 20, "20", "", "", true, 3, mm).framed);
    // inch documents show deviations in inches
    core::UnitSystem inch;
    inch.length = core::LengthUnit::Inch;
    t = DimTolerance{};
    t.mode = ToleranceMode::Symmetric;
    t.upper = 0.254;
    QCOMPARE(composeTolerance(t, 25.4, "1", "", "", true, 3, inch).after, std::string(" \xC2\xB1" "0.01"));
  }

  void jsonRoundTrip() {
    Dimension d;
    d.tolerance.mode = ToleranceMode::Fit;
    d.tolerance.fit = "H7/g6";
    d.tolerance.fitDisplay = FitDisplay::DesignationDeviations;
    d.tolerance.precision = 3;
    Dimension r = Dimension::fromJson(d.toJson());
    QVERIFY(r.tolerance == d.tolerance);
    Dimension plain;
    QVERIFY(!plain.toJson().contains("tolerance"));
    QVERIFY(!Dimension::fromJson(plain.toJson()).tolerance.isSet());
  }

  void renderedBlock() {
    Fixture fx;
    QVERIFY(fx.dimId != core::kInvalidId && fx.diaId != core::kInvalidId);
    // untoleranced: a single text
    DisplayList plain = fx.render(fx.dim());
    QVERIFY(hasText(plain, "10"));
    fx.dim().tolerance.mode = ToleranceMode::Deviation;
    fx.dim().tolerance.upper = 0.1;
    fx.dim().tolerance.lower = -0.05;
    DisplayList dev = fx.render(fx.dim());
    QVERIFY(hasText(dev, "10"));
    QVERIFY(hasText(dev, "+0.10"));
    QVERIFY(hasText(dev, "-0.05"));
    // the stacked deviations are smaller, right of the value, one above the other
    const DisplayText *mainT = nullptr, *up = nullptr, *lo = nullptr;
    for (const auto& t : dev.texts) {
      if (t.text == "10") mainT = &t;
      if (t.text == "+0.10") up = &t;
      if (t.text == "-0.05") lo = &t;
    }
    QVERIFY(mainT && up && lo);
    QVERIFY(up->height < mainT->height);
    QVERIFY(up->pos.x > mainT->pos.x && near(up->pos.x, lo->pos.x, 1e-9));
    QVERIFY(up->pos.y < lo->pos.y);  // sheet y down: upper deviation on top
    // basic: framed value
    fx.dim().tolerance = DimTolerance{};
    fx.dim().tolerance.mode = ToleranceMode::Basic;
    DisplayList basic = fx.render(fx.dim());
    QCOMPARE(basic.curves.size(), plain.curves.size() + 1);
    // fit on the diameter
    fx.dia().tolerance.mode = ToleranceMode::Fit;
    fx.dia().tolerance.fit = "H7";
    DisplayList fit = fx.render(fx.dia());
    QVERIFY(hasText(fit, "\xC3\x98" "4 H7"));
  }

  void exportedTolerances() {
    Fixture fx;
    fx.dim().tolerance.mode = ToleranceMode::Symmetric;
    fx.dim().tolerance.upper = 0.2;
    fx.dia().tolerance.mode = ToleranceMode::Limits;
    fx.dia().tolerance.upper = 0.012;
    fx.dia().tolerance.lower = 0;
    ExportOptions opts;
    std::string dxf = dxfText(*fx.model, *fx.sheet, opts);
    QVERIFY(dxf.find("10 %%p0.2") != std::string::npos);
    QVERIFY(dxf.find("%%c4.012") != std::string::npos);
    QVERIFY(dxf.find("%%c4.000") != std::string::npos);
    QCOMPARE(DxfWriter::dxfEncodeText("30\xC2\xB0"), std::string("30%%d"));
    QTemporaryDir dir;
    std::string pdf = dir.filePath("tol.pdf").toStdString();
    std::string svg = dir.filePath("tol.svg").toStdString();
    QVERIFY(exportPdf(*fx.model, pdf, opts));
    QVERIFY(exportSvg(*fx.model, *fx.sheet, svg, opts));
    QFile f(QString::fromStdString(svg));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QByteArray content = f.readAll();
    QVERIFY(content.contains("4.012"));
    QVERIFY(QFileInfo(QString::fromStdString(pdf)).size() > 1000);
  }
};

QTEST_MAIN(ToleranceTest)
#include "test_drawing_tolerance.moc"
