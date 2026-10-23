// Tests for hole / thread callouts (B-rep hole recognition), GD&T frames, datum and surface
// texture symbols, ordinate / baseline / chain dimensions, revision tables, their persistence
// inside sheets, model integration (move, delete, undo) and PDF / SVG / DXF export.
#include "core/Document.h"
#include "core/UndoStack.h"
#include "core/tests/support/TestFeatures.h"
#include "drawing/CalloutRenderer.h"
#include "drawing/Callouts.h"
#include "drawing/DrawingExport.h"
#include "drawing/DrawingModel.h"
#include "drawing/SheetRenderer.h"
#include "support/HoleTestFeatures.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <algorithm>
#include <cmath>

using namespace fabricad;
using namespace fabricad::drawing;

namespace {
bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }

bool hasText(const DisplayList& l, const std::string& t) {
  for (const auto& x : l.texts)
    if (x.text == t) return true;
  return false;
}

std::string plain(const std::vector<RichLine>& lines) {
  std::string s;
  for (const auto& l : lines) s += richLinePlainText(l) + "|";
  return s;
}

/// Hole block in a top view at scale 1 (A3, first angle).
struct Fixture {
  core::Document doc;
  DrawingModel* model = nullptr;
  Sheet* sheet = nullptr;
  core::ObjectId viewId = core::kInvalidId;
  core::ObjectId bodyId = core::kInvalidId;

  Fixture() {
    doc.setName("Block");
    doc.addFeature(std::make_unique<test::HoleBlockFeature>());
    doc.recompute();
    bodyId = doc.liveBodies().front();
    model = DrawingModel::forDocument(doc);
    model->setSynchronous(true);
    sheet = &model->addSheet(*sheetFormatByName("A3"), ProjectionStandard::FirstAngle);
    viewId = model->addBaseView(*sheet, {}, ViewOrientation::Top, 1.0, {150, 140}).id;
    model->updateGeometry();
  }
  const DrawingView& view() const { return *model->view(viewId); }
  /// Full circle of radius r nearest to the world XY position (top view: local = world - centre).
  const Curve2D* circle(double r) const {
    for (const auto& c : view().curves)
      if (c.isCircle() && near(c.radius, r, 1e-6) && c.sourceBody != core::kInvalidId) return &c;
    return nullptr;
  }
  /// View vertex with minimal local x + y (the block corner at world (0, 0)).
  const Vertex2D* corner() const {
    const Vertex2D* best = nullptr;
    for (const auto& v : view().vertices)
      if (!best || v.p.x + v.p.y < best->p.x + best->p.y) best = &v;
    return best;
  }
  CalloutRenderer renderer() const { return CalloutRenderer(model->style(), doc.units()); }
};
}  // namespace

class CalloutsTest : public QObject {
  Q_OBJECT
 private slots:
  void holeRecognition() {
    Fixture fx;
    QVERIFY(fx.view().geometryValid);
    const Curve2D* cb = fx.circle(3.3);
    const Curve2D* cbTop = fx.circle(5.5);
    const Curve2D* cs = fx.circle(2.5);
    const Curve2D* dr = fx.circle(2.0);
    QVERIFY(cb && cbTop && cs && dr);

    HoleInfo a = analyzeHole(fx.doc, *cb);
    QVERIFY(a.ok);
    QCOMPARE(a.type, HoleInfo::Counterbore);
    QVERIFY(near(a.diameter, 6.6) && !a.through && near(a.depth, 15, 1e-4));
    QVERIFY(near(a.cboreDiameter, 11) && near(a.cboreDepth, 6, 1e-4));
    HoleInfo a2 = analyzeHole(fx.doc, *cbTop);  // picking the counterbore circle: same hole
    QCOMPARE(a2.type, HoleInfo::Counterbore);
    QVERIFY(near(a2.diameter, 6.6));

    HoleInfo b = analyzeHole(fx.doc, *cs);
    QVERIFY(b.ok);
    QCOMPARE(b.type, HoleInfo::Countersink);
    QVERIFY(b.through);
    QVERIFY(near(b.csinkDiameter, 10, 1e-4) && near(b.csinkAngle, 90, 1e-4));

    HoleInfo c = analyzeHole(fx.doc, *dr);
    QVERIFY(c.ok);
    QCOMPARE(c.type, HoleInfo::Simple);
    QVERIFY(!c.through && near(c.depth, 8, 1e-4) && near(c.drillAngle, 118, 1e-3));
    QCOMPARE(c.source, std::string("geometry"));

    // the outline of the block is not a hole
    for (const auto& curve : fx.view().curves)
      if (curve.isLine()) {
        QVERIFY(!analyzeHole(fx.doc, curve).ok);
        break;
      }
  }

  void calloutText() {
    Fixture fx;
    HoleCallout call;
    std::string cb = plain(holeCalloutLines(analyzeHole(fx.doc, *fx.circle(3.3)), call, fx.doc.units()));
    QVERIFY2(cb == "\xC3\x98" "6.6 DEEP 15|CBORE  \xC3\x98" "11 DEEP 6|", cb.c_str());
    std::string cs = plain(holeCalloutLines(analyzeHole(fx.doc, *fx.circle(2.5)), call, fx.doc.units()));
    QVERIFY2(cs == "\xC3\x98" "5 THRU|CSK  \xC3\x98" "10 X 90\xC2\xB0|", cs.c_str());
    call.countOverride = 4;
    std::string dr = plain(holeCalloutLines(analyzeHole(fx.doc, *fx.circle(2.0)), call, fx.doc.units()));
    QVERIFY2(dr == "4x \xC3\x98" "4 DEEP 8|", dr.c_str());
    // the callout lines carry the symbols as glyphs, not characters
    auto lines = holeCalloutLines(analyzeHole(fx.doc, *fx.circle(3.3)), HoleCallout{}, fx.doc.units());
    QCOMPARE(lines[1].front().glyph, Glyph::Counterbore);
    // override text
    call = HoleCallout{};
    call.overrideText = "A\nB";
    QCOMPARE(holeCalloutLines(HoleInfo{}, call, fx.doc.units()).size(), size_t(2));
  }

  void threads() {
    double d = 0, p = 0;
    QCOMPARE(metricThreadForHole(6.8, &d, &p), std::string("M8x1.25"));
    QVERIFY(near(d, 8) && near(p, 1.25));
    QCOMPARE(metricThreadForHole(5.0), std::string("M6x1"));
    QCOMPARE(metricThreadForHole(10.0), std::string("M10x1.5"));
    QCOMPARE(metricThreadForHole(7.4), std::string());
    HoleInfo tapped;
    tapped.ok = true;
    tapped.diameter = 6.8;
    tapped.depth = 15;
    HoleCallout c;
    c.thread = true;
    core::UnitSystem mm;
    QCOMPARE(plain(holeCalloutLines(tapped, c, mm)), std::string("M8x1.25 - 6H DEEP 15|\xC3\x98" "6.8 DEEP 15|"));
    tapped.through = true;
    c.threadOverride = "M8x1";
    c.threadClass = "6G";
    QCOMPARE(plain(holeCalloutLines(tapped, c, mm)), std::string("M8x1 - 6G THRU|"));
    // a plain hole callout of a hole with feature thread data reads thread first, then the tap
    // drill, like a thread callout (DRW2-VIEWS-ANNOT-13)
    tapped.thread = "M8x1.25";
    tapped.threadClass = "6H";
    tapped.threadDepth = 12;
    tapped.through = false;
    QCOMPARE(plain(holeCalloutLines(tapped, HoleCallout{}, mm)),
             std::string("M8x1.25 - 6H DEEP 12|\xC3\x98" "6.8 DEEP 15|"));
  }

  void calloutInSheet() {
    Fixture fx;
    const Curve2D* cb = fx.circle(3.3);
    HoleCallout c;
    c.id = fx.model->allocateId();
    c.viewId = fx.viewId;
    c.ref = DimRef::toCurve(*cb);
    c.position = fx.view().toSheet(cb->center) + Point2{15, -15};
    fx.sheet->callouts.holeCallouts.push_back(c);
    fx.model->refreshAnnotations(*fx.sheet);
    const HoleCallout& r = fx.sheet->callouts.holeCallouts.front();
    QVERIFY(!r.orphaned);
    QCOMPARE(r.lines.size(), size_t(2));
    DisplayList l;
    fx.renderer().addHoleCallout(l, *fx.sheet, r);
    QVERIFY(hasText(l, "\xC3\x98" "6.6"));
    QVERIFY(hasText(l, "15"));
    QVERIFY(!l.fills.empty());  // leader arrow
    // the arrow tip touches the circle
    Point2 centre = fx.view().toSheet(cb->center);
    bool onCircle = false;
    for (const auto& f : l.fills)
      if (near(f.polygon[0].distance(centre), 3.3, 1e-6)) onCircle = true;
    QVERIFY(onCircle);
    // an unresolvable reference is reported as orphaned and drawn as "?"
    HoleCallout bad = c;
    bad.id = fx.model->allocateId();
    bad.ref = DimRef{};
    bad.ref.fallback = {1000, 1000};
    fx.sheet->callouts.holeCallouts.push_back(bad);
    fx.model->refreshAnnotations(*fx.sheet);
    QVERIFY(fx.sheet->callouts.holeCallouts.back().orphaned);
    DisplayList e;
    fx.renderer().addHoleCallout(e, *fx.sheet, fx.sheet->callouts.holeCallouts.back());
    QVERIFY(e.texts.size() == 1 && e.texts[0].error);
  }

  void featureControlFrames() {
    FcfRow r;
    QVERIFY(parseFcfRow("position dia 0.05 M A B C", r));
    QCOMPARE(r.characteristic, GdtCharacteristic::Position);
    QVERIFY(r.diameterZone && near(r.tolerance, 0.05));
    QCOMPARE(r.modifier, GdtModifier::MMC);
    QCOMPARE(r.datums.size(), size_t(3));
    QCOMPARE(r.datums[2].letter, std::string("C"));
    QVERIFY(parseFcfRow("perpendicularity 0.02 A M", r));
    QCOMPARE(r.datums.front().modifier, GdtModifier::MMC);
    QCOMPARE(r.modifier, GdtModifier::None);
    QVERIFY(parseFcfRow("flatness 0.01", r));
    QVERIFY(r.datums.empty() && !gdtNeedsDatum(r.characteristic));
    QVERIFY(parseFcfRow("position \xC3\x98" "0.1 A", r));
    QVERIFY(r.diameterZone && near(r.tolerance, 0.1));
    QVERIFY(!parseFcfRow("wobble 0.1", r));
    QVERIFY(!parseFcfRow("position A B", r));

    Fixture fx;
    FeatureControlFrame f;
    parseFcfRow("position dia 0.05 M A B C", f.rows[0]);
    f.position = {200, 60};
    f.anchor = AnchorPoint::onView(fx.view(), fx.view().toSheet(fx.circle(2.0)->center));
    f.above = "3x";
    DisplayList l;
    CalloutRenderer cr = fx.renderer();
    cr.addFeatureFrame(l, *fx.sheet, f);
    QVERIFY(hasText(l, "\xC3\x98" "0.05"));
    QVERIFY(hasText(l, "A") && hasText(l, "B") && hasText(l, "C"));
    QVERIFY(hasText(l, "M"));   // MMC modifier letter in its circle
    QVERIFY(hasText(l, "3x"));
    int circles = 0;
    for (const auto& c : l.curves)
      if (c.isCircle()) ++circles;
    QVERIFY(circles >= 2);  // position glyph + modifier circle
    QCOMPARE(l.fills.size(), size_t(1));  // leader arrow
    const double H = 2 * fx.model->style().dimTextHeight;
    // the frame spans exactly its row width to the right of its position
    double maxX = 0;
    for (const auto& c : l.curves)
      if (c.type == CurveType::Polyline) maxX = std::max(maxX, c.bbox().max.x);
    QVERIFY(near(maxX, 200 + cr.frameRowWidth(f.rows[0]), 1e-9));
    // composite frame: the characteristic cell spans both rows
    FeatureControlFrame comp = f;
    comp.leader = false;
    comp.above.clear();
    FcfRow second;
    parseFcfRow("position dia 0.01 A", second);
    comp.rows.push_back(second);
    DisplayList lc;
    cr.addFeatureFrame(lc, *fx.sheet, comp);
    bool tallCell = false;
    for (const auto& c : lc.curves)
      if (c.type == CurveType::Polyline && near(c.bbox().height(), 2 * H, 1e-9) && near(c.bbox().width(), H, 1e-9)) tallCell = true;
    QVERIFY(tallCell);
    QVERIFY(lc.fills.empty());
    // JSON
    FeatureControlFrame back = FeatureControlFrame::fromJson(comp.toJson());
    QCOMPARE(back.rows.size(), size_t(2));
    QCOMPARE(back.rows[0].datums.size(), size_t(3));
    QCOMPARE(back.rows[0].modifier, GdtModifier::MMC);
    QVERIFY(!back.leader);
  }

  void datumAndSurfaceSymbols() {
    Fixture fx;
    CalloutRenderer cr = fx.renderer();
    DatumFeature d;
    d.letter = "B";
    d.anchor = AnchorPoint::free({100, 100});
    d.position = {100, 85};
    DisplayList ld;
    cr.addDatum(ld, *fx.sheet, d);
    QVERIFY(hasText(ld, "B"));
    QCOMPARE(ld.fills.size(), size_t(1));
    // the triangle base lies on the anchor
    const auto& tri = ld.fills.front().polygon;
    QVERIFY(near((tri[0] + tri[1]).x * 0.5, 100, 1e-9) && near((tri[0] + tri[1]).y * 0.5, 100, 1e-9));
    QVERIFY(tri[2].y < 100);  // apex towards the box (above)

    SurfaceFinish s;
    s.roughness = "Ra 1.6";
    s.method = "ground";
    s.lay = "\xE2\x8A\xA5";
    s.anchor = AnchorPoint::free({120, 120});
    DisplayList ls;
    cr.addSurfaceFinish(ls, *fx.sheet, s);
    QVERIFY(hasText(ls, "Ra 1.6") && hasText(ls, "ground"));
    QVERIFY(ls.fills.empty());  // no leader
    // V + bar (material removal required) + extension line + lay glyph
    size_t required = ls.curves.size();
    s.process = SurfaceProcess::RemovalProhibited;
    DisplayList lp;
    cr.addSurfaceFinish(lp, *fx.sheet, s);
    bool circle = false;
    for (const auto& c : lp.curves)
      if (c.isCircle()) circle = true;
    QVERIFY(circle);
    QCOMPARE(lp.curves.size(), required);
    s.process = SurfaceProcess::Any;
    s.leader = true;
    s.position = {140, 100};
    DisplayList ll;
    cr.addSurfaceFinish(ll, *fx.sheet, s);
    QCOMPARE(ll.fills.size(), size_t(1));
    QVERIFY(near(ll.fills.front().polygon[0].x, 120) && near(ll.fills.front().polygon[0].y, 120));
    SurfaceFinish back = SurfaceFinish::fromJson(s.toJson());
    QCOMPARE(back.roughness, s.roughness);
    QCOMPARE(back.lay, s.lay);
    QCOMPARE(back.process, SurfaceProcess::Any);
    QVERIFY(back.leader);
  }

  void ordinateDimensions() {
    Fixture fx;
    const Vertex2D* o = fx.corner();
    QVERIFY(o);
    OrdinateDimension od;
    od.id = fx.model->allocateId();
    od.viewId = fx.viewId;
    od.axis = OrdinateAxis::X;
    od.origin = DimRef::toVertex(*o);
    Point2 os = fx.view().toSheet(o->p);
    od.originText = {os.x, os.y - 40};
    for (double r : {3.3, 2.5, 2.0}) {
      OrdinateMember m;
      m.ref = DimRef::toCurve(*fx.circle(r));
      m.textPos = {fx.view().toSheet(fx.circle(r)->center).x, od.originText.y};
      od.members.push_back(m);
    }
    updateOrdinate(od, fx.view());
    QVERIFY(od.valid);
    QVERIFY(near(od.members[0].value, 15) && near(od.members[1].value, 35) && near(od.members[2].value, 50));
    od.axis = OrdinateAxis::Y;
    updateOrdinate(od, fx.view());
    QVERIFY(near(od.members[0].value, 20));
    od.axis = OrdinateAxis::X;
    updateOrdinate(od, fx.view());
    fx.sheet->callouts.ordinates.push_back(od);
    DisplayList l;
    fx.renderer().addOrdinate(l, *fx.sheet, od);
    QVERIFY(hasText(l, "0") && hasText(l, "15") && hasText(l, "35") && hasText(l, "50"));
    for (const auto& t : l.texts) QCOMPARE(t.angle, 90.0);  // X ordinates read upwards
    QCOMPARE(l.curves.size(), size_t(4));
    OrdinateDimension back = OrdinateDimension::fromJson(od.toJson());
    QCOMPARE(back.members.size(), size_t(3));
    QCOMPARE(back.axis, OrdinateAxis::X);
  }

  void baselineAndChain() {
    Fixture fx;
    const DrawingView& v = fx.view();
    DimRef origin = DimRef::toVertex(*fx.corner());
    std::vector<DimRef> holes;
    for (double r : {2.0, 3.3, 2.5}) holes.push_back(DimRef::toCurve(*fx.circle(r)));
    Point2 above{150, v.sheetBBox().min.y - 10};
    auto base = makeBaselineDimensions(v, origin, holes, DimensionType::Horizontal, above, 8);
    QCOMPARE(base.size(), size_t(3));
    QVERIFY(near(base[0].value, 15) && near(base[1].value, 35) && near(base[2].value, 50));
    QVERIFY(near(base[0].textPos.y, above.y) && near(base[1].textPos.y, above.y - 8) && near(base[2].textPos.y, above.y - 16));
    for (const auto& d : base) QVERIFY(d.refs.front().isVertex);
    std::vector<DimRef> pts = holes;
    pts.push_back(origin);
    auto chain = makeChainDimensions(v, pts, DimensionType::Horizontal, above);
    QCOMPARE(chain.size(), size_t(3));
    QVERIFY(near(chain[0].value, 15) && near(chain[1].value, 20) && near(chain[2].value, 15));
    for (const auto& d : chain) QVERIFY(near(d.textPos.y, above.y));
    // vertical baseline to the left of the view
    Point2 left{v.sheetBBox().min.x - 10, 140};
    auto vert = makeBaselineDimensions(v, origin, {holes[1]}, DimensionType::Vertical, left, 8);
    QCOMPARE(vert.size(), size_t(1));
    QVERIFY(near(vert[0].value, 20));
  }

  void revisionTable() {
    Fixture fx;
    RevisionTable t;
    QCOMPARE(t.nextRev(), std::string("A"));
    t.rows.push_back({"A", "Initial release", "2026-10-01", "MD"});
    QCOMPARE(t.nextRev(), std::string("B"));
    for (int i = 0; i < 25; ++i) t.rows.push_back({});
    QCOMPARE(t.nextRev(), std::string("AA"));
    t.rows.resize(2);
    t.rows[1] = {"B", "Hole moved", "2026-10-20", "MD"};
    t.id = fx.model->allocateId();
    t.position = defaultRevisionTablePosition(*fx.sheet, t);
    QVERIFY(near(t.position.x + t.width(), fx.sheet->frameRect().max.x));
    QVERIFY(near(t.position.y, fx.sheet->frameRect().min.y));
    fx.sheet->callouts.revisionTables.push_back(t);
    DisplayList l;
    fx.renderer().addRevisionTable(l, t);
    QVERIFY(hasText(l, "Rev") && hasText(l, "Hole moved") && hasText(l, "2026-10-20"));
    // the title block shows the latest revision
    auto values = titleBlockValues(*fx.model, *fx.sheet);
    QCOMPARE(values["Rev"], std::string("B"));
    RevisionTable back = RevisionTable::fromJson(t.toJson());
    QCOMPARE(back.rows.size(), size_t(2));
    QCOMPARE(back.rows[1].description, std::string("Hole moved"));
  }

  void containerAndModel() {
    Fixture fx;
    CalloutSet& cs = fx.sheet->callouts;
    QVERIFY(cs.empty());
    HoleCallout h;
    h.id = fx.model->allocateId();
    h.viewId = fx.viewId;
    h.ref = DimRef::toCurve(*fx.circle(2.5));
    h.position = {200, 100};
    FeatureControlFrame f;
    f.id = fx.model->allocateId();
    f.position = {220, 60};
    f.anchor = AnchorPoint::onView(fx.view(), {150, 140});
    DatumFeature d;
    d.id = fx.model->allocateId();
    d.anchor = AnchorPoint::free({60, 60});
    d.position = {60, 50};
    SurfaceFinish s;
    s.id = fx.model->allocateId();
    s.anchor = AnchorPoint::onView(fx.view(), {150, 120});
    RevisionTable t;
    t.id = fx.model->allocateId();
    QJsonObject before = fx.model->toJson();
    core::UndoStack undo;
    fx.model->edit("Add callouts", &undo, [&]() {
      cs.holeCallouts.push_back(h);
      cs.featureFrames.push_back(f);
      cs.datums.push_back(d);
      cs.surfaceFinishes.push_back(s);
      cs.revisionTables.push_back(t);
      fx.model->refreshAnnotations(*fx.sheet);
    });
    Sheet* sh = fx.model->activeSheet();
    QCOMPARE(sh->callouts.ids().size(), size_t(5));
    QVERIFY(sh->contains(h.id) && sh->contains(t.id));
    auto attached = sh->entitiesOf(fx.viewId);
    QVERIFY(std::find(attached.begin(), attached.end(), h.id) != attached.end());
    QVERIFY(std::find(attached.begin(), attached.end(), f.id) != attached.end());
    QVERIFY(std::find(attached.begin(), attached.end(), d.id) == attached.end());
    QCOMPARE(sh->callouts.viewOf(s.id), fx.viewId);

    // persistence through the sheet JSON
    Sheet copy = Sheet::fromJson(sh->toJson());
    QCOMPARE(copy.callouts.ids(), sh->callouts.ids());

    // moving the view carries attached callouts; free ones stay
    Point2 hp = sh->callouts.holeCallout(h.id)->position;
    Point2 sAnchor = sh->callouts.surfaceFinish(s.id)->anchor.resolve(*sh);
    fx.model->moveView(fx.viewId, fx.view().position + Point2{10, 5});
    sh = fx.model->activeSheet();
    QVERIFY(near(sh->callouts.holeCallout(h.id)->position.x, hp.x + 10));
    QVERIFY(near(sh->callouts.surfaceFinish(s.id)->anchor.resolve(*sh).y, sAnchor.y + 5));
    QVERIFY(near(sh->callouts.datum(d.id)->position.x, 60));
    // moveObjects on callouts
    fx.model->moveObjects(*sh, {d.id, t.id, s.id}, {1, 2});
    QVERIFY(near(sh->callouts.datum(d.id)->position.y, 52));
    QVERIFY(near(sh->callouts.revisionTable(t.id)->position.x, 1));
    QVERIFY(near(sh->callouts.surfaceFinish(s.id)->anchor.resolve(*sh).y, sAnchor.y + 7));

    // removing an entity, then the view (attached callouts go with it)
    QVERIFY(fx.model->removeEntity(d.id));
    QVERIFY(!sh->contains(d.id));
    QVERIFY(fx.model->removeView(fx.viewId));
    sh = fx.model->activeSheet();
    QVERIFY(!sh->contains(h.id) && !sh->contains(f.id) && !sh->contains(s.id));
    QVERIFY(sh->contains(t.id));

    // undo of the creation step restores the state before
    fx.model->fromJson(before);
    QVERIFY(fx.model->activeSheet()->callouts.empty());
    undo.undo();  // undo the "Add callouts" snapshot
    QVERIFY(fx.model->activeSheet()->callouts.empty());
    undo.redo();
    QCOMPARE(fx.model->activeSheet()->callouts.ids().size(), size_t(5));
  }

  void exportAll() {
    Fixture fx;
    CalloutSet& cs = fx.sheet->callouts;
    HoleCallout h;
    h.id = fx.model->allocateId();
    h.viewId = fx.viewId;
    h.ref = DimRef::toCurve(*fx.circle(3.3));
    h.position = {200, 100};
    cs.holeCallouts.push_back(h);
    FeatureControlFrame f;
    f.id = fx.model->allocateId();
    parseFcfRow("position dia 0.1 A", f.rows[0]);
    f.position = {220, 60};
    f.anchor = AnchorPoint::free({170, 120});
    cs.featureFrames.push_back(f);
    DatumFeature d;
    d.id = fx.model->allocateId();
    d.anchor = AnchorPoint::free({120, 170});
    d.position = {120, 185};
    cs.datums.push_back(d);
    SurfaceFinish s;
    s.id = fx.model->allocateId();
    s.roughness = "Ra 0.8";
    s.anchor = AnchorPoint::free({150, 120});
    cs.surfaceFinishes.push_back(s);
    RevisionTable t;
    t.id = fx.model->allocateId();
    t.rows.push_back({"A", "First issue", "2026-10-20", "QA"});
    t.position = defaultRevisionTablePosition(*fx.sheet, t);
    cs.revisionTables.push_back(t);
    fx.model->refreshAnnotations(*fx.sheet);

    SheetRenderer r(fx.model->style(), fx.doc.units());
    DisplayList all;
    r.addSheet(all, *fx.sheet, titleBlockValues(*fx.model, *fx.sheet));
    QVERIFY(hasText(all, "Ra 0.8") && hasText(all, "First issue") && hasText(all, "\xC3\x98" "6.6"));

    ExportOptions opts;
    std::string dxf = dxfText(*fx.model, *fx.sheet, opts);
    QVERIFY(dxf.find("Ra 0.8") != std::string::npos);
    QVERIFY(dxf.find("%%c6.6") != std::string::npos);
    QVERIFY(dxf.find("%%c0.1") != std::string::npos);
    QVERIFY(dxf.find("First issue") != std::string::npos);
    QVERIFY(dxf.find("SOLID") != std::string::npos);  // arrows / datum triangle
    QTemporaryDir dir;
    QString pdf = dir.filePath("callouts.pdf"), svg = dir.filePath("callouts.svg"), dxfPath = dir.filePath("callouts.dxf");
    QVERIFY(exportPdf(*fx.model, pdf.toStdString(), opts));
    QVERIFY(exportSvg(*fx.model, *fx.sheet, svg.toStdString(), opts));
    QVERIFY(exportDxf(*fx.model, *fx.sheet, dxfPath.toStdString(), opts));
    QFile f2(svg);
    QVERIFY(f2.open(QIODevice::ReadOnly));
    QByteArray content = f2.readAll();
    QVERIFY(content.contains("Ra 0.8"));
    QVERIFY(content.contains("First issue"));
    QVERIFY(QFileInfo(pdf).size() > 1000);
    QVERIFY(QFileInfo(dxfPath).size() > 1000);
  }
};

QTEST_MAIN(CalloutsTest)
#include "test_drawing_callouts.moc"
