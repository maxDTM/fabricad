// FabriCAD app: application-level tools (core::LambdaTool and friends) registered next to
// the module tools so shortcuts, the S-box, the command line and the marking menu treat
// them like any other command.
//
//   app.new/open/save/save-as (Ctrl+N/O/S, Ctrl+Shift+S), app.undo/redo (Ctrl+Z/Y,
//   Ctrl+Shift+Z), app.delete (Del), app.toggle-visibility (V), app.show-all,
//   app.compute-all (Ctrl+B), app.parameters, app.edit-properties, app.command-line
//   (Ctrl+9), app.search, app.help (F1), app.workspace-design/-drawing,
//   view.fit (F6), view.home (Home), view.front..iso (Ctrl+1..7), view.look-at (Ctrl+8),
//   view.orbit-left/right/up/down (Shift+arrows), view.orthographic/perspective,
//   view.shaded/shaded-edges/wireframe/hidden-line, view.grid, view.snap,
//   select.priority-* (SOLID > SELECT group).
// View commands are also registered as MainWindow direct actions (they never cancel the
// active command).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

namespace fabricad::core {
class ToolRegistry;
}

namespace fabricad::app {

class MainWindow;

void registerAppTools(MainWindow& window, core::ToolRegistry& registry);

}  // namespace fabricad::app
