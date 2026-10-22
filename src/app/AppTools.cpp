// FabriCAD app: application-level tools.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/AppTools.h"

#include "app/ThemeManager.h"

#include "app/AppActions.h"
#include "app/AppearancePicker.h"
#include "app/PreferencesDialog.h"

#include "app/MainWindow.h"
#include "app/PropertyEditor.h"
#include "core/AppHost.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Expression.h"
#include "core/GeometryRefs.h"
#include "core/Selection.h"
#include "core/UndoStack.h"
#include "viewer/ViewerWidget.h"

#include <QColor>

#include <array>
#include <cctype>
#include <format>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fabricad::app {

using core::ToolContext;
using core::ToolInfo;

namespace {

ToolInfo info(const std::string& id, const std::string& name, const std::string& tip, const std::string& shortcut,
              std::vector<std::string> aliases, const std::string& icon = {}) {
  ToolInfo i;
  i.id = id;
  i.name = name;
  i.tooltip = tip;
  i.shortcut = shortcut;
  i.aliases = std::move(aliases);
  i.icon = icon;
  i.repeatable = false;
  return i;
}

/// The single modeless Preferences dialog of the window, bound to the active design.
void showPreferences(MainWindow* win, PreferencesDialog::Page page) {
  auto* dlg = win->findChild<PreferencesDialog*>();
  if (!dlg) {
    dlg = new PreferencesDialog(win->document(), win->viewer(), win);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
  } else {
    dlg->setTarget(win->document(), win->viewer());
  }
  dlg->showPage(page);
  dlg->show();
  dlg->raise();
  dlg->activateWindow();
}

std::string upper(std::string s) {
  for (char& ch : s) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  return s;
}

std::string lowerStr(std::string s) {
  for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return s;
}

}  // namespace

void registerAppTools(MainWindow& w, core::ToolRegistry& reg) {
  MainWindow* win = &w;
  auto add = [&](ToolInfo i, std::function<void(ToolContext&)> fn, std::function<bool(const ToolContext&)> enabled = {}) {
    reg.registerTool(std::make_unique<core::LambdaTool>(std::move(i), std::move(fn), std::move(enabled)));
  };
  /// View command: a LambdaTool plus a direct action that keeps the active tool running.
  auto view = [&](ToolInfo i, std::function<void(viewer::ViewerWidget&)> fn) {
    i.needsDocument = false;
    i.keepsActiveTool = true;  // ToolManager::run / command line keep the active tool running
    const std::string id = i.id;
    auto run = [win, fn] {
      if (win->viewer()) fn(*win->viewer());
    };
    add(std::move(i), [run](ToolContext&) { run(); });
    win->addDirectAction(id, run);
  };

  // --- file -------------------------------------------------------------------------------
  {
    ToolInfo i = info("app.new", "New Design", "Start a new, empty design", "Ctrl+N", {"NEW"}, "new");
    i.needsDocument = false;
    // QTimer-free: the tool finishes before the document is replaced (ToolManager is idle then)
    add(i, [win](ToolContext&) { QMetaObject::invokeMethod(win, [win] { win->newDocument(); }, Qt::QueuedConnection); });
  }
  {
    ToolInfo i = info("app.open", "Open", "Open a design or insert a STEP/STL file", "Ctrl+O", {"OPEN"}, "open");
    i.needsDocument = false;
    add(i, [win](ToolContext& c) {
      if (!c.args.empty()) {
        const QString path = QString::fromStdString(c.args[0]);
        QMetaObject::invokeMethod(win, [win, path] {
          QString err;
          if (win->maybeSave() && !win->openFile(path, &err))
            win->showMessage("Cannot open " + path.toStdString() + ": " + err.toStdString(), core::MessageLevel::Error);
        }, Qt::QueuedConnection);
        return;
      }
      QMetaObject::invokeMethod(win, [win] { win->open(); }, Qt::QueuedConnection);
    });
  }
  add(info("app.save", "Save", "Save the design", "Ctrl+S", {"SAVE", "QSAVE"}, "save"), [win](ToolContext& c) {
    if (!c.args.empty()) {
      QString err;
      if (!win->saveTo(QString::fromStdString(c.args[0]), &err))
        c.showMessage("Save failed: " + err.toStdString(), true);
      return;
    }
    QMetaObject::invokeMethod(win, [win] { win->save(); }, Qt::QueuedConnection);
  });
  add(info("app.save-as", "Save As", "Save the design under a new name", "Ctrl+Shift+S", {"SAVEAS"}, "save-as"),
      [win](ToolContext& c) {
        if (!c.args.empty()) {
          QString err;
          if (!win->saveTo(QString::fromStdString(c.args[0]), &err))
            c.showMessage("Save failed: " + err.toStdString(), true);
          return;
        }
        QMetaObject::invokeMethod(win, [win] { win->saveAs(); }, Qt::QueuedConnection);
      });

  // --- edit ---------------------------------------------------------------------------------
  auto canUndo = [](const ToolContext& c) { return c.undo && c.undo->canUndo(); };
  auto canRedo = [](const ToolContext& c) { return c.undo && c.undo->canRedo(); };
  auto undo = [](ToolContext& c) {
    if (c.undo && c.undo->canUndo()) c.undo->undo();
  };
  auto redo = [](ToolContext& c) {
    if (c.undo && c.undo->canRedo()) c.undo->redo();
  };
  add(info("app.undo", "Undo", "Undo the last change", "Ctrl+Z", {"U", "UNDO"}, "undo"), undo, canUndo);
  add(info("app.redo", "Redo", "Redo the last undone change", "Ctrl+Y", {"REDO"}, "redo"), redo, canRedo);
  {
    ToolInfo i = info("app.redo-alt", "Redo", "Redo the last undone change", "Ctrl+Shift+Z", {}, "redo");
    i.hidden = true;
    add(i, redo, canRedo);
  }
  add(info("app.delete", "Delete", "Delete the selected features, bodies or components", "Del", {"DELETE", "ERASE"},
           "delete"),
      [win](ToolContext&) { win->deleteSelection(); },
      [](const ToolContext& c) { return c.selection && !c.selection->isEmpty(); });
  add(info("app.toggle-visibility", "Show/Hide", "Toggle visibility of the selection", "V", {"HIDE", "SHOW"},
           "visibility"),
      [win](ToolContext&) { win->toggleSelectionVisibility(); },
      [](const ToolContext& c) { return c.selection && !c.selection->isEmpty(); });
  add(info("app.show-all", "Show All", "Show all bodies, components, sketches and construction geometry", "",
           {"SHOWALL"}, "eye"),
      [win](ToolContext&) { win->showAllObjects(); });
  add(info("app.isolate", "Isolate", "Hide everything except the selected bodies and components", "", {"ISOLATE"},
           "visibility"),
      [](ToolContext& c) {
        if (!c.document) return;
        std::vector<core::ShapeRef> items = c.preselection;
        if (items.empty() && c.selection) items = c.selection->items();
        if (auto cmd = isolate(*c.document, items)) {
          if (c.undo) c.undo->push(std::move(cmd));
          else cmd->redo();
          c.showMessage("Isolated: Show All restores the other objects");
        } else {
          c.showMessage("Select bodies or components to isolate", true);
        }
      },
      [](const ToolContext& c) { return c.selection && !c.selection->isEmpty(); });
  add(info("app.compute-all", "Compute All", "Recompute the whole timeline", "Ctrl+B", {"REGEN", "COMPUTE"}, "compute"),
      [](ToolContext& c) {
        if (c.document) c.document->recomputeAll();
      });
  {
    ToolInfo i = info("app.parameters", "Change Parameters", "Edit user parameters and expressions", "",
                      {"PARAMETERS", "PARAM", "SET"}, "parameters");
    i.tab = "SOLID";
    i.group = "MODIFY";
    i.order = 900;
    i.keepsActiveTool = true;  // "SET width=40 mm" during a command does not cancel it
    add(i, [win](ToolContext& c) {
      // "SET width=40 mm" sets a parameter from the command line
      if (!c.args.empty() && c.document) {
        std::string line;
        for (const std::string& a : c.args) line += (line.empty() ? "" : " ") + a;
        if (auto eq = line.find('='); eq != std::string::npos) {
          std::string name = line.substr(0, eq), expr = line.substr(eq + 1);
          while (!name.empty() && name.back() == ' ') name.pop_back();
          while (!expr.empty() && expr.front() == ' ') expr.erase(expr.begin());
          // APP-02/05: validate before pushing; never remove a parameter that is in use
          if (!core::isValidParameterName(name)) {
            c.showMessage("invalid parameter name: " + name, true);
            return;
          }
          if (expr.empty()) {
            if (!c.document->parameter(name)) {
              c.showMessage("no parameter named " + name, true);
              return;
            }
            if (const std::string why = checkParameterEdit(*c.document, name, ""); !why.empty()) {
              c.showMessage(why, true);
              return;
            }
          }
          // undoable (SetParameterCommand recomputes)
          if (c.undo) {
            auto cmd = std::make_unique<core::SetParameterCommand>(*c.document, name, expr);
            c.undo->push(std::move(cmd));
          } else if (!c.document->setParameter(name, expr)) {
            c.showMessage("invalid parameter name: " + name, true);
            return;
          } else {
            c.document->recompute();
          }
          if (const core::Document::Parameter* p = c.document->parameter(name); p && !p->ok)
            c.showMessage("parameter " + name + ": " + p->error, true);
          else if (!p && !expr.empty()) c.showMessage("invalid parameter name: " + name, true);
          else if (p) c.showMessage(std::format("{} = {} ({})", name, expr, p->value.value));
          return;
        }
      }
      win->showParameters();
    });
  }
  reg.registerTool(std::make_unique<EditPropertiesTool>());

  // --- appearance (A, SOLID > MODIFY, APP-04) ---------------------------------------------------
  {
    ToolInfo i = info("app.appearance", "Appearance", "Colour of the selected bodies or components", "A",
                      {"APPEARANCE"}, "display");
    i.tab = "SOLID";
    i.group = "MODIFY";
    i.order = 910;
    add(i,
        [win](ToolContext& c) {
          if (!c.document) return;
          std::vector<core::ShapeRef> items = c.preselection;
          if (items.empty() && c.selection) items = c.selection->items();
          const std::vector<core::ObjectId> bodies = appearanceTargets(*c.document, items);
          if (bodies.empty()) {
            c.showMessage("Select bodies or components, then press A", true);
            return;
          }
          // scripts/tests: "APPEARANCE #rrggbb" sets the colour without the modal dialog
          if (!c.args.empty()) {
            const QColor col(QString::fromStdString(c.args[0]));
            if (!col.isValid()) {
              c.showMessage("APPEARANCE: expected a colour like #ff8800, got " + c.args[0], true);  // colour-lint: ok (help text)
              return;
            }
            const std::array<float, 4> rgba{static_cast<float>(col.redF()), static_cast<float>(col.greenF()),
                                            static_cast<float>(col.blueF()), 1.0f};
            if (auto cmd = makeSetBodiesColor(*c.document, bodies, rgba)) {
              if (c.undo) c.undo->push(std::move(cmd));
              else cmd->redo();
            }
            return;
          }
          // the colour dialog is modal: open it after the tool has finished
          QMetaObject::invokeMethod(win, [win, bodies] { pickAppearance(win, win->document(), bodies); },
                                    Qt::QueuedConnection);
        },
        [win](const ToolContext& c) { return c.document && !(win->viewer() && win->viewer()->inSketchMode()); });
  }

  // --- preferences / units (TOOLS > UTILITIES, APP-03 / APP-09) -------------------------------------
  {
    ToolInfo i = info("app.preferences", "Preferences", "Units, zoom direction and other settings", "",
                      {"PREFERENCES", "PREFS"}, "settings");
    i.needsDocument = false;
    i.tab = "TOOLS";
    i.group = "UTILITIES";
    i.order = 20;  // not promoted: the group shows its first two (Change Parameters, Preferences)
    add(i, [win](ToolContext&) { showPreferences(win, PreferencesDialog::Page::General); });
  }
  {
    // live theme switch (redesign phase 0; the Preferences Appearance page comes in phase 1)
    ToolInfo i = info("app.theme", "Theme", "Colour theme: THEME dark|light|system (no argument: toggle dark/light)", "",
                      {"THEME"}, "settings");
    i.needsDocument = false;
    add(i, [](ToolContext& c) {
      ThemeManager& tm = ThemeManager::instance();
      ThemeManager::Mode m = tm.theme() == theme::Theme::Dark ? ThemeManager::Mode::Light : ThemeManager::Mode::Dark;
      if (!c.args.empty() && !ThemeManager::parseMode(QString::fromStdString(c.args[0]), &m)) {
        c.showMessage("THEME: expected dark, light or system, got " + c.args[0], true);
        return;
      }
      tm.setMode(m);
      c.showMessage("Theme: " + ThemeManager::modeName(m).toStdString());
    });
  }
  {
    ToolInfo i = info("app.units", "Units", "Units of the active design: UNITS mm|cm|m|in|ft [deg|rad]", "",
                      {"UNITS", "UN"}, "settings");
    add(i, [win](ToolContext& c) {
      if (c.args.empty() || !c.document) {
        showPreferences(win, PreferencesDialog::Page::Document);
        return;
      }
      core::UnitSystem u = c.document->units();
      for (const std::string& a : c.args) {
        const std::string t = lowerStr(a);
        if (auto l = core::lengthUnitFromSymbol(t)) u.length = *l;
        else if (auto g = core::angleUnitFromSymbol(t)) u.angle = *g;
        else {
          c.showMessage("UNITS: unknown unit '" + a + "' (mm, cm, m, in, ft, deg, rad)", true);
          return;
        }
      }
      if (auto cmd = makeSetUnits(*c.document, u)) {
        if (c.undo) c.undo->push(std::move(cmd));
        else cmd->redo();
      }
      c.showMessage(std::format("Units: {}, {}", core::lengthUnitSymbol(u.length), core::angleUnitSymbol(u.angle)));
    });
  }

  // --- shell ---------------------------------------------------------------------------------
  {
    ToolInfo i = info("app.command-line", "Command Line", "Show or hide the command line", "Ctrl+9", {"CMDLINE"},
                      "command-line");
    i.needsDocument = false;
    view(i, [win](viewer::ViewerWidget&) { win->toggleCommandLine(); });
  }
  {
    ToolInfo i = info("app.search", "Command Search", "Search all commands (S)", "", {"SEARCH"}, "search");
    i.needsDocument = false;
    view(i, [win](viewer::ViewerWidget&) { win->openCommandSearch(); });
  }
  {
    ToolInfo i = info("app.help", "Keyboard Shortcuts", "List of commands, shortcuts and aliases", "F1", {"HELP"}, "help");
    i.needsDocument = false;
    view(i, [win](viewer::ViewerWidget&) { win->showShortcutsHelp(); });
  }
  {
    ToolInfo i = info("app.workspace-design", "Design Workspace", "Switch to the Design workspace", "", {"DESIGN"},
                      "workspace-design");
    i.needsDocument = false;
    add(i, [win](ToolContext&) { QMetaObject::invokeMethod(win, [win] { win->setWorkspace(core::Workspace::Design); }, Qt::QueuedConnection); });
    i = info("app.workspace-drawing", "Drawing Workspace", "Switch to the Drawing workspace", "", {"DRAWING"},
             "workspace-drawing");
    i.needsDocument = false;
    add(i, [win](ToolContext&) { QMetaObject::invokeMethod(win, [win] { win->setWorkspace(core::Workspace::Drawing); }, Qt::QueuedConnection); });
  }

  // --- view ------------------------------------------------------------------------------------
  view(info("view.fit", "Fit", "Fit the whole model in the view", "F6", {"FIT", "ZOOMEXTENTS", "ZE"}, "fit"),
       [](viewer::ViewerWidget& v) { v.fitAll(true); });
  {
    // ZOOM E|A fits, ZOOM <factor> zooms about the centre, ZOOM W = nav-bar zoom drag (APP-09).
    // No "Z" alias: keepsActiveTool verbs are never offered to the active tool, so a "Z"
    // answer in a prompt (axis, keyword) would be swallowed.
    ToolInfo i = info("view.zoom", "Zoom", "ZOOM E (extents) | ZOOM <factor> | ZOOM W (drag to zoom)", "", {"ZOOM"},
                      "zoom");
    i.needsDocument = false;
    i.keepsActiveTool = true;
    add(i, [win](ToolContext& c) {
      viewer::ViewerWidget* v = win->viewer();
      if (!v) return;
      const std::string a = c.args.empty() ? "E" : upper(c.args[0]);
      if (a == "E" || a == "A" || a == "EXTENTS" || a == "ALL") {
        v->fitAll(true);
        return;
      }
      if (a == "W" || a == "WINDOW") {
        v->setLeftButtonNavigation(viewer::NavAction::Zoom);
        c.showMessage("Drag with the left button to zoom, Esc to stop");
        return;
      }
      std::string num = a;
      if (!num.empty() && num.back() == 'X') num.pop_back();
      try {
        size_t used = 0;
        const double f = std::stod(num, &used);
        if (used == num.size() && f > 0) {
          v->zoomBy(f, true);
          return;
        }
      } catch (...) {
      }
      c.showMessage("ZOOM: expected E, W or a factor such as 2 or 0.5", true);
    });
  }
  view(info("view.home", "Home View", "Go to the home view", "Home", {"HOME"}, "home"),
       [](viewer::ViewerWidget& v) { v.setStandardView(core::StandardView::Home, true); });
  struct Std {
    const char* id;
    const char* name;
    const char* key;
    core::StandardView v;
  };
  for (const Std& s : {Std{"view.front", "Front", "Ctrl+1", core::StandardView::Front},
                       Std{"view.back", "Back", "Ctrl+2", core::StandardView::Back},
                       Std{"view.left", "Left", "Ctrl+3", core::StandardView::Left},
                       Std{"view.right", "Right", "Ctrl+4", core::StandardView::Right},
                       Std{"view.top", "Top", "Ctrl+5", core::StandardView::Top},
                       Std{"view.bottom", "Bottom", "Ctrl+6", core::StandardView::Bottom},
                       Std{"view.iso", "Isometric", "Ctrl+7", core::StandardView::Iso}}) {
    std::string alias = s.name;
    for (char& ch : alias) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    std::vector<std::string> aliases{alias};
    if (s.v == core::StandardView::Iso) aliases.push_back("ISO");  // docs/SCRIPTING.md
    view(info(s.id, std::string(s.name) + " View", std::string("Look from the ") + s.name, s.key, aliases, "views"),
         [v = s.v](viewer::ViewerWidget& vw) { vw.setStandardView(v, true); });
  }
  view(info("view.look-at", "Look At", "Look straight at the selected face or plane", "Ctrl+8", {"LOOKAT", "PLAN"},
            "look-at"),
       [win](viewer::ViewerWidget& v) {
         core::Document& doc = win->document();
         for (const core::ShapeRef& r : doc.selection().items())
           if (auto plane = core::resolvePlane(doc, r)) {
             v.lookAt(*plane, true);
             return;
           }
         if (v.inSketchMode()) v.lookAt(v.sketchPlane(), true);
         else win->showMessage("Select a planar face or plane to look at", core::MessageLevel::Info);
       });
  struct Orbit {
    const char* id;
    const char* name;
    const char* key;
    double yaw, pitch;
  };
  for (const Orbit& o : {Orbit{"view.orbit-left", "Orbit Left", "Shift+Left", -15, 0},
                         Orbit{"view.orbit-right", "Orbit Right", "Shift+Right", 15, 0},
                         Orbit{"view.orbit-up", "Orbit Up", "Shift+Up", 0, 15},
                         Orbit{"view.orbit-down", "Orbit Down", "Shift+Down", 0, -15}}) {
    ToolInfo i = info(o.id, o.name, "Rotate the view by 15 degrees", o.key, {}, "orbit");
    i.hidden = true;
    view(i, [o](viewer::ViewerWidget& v) { v.orbitBy(o.yaw, o.pitch, true); });
  }
  view(info("view.orthographic", "Orthographic", "Orthographic camera", "", {"ORTHO"}, "views"),
       [](viewer::ViewerWidget& v) { v.setProjection(core::ProjectionMode::Orthographic); });
  view(info("view.perspective", "Perspective", "Perspective camera", "", {"PERSP"}, "views"),
       [](viewer::ViewerWidget& v) { v.setProjection(core::ProjectionMode::Perspective); });
  struct Mode {
    const char* id;
    const char* name;
    const char* alias;
    core::DisplayMode m;
  };
  for (const Mode& m : {Mode{"view.shaded", "Shaded", "SHADE", core::DisplayMode::Shaded},
                        Mode{"view.shaded-edges", "Shaded with Edges", "SHADEDGES", core::DisplayMode::ShadedEdges},
                        Mode{"view.wireframe", "Wireframe", "WIRE", core::DisplayMode::Wireframe},
                        Mode{"view.hidden-line", "Hidden Line", "HIDE3D", core::DisplayMode::HiddenLine}})
    view(info(m.id, m.name, std::string("Visual style: ") + m.name, "", {m.alias}, "display"),
         [mode = m.m](viewer::ViewerWidget& v) { v.setDisplayMode(mode); });
  view(info("view.grid", "Layout Grid", "Show or hide the grid", "", {"GRID"}, "grid"),
       [](viewer::ViewerWidget& v) { v.setGridVisible(!v.isGridVisible()); });
  view(info("view.snap", "Snap to Grid", "Toggle grid snapping in sketches", "", {"SNAP"}, "grid"),
       [](viewer::ViewerWidget& v) { v.setGridSnap(!v.isGridSnap(), 0.0); });

  // --- selection priority (SELECT group) ----------------------------------------------------------
  struct Prio {
    const char* id;
    const char* name;
    const char* icon;
    viewer::SelectionPriority p;
    int order;
  };
  for (const Prio& p : {Prio{"select.default", "Select", "select", viewer::SelectionPriority::Default, 10},
                        Prio{"select.priority-body", "Select Body Priority", "select-body", viewer::SelectionPriority::Body, 20},
                        Prio{"select.priority-face", "Select Face Priority", "select-face", viewer::SelectionPriority::Face, 21},
                        Prio{"select.priority-edge", "Select Edge Priority", "select-edge", viewer::SelectionPriority::Edge, 22},
                        Prio{"select.priority-vertex", "Select Vertex Priority", "select-vertex",
                             viewer::SelectionPriority::Vertex, 23},
                        Prio{"select.priority-component", "Select Component Priority", "select-component",
                             viewer::SelectionPriority::Component, 24}}) {
    ToolInfo i = info(p.id, p.name, "Restrict viewport picking", "", {}, p.icon);
    i.tab = "SOLID";
    i.group = "SELECT";
    i.order = p.order;
    i.promoted = p.p == viewer::SelectionPriority::Default;
    // UX-39: the active priority shows as checked (radio group)
    i.checked = [win, prio = p.p] { return win->viewer() && win->viewer()->selectionPriority() == prio; };
    i.exclusiveGroup = "select-priority";
    view(i, [p](viewer::ViewerWidget& v) { v.setSelectionPriority(p.p); });
  }

  // --- selection filters (SELECT > Selection Filters, ux.md 2.1) ---------------------------------
  // Toggles of a user mask in the SelectionModel; ignored while a command sets its own filter.
  struct Filt {
    const char* id;
    const char* name;
    core::ShapeKindMask kinds;
    int order;
  };
  using core::ShapeKind;
  for (const Filt& f :
       {Filt{"select.filter-bodies", "Bodies", core::mask(ShapeKind::Body), 40},
        Filt{"select.filter-faces", "Faces", core::mask(ShapeKind::Face), 41},
        Filt{"select.filter-edges", "Edges", core::mask(ShapeKind::Edge), 42},
        Filt{"select.filter-vertices", "Vertices", core::mask(ShapeKind::Vertex), 43},
        Filt{"select.filter-sketches", "Sketch Geometry",
             ShapeKind::Sketch | ShapeKind::Profile | ShapeKind::SketchCurve | ShapeKind::SketchPoint, 44},
        Filt{"select.filter-construction", "Construction Geometry",
             ShapeKind::ConstructionPlane | ShapeKind::ConstructionAxis | ShapeKind::ConstructionPoint |
                 ShapeKind::OriginPlane | ShapeKind::OriginAxis | ShapeKind::OriginPoint,
             45},
        Filt{"select.filter-components", "Components", core::mask(ShapeKind::Component), 46}}) {
    ToolInfo i = info(f.id, std::string("Filter: ") + f.name,
                      std::string("Selection filter: allow picking ") + f.name, "", {}, "select");
    i.tab = "SOLID";
    i.group = "SELECT";
    i.order = f.order;
    i.keepsActiveTool = true;
    i.checked = [win, kinds = f.kinds] { return (win->document().selection().userFilter() & kinds) == kinds; };
    const std::string id = i.id;
    auto toggle = [win, kinds = f.kinds, name = std::string(f.name)] {
      core::SelectionModel& sel = win->document().selection();
      const bool on = (sel.userFilter() & kinds) == kinds;
      sel.setUserFilter(on ? sel.userFilter() & ~kinds : sel.userFilter() | kinds);
      if (win->viewer() && !win->toolManager().isActive()) win->viewer()->setSelectionFilter(sel.effectiveFilter());
      win->showMessage(std::format("Selection filter: {} {}", name, on ? "off" : "on"), core::MessageLevel::Info);
    };
    add(std::move(i), [toggle](ToolContext&) { toggle(); });
    win->addDirectAction(id, toggle);
  }
  {
    ToolInfo i = info("select.filter-all", "Filter: Select All", "Selection filter: allow every kind again", "", {},
                      "select");
    i.tab = "SOLID";
    i.group = "SELECT";
    i.order = 49;
    i.keepsActiveTool = true;
    auto all = [win] {
      win->document().selection().setUserFilter(core::kAllKinds);
      if (win->viewer() && !win->toolManager().isActive()) win->viewer()->setSelectionFilter(core::kAllKinds);
    };
    add(std::move(i), [all](ToolContext&) { all(); });
    win->addDirectAction("select.filter-all", all);
  }
}

}  // namespace fabricad::app
