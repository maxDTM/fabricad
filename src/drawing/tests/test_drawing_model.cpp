// Tests for DrawingModel: sheets, views, projection through the Document, associative
// dimensions, undo, persistence, parts lists.
#include "core/Commands.h"
#include "core/Document.h"
#include "core/tests/support/TestFeatures.h"
#include "drawing/DrawingModel.h"

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
  core::ObjectId boxFeature = core::kInvalidId;
  core::ObjectId bodyId = core::kInvalidId;

  Fixture() {
    core::test::registerTestFeatures();
    auto box = std::make_unique<core::test::BoxFeature>();  // 10 x 20 x 30
    core::Feature* f = doc.addFeature(std::move(box));
    boxFeature = f->id();
    doc.recompute();
    bodyId = doc.liveBodies().front();
    model = DrawingModel::forDocument(doc);
    model->setSynchronous(true);
  }
};

int countClass(const std::vector<Curve2D>& cs, LineClass cls) {
  int n = 0;
  for (const auto& c : cs)
    if (c.cls == cls) ++n;
  return n;
}

const Curve2D* topEdge(const DrawingView& v) {
  // horizontal visible segment with the largest y
  const Curve2D* best = nullptr;
  for (const auto& c : v.curves)
    if (c.cls == LineClass::Visible && c.type == CurveType::Segment && near(c.a.y, c.b.y, 1e-6))
      if (!best || c.a.y > best->a.y) best = &c;
  return best;
}
}  // namespace

class ModelTest : public QObject {
  Q_OBJECT
 private slots:
  void modelPerDocument() {
    core::Document doc;
    QVERIFY(DrawingModel::find(doc) == nullptr);
    DrawingModel* m = DrawingModel::forDocument(doc);
    QVERIFY(m != nullptr);
    QCOMPARE(DrawingModel::forDocument(doc), m);
    QCOMPARE(DrawingModel::find(doc), m);
    QCOMPARE(m->parent(), &doc);
  }

  void sheetsAndFormats() {
    Fixture fx;
    QVERIFY(sheetFormatByName("A4").has_value());
    QVERIFY(near(sheetFormatByName("A4")->width, 297));
    QVERIFY(sheetFormatByName("ANSI B").has_value());
    QVERIFY(!sheetFormatByName("Letterbox").has_value());
    Sheet& s = fx.model->addSheet(*sheetFormatByName("A3"), ProjectionStandard::ThirdAngle);
    QCOMPARE(s.name, std::string("Sheet1"));
    QCOMPARE(fx.model->activeSheetId(), s.id);
    BBox2 f = s.frameRect();
    QVERIFY(near(f.min.x, 20) && near(f.min.y, 10) && near(f.max.x, 410) && near(f.max.y, 287));
    BBox2 tb = s.titleBlockRect();
    QVERIFY(near(tb.max.x, 410) && near(tb.max.y, 287) && near(tb.width(), 180));
    Sheet& s2 = fx.model->addSheet(*sheetFormatByName("A4"), ProjectionStandard::FirstAngle);
    QCOMPARE(s2.name, std::string("Sheet2"));
    QVERIFY(fx.doc.isModified());
    QVERIFY(fx.model->removeSheet(s2.id));
    QCOMPARE(fx.model->sheets().size(), size_t(1));
  }

  void baseViewProjects() {
    Fixture fx;
    Sheet& s = fx.model->addSheet(*sheetFormatByName("A3"), ProjectionStandard::ThirdAngle);
    DrawingView& v = fx.model->addBaseView(s, {core::ShapeRef::body(fx.bodyId)}, ViewOrientation::Front, 1.0, {100, 150});
    QVERIFY(!v.geometryValid);
    fx.model->updateGeometry();
    QVERIFY(!fx.model->isBusy());
    const DrawingView* pv = fx.model->view(v.id);
    QVERIFY(pv->geometryValid);
    QCOMPARE(countClass(pv->curves, LineClass::Visible), 4);
    QVERIFY(near(pv->bbox.width(), 10.0, 1e-6));
    QVERIFY(near(pv->bbox.height(), 30.0, 1e-6));
    BBox2 sb = pv->sheetBBox();
    QVERIFY(near(sb.min.x, 95) && near(sb.max.x, 105) && near(sb.min.y, 135) && near(sb.max.y, 165));
    QCOMPARE(pv->label, std::string("VIEW1"));
    // all visible bodies when sources are empty
    DrawingView& all = fx.model->addBaseView(s, {}, ViewOrientation::Top, 2.0, {250, 150});
    fx.model->updateGeometry();
    QVERIFY(fx.model->view(all.id)->geometryValid);
    QVERIFY(near(fx.model->view(all.id)->bbox.width(), 10.0, 1e-6));
    QVERIFY(near(fx.model->view(all.id)->bbox.height(), 20.0, 1e-6));
  }

  void projectedViews() {
    Fixture fx;
    Sheet& s = fx.model->addSheet(*sheetFormatByName("A3"), ProjectionStandard::ThirdAngle);
    DrawingView& base = fx.model->addBaseView(s, {core::ShapeRef::body(fx.bodyId)}, ViewOrientation::Front, 1.0, {100, 150});
    core::ObjectId baseId = base.id;
    DrawingView* right = fx.model->addProjectedView(s, baseId, ProjectionDirection::Right, {180, 142});
    // each returned pointer is only valid until the next view is added (the views vector grows)
    QVERIFY(right);
    QCOMPARE(right->alignment, ViewAlignment::Horizontal);
    QVERIFY(near(right->position.y, 150));  // aligned with the parent
    const core::ObjectId rightId = right->id;
    DrawingView* above = fx.model->addProjectedView(s, baseId, ProjectionDirection::Above, {103, 80});
    QCOMPARE(above->alignment, ViewAlignment::Vertical);
    QVERIFY(near(above->position.x, 100));
    const core::ObjectId aboveId = above->id;
    DrawingView* iso = fx.model->addProjectedView(s, baseId, ProjectionDirection::AboveRight, {200, 80});
    QCOMPARE(iso->alignment, ViewAlignment::None);
    const core::ObjectId isoId = iso->id;
    fx.model->updateGeometry();
    // third angle: right view shows Y x Z = 20 x 30, top view shows X x Y = 10 x 20
    QVERIFY(near(fx.model->view(rightId)->bbox.width(), 20.0, 1e-6));
    QVERIFY(near(fx.model->view(rightId)->bbox.height(), 30.0, 1e-6));
    QVERIFY(near(fx.model->view(aboveId)->bbox.width(), 10.0, 1e-6));
    QVERIFY(near(fx.model->view(aboveId)->bbox.height(), 20.0, 1e-6));
    QCOMPARE(countClass(fx.model->view(isoId)->curves, LineClass::Visible), 9);
    // moving the parent moves children; moving a child is constrained
    fx.model->moveView(baseId, {110, 160});
    QVERIFY(near(fx.model->view(rightId)->position.x, 190) && near(fx.model->view(rightId)->position.y, 160));
    QVERIFY(near(fx.model->view(aboveId)->position.x, 110) && near(fx.model->view(aboveId)->position.y, 90));
    fx.model->moveView(rightId, {220, 50});
    QVERIFY(near(fx.model->view(rightId)->position.x, 220) && near(fx.model->view(rightId)->position.y, 160));
    // removing the base removes the children
    QVERIFY(fx.model->removeView(baseId));
    QVERIFY(fx.model->view(rightId) == nullptr);
    QVERIFY(fx.model->view(aboveId) == nullptr);
    QVERIFY(s.views.empty());
  }

  // Projected / section views never sit on their parent, on each other or outside the frame.
  void autoPlacementKeepsViewsApart() {
    Fixture fx;
    Sheet& s = fx.model->addSheet(*sheetFormatByName("A3"), ProjectionStandard::ThirdAngle);
    DrawingView& base = fx.model->addBaseView(s, {core::ShapeRef::body(fx.bodyId)}, ViewOrientation::Front, 1.0, {100, 150});
    const core::ObjectId baseId = base.id;
    fx.model->updateGeometry();
    const BBox2 pb = fx.model->view(baseId)->sheetBBox();
    // far too close on the right (would overlap), far off the sheet above, on top of it for the section
    const core::ObjectId rightId = fx.model->addProjectedView(s, baseId, ProjectionDirection::Right, {pb.max.x + 1, 150})->id;
    const core::ObjectId aboveId = fx.model->addProjectedView(s, baseId, ProjectionDirection::Above, {100, -500})->id;
    SectionLine cut;
    cut.p1 = {-5, -40};
    cut.p2 = {-5, 40};  // vertical cut, view goes to the right of the parent
    cut.arrow = {1, 0};
    const core::ObjectId sectionId = fx.model->addSectionView(s, baseId, cut, {pb.max.x - 1, 150})->id;
    fx.model->updateGeometry();
    const BBox2 frame = s.frameRect();
    for (core::ObjectId id : {baseId, rightId, aboveId, sectionId}) {
      const DrawingView* v = fx.model->view(id);
      QVERIFY(v && v->geometryValid);
      BBox2 b = v->sheetBBox();
      QVERIFY2(b.min.x >= frame.min.x && b.max.x <= frame.max.x && b.min.y >= frame.min.y && b.max.y <= frame.max.y,
               qPrintable(QString("%1 outside: %2..%3 x %4..%5").arg(v->label.c_str()).arg(b.min.x).arg(b.max.x).arg(b.min.y).arg(b.max.y)));
    }
    const BBox2 rb = fx.model->view(rightId)->sheetBBox(), ab = fx.model->view(aboveId)->sheetBBox(),
                sb = fx.model->view(sectionId)->sheetBBox();
    QVERIFY2(rb.min.x >= pb.max.x + 15.0 - 1e-6, qPrintable(QString::number(rb.min.x - pb.max.x)));
    QVERIFY(near(fx.model->view(rightId)->position.y, 150));  // still aligned
    QVERIFY2(ab.max.y <= pb.min.y - 15.0 + 1e-6, qPrintable(QString::number(pb.min.y - ab.max.y)));
    QVERIFY(near(fx.model->view(aboveId)->position.x, 100));
    // the section went right (along the cut normal) and clears both the parent and the right view
    auto overlap = [](const BBox2& a, const BBox2& b) {
      return a.min.x < b.max.x && b.min.x < a.max.x && a.min.y < b.max.y && b.min.y < a.max.y;
    };
    QVERIFY(!overlap(sb, pb));
    QVERIFY(!overlap(sb, rb));
    QVERIFY(!overlap(rb, ab));
    QVERIFY(!fx.model->view(sectionId)->autoPlace);  // placed once; the user owns it from now on
    // a user move is final: dropping the right view onto the parent is not corrected
    fx.model->moveView(rightId, {pb.max.x + 1, 150});
    fx.model->updateGeometry();
    QVERIFY(near(fx.model->view(rightId)->position.x, pb.max.x + 1));
  }

  void firstAngleProjection() {
    Fixture fx;
    Sheet& s = fx.model->addSheet(*sheetFormatByName("A3"), ProjectionStandard::FirstAngle);
    DrawingView& base = fx.model->addBaseView(s, {core::ShapeRef::body(fx.bodyId)}, ViewOrientation::Front, 1.0, {100, 150});
    DrawingView* right = fx.model->addProjectedView(s, base.id, ProjectionDirection::Right, {180, 150});
    core::ObjectId id = right->id;
    fx.model->updateGeometry();
    // first angle: the view to the right is the LEFT side view; viewer direction -X
    const DrawingView* v = fx.model->view(id);
    QVERIFY(near(v->frame.Direction().X(), -1.0, 1e-9));
    QVERIFY(near(v->bbox.width(), 20.0, 1e-6));
  }

  void dimensionsAreAssociative() {
    Fixture fx;
    Sheet& s = fx.model->addSheet(*sheetFormatByName("A3"), ProjectionStandard::ThirdAngle);
    DrawingView& base = fx.model->addBaseView(s, {core::ShapeRef::body(fx.bodyId)}, ViewOrientation::Front, 2.0, {100, 150});
    core::ObjectId viewId = base.id;
    fx.model->updateGeometry();
    const DrawingView* v = fx.model->view(viewId);
    const Curve2D* top = topEdge(*v);
    QVERIFY(top);
    QVERIFY(near(top->length(), 10.0));
    // "D" on a single line -> linear
    Dimension d;
    QVERIFY(inferDimension({DimPick{false, {}, *top}}, d));
    QCOMPARE(d.type, DimensionType::Linear);
    d.viewId = viewId;
    d.textPos = v->toSheet({0, 25});  // above the view: horizontal
    Dimension& added = fx.model->addDimension(s, d);
    QVERIFY(added.valid);
    QVERIFY(near(added.value, 10.0, 1e-6));
    core::ObjectId dimId = added.id;
    core::UnitSystem units;
    units.precision = 2;
    QCOMPARE(formatDimensionText(added, units), std::string("10"));
    DimLayout L = layoutDimension(added, *v, fx.model->style(), units);
    QVERIFY(L.ok);
    QCOMPARE(L.arrows.size(), size_t(2));
    QVERIFY(L.lines.size() >= 3);
    QCOMPARE(L.text, std::string("10"));
    // Radius / diameter on a circle
    Curve2D circ = Curve2D::circle({0, 0}, 4);
    Dimension rd;
    QVERIFY(inferDimension({DimPick{false, {}, circ}}, rd));
    QCOMPARE(rd.type, DimensionType::Diameter);
    Curve2D arc = Curve2D::arc({0, 0}, 4, 0, 90);
    QVERIFY(inferDimension({DimPick{false, {}, arc}}, rd));
    QCOMPARE(rd.type, DimensionType::Radius);
    // two non-parallel lines -> angle
    Dimension ad;
    QVERIFY(inferDimension({DimPick{false, {}, Curve2D::segment({0, 0}, {10, 0})}, DimPick{false, {}, Curve2D::segment({0, 0}, {0, 10})}}, ad));
    QCOMPARE(ad.type, DimensionType::Angle);

    // Now change the model: box width 10 -> 14. The dimension follows the edge.
    fx.doc.undoStack().push(std::make_unique<core::ModifyFeatureCommand>(
        fx.doc, fx.boxFeature, [](core::Feature& f) { f.prop("dx").setDouble(14.0); }));
    QVERIFY(!fx.model->isBusy());
    const Dimension* dim = fx.model->sheet(s.id)->dimension(dimId);
    QVERIFY(dim);
    QVERIFY(dim->valid);
    QVERIFY2(near(dim->value, 14.0, 1e-6), qPrintable(QString::number(dim->value)));
    // undo the model change: back to 10
    fx.doc.undoStack().undo();
    dim = fx.model->sheet(s.id)->dimension(dimId);
    QVERIFY(near(dim->value, 10.0, 1e-6));
  }

  // Edge indices are renumbered by every upstream edit; refs resolve by history name first,
  // then by the body-local signature, and an index hit far from the pick is refused (orphan).
  void dimensionRefsResolveByNameAndOrphan() {
    auto makeView = [](double topY) {
      DrawingView v;
      v.geometryValid = true;
      v.position = {100, 100};
      Curve2D a = Curve2D::segment({0, topY}, {20, topY});
      a.sourceBody = 7;
      a.sourceEdge = 1;
      a.sourceName = "{Extrude1/End|Extrude1/Side/c1}";
      a.sourceSig.geomType = 0;
      a.sourceSig.center = core::Vec3(10, topY, 0);
      a.sourceSig.size = 20;
      Curve2D b = Curve2D::segment({0, 0}, {20, 0});
      b.sourceBody = 7;
      b.sourceEdge = 2;
      b.sourceName = "{Extrude1/Start|Extrude1/Side/c3}";
      b.sourceSig.geomType = 0;
      b.sourceSig.center = core::Vec3(10, 0, 0);
      b.sourceSig.size = 20;
      v.curves = {a, b};
      for (const auto& c : v.curves) v.bbox.add(c.bbox());
      v.vertices = collectVertices(v.curves, 1e-3);
      return v;
    };
    DrawingView v = makeView(10);
    DimRef ref = DimRef::toCurve(v.curves[0]);
    QCOMPARE(ref.name, v.curves[0].sourceName);
    QVERIFY(ref.sig.isValid());
    QCOMPARE(ref.edge, 1);
    DimTarget t = resolveDimRef(ref, v);
    QVERIFY(t.ok);
    QCOMPARE(t.method, 1);
    // renumbered and moved (model edit): the named edge is found, not the one with the old index
    DrawingView v2 = makeView(14);
    std::swap(v2.curves[0].sourceEdge, v2.curves[1].sourceEdge);
    v2.vertices = collectVertices(v2.curves, 1e-3);
    t = resolveDimRef(ref, v2);
    QVERIFY(t.ok);
    QCOMPARE(t.method, 1);
    QVERIFY(near(t.curve.a.y, 14.0, 1e-9));
    // the vertex of that edge too
    DimRef vref = DimRef::toVertex(v.vertices[0]);
    QVERIFY(!vref.name.empty());
    t = resolveDimRef(vref, v2);
    QVERIFY(t.ok);
    QVERIFY(t.isVertex);
    QVERIFY(near(t.point.y, 14.0, 1e-9));
    // names lost, index 1 now denotes the far edge: orphaned, never silently re-attached
    DrawingView v3 = v2;
    for (auto& c : v3.curves) c.sourceName.clear();
    for (auto& c : v3.curves) c.sourceSig = core::GeomSignature{};
    t = resolveDimRef(ref, v3);
    QVERIFY(!t.ok);
    Dimension d;
    d.type = DimensionType::Linear;
    d.refs = {ref};
    QVERIFY(!updateDimension(d, v3));
    QVERIFY(d.orphaned);
    QCOMPARE(formatDimensionText(d, core::UnitSystem{}), std::string("?"));
    // signature fallback: unnamed but geometrically identical edge, renumbered
    DrawingView v4 = makeView(10);
    for (auto& c : v4.curves) c.sourceName.clear();
    std::swap(v4.curves[0].sourceEdge, v4.curves[1].sourceEdge);
    DimRef ref2 = ref;
    ref2.name.clear();
    t = resolveDimRef(ref2, v4);
    QVERIFY(t.ok);
    QCOMPARE(t.method, 2);
    QVERIFY(near(t.curve.a.y, 10.0, 1e-9));
    // index path still works when nothing else is known and the edge is where it was
    DimRef ref3 = ref;
    ref3.name.clear();
    ref3.sig = core::GeomSignature{};
    DrawingView v5 = makeView(10);
    for (auto& c : v5.curves) c.sourceName.clear();
    t = resolveDimRef(ref3, v5);
    QVERIFY(t.ok);
    QCOMPARE(t.method, 3);
    // JSON round trip keeps the identity
    DimRef rt = DimRef::fromJson(ref.toJson());
    QCOMPARE(rt.name, ref.name);
    QVERIFY(rt.sig.isValid());
    QCOMPARE(rt.edge, ref.edge);
  }

  void verticalAndAlignedMeasurement() {
    Fixture fx;
    Sheet& s = fx.model->addSheet(*sheetFormatByName("A3"), ProjectionStandard::ThirdAngle);
    DrawingView& base = fx.model->addBaseView(s, {core::ShapeRef::body(fx.bodyId)}, ViewOrientation::Front, 1.0, {100, 150});
    fx.model->updateGeometry();
    const DrawingView* v = fx.model->view(base.id);
    // two opposite corners: (-5,-15) and (5,15)
    const Vertex2D* a = v->pickVertex({-5, -15}, 1e-3);
    const Vertex2D* b = v->pickVertex({5, 15}, 1e-3);
    QVERIFY(a && b);
    Dimension d;
    QVERIFY(inferDimension({DimPick{true, *a, {}}, DimPick{true, *b, {}}}, d));
    d.viewId = base.id;
    d.textPos = v->toSheet({20, 0});  // to the right: vertical
    Dimension& dv = fx.model->addDimension(s, d);
    QVERIFY(near(dv.value, 30.0, 1e-6));
    d.textPos = v->toSheet({0, 30});  // above: horizontal
    Dimension& dh = fx.model->addDimension(s, d);
    QVERIFY(near(dh.value, 10.0, 1e-6));
    d.type = DimensionType::Aligned;
    Dimension& da = fx.model->addDimension(s, d);
    QVERIFY(near(da.value, std::sqrt(100.0 + 900.0), 1e-6));
  }

  void detailAndSectionViews() {
    Fixture fx;
    Sheet& s = fx.model->addSheet(*sheetFormatByName("A3"), ProjectionStandard::ThirdAngle);
    DrawingView& base = fx.model->addBaseView(s, {core::ShapeRef::body(fx.bodyId)}, ViewOrientation::Front, 1.0, {100, 150});
    core::ObjectId baseId = base.id;
    fx.model->updateGeometry();
    DetailSpec spec;
    spec.center = {5, 15};  // top-right corner
    spec.radius = 4;
    DrawingView* det = fx.model->addDetailView(s, baseId, spec, 2.0, {250, 100});
    QVERIFY(det);
    QCOMPARE(det->label, std::string("DETAIL A"));
    QVERIFY(det->geometryValid);
    // two clipped edges + the detail circle
    QCOMPARE(countClass(det->curves, LineClass::Visible), 2);
    QCOMPARE(countClass(det->curves, LineClass::Detail), 1);
    for (const auto& c : det->curves)
      if (c.cls == LineClass::Visible) QVERIFY(c.length() <= 4.0 + 1e-6);
    core::ObjectId detId = det->id;

    SectionLine line;
    line.p1 = {0, -20};
    line.p2 = {0, 20};
    line.arrow = {-1, 0};
    DrawingView* sec = fx.model->addSectionView(s, baseId, line, {200, 150});
    QVERIFY(sec);
    QCOMPARE(sec->label, std::string("B-B"));
    core::ObjectId secId = sec->id;
    fx.model->updateGeometry();
    const DrawingView* sv = fx.model->view(secId);
    QVERIFY2(sv->geometryValid, sv->error.c_str());
    QVERIFY(countClass(sv->curves, LineClass::Hatch) > 0);
    QVERIFY(countClass(sv->curves, LineClass::Section) == 4);
    // half of the 10 wide box remains: the section shows Y x Z = 20 x 30
    QVERIFY(near(sv->bbox.width(), 20.0, 1e-4));
    QVERIFY(near(sv->bbox.height(), 30.0, 1e-4));
    QVERIFY(near(sv->position.y, 150));  // horizontal alignment with the parent

    // the model recompute rebuilds detail + section
    fx.doc.notifyFeatureChanged(fx.boxFeature);
    fx.doc.recompute();
    QVERIFY(fx.model->view(detId)->geometryValid);
    QVERIFY(fx.model->view(secId)->geometryValid);
  }

  void undoRedoSnapshots() {
    Fixture fx;
    core::UndoStack& undo = fx.doc.undoStack();
    Sheet& s = fx.model->addSheet(*sheetFormatByName("A3"), ProjectionStandard::ThirdAngle);
    core::ObjectId sheetId = s.id;
    core::ObjectId viewId = core::kInvalidId;
    fx.model->edit("Base view", &undo, [&]() {
      viewId = fx.model->addBaseView(*fx.model->sheet(sheetId), {}, ViewOrientation::Front, 1.0, {100, 100}).id;
    });
    QVERIFY(fx.model->view(viewId) != nullptr);
    QVERIFY(fx.model->view(viewId)->geometryValid);
    QVERIFY(undo.canUndo());
    undo.undo();
    QVERIFY(fx.model->view(viewId) == nullptr);
    undo.redo();
    QVERIFY(fx.model->view(viewId) != nullptr);
    // geometry restored from the cache without a new projection
    QVERIFY(fx.model->view(viewId)->geometryValid);
    // merged drags
    fx.model->edit("Move", &undo, [&]() { fx.model->moveView(viewId, {120, 100}); }, 77);
    fx.model->edit("Move", &undo, [&]() { fx.model->moveView(viewId, {140, 100}); }, 77);
    int count = undo.count();
    undo.undo();
    QVERIFY(near(fx.model->view(viewId)->position.x, 100));
    QCOMPARE(undo.count(), count);
    undo.redo();
    QVERIFY(near(fx.model->view(viewId)->position.x, 140));
  }

  void persistence() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    std::string path = (dir.path() + "/drawing.fcad").toStdString();
    core::ObjectId viewId, dimId, sheetId;
    {
      Fixture fx;
      Sheet& s = fx.model->addSheet(*sheetFormatByName("A4"), ProjectionStandard::FirstAngle);
      s.titleBlock.setField("DrawnBy", "QA");
      sheetId = s.id;
      DrawingView& v = fx.model->addBaseView(s, {core::ShapeRef::body(fx.bodyId)}, ViewOrientation::Front, 0.5, {100, 100});
      v.hiddenLines = true;
      viewId = v.id;
      fx.model->updateGeometry();
      const Curve2D* top = topEdge(*fx.model->view(viewId));
      Dimension d;
      inferDimension({DimPick{false, {}, *top}}, d);
      d.viewId = viewId;
      d.textPos = {100, 80};
      dimId = fx.model->addDimension(s, d).id;
      TextNote n;
      n.text = "NOTE";
      n.position = {30, 30};
      fx.model->addNote(s, n);
      fx.model->addPartsList(s, PartsList{});
      std::string err;
      QVERIFY2(fx.doc.save(path, &err), err.c_str());
      QJsonObject ext = fx.doc.extensionData(DrawingModel::kExtensionKey);
      QVERIFY(ext.contains("sheets"));
      QCOMPARE(ext["sheets"].toArray().size(), 1);
    }
    {
      core::Document doc;
      std::string err;
      QVERIFY2(doc.load(path, &err), err.c_str());
      DrawingModel* m = DrawingModel::forDocument(doc);
      m->setSynchronous(true);
      QCOMPARE(m->sheets().size(), size_t(1));
      Sheet* s = m->sheet(sheetId);
      QVERIFY(s);
      QCOMPARE(s->format.name, std::string("A4"));
      QCOMPARE(s->standard, ProjectionStandard::FirstAngle);
      QCOMPARE(s->titleBlock.field("DrawnBy"), std::string("QA"));
      QCOMPARE(s->views.size(), size_t(1));
      QCOMPARE(s->dimensions.size(), size_t(1));
      QCOMPARE(s->notes.size(), size_t(1));
      QCOMPARE(s->partsLists.size(), size_t(1));
      const DrawingView* v = m->view(viewId);
      QVERIFY(v);
      QVERIFY(v->hiddenLines);
      QVERIFY(near(v->scale, 0.5));
      m->updateGeometry();
      m->waitForIdle();
      QVERIFY(m->view(viewId)->geometryValid);
      const Dimension* d = s->dimension(dimId);
      QVERIFY(d && d->valid);
      QVERIFY(near(d->value, 10.0, 1e-6));
      QCOMPARE(s->partsLists.front().rows.size(), size_t(1));
    }
  }

  void partsListAndBalloons() {
    Fixture fx;
    // part document: one row per body
    std::vector<BomRow> rows = buildBom(fx.doc);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0].item, 1);
    QCOMPARE(rows[0].keys.front(), fx.bodyId);
    // assembly: two components, one of them occurring twice
    core::Component& bracket = fx.doc.addComponent("Bracket");
    bracket.partNumber = "BR-100";
    bracket.material = "Steel";
    core::Component& pin = fx.doc.addComponent("Pin");
    core::Component& pin2 = fx.doc.addComponent("Pin");
    pin2.definition = pin.id;
    fx.doc.setActiveComponent(bracket.id);
    fx.doc.addFeature(std::make_unique<core::test::BoxFeature>());
    fx.doc.setActiveComponent(pin.id);
    fx.doc.addFeature(std::make_unique<core::test::BoxFeature>());
    fx.doc.recompute();
    rows = buildBom(fx.doc);
    QCOMPARE(rows.size(), size_t(2));
    QCOMPARE(rows[0].partNumber, std::string("BR-100"));
    QCOMPARE(rows[0].material, std::string("Steel"));
    QCOMPARE(rows[1].quantity, 2);
    QCOMPARE(bomItemFor(rows, pin2.id), 2);
    QCOMPARE(bomItemFor(rows, bracket.id), 1);
    QCOMPARE(bomItemFor(rows, 9999), 0);

    Sheet& s = fx.model->addSheet(*sheetFormatByName("A3"), ProjectionStandard::ThirdAngle);
    DrawingView& v = fx.model->addBaseView(s, {}, ViewOrientation::IsoTopRight, 1.0, {150, 150});
    fx.model->updateGeometry();
    PartsList pl;
    pl.position = {230, 200};
    PartsList& added = fx.model->addPartsList(s, pl);
    QCOMPARE(added.rows.size(), size_t(2));
    QVERIFY(near(added.height(), 7.0 * 3));
    Balloon b;
    b.viewId = v.id;
    b.itemKey = pin2.id;
    b.position = {200, 100};
    Balloon& bb = fx.model->addBalloon(s, b);
    QCOMPARE(bb.itemNumber, 2);
    // restricted to one component
    DrawingView& only = fx.model->addBaseView(s, {core::ShapeRef::component(bracket.id)}, ViewOrientation::Front, 1.0, {80, 80});
    QCOMPARE(fx.model->bom(&only).size(), size_t(1));
    // UX-37: a view made from a body preselection still lists the whole design
    const core::ObjectId bracketBody = fx.doc.bodiesOf(bracket.id).front();
    DrawingView& bodyView =
        fx.model->addBaseView(s, {core::ShapeRef::body(bracketBody)}, ViewOrientation::Front, 1.0, {80, 200});
    QCOMPARE(fx.model->bom(&bodyView).size(), size_t(2));
    // a parts list placed past the frame edge is pulled inside it
    const BBox2 frame = s.frameRect();
    PartsList edge;
    edge.position = {frame.max.x - 5, frame.max.y - 5};
    {
      const PartsList& e = fx.model->addPartsList(s, edge);
      QVERIFY(e.position.x + e.width() <= frame.max.x + 1e-6);
      QVERIFY(e.position.y + e.height() <= frame.max.y + 1e-6);
    }
    // wider than the frame: the columns shrink to fit
    PartsList wide;
    for (double& c : wide.columnWidths) c *= 20;
    const PartsList& w = fx.model->addPartsList(s, wide);
    QVERIFY(w.width() <= frame.width() + 1e-6);
    QVERIFY(w.position.x >= frame.min.x - 1e-6);
  }

  void centerMarksAndNotes() {
    Fixture fx;
    Sheet& s = fx.model->addSheet(*sheetFormatByName("A3"), ProjectionStandard::ThirdAngle);
    // add a hole so the top view has a circle
    auto hole = std::make_unique<core::test::HoleFeature>();
    hole->prop("body").setRef(core::ShapeRef::body(fx.bodyId));
    fx.doc.addFeature(std::move(hole));
    fx.doc.recompute();
    DrawingView& top = fx.model->addBaseView(s, {}, ViewOrientation::Top, 1.0, {100, 100});
    fx.model->updateGeometry();
    const DrawingView* v = fx.model->view(top.id);
    const Curve2D* circle = nullptr;
    for (const auto& c : v->curves)
      if (c.isCircle()) circle = &c;
    QVERIFY(circle);
    CenterMark m;
    m.viewId = top.id;
    m.ref = DimRef::toCurve(*circle);
    CenterMark& cm = fx.model->addCenterMark(s, m);
    auto geo = layoutCenterMark(cm, *v);
    QCOMPARE(geo.size(), size_t(2));
    QVERIFY(near(geo[0].length(), 2 * (2.0 + 2.0)));  // radius 2 + 2 mm extension on both sides
    // centre line between the two vertical edges of the box outline
    const Curve2D *left = nullptr, *right = nullptr;
    for (const auto& c : v->curves)
      if (c.type == CurveType::Segment && near(c.a.x, c.b.x, 1e-6)) {
        if (c.a.x < 0) left = &c;
        else right = &c;
      }
    QVERIFY(left && right);
    CenterLine cl;
    cl.viewId = top.id;
    cl.a = DimRef::toCurve(*left);
    cl.b = DimRef::toCurve(*right);
    auto lgeo = layoutCenterLine(fx.model->addCenterLine(s, cl), *v);
    QCOMPARE(lgeo.size(), size_t(1));
    QVERIFY(near(lgeo[0].a.x, 100.0, 1e-6));  // midway, at the view origin
    // removing the view removes the attached marks
    QVERIFY(fx.model->removeView(top.id));
    QVERIFY(s.centerMarks.empty() && s.centerLines.empty());
  }
};

QTEST_GUILESS_MAIN(ModelTest)
#include "test_drawing_model.moc"
