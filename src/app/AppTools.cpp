// FabriCAD app: application-level tools.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/AppTools.h"

#include "app/MainWindow.h"
#include "app/PropertyEditor.h"
#include "core/AppHost.h"
#include "core/Document.h"
#include "core/GeometryRefs.h"
#include "core/Selection.h"
#include "core/UndoStack.h"
#include "viewer/ViewerWidget.h"

#include <cctype>
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
    add(i, [win](ToolContext& c) {
      // "SET width=40 mm" sets a parameter from the command line
      if (!c.args.empty() && c.document) {
        std::string line;
        for (const std::string& a : c.args) line += (line.empty() ? "" : " ") + a;
        if (auto eq = line.find('='); eq != std::string::npos) {
          std::string name = line.substr(0, eq), expr = line.substr(eq + 1);
          while (!name.empty() && name.back() == ' ') name.pop_back();
          while (!expr.empty() && expr.front() == ' ') expr.erase(expr.begin());
          if (!c.document->setParameter(name, expr)) c.showMessage("invalid parameter name: " + name, true);
          else c.document->recompute();
          return;
        }
      }
      win->showParameters();
    });
  }
  reg.registerTool(std::make_unique<EditPropertiesTool>());

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
    view(info(s.id, std::string(s.name) + " View", std::string("Look from the ") + s.name, s.key, {alias}, "views"),
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
    view(i, [p](viewer::ViewerWidget& v) { v.setSelectionPriority(p.p); });
  }
}

}  // namespace fabricad::app
