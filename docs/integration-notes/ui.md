# Integration notes: ui module

Append requests for changes to shared files here (format: see README.md).

## Summary of what the ui module provides (2026-10-19)

Targets: `fabricad_viewer` (src/viewer), `fabricad_app_lib` + `fabricad` exe (src/app),
resources in `resources/` (`fabricad.qrc`, `theme-light.qss`, `icons/*.svg`).
Build dir used: `build-ui/`. Tests: `fabricad_viewer_test_{scene_model,topo_helpers,viewer_math,viewer_smoke}`,
`fabricad_app_test_app_logic` (22 cases), `fabricad_app_test_main_window` (14 cases, headless MainWindow).
All pass with `QT_QPA_PLATFORM=offscreen`.

Measured: MainWindow construction 20-50 ms with 163 tools registered; window shown about 130-280 ms after
process start; OCCT GL view ready about 450 ms after start (created 30 ms after `show()`).

### Viewer (`viewer::ViewerWidget`, implements `core::IViewerContext`)
* OCCT V3d/AIS in a QOpenGLWidget (`src/viewer/occt/`, adapted from gkv311's MIT sample), created
  lazily by `initializeView()`. Falls back to `NullViewerContext` behaviour when there is no GL
  (offscreen tests, Wayland-only).
* Display sync from Document signals through the GL-free `SceneModel` (bodies, sketch/construction/joint
  `outputShape()`s with status Ok, origin geometry, component `worldPlacement`, ghosting).
* Fusion navigation (MMB pan, Shift+MMB orbit, wheel zoom at cursor, MMB double-click fit), presets
  Fusion/SolidWorks/AutoCAD/Inventor, ViewCube (top right) with a right-click menu, ortho/perspective,
  display modes, adaptive grid + snap in sketch mode, home view, light rig + body material tuned to
  ux.md 6.1 colours.
* Input -> `core::ViewerMouseEvent` (ray, plane point, pick as ShapeRef via `makeBrepRef` / sketch refs),
  routed to `ToolManager::mouse*`; default selection (click, Ctrl toggle/Shift add, window/crossing box,
  double-click tangent edge chain) only when the tool did not consume it. Mirrors `SelectionModel::filter()`.
  Preselection highlight.

### App shell (`app::MainWindow`, implements `core::IAppHost`)
* App bar (File menu: new/open/recent/save/save as/import/export/parameters, Save, Undo/Redo with
  command names, document label, command search, help), ribbon (workspace switcher DESIGN/DRAWING,
  tabs + group dropdowns from `ToolRegistry`, promoted = big icon, contextual SKETCH tab with Finish
  Sketch, flat CONSTRAINTS group), Browser dock (eyes, rename F2, context menus, hover preselect,
  selection mirroring, error/warning colours), Timeline dock (status colours, rollback marker drag ->
  `SetRollbackCommand`, drag-reorder with `canMoveFeature` check -> `MoveFeatureCommand`, double-click
  edit, right-click Edit/Roll here/Suppress/Rename/Delete/Find), command dialog host around
  `ITool::createDialog` (Enter/Esc/OK enable), Sketch Palette (session options, constraint buttons,
  Finish), nav bar overlay, S-key command search (fuzzy, recent, pinned), radial marking menu (click +
  gestures, context-dependent), AutoCAD command line (aliases, args, history, autocomplete, Ctrl+9),
  status bar (prompt, messages, selection text, sketch DOF via `SketchSession::statusText()`, units).
* Keyboard routing per ux.md 9 (`ShortcutRouter`): single letters only with viewport/browser/timeline
  focus, tool `keyPress` first, Esc cancel / clear selection, Enter accept-or-repeat, Space repeat.
* App tools (LambdaTool unless noted): `app.new/open/save/save-as/undo/redo/redo-alt/delete/
  toggle-visibility/show-all/compute-all/parameters/command-line/search/help/workspace-design/
  workspace-drawing`, `app.edit-properties` (generic PropertyEditor dialog, ModifyFeatureCommand),
  `view.fit/home/front/back/left/right/top/bottom/iso/look-at/orbit-*/orthographic/perspective/
  shaded/shaded-edges/wireframe/hidden-line/grid/snap`, `select.default/priority-*`.
  `app.save <path>` / `app.open <path>` work from the command line (scripting/tests).
* File I/O: `.fcad` open/save (UI state in `extensionData("ui")`: hidden features, folder eyes, display
  mode, projection), STEP/STL open goes through `part.import-file`, recent files (QSettings),
  unsaved-changes prompt, window geometry persisted.
* Env vars: `FABRICAD_STARTUP_TIMING=1` (phase timing), `FABRICAD_SCREENSHOT=out.png` (+
  `FABRICAD_SCREENSHOT_DELAY` ms) grabs the window and quits, `FABRICAD_QUIT_AFTER_STARTUP=1`,
  `FABRICAD_MSAA=n`, `FABRICAD_ALLOW_WAYLAND=1`.

No FreeCAD code was copied. The only vendored code is the gkv311 sample (see the first entry below).

---

## 2026-10-19 — THIRD_PARTY.md row for the OCCT/Qt viewer sample        (status: done)
What I need: add a row to `THIRD_PARTY.md` "Vendored / adapted code":
`src/viewer/occt/` (OcctGlTools.*, OcctQtTools.*, OcctView.*) | github.com/gkv311/occt-samples-qopenglwidget |
`72e32d3a8398` | MIT | OcctGlTools/OcctQtTools copied, OcctView adapted from OcctQOpenGLWidgetViewer.
Why: license compliance. Headers carry the original copyright and MIT notice.

## 2026-10-19 — Wire the Drawing workspace widget                          (status: done — main.cpp sets the factory)
What I need: once `drawing::DrawingWorkspaceWidget` exists, register it in `src/app/main.cpp` (or tell me
the constructor and I will):
```cpp
fabricad::app::MainWindow::setDrawingWorkspaceFactory(
    [](core::Document& doc, core::ToolManager& tm, QWidget* parent) -> QWidget* {
      return new drawing::DrawingWorkspaceWidget(doc, tm, parent);  // adjust to the real ctor
    });
```
(must be called before the user switches to DRAWING; the page is created lazily on the first switch).
Workaround in place: a placeholder page saying the workspace is not available.

## 2026-10-19 — Sketch module contract used by the shell                   (status: done — sketch.edit {id} is registered)
* Edit sketch (timeline/browser double-click, double-click on sketch geometry): the shell runs
  `sketch.edit` with args `{"<sketchId>"}`; if that id is not registered it falls back to
  `sketch.create` with `{"edit", "<sketchId>"}`. Please register one of them.
* `sketch.finish` is shown as the green FINISH SKETCH button (ribbon + palette); the SKETCH tab is shown
  via `IAppHost::setContextualTab("SKETCH", true/false)`.
* The idle tool must be hidden and its id must end in `.select` (`sketch.select`): the shell then treats
  Enter/Space as "repeat last", Esc as "clear selection" and the marking menu as idle-in-sketch.
* Tools with `tab = "SKETCH", group = "CONSTRAINTS"` appear as one-click buttons in the ribbon and the
  Sketch Palette. Palette options call `SketchSession::setConstructionMode/setSnapEnabled/setShow*`.
* Status bar shows `SketchSession::current()->statusText()`; it is coloured red when it contains
  "over"/"conflict", black for "fully", blue otherwise. Emit `SketchSession::changed` after solves.
* Marking-menu ids looked up (first registered wins): `sketch.line`, `sketch.circle|circle-center|
  center-circle`, `sketch.rectangle|rectangle-2pt|rect`, `sketch.trim`, `sketch.dimension`,
  `sketch.construction|toggle-construction`, `sketch.offset`, `sketch.project`.
* Delete key in sketch mode: the app calls `SketchSession::deleteSelection()` first.

## 2026-10-19 — Non-interrupting (view) commands in ToolManager            (status: done — `ToolInfo::keepsActiveTool`; view.* tools set it, command line included)
What I need: a `ToolInfo::keepsActiveTool` (or `interrupts = false`) flag; `ToolManager::run()` would
execute such instant tools without cancelling the active one.
Why: F6/Home/Ctrl+1..8/Look At/display modes must not cancel a running command (ux.md 5, Fusion).
Workaround in place: `MainWindow::addDirectAction()` executes view tools directly while a tool is
active (shortcuts, ribbon, S-box, marking menu go through `MainWindow::runCommand`). The command line
still goes through `ToolManager::runCommandLine` and therefore cancels.

## 2026-10-19 — Signal after ITool::activate()                             (status: done — `ToolManager::toolStarted`, MainWindow builds dialogs on it)
What I need: `ToolManager` emits `toolActivated(id)` *before* `activate()`. Please add
`toolStarted(id)` emitted after `activate()` returned with the tool still active.
Why: the command dialog must be built after activate() (ARCHITECTURE 8).
Workaround in place: the shell builds the dialog on the next event-loop turn (activation serial check).

## 2026-10-19 — Shortcut normalisation in ToolManager::triggerShortcut     (status: done — QKeySequence portable text)
What I need: compare `QKeySequence(s, PortableText).toString(PortableText)` instead of lower-casing,
so "Delete" == "Del", "Ctrl+Return" == "Ctrl+Enter" etc.
Workaround in place: the app matches normalised strings itself and passes the tool's own string.

## 2026-10-19 — Feature edit hook                                          (status: done — `Feature::editToolId()`, consulted first by app::editCommandFor; the joint tool still uses the convention fallback)
What I need: `virtual std::string Feature::editToolId() const` (empty = generic editor).
Why: the shell currently dispatches edits by convention (`app::editCommandFor`: Sketch -> sketch.edit,
part types -> `part::toolIdForFeatureType`, joints -> `assembly.edit-joint|assembly.joint {"edit", id}`,
else `app.edit-properties`). Assembly agent: please accept `{"edit", "<jointId>"}` in your joint tool
or register `assembly.edit-joint`.

## 2026-10-19 — Feature -> component association                         (status: done — `Feature::component()`; the browser lists sketches/construction/joints under their component)
What I need: a way to know which component a timeline feature belongs to (e.g. `Feature::component()`).
Why: the browser shows Sketches/Construction/Joints only under the root component; Fusion shows them
under the owning component.

## 2026-10-19 — Platform selection in main()                              (status: info)
The desktop here exports `QT_QPA_PLATFORM="wayland;xcb"`, which made Qt pick Wayland and the GLX-only
OCCT view fail. `main.cpp` now forces `xcb` whenever the variable is unset **or starts with "wayland"**
and `DISPLAY` is set (XWayland). `offscreen`/`xcb` are kept; `FABRICAD_ALLOW_WAYLAND=1` opts out.

## 2026-10-19 — Packaging suggestions                                       (status: open, low)
* Install `resources/icons/app/fabricad-app.svg` (hicolor PNGs: `resources/icons/app/render_app_icons.sh`) as the app icon and add a `fabricad.desktop` file
  (`Exec=fabricad %f`, `MimeType=application/x-fabricad;`) when an install target is added.

## Known gaps / follow-ups in the ui module
* "Select Other" (hold LMB on stacked entities) not implemented yet; `pickAll()` exists.
* Rollback marker applies on release (no live roll while dragging); timeline groups not implemented.
* Single document (no document tabs); no Preferences dialog yet (navigation preset, reverse zoom and
  grid settings are in the nav-bar menus and persisted in QSettings); themes: dark/light/system from
  design tokens since the redesign phase 0 (2026-10-22).
* Shortcut remapping UI, marking-menu customisation, F7 slice not implemented.

## 2026-10-22 — UI redesign phase 0: design tokens (branch ui/tokens)     (status: open — integrator merge)
What changed (docs/plans/ui-redesign.md sections 2-4, phase 0 items 0.1-0.3):
* New static library `fabricad_theme` (`src/theme/`, QtGui only): `theme::Tokens` (colours per theme,
  aliases, text styles, sizes; tables generated into `src/theme/ThemeTokens.inc`), `ColorMath`
  (WCAG contrast, CIEDE2000, Machado CVD), `renderQss()`. Linked by `fabricad_viewer` and the app.
* **Top-level `CMakeLists.txt`**: one line `add_subdirectory(src/theme)` before `src/viewer` (the
  plan assigns `src/theme` to this worktree; please keep it when merging).
* `resources/theme/tokens.json` is the single source; `tools/theme/gen_tokens.py` regenerates
  `ThemeTokens.inc`, `docs/design/palette.md`, `resources/theme/icon-palette.json` (for the icons
  worktree) and `resources/ui/{dark,light}/*.svg`. ctest `theme_tokens` checks sync, contrast and CVD.
* `app::ThemeManager` replaces `Theme.{h,cpp}` and `resources/theme-light.qss` (deleted): Dark /
  Light / Follow system, QSettings `ui/theme` (default dark), `FABRICAD_THEME` env override, live
  switch (`THEME dark|light|system` command, also in the S-box), JetBrains Mono bundled, caps via
  `QFont::AllUppercase` (values, typed text and user names keep their case).
* `viewer::ViewerPalette` (phase-0 interface) + `ViewerWidget::setViewerPalette()` (not
  `setPalette`, which would hide `QWidget::setPalette`): background, grid, face edges, selection,
  preselection, ViewCube, cube controls, rubber band. The rest of the viewer is phase 4.
* Colour lint: ctest `theme_color_lint` (`tools/theme/audit_colors.py`, allow-list
  `tools/theme/color-allowlist.txt`). New colour literals in `src/` fail the build's tests.
* Integrator (plan section 9): add `fabricad_theme` to the `ARCHITECTURE.md` module map.

## 2026-10-22 — Colour roles and token styling for other modules            (status: open — requests)
What I need (plan section 9; until then each file is allow-listed with its count):
* core: `core::ColorRole` on `PreviewStyle` (`Live`, `Join`, `Cut`, `NewBody`, `SketchFree`,
  `Inference`, `Dimension`, ...) and roles for the manipulator colours; RGBA stays as a fallback.
* sketch: `colors::` constants in `SketchSession.h` -> roles (note the NEW `sk.free`:
  `#4775FF` dark / `#00248F` light, chosen so free curves stay apart from selection purple under
  protan/deutan/tritan simulation, decision 8); `ValueInputOverlay` inline QSS -> objectName/property
  hooks styled by `resources/theme/app.qss.tmpl`.
* part: `PropertyPanel` inline styles (`#D93025` error border, `#0B6CFF` checked button,
  `#6B7079` message) -> dynamic properties (`invalid="true"`, `messageLevel`) that the token QSS
  already styles; the SELECT button reads bright blue on both themes until then.
* assembly: `ToolCommon` inline QLineEdit error style -> `invalid="true"`; joint/triad/interference
  colours -> roles.
* drawing: `DrawingWorkspaceWidget` label colours (`#555`, `#666`: low contrast on the dark theme)
  -> QSS/tokens; canvas surround `drawing.canvas`, selection `drawing.select`; paper/ink stay.
Why: the dark theme is the default; these spots still carry light-theme colours.
