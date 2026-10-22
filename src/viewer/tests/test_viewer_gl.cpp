// GL regression test for viewport picking and redraw (UX-01/UX-02). It needs a real X display
// with GLX, so it only runs when FABRICAD_GL_DISPLAY names one (e.g. a private Xwayland from
// tools/uitest/ui.sh: FABRICAD_GL_DISPLAY=:41). Otherwise, as under the default offscreen
// ctest run, every case is skipped. ctest label: gui.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "viewer/ViewerWidget.h"

#include "core/Document.h"
#include "core/Selection.h"
#include "core/tests/support/TestFeatures.h"

#include <QApplication>
#include <QOpenGLWidget>
#include <QSignalSpy>
#include <QtTest/QtTest>

using namespace fabricad;
using core::ShapeKind;

namespace {
bool glEnabled() { return !qEnvironmentVariable("FABRICAD_GL_DISPLAY").isEmpty(); }
}  // namespace

class ViewerGlTest : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase() {
    if (!glEnabled()) QSKIP("set FABRICAD_GL_DISPLAY=:N (a private X display) to run the GL viewer tests");
    core::test::registerTestFeatures();
  }

  void hoverAndWindowSelectPickTheBody() {
    core::Document doc;
    doc.addFeature(std::make_unique<core::test::BoxFeature>());
    doc.recompute();
    viewer::ViewerWidget w;
    w.setDocument(&doc);
    w.resize(800, 600);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    QSignalSpy ready(&w, &viewer::ViewerWidget::viewInitialized);
    QVERIFY2(w.initializeView(), qPrintable(w.glInfo()));
    QTRY_VERIFY_WITH_TIMEOUT(w.hasGlView(), 10000);
    w.syncNow();
    w.setStandardView(core::StandardView::Iso, false);
    w.fitAll(false);
    QTest::qWait(200);

    // UX-02: the box centre is pickable (it was AIS_SOD_Nothing everywhere before the fix)
    core::ShapeRef r;
    gp_Pnt hit;
    double sx = 0, sy = 0;
    QVERIFY(w.worldToScreen(gp_Pnt(5, 10, 15), sx, sy));
    QVERIFY2(w.pick(int(sx), int(sy), core::kAllKinds, r, &hit), "nothing picked over the box");
    QVERIFY(r.isBrep());
    QVERIFY(core::hasKind(core::kBrepKinds, r.kind));
    const auto all = w.pickAll(int(sx), int(sy), core::kAllKinds);
    QVERIFY(!all.empty());

    // empty space picks nothing
    core::ShapeRef none;
    QVERIFY(!w.pick(20, 580, core::kAllKinds, none));

    // window select (left to right, enclosing the whole box) selects its entities
    auto* gl = w.findChild<QOpenGLWidget*>();
    QVERIFY(gl);
    QTest::mousePress(gl, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
    QTest::mouseMove(gl, QPoint(300, 300));
    QTest::mouseMove(gl, QPoint(650, 560));
    QTest::mouseRelease(gl, Qt::LeftButton, Qt::NoModifier, QPoint(650, 560));
    QVERIFY2(!doc.selection().items().empty(), "window select returned nothing");

    // a click in empty space clears it again
    QTest::mouseClick(gl, Qt::LeftButton, Qt::NoModifier, QPoint(20, 580));
    QTRY_VERIFY(doc.selection().items().empty());
  }
};

int main(int argc, char** argv) {
  if (glEnabled()) {
    qputenv("QT_QPA_PLATFORM", "xcb");
    qputenv("DISPLAY", qgetenv("FABRICAD_GL_DISPLAY"));
    viewer::setupGlPlatform();
  }
  QApplication app(argc, argv);
  ViewerGlTest t;
  return QTest::qExec(&t, argc, argv);
}

#include "test_viewer_gl.moc"
