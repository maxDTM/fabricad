// Unit tests for the widget-free app logic: keyboard routing, toolbar model, timeline
// geometry, marking menu, command search, app actions (delete/visibility/rename/suppress/
// edit dispatch), UI state, selection text and the generic property edit tool.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/AppActions.h"
#include "app/AppState.h"
#include "app/CommandSearch.h"
#include "app/MarkingMenuModel.h"
#include "app/PropertyEditor.h"
#include "assembly/JointFeature.h"
#include "app/ShortcutRouter.h"
#include "app/TimelineLayout.h"
#include "app/ToolbarModel.h"
#include "app/UiSettings.h"
#include "app/StatusCells.h"
#include "app/TitleBar.h"
#include "core/AppHost.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Materials.h"
#include "core/ShapeUtils.h"
#include "core/Selection.h"
#include "core/Tool.h"
#include "core/UndoStack.h"
#include "core/ViewerContext.h"
#include "core/tests/support/TestFeatures.h"
#include "viewer/ViewerWidget.h"

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <QComboBox>
#include <QLineEdit>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace fabricad;
using core::ShapeKind;
using core::ShapeRef;

namespace {

core::ToolInfo ti(const std::string& id, const std::string& tab, const std::string& group, int order = 100,
                  bool promoted = false, const std::string& shortcut = {}) {
  core::ToolInfo i;
  i.id = id;
  i.name = id.substr(id.find('.') + 1);
  i.tab = tab;
  i.group = group;
  i.order = order;
  i.promoted = promoted;
  i.shortcut = shortcut;
  return i;
}

void addTool(core::ToolRegistry& r, core::ToolInfo i) {
  r.registerTool(std::make_unique<core::LambdaTool>(std::move(i), [](core::ToolContext&) {}));
}

core::ObjectId addBox(core::Document& doc, double dx = 10) {
  auto f = std::make_unique<core::test::BoxFeature>();
  f->prop("dx").setDouble(dx);
  auto cmd = std::make_unique<core::AddFeatureCommand>(doc, std::move(f));
  core::AddFeatureCommand* raw = cmd.get();
  doc.undoStack().push(std::move(cmd));
  return raw->featureId();
}

}  // namespace

class TestAppLogic : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName("FabriCADTest");
    QCoreApplication::setApplicationName("test_app_logic");
    QSettings().clear();
    core::test::registerTestFeatures();
  }

  // --- keyboard routing -------------------------------------------------------------
  void shortcutText() {
    QCOMPARE(app::keySequenceText(Qt::Key_Z, Qt::ControlModifier), std::string("Ctrl+Z"));
    QCOMPARE(app::keySequenceText(Qt::Key_J, Qt::ShiftModifier), std::string("Shift+J"));
    QCOMPARE(app::normalizeShortcut("ctrl+shift+s"), std::string("Ctrl+Shift+S"));
    QCOMPARE(app::normalizeShortcut("Delete"), std::string("Del"));
    QCOMPARE(app::normalizeShortcut("Del"), app::keySequenceText(Qt::Key_Delete, 0));
    QCOMPARE(app::normalizeShortcut("F6"), std::string("F6"));
    QCOMPARE(app::normalizeShortcut(""), std::string());
  }

  void routeCanvasKeys() {
    app::KeyContext canvas{true, false, false};
    QCOMPARE(app::routeKey(Qt::Key_E, 0, canvas).action, app::KeyAction::Shortcut);
    QCOMPARE(app::routeKey(Qt::Key_E, 0, canvas).sequence, std::string("E"));
    QCOMPARE(app::routeKey(Qt::Key_S, 0, canvas).action, app::KeyAction::OpenSearch);
    QCOMPARE(app::routeKey(Qt::Key_S, Qt::ControlModifier, canvas).action, app::KeyAction::Shortcut);
    QCOMPARE(app::routeKey(Qt::Key_Escape, 0, canvas).action, app::KeyAction::Cancel);
    QCOMPARE(app::routeKey(Qt::Key_Return, 0, canvas).action, app::KeyAction::AcceptOrRepeat);
    QCOMPARE(app::routeKey(Qt::Key_Return, Qt::ControlModifier, canvas).action, app::KeyAction::Shortcut);
    QCOMPARE(app::routeKey(Qt::Key_Space, 0, canvas).action, app::KeyAction::Repeat);
    QCOMPARE(app::routeKey(Qt::Key_Colon, Qt::ShiftModifier, canvas).action, app::KeyAction::FocusCommandLine);
    QCOMPARE(app::routeKey(Qt::Key_Shift, Qt::ShiftModifier, canvas).action, app::KeyAction::None);
    app::KeyContext tool{true, false, true};
    QVERIFY(app::routeKey(Qt::Key_E, 0, tool).toolFirst);
    QCOMPARE(app::routeKey(Qt::Key_Space, 0, tool).action, app::KeyAction::ForwardOnly);
    QCOMPARE(app::routeKey(Qt::Key_Tab, 0, tool).action, app::KeyAction::ForwardOnly);
    QCOMPARE(app::routeKey(Qt::Key_Tab, 0, canvas).action, app::KeyAction::None);
    // SK-01: Ctrl+Return / Ctrl+keypad Enter is a shortcut before the tool, and both are "Ctrl+Return"
    for (int k : {int(Qt::Key_Return), int(Qt::Key_Enter)}) {
      const app::KeyRoute r = app::routeKey(k, Qt::ControlModifier | Qt::KeypadModifier, tool);
      QCOMPARE(r.action, app::KeyAction::Shortcut);
      QVERIFY(!r.toolFirst);
      QCOMPARE(r.sequence, app::normalizeShortcut("Ctrl+Return"));
    }
    QVERIFY(app::routeKey(Qt::Key_Enter, 0, tool).toolFirst);
    app::KeyContext field{false, true, true};
    QCOMPARE(app::routeKey(Qt::Key_Enter, Qt::ControlModifier, field).sequence, app::normalizeShortcut("Ctrl+Return"));
  }

  void routeTextFieldKeys() {
    app::KeyContext text{false, true, true};
    // single letters stay in text fields; global combos still work; edit keys stay local
    QCOMPARE(app::routeKey(Qt::Key_E, 0, text).action, app::KeyAction::None);
    QCOMPARE(app::routeKey(Qt::Key_Escape, 0, text).action, app::KeyAction::None);
    QCOMPARE(app::routeKey(Qt::Key_S, Qt::ControlModifier, text).action, app::KeyAction::Shortcut);
    QCOMPARE(app::routeKey(Qt::Key_Z, Qt::ControlModifier, text).action, app::KeyAction::None);
    QCOMPARE(app::routeKey(Qt::Key_C, Qt::ControlModifier, text).action, app::KeyAction::None);
    QVERIFY(app::isStandardEditKey(Qt::Key_V, Qt::ControlModifier));
    QVERIFY(!app::isStandardEditKey(Qt::Key_B, Qt::ControlModifier));
    app::KeyContext other{false, false, false};  // e.g. a button in a dialog
    QCOMPARE(app::routeKey(Qt::Key_Z, Qt::ControlModifier, other).action, app::KeyAction::Shortcut);
  }

  // --- toolbar --------------------------------------------------------------------------
  void toolbarModel() {
    core::ToolRegistry reg;
    addTool(reg, ti("part.fillet", "SOLID", "MODIFY", 20));
    addTool(reg, ti("part.extrude", "SOLID", "CREATE", 10, true, "E"));
    addTool(reg, ti("part.hole", "SOLID", "CREATE", 30));
    addTool(reg, ti("part.revolve", "SOLID", "CREATE", 20, true));
    addTool(reg, ti("sketch.line", "SKETCH", "CREATE", 10, true, "L"));
    addTool(reg, ti("x.custom", "ZZZ", "WEIRD", 10));
    addTool(reg, ti("part.chamfer", "SOLID", "MODIFY", 30));
    addTool(reg, ti("part.shell", "SOLID", "MODIFY", 40));
    addTool(reg, ti("view.fit", "", ""));  // no tab: search only
    core::ToolInfo hidden = ti("app.hidden", "SOLID", "CREATE");
    hidden.hidden = true;
    addTool(reg, hidden);
    const auto model = app::buildToolbarModel(reg, core::Workspace::Design);
    QCOMPARE(model.size(), size_t(3));
    QCOMPARE(model[0].name, std::string("SOLID"));
    QCOMPARE(model[1].name, std::string("SKETCH"));
    QVERIFY(model[1].contextual);
    QCOMPARE(model[2].name, std::string("ZZZ"));
    QCOMPARE(model[0].groups.size(), size_t(2));
    QCOMPARE(model[0].groups[0].name, std::string("CREATE"));
    const auto& create = model[0].groups[0];
    QCOMPARE(create.all.size(), size_t(3));  // hidden tool excluded
    QCOMPARE(create.all[0]->info().id, std::string("part.extrude"));
    QCOMPARE(create.all[1]->info().id, std::string("part.revolve"));
    QCOMPARE(create.promoted.size(), size_t(2));
    // MODIFY has no promoted tool: first two by order become big icons
    const auto& modify = model[0].groups[1];
    QCOMPARE(modify.promoted.size(), size_t(2));
    QCOMPARE(modify.promoted[0]->info().id, std::string("part.fillet"));
    QVERIFY(app::buildToolbarModel(reg, core::Workspace::Drawing).empty());
    QCOMPARE(app::toolTooltip(reg.tool("part.extrude")->info()), std::string("extrude  (E)"));
  }

  // --- main toolbar sections (redesign phase 1) -------------------------------------------
  void toolbarSections() {
    core::ToolRegistry reg;
    addTool(reg, ti("sketch.create", "SOLID", "CREATE", 1, true));
    addTool(reg, ti("part.extrude", "SOLID", "CREATE", 10, true, "E"));
    addTool(reg, ti("part.revolve", "SOLID", "CREATE", 20, true));
    addTool(reg, ti("part.hole", "SOLID", "CREATE", 30));
    addTool(reg, ti("part.fillet", "SOLID", "MODIFY", 20, true));
    addTool(reg, ti("assembly.joint", "SOLID", "ASSEMBLE", 10, true));
    addTool(reg, ti("assembly.ground", "SOLID", "ASSEMBLE", 20));
    addTool(reg, ti("assembly.rigid-group", "SOLID", "ASSEMBLE", 30));
    addTool(reg, ti("inspect.measure", "SOLID", "INSPECT", 10, true));
    addTool(reg, ti("select.default", "SOLID", "SELECT", 10));
    addTool(reg, ti("part.export-stl", "TOOLS", "EXPORT", 10, true));
    addTool(reg, ti("x.surface", "SURFACE", "CREATE", 10, true));
    addTool(reg, ti("sketch.line", "SKETCH", "CREATE", 10, true, "L"));
    addTool(reg, ti("sketch.constraint.equal", "SKETCH", "CONSTRAINTS", 10));
    addTool(reg, ti("sketch.constraint.fix", "SKETCH", "CONSTRAINTS", 20));
    addTool(reg, ti("sketch.toggle-grid", "SKETCH", "OPTIONS", 10));
    addTool(reg, ti("sketch.finish", "SKETCH", "FINISH SKETCH", 10, true));
    core::ToolInfo params = ti("app.parameters", "SOLID", "MODIFY", 90);
    addTool(reg, params);
    addTool(reg, ti("app.preferences", "TOOLS", "UTILITIES", 20));

    auto names = [](const std::vector<app::ToolbarSection>& v) {
      std::vector<std::string> n;
      for (const auto& s : v) n.push_back(s.name);
      return n;
    };
    // default page: SKETCH CREATE MODIFY ASSEMBLE INSPECT, MORE (SELECT, TOOLS, SURFACE), DOCUMENT
    const auto page = app::buildToolbarSections(reg, core::Workspace::Design);
    QCOMPARE(names(page), (std::vector<std::string>{"SKETCH", "CREATE", "MODIFY", "ASSEMBLE", "INSPECT", "MORE", "DOCUMENT"}));
    QCOMPARE(page[0].toolbarTab, std::string("SOLID"));
    QCOMPARE(page[0].all.size(), size_t(1));  // sketch.create moved out of CREATE
    QCOMPARE(page[1].toolbarGroup, std::string("CREATE"));
    QCOMPARE(page[1].promoted.size(), size_t(2));  // extrude, revolve
    QCOMPARE(page[1].all.size(), size_t(3));
    const app::ToolbarSection& more = page[5];
    QCOMPARE(more.kind, app::SectionKind::More);
    std::vector<std::string> entries;
    for (const auto& e : more.more) entries.push_back(e.label);
    QVERIFY(std::find(entries.begin(), entries.end(), "TOOLS") != entries.end());
    QVERIFY(std::find(entries.begin(), entries.end(), "SURFACE") != entries.end());
    QVERIFY(std::find(entries.begin(), entries.end(), "SELECT") != entries.end());
    QCOMPARE(page.back().kind, app::SectionKind::Document);
    QCOMPARE(page.back().all.size(), size_t(2));
    QCOMPARE(page.back().all[0]->info().id, std::string("app.parameters"));
    for (const auto& s : page)
      if (s.kind == app::SectionKind::Tools) QVERIFY2(!s.toolbarTab.empty() && !s.toolbarGroup.empty(), s.name.c_str());

    // contextual SKETCH page: the sketch sections first, INSPECT kept, MORE, FINISH, DOCUMENT
    const auto sk = app::buildToolbarSections(reg, core::Workspace::Design, app::DesignFilter::Part, true);
    QCOMPARE(names(sk), (std::vector<std::string>{"CREATE", "CONSTRAINTS", "INSPECT", "MORE", "FINISH SKETCH", "DOCUMENT"}));
    QCOMPARE(sk[0].toolbarTab, std::string("SKETCH"));
    QVERIFY(sk[1].flat);
    QCOMPARE(sk[1].promoted.size(), size_t(2));  // flat: every constraint
    QCOMPARE(sk[4].kind, app::SectionKind::Finish);
    QCOMPARE(sk[4].all.front()->info().id, std::string("sketch.finish"));

    // Assembly filter: ASSEMBLE first with more tools, modelling sections caption-only
    const auto asmb = app::buildToolbarSections(reg, core::Workspace::Design, app::DesignFilter::Assembly);
    QCOMPARE(asmb.front().name, std::string("ASSEMBLE"));
    QCOMPARE(asmb.front().promoted.size(), size_t(3));
    for (const auto& s : asmb)
      if (s.name == "CREATE" || s.name == "MODIFY" || s.name == "SKETCH") QVERIFY(s.startCollapsed);
      else QVERIFY2(!s.startCollapsed, s.name.c_str());

    app::DesignFilter f;
    QVERIFY(app::parseDesignFilter("Assembly", &f) && f == app::DesignFilter::Assembly);
    QVERIFY(!app::parseDesignFilter("weird", &f));
    core::ToolInfo nc = ti("assembly.new-component", "SOLID", "ASSEMBLE");
    nc.name = "New Component";
    QCOMPARE(app::shortToolLabel(nc), std::string("New comp"));
    core::ToolInfo imp = ti("x.import", "SOLID", "INSERT");
    imp.name = "Import File...";
    QCOMPARE(app::shortToolLabel(imp), std::string("Import File"));
  }

  void toolbarFit() {
    // three sections of 3 x 60 px buttons, captions 50, padding 10, spacing 2
    std::vector<app::FitSection> in(3);
    for (int i = 0; i < 3; ++i) {
      in[size_t(i)].buttonWidths = {60, 60, 60};
      in[size_t(i)].captionWidth = 50;
      in[size_t(i)].collapsedWidth = 56;
      in[size_t(i)].collapsePriority = i * 10;  // the last collapses first
    }
    const int full = 3 * (184 + 10);
    int used = 0;
    auto r = app::fitToolbar(in, full, 2, 10, &used);
    QCOMPARE(used, full);
    for (const auto& x : r) QVERIFY(x.visibleButtons == 3 && !x.collapsed);
    // 70 px short: the right-most fullest section folds one button
    r = app::fitToolbar(in, full - 50, 2, 10, &used);
    QCOMPARE(r[2].visibleButtons, 2);
    QCOMPARE(r[1].visibleButtons, 3);
    QVERIFY(used <= full - 50);
    // much narrower: every section down to one button, then collapses by priority
    r = app::fitToolbar(in, 3 * 70 - 1, 2, 10, &used);  // one button each is 3 x 70: one more px short
    QVERIFY(r[2].collapsed);
    QVERIFY(!r[0].collapsed);
    QVERIFY(used <= 3 * 70 - 1);
    QCOMPARE(r[0].visibleButtons, 1);
    // nothing fits: everything caption-only, never negative
    r = app::fitToolbar(in, 10, 2, 10, &used);
    for (const auto& x : r) QVERIFY(x.collapsed && x.visibleButtons == 0);
    // fixed sections (FINISH SKETCH) never fold; startCollapsed sections start caption-only
    in[0].fixed = true;
    in[1].startCollapsed = true;
    r = app::fitToolbar(in, 10, 2, 10, &used);
    QVERIFY(!r[0].collapsed && r[0].visibleButtons == 3);
    r = app::fitToolbar(in, 5000, 2, 10, &used);
    QVERIFY(r[1].collapsed);
  }

  void uiSettings() {
    QSettings().remove("ui");
    app::UiSettings& ui = app::UiSettings::instance();
    // defaults (section 6.1)
    QCOMPARE(ui.string("ui/theme"), QString("dark"));
    QCOMPARE(ui.string("ui/toolbarArea"), QString("top"));
    QCOMPARE(ui.string("ui/sidebarSide"), QString("left"));
    QCOMPARE(ui.string("ui/cardPlacement"), QString("floatRight"));
    QCOMPARE(ui.string("ui/quickToolbar"), QString("off"));
    QCOMPARE(ui.string("ui/orientation"), QString("hexDial"));
    QCOMPARE(ui.string("ui/timelineStyle"), QString("featureList"));
    QCOMPARE(ui.number("ui/fontScale"), 100);
    QVERIFY(ui.flag("ui/glow"));
    QVERIFY(!ui.flag("ui/scanlines"));
    QCOMPARE(ui.string("ui/designFilter"), QString("part"));
    // live signal, validation, normalisation, round trip through QSettings
    QSignalSpy spy(&ui, &app::UiSettings::changed);
    QVERIFY(ui.set("ui/sidebarSide", "RIGHT"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toString(), QString("ui/sidebarSide"));
    QCOMPARE(QSettings().value("ui/sidebarSide").toString(), QString("right"));
    QVERIFY(!ui.set("ui/sidebarSide", "top"));
    QVERIFY(!ui.set("ui/fontScale", 200));
    QVERIFY(!ui.set("ui/nope", 1));
    QCOMPARE(spy.count(), 0);
    QVERIFY(ui.set("ui/sidebarSide", "right"));  // unchanged: no signal
    QCOMPARE(spy.count(), 0);
    QSettings().setValue("ui/toolbarArea", "diagonal");  // garbage in the file reads as the default
    QCOMPARE(ui.string("ui/toolbarArea"), QString("top"));
    for (const auto& k : app::UiSettings::keys()) {
      QVERIFY2(k.key.startsWith("ui/"), qPrintable(k.key));
      ui.reset(k.key);
      QCOMPARE(ui.value(k.key), k.defaultValue);
    }
  }

  void statusCells() {
    QCOMPARE(app::RebuildMeter::barsFor(-1), 0);
    QCOMPARE(app::RebuildMeter::barsFor(0), 1);
    QCOMPARE(app::RebuildMeter::barsFor(14), 3);
    QCOMPARE(app::RebuildMeter::barsFor(5000), 8);
    QCOMPARE(app::keycapText("Alt+1"), QString("Alt 1"));
    bool mod = false;
    QCOMPARE(app::DocumentTabBar::plainText("bracket *", &mod), QString("bracket"));
    QVERIFY(mod);
    QCOMPARE(app::DocumentTabBar::plainText("bracket", &mod), QString("bracket"));
    QVERIFY(!mod);
    // the workspace shortcuts are defined once and do not take the standard views' Ctrl+1..7
    for (const auto& w : app::kWorkspaceShortcuts) QVERIFY(!QString(w.shortcut).startsWith("Ctrl"));
  }

  // --- timeline --------------------------------------------------------------------------
  void timelineLayout() {
    app::TimelineLayout L;
    std::vector<app::TimelineEntry> es;
    for (int i = 0; i < 4; ++i) {
      app::TimelineEntry e;
      e.id = 100 + i;
      e.docIndex = i == 3 ? 4 : i;  // doc index 3 is a timeline-hidden feature
      es.push_back(e);
    }
    L.setFeatureCount(5);
    L.setEntries(es, 2);  // features 0,1 active
    QCOMPARE(L.markerGap(), 2);
    QVERIFY(L.markerRect().left() > L.itemRect(1).right());
    QVERIFY(L.itemRect(2).left() > L.markerRect().right());
    QCOMPARE(L.hitItem(L.itemRect(2).center()), 2);
    QVERIFY(L.hitMarker(L.markerRect().center()));
    QCOMPARE(L.hitItem(QPoint(-50, 16)), -1);
    QCOMPARE(L.rollbackIndexForGap(0), 0);
    QCOMPARE(L.rollbackIndexForGap(1), 1);
    QCOMPARE(L.rollbackIndexForGap(3), 3);
    QCOMPARE(L.rollbackIndexForGap(4), 5);  // end = all features, including hidden ones
    QCOMPARE(L.gapForX(L.gapX(3)), 3);
    QCOMPARE(L.gapForX(-100), 0);
    QCOMPARE(L.gapForX(100000), 4);
    // reorder: no-op drops next to itself, otherwise the doc index of the neighbour
    QCOMPARE(L.moveTargetForGap(1, 1), -1);
    QCOMPARE(L.moveTargetForGap(1, 2), -1);
    QCOMPARE(L.moveTargetForGap(0, 3), 2);
    QCOMPARE(L.moveTargetForGap(3, 0), 0);
    QCOMPARE(L.moveTargetForGap(9, 0), -1);
    QVERIFY(L.totalWidth() > L.itemRect(3).right());
  }

  void timelineEntriesFromDocument() {
    core::Document doc;
    addBox(doc);
    addBox(doc, 5);
    doc.setRollbackIndex(1);
    const auto es = app::TimelineLayout::entriesFrom(doc);
    QCOMPARE(es.size(), size_t(2));
    QVERIFY(es[0].active);
    QVERIFY(!es[1].active);
    QCOMPARE(es[0].status, core::FeatureStatus::Ok);
    QCOMPARE(es[1].type, std::string("TestBox"));
  }

  // --- marking menu ----------------------------------------------------------------------
  void markingSlots() {
    QCOMPARE(app::slotForOffset(0, -50), int(app::N));
    QCOMPARE(app::slotForOffset(50, 0), int(app::E));
    QCOMPARE(app::slotForOffset(0, 50), int(app::S));
    QCOMPARE(app::slotForOffset(-50, 0), int(app::W));
    QCOMPARE(app::slotForOffset(40, -40), int(app::NE));
    QCOMPARE(app::slotForOffset(-40, 40), int(app::SW));
    QCOMPARE(app::slotForOffset(5, 5), -1);
    QCOMPARE(app::slotForOffset(-1, -60), int(app::N));  // just left of north still N
    for (int s = 0; s < app::kSlotCount; ++s) {
      double dx, dy;
      app::slotDirection(s, dx, dy);
      QCOMPARE(app::slotForOffset(dx * 80, dy * 80), s);
    }
  }

  void markingContent() {
    core::ToolRegistry reg;
    for (const char* id : {"part.extrude", "part.press-pull", "part.fillet", "part.chamfer", "app.undo", "app.delete",
                           "sketch.create", "sketch.line", "sketch.finish", "sketch.trim", "view.fit"})
      addTool(reg, ti(id, "SOLID", "CREATE"));
    app::MarkingContext ctx;
    auto m = app::buildMarkingMenu(reg, ctx);
    QCOMPARE(m.radial[app::E].command, std::string("part.extrude"));
    QCOMPARE(m.radial[app::W].command, std::string("part.press-pull"));
    QCOMPARE(m.radial[app::S].command, std::string("sketch.create"));
    QVERIFY(m.radial[app::N].command.empty());   // nothing to repeat yet
    QVERIFY(m.radial[app::NE].command.empty());  // delete needs a selection
    ctx.lastToolId = "part.fillet";
    ctx.selectionKinds = {ShapeKind::Edge, ShapeKind::Edge};
    m = app::buildMarkingMenu(reg, ctx);
    QCOMPARE(m.radial[app::N].command, std::string(app::kCmdRepeat));
    QCOMPARE(m.radial[app::N].label, std::string("Repeat fillet"));
    QCOMPARE(m.radial[app::E].command, std::string("part.fillet"));
    QCOMPARE(m.radial[app::W].command, std::string("part.chamfer"));
    QCOMPARE(m.radial[app::NE].command, std::string("app.delete"));
    ctx.inSketch = true;
    m = app::buildMarkingMenu(reg, ctx);
    QCOMPARE(m.radial[app::E].command, std::string("sketch.line"));
    QCOMPARE(m.radial[app::S].command, std::string("sketch.finish"));
    QCOMPARE(m.radial[app::W].command, std::string("sketch.trim"));
    ctx.toolActive = true;
    ctx.activeToolName = "Extrude";
    m = app::buildMarkingMenu(reg, ctx);
    QCOMPARE(m.radial[app::N].command, std::string(app::kCmdOk));
    QCOMPARE(m.radial[app::S].command, std::string(app::kCmdCancel));
    QVERIFY(m.radial[app::NE].command.empty());  // UX-41: no repeat while a command runs
    QVERIFY(!m.isEmpty());

    // UX-41: outside a sketch, Repeat never offers a sketch tool; it names the last model command.
    addTool(reg, ti("sketch.rectangle-2pt", "SKETCH", "CREATE"));
    ctx = {};
    ctx.lastToolId = "sketch.rectangle-2pt";
    m = app::buildMarkingMenu(reg, ctx);
    QVERIFY(m.radial[app::N].command.empty());
    ctx.lastModelToolId = "part.extrude";
    m = app::buildMarkingMenu(reg, ctx);
    QCOMPARE(m.radial[app::N].command, std::string("part.extrude"));
    QCOMPARE(m.radial[app::N].label, std::string("Repeat extrude"));
    ctx.lastToolId = "part.extrude";
    m = app::buildMarkingMenu(reg, ctx);
    QCOMPARE(m.radial[app::N].command, std::string(app::kCmdRepeat));
    ctx.lastToolId = "sketch.rectangle-2pt";
    ctx.inSketch = true;
    m = app::buildMarkingMenu(reg, ctx);
    QCOMPARE(m.radial[app::N].command, std::string(app::kCmdRepeat));
    QCOMPARE(m.radial[app::N].label, std::string("Repeat rectangle-2pt"));
  }

  // --- command search ----------------------------------------------------------------------
  void searchIds() {
    core::ToolRegistry reg;
    addTool(reg, ti("part.extrude", "SOLID", "CREATE"));
    addTool(reg, ti("part.fillet", "SOLID", "MODIFY"));
    core::ToolInfo h = ti("app.secret", "", "");
    h.hidden = true;
    addTool(reg, h);
    auto r = app::searchCommandIds(reg, "ext", {}, {});
    QVERIFY(!r.empty());
    QCOMPARE(r.front(), std::string("part.extrude"));
    r = app::searchCommandIds(reg, "", {"part.fillet"}, {"part.extrude", "part.fillet", "app.secret", "gone.tool"});
    QCOMPARE(r, (std::vector<std::string>{"part.fillet", "part.extrude"}));
    QVERIFY(app::searchCommandIds(reg, "zzzzzz", {}, {}).empty());
  }

  // --- state / selection text -----------------------------------------------------------------
  void recentFiles() {
    app::RecentFiles rf(3);
    rf.add("/tmp/a.fcad");
    rf.add("/tmp/b.fcad");
    rf.add("/tmp/a.fcad");
    QCOMPARE(rf.list().size(), 2);
    QCOMPARE(rf.list().front(), QString("/tmp/a.fcad"));
    rf.add("/tmp/c.fcad");
    rf.add("/tmp/d.fcad");
    QCOMPARE(rf.list().size(), 3);
    QCOMPARE(rf.list().back(), QString("/tmp/a.fcad"));  // oldest (b) dropped
    rf.save();
    app::RecentFiles other(3);
    other.load();
    QCOMPARE(other.list(), rf.list());
    rf.remove("/tmp/d.fcad");
    QCOMPARE(rf.list().front(), QString("/tmp/c.fcad"));
  }

  void uiStateRoundTrip() {
    app::UiState s;
    s.hiddenFeatures = {17, 42};
    s.showOrigin = true;
    s.showJoints = false;
    s.displayMode = core::DisplayMode::Wireframe;
    s.projection = core::ProjectionMode::Perspective;
    QCOMPARE(app::UiState::fromJson(s.toJson()), s);
    QCOMPARE(app::UiState::fromJson({}), app::UiState{});
    QCOMPARE(app::displayModeFromName("bogus"), core::DisplayMode::ShadedEdges);
  }

  void selectionText() {
    core::Document doc;
    const core::ObjectId fid = addBox(doc);
    const core::ObjectId body = doc.feature(fid)->createdBodies().front();
    const std::string bodyName = doc.body(body)->name;
    QCOMPARE(app::describeSelection(doc, {}), std::string());
    QCOMPARE(app::describeSelection(doc, {ShapeRef::body(body)}), bodyName);
    QCOMPARE(app::describeSelection(doc, {ShapeRef::face(body, 1)}), "Face of " + bodyName);
    QCOMPARE(app::describeSelection(doc, {ShapeRef::face(body, 1), ShapeRef::face(body, 2)}), std::string("2 Faces"));
    QCOMPARE(app::describeSelection(doc, {ShapeRef::face(body, 1), ShapeRef::edge(body, 2)}), std::string("2 items"));
    QCOMPARE(app::describeSelection(doc, {ShapeRef::originPlane("XY")}), std::string("XY Plane"));
    QCOMPARE(app::describeSelection(doc, {ShapeRef::vertex(body, 1), ShapeRef::vertex(body, 2)}), std::string("2 Vertices"));
  }

  // --- app actions --------------------------------------------------------------------------
  void featureRefs() {
    core::Document doc;
    const core::ObjectId fid = addBox(doc);
    const ShapeRef r = app::featureRef(*doc.feature(fid));
    QCOMPARE(r.kind, ShapeKind::Feature);
    QCOMPARE(app::featureOfRef(r), fid);
    QCOMPARE(app::featureOfRef(ShapeRef::sketch(77)), core::ObjectId(77));
    QCOMPARE(app::featureOfRef(ShapeRef::face(5, 1)), core::kInvalidId);
  }

  void deleteFeaturesAndBodies() {
    core::Document doc;
    const core::ObjectId f1 = addBox(doc);
    const core::ObjectId f2 = addBox(doc, 4);
    const core::ObjectId b2 = doc.feature(f2)->createdBodies().front();
    // a body maps to its creating feature; duplicates collapse; sub-shapes are ignored
    app::DeletePlan plan = app::planDelete(doc, {ShapeRef::body(b2), ShapeRef::feature(f2), ShapeRef::face(b2, 1)});
    QCOMPARE(plan.features, std::vector<core::ObjectId>{f2});
    QCOMPARE(plan.bodyNotes.size(), size_t(1));
    QVERIFY(plan.summary(doc).find(doc.feature(f2)->name()) != std::string::npos);
    plan = app::planDelete(doc, {ShapeRef::feature(f2), ShapeRef::feature(f1)});
    QCOMPARE(plan.features, (std::vector<core::ObjectId>{f1, f2}));  // document order
    doc.undoStack().push(app::makeDeleteCommand(doc, plan));
    QCOMPARE(doc.featureCount(), 0);
    QVERIFY(doc.liveBodies().empty());
    doc.undoStack().undo();
    QCOMPARE(doc.featureCount(), 2);
    QCOMPARE(doc.featureAt(0)->id(), f1);
    QCOMPARE(doc.featureAt(1)->id(), f2);
    QCOMPARE(doc.liveBodies().size(), size_t(2));
    QVERIFY(app::makeDeleteCommand(doc, app::planDelete(doc, {ShapeRef::face(b2, 1)})) == nullptr);
  }

  void deletePlanListsDependents() {
    // DOC-09: a later feature reading the deleted one is listed (the Delete prompt names it)
    core::Document doc;
    const core::ObjectId f1 = addBox(doc);
    const core::ObjectId b1 = doc.feature(f1)->createdBodies().front();
    auto hole = std::make_unique<core::test::HoleFeature>();
    hole->prop("body").setRef(ShapeRef::body(b1));
    const std::string holeName = "Hole9";
    hole->setName(holeName);
    doc.undoStack().push(std::make_unique<core::AddFeatureCommand>(doc, std::move(hole)));
    app::DeletePlan plan = app::planDelete(doc, {ShapeRef::feature(f1)});
    QCOMPARE(plan.dependents, std::vector<std::string>{holeName});
    // deleting both: nothing is left to fail
    plan = app::planDelete(doc, {ShapeRef::feature(f1), ShapeRef::feature(doc.featureAt(1)->id())});
    QVERIFY(plan.dependents.empty());
  }

  void deleteComponent() {
    core::Document doc;
    core::Component& c = doc.addComponent("Bracket");
    const core::ObjectId cid = c.id;
    app::DeletePlan plan = app::planDelete(doc, {ShapeRef::component(cid), ShapeRef::component(core::kRootComponentId)});
    QCOMPARE(plan.components, std::vector<core::ObjectId>{cid});  // root is never deleted
    // ASM-02: the browser / Del delete goes through the assembly delete, so a joint on the
    // component goes with it (and comes back on undo)
    const core::ObjectId other = doc.addComponent("Base").id;
    auto joint = std::make_unique<assembly::JointFeature>();
    joint->setComponents(cid, other);
    const core::ObjectId jid = doc.addFeature(std::move(joint))->id();
    doc.undoStack().push(app::makeDeleteCommand(doc, plan));
    QVERIFY(doc.component(cid) == nullptr);
    QVERIFY(doc.feature(jid) == nullptr);
    doc.undoStack().undo();
    QVERIFY(doc.component(cid) != nullptr);
    QVERIFY(doc.feature(jid) != nullptr);
  }

  void visibility() {
    core::Document doc;
    const core::ObjectId f = addBox(doc);
    const core::ObjectId b = doc.feature(f)->createdBodies().front();
    viewer::ViewerWidget viewer;
    viewer.setDocument(&doc);
    auto cmd = app::toggleVisibility(doc, &viewer, {ShapeRef::face(b, 2), ShapeRef::body(b)});
    QVERIFY(cmd);
    doc.undoStack().push(std::move(cmd));
    QVERIFY(!doc.body(b)->visible);
    QVERIFY(doc.liveBodies(true).empty());
    doc.undoStack().undo();
    QVERIFY(doc.body(b)->visible);
    // feature display state lives in the viewer
    QVERIFY(!app::toggleVisibility(doc, &viewer, {ShapeRef::sketch(f)}));
    QVERIFY(viewer.isFeatureHidden(f));
    QVERIFY(!app::toggleVisibility(doc, &viewer, {ShapeRef::originPlane("XY")}));
    QVERIFY(viewer.isOriginVisible());
    doc.undoStack().push(core::makeBodyEditCommand(doc, b, [](core::Body& x) { x.visible = false; }));
    auto all = app::showAll(doc, &viewer);
    QVERIFY(all);
    doc.undoStack().push(std::move(all));
    QVERIFY(doc.body(b)->visible);
    QVERIFY(!viewer.isFeatureHidden(f));
  }

  void renameAndSuppress() {
    core::Document doc;
    const core::ObjectId f = addBox(doc);
    const core::ObjectId b = doc.feature(f)->createdBodies().front();
    QVERIFY(!app::makeRenameFeature(doc, f, ""));
    QVERIFY(!app::makeRenameFeature(doc, f, doc.feature(f)->name()));
    doc.undoStack().push(app::makeRenameFeature(doc, f, "Base"));
    QCOMPARE(doc.feature(f)->name(), std::string("Base"));
    doc.undoStack().push(app::makeRenameBody(doc, b, "Plate"));
    QCOMPARE(doc.body(b)->name, std::string("Plate"));
    doc.undoStack().push(app::makeRenameComponent(doc, core::kRootComponentId, "Bracket"));
    QCOMPARE(doc.name(), std::string("Bracket"));
    QCOMPARE(doc.rootComponent().name, std::string("Bracket"));
    doc.undoStack().undo();
    QCOMPARE(doc.name(), std::string("Untitled"));
    doc.undoStack().push(app::makeSuppress(doc, f, true));
    QVERIFY(doc.feature(f)->isSuppressed());
    QVERIFY(doc.liveBodies().empty());
    QVERIFY(!app::makeSuppress(doc, f, true));
    doc.undoStack().undo();
    QVERIFY(!doc.feature(f)->isSuppressed());
    QCOMPARE(doc.liveBodies().size(), size_t(1));
  }

  void editDispatch() {
    core::Document doc;
    const core::ObjectId f = addBox(doc);
    core::ToolRegistry reg;
    QVERIFY(!app::editCommandFor(reg, *doc.feature(f)).valid());
    reg.registerTool(std::make_unique<app::EditPropertiesTool>());
    const app::EditCommand c = app::editCommandFor(reg, *doc.feature(f));
    QCOMPARE(c.toolId, std::string("app.edit-properties"));
    QCOMPARE(c.args, std::vector<std::string>{std::to_string(f)});
  }

  // --- property editing ----------------------------------------------------------------------
  void numericProperty() {
    core::Document doc;
    doc.setParameter("width", "40 mm");
    doc.evaluateParameters();
    core::Property p("d", core::PropertyType::Length);
    std::string err;
    QVERIFY(app::setNumericProperty(doc, p, "12.5", &err));
    QCOMPARE(p.toDouble(), 12.5);
    QVERIFY(!p.hasExpression());
    QVERIFY(app::setNumericProperty(doc, p, "width/2 + 1 cm", &err));
    QCOMPARE(p.toDouble(), 30.0);
    QCOMPARE(p.expression(), std::string("width/2 + 1 cm"));
    QVERIFY(!app::setNumericProperty(doc, p, "width +", &err));
    QVERIFY(!err.empty());
    QCOMPARE(p.toDouble(), 30.0);  // unchanged on error
    QVERIFY(!app::setNumericProperty(doc, p, "30 deg", &err));
    p.flags().allowNegative = false;
    QVERIFY(!app::setNumericProperty(doc, p, "-3", &err));
  }

  void editPropertiesTool() {
    core::Document doc;
    const core::ObjectId f = addBox(doc, 10);
    const core::ObjectId body = doc.feature(f)->createdBodies().front();
    core::ToolRegistry reg;
    reg.registerTool(std::make_unique<app::EditPropertiesTool>());
    core::NullViewerContext viewer;  // outlive the manager
    core::NullAppHost host;
    core::ToolManager mgr(reg);
    mgr.setDocument(&doc);
    mgr.setViewer(&viewer);
    mgr.setHost(&host);
    const int undoBefore = doc.undoStack().count();
    auto bboxX = [&] {
      Bnd_Box b;
      BRepBndLib::Add(doc.bodyShape(body), b);
      double x0, y0, z0, x1, y1, z1;
      b.SetGap(0.0);
      b.Get(x0, y0, z0, x1, y1, z1);
      return std::round((x1 - x0) * 1000.0) / 1000.0;  // Bnd_Box includes shape tolerance
    };

    // OK path: live preview, one undo step
    QVERIFY(mgr.run("app.edit-properties", {std::to_string(f)}));
    auto* tool = static_cast<app::EditPropertiesTool*>(mgr.activeTool());
    QCOMPARE(tool->featureId(), f);
    QVERIFY(tool->setNumeric(*mgr.activeContext(), "dx", "25"));
    QCOMPARE(bboxX(), 25.0);
    QVERIFY(!tool->setNumeric(*mgr.activeContext(), "dx", "nonsense ("));
    QVERIFY(mgr.runCommandLine("dx=30"));
    QCOMPARE(bboxX(), 30.0);
    mgr.accept();
    QVERIFY(!mgr.isActive());
    QCOMPARE(doc.undoStack().count(), undoBefore + 1);
    QCOMPARE(bboxX(), 30.0);
    doc.undoStack().undo();
    QCOMPARE(bboxX(), 10.0);
    doc.undoStack().redo();
    QCOMPARE(bboxX(), 30.0);

    // Cancel path restores the feature, no undo entry
    QVERIFY(mgr.run("app.edit-properties", {"edit", std::to_string(f)}));
    QVERIFY(mgr.runCommandLine("7"));  // bare value -> first numeric property
    QCOMPARE(bboxX(), 7.0);
    mgr.cancel();
    QCOMPARE(bboxX(), 30.0);
    QCOMPARE(doc.undoStack().count(), undoBefore + 1);

    // dialog body builds headless
    QVERIFY(mgr.run("app.edit-properties", {std::to_string(f)}));
    QWidget parent;
    std::unique_ptr<QWidget> dlg(mgr.activeTool()->createDialog(*mgr.activeContext(), &parent));
    QVERIFY(dlg);
    QVERIFY(dlg->findChildren<QLineEdit*>().size() >= 4);  // name + dx, dy, dz (+ bodyName)
    dlg.reset();
    mgr.cancel();

    // unknown feature: tool cancels itself with a message
    QVERIFY(mgr.run("app.edit-properties", {"999999"}));
    QVERIFY(!mgr.isActive());
    QVERIFY(!host.messages().empty());
  }

  void materialAndComponentProperties() {
    auto near = [](double a, double b) { return std::abs(a - b) < 1e-6; };
    QVERIFY(core::materialLibrary().size() >= 20);
    QVERIFY(core::findMaterial("steel") && near(core::findMaterial("steel")->density, 7.85));
    QVERIFY(!core::findMaterial("Unobtainium"));
    core::Document doc;
    core::test::registerTestFeatures();
    auto box = std::make_unique<core::test::BoxFeature>();
    doc.addFeature(std::move(box));
    doc.recompute();
    QVERIFY(!doc.liveBodies().empty());
    const core::ObjectId bodyId = doc.liveBodies().front();
    const std::array<float, 4> before = doc.body(bodyId)->color;
    // library material: name, density and colour; undoable
    auto cmd = app::makeSetBodyMaterial(doc, bodyId, "Aluminum 6061", 0.0, true);
    QVERIFY(cmd);
    doc.undoStack().push(std::move(cmd));
    QCOMPARE(doc.body(bodyId)->material, std::string("Aluminum 6061"));
    QVERIFY(near(doc.body(bodyId)->density, 2.70));
    QVERIFY(doc.body(bodyId)->color != before);
    QVERIFY(!app::makeSetBodyMaterial(doc, bodyId, "Aluminum 6061", 0.0, true));  // no-op
    doc.undoStack().undo();
    QVERIFY(doc.body(bodyId)->material.empty());
    QVERIFY(near(doc.body(bodyId)->density, 0.0));
    QVERIFY(doc.body(bodyId)->color == before);
    // custom material keeps the given density and the colour
    doc.undoStack().push(app::makeSetBodyMaterial(doc, bodyId, "Unobtainium", 12.5, true));
    QCOMPARE(doc.body(bodyId)->material, std::string("Unobtainium"));
    QVERIFY(near(doc.body(bodyId)->density, 12.5));
    QVERIFY(doc.body(bodyId)->color == before);
    // appearance
    doc.undoStack().push(app::makeSetBodyColor(doc, bodyId, {1.0f, 0.0f, 0.0f, 1.0f}));
    QVERIFY(near(doc.body(bodyId)->color[0], 1.0));
    // component properties feed the BOM
    core::Component& comp = doc.addComponent("Bracket", core::kRootComponentId);
    const core::ObjectId compId = comp.id;
    doc.undoStack().push(app::makeSetComponentProperties(doc, compId, "BRK-0012", "Mounting bracket", "Steel"));
    QCOMPARE(doc.component(compId)->partNumber, std::string("BRK-0012"));
    QCOMPARE(doc.component(compId)->description, std::string("Mounting bracket"));
    QCOMPARE(doc.component(compId)->material, std::string("Steel"));
    QVERIFY(!app::makeSetComponentProperties(doc, compId, "BRK-0012", "Mounting bracket", "Steel"));
    doc.undoStack().undo();
    QVERIFY(doc.component(compId)->partNumber.empty());
    // mass: body density * volume (BoxFeature is 10 x 20 x 30 = 6000 mm3)
    QVERIFY(near(doc.body(bodyId)->density * core::volume(doc.bodyShape(bodyId)) / 1000.0, 12.5 * 6.0));
  }

  // Enum changes re-derive the property flags and rebuild the rows on the next event-loop turn
  // (never synchronously from the combo's own signal).
  void propertyEditorRebuildsOnEnumChange() {
    core::Document doc;
    assembly::JointFeature joint;  // Rigid: motion value rows hidden
    app::PropertyEditor editor(doc, joint);
    QVERIFY(!editor.findChild<QLineEdit*>("prop_angle"));
    QVERIFY(!editor.findChild<QLineEdit*>("prop_limitMin"));
    auto* type = editor.findChild<QComboBox*>("prop_type");
    QVERIFY(type);
    int edited = 0;
    QObject::connect(&editor, &app::PropertyEditor::propertyEdited, [&](const std::string&) { ++edited; });
    type->setCurrentIndex(static_cast<int>(assembly::JointType::Revolute));
    QCOMPARE(edited, 1);
    QCOMPARE(joint.jointType(), assembly::JointType::Revolute);
    QVERIFY(!joint.prop("angle").flags().hidden);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
    QTest::qWait(20);
    QVERIFY(editor.findChild<QLineEdit*>("prop_angle"));
    QVERIFY(editor.findChild<QLineEdit*>("prop_limitMin"));
    auto* type2 = editor.findChild<QComboBox*>("prop_type");
    QVERIFY(type2);
    QCOMPARE(type2->currentIndex(), static_cast<int>(assembly::JointType::Revolute));
    type2->setCurrentIndex(static_cast<int>(assembly::JointType::Rigid));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
    QVERIFY(!editor.findChild<QLineEdit*>("prop_angle"));
    QCOMPARE(edited, 2);
  }
};

QTEST_MAIN(TestAppLogic)
#include "test_app_logic.moc"
