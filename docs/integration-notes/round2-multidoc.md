# Round 2, multidoc agent: document tabs + headless scripting (branch `feat/multidoc`)

What was built, where it touches shared files, what the integrator should expect when merging
with the other round-2 branches, and follow-ups.

## Delivered

**Multi-document tabs** (`src/app/DocumentTab.{h,cpp}`, `MainWindow`)
* `DocumentTab` owns a `Document` (shared_ptr; undo stack and selection live inside it), its own
  `ToolManager`, viewer (camera), browser tree, timeline, command dialog, nav bar and sketch
  palette. Drawing tabs share the design's document (one `shared_ptr`, two tabs) with their own
  `ToolManager` and `DrawingWorkspaceWidget` page.
* `MainWindow` keeps `tabs_`, the `QTabBar` in the application bar (`+` = New Design, close
  buttons, drag to reorder, titles `Name *` / `Name Drawing *`) and three `QStackedWidget`s
  (central, browser dock, timeline dock). `document_/manager_/viewer_/browser_/timeline_/dialog_`
  now always point at the active tab's objects; `activateTab()` rebinds ribbon, command line,
  command search and status bar (`Ribbon/CommandLine/CommandSearch::setToolManager`).
* New/Open create tabs (a pristine, untouched start-up tab is reused; opening an already open
  file activates its tab); Design <-> Drawing workspace switches open/activate the linked tab;
  "New Drawing" from a design opens the drawing tab; closing the last tab of a document prompts
  for unsaved changes (one prompt per document on window close); closing the last tab leaves a
  fresh design. Switching tabs cancels the command of the tab being left and finishes its open
  sketch (the `SketchSession` is a per-process singleton). Autosave covers every open document;
  per-document UI state is stored per tab.

**Scripting** (`src/app/ScriptRunner.{h,cpp}`, `ScriptTools.{h,cpp}`, `Selectors.{h,cpp}`,
`main.cpp`, `docs/SCRIPTING.md`)
* `fabricad --script file.fcs [--offscreen] [--log out.txt] [--keep-going] [--stay] [--quiet]`.
  Every line goes through `CommandLine::submit()` -> `ToolManager::runCommandLine()`, i.e. the
  typed-input path; the runner only strips comments, pumps the event loop after each line,
  times it and collects the `showMessage()` output (`MainWindow::messageShown` signal). Exit
  codes 0 / 1 (a line failed) / 2 (unreadable script).
* Shell verbs as `keepsActiveTool` instant tools: `OK CANCEL SELECT PICK INPUT INFO LIST CHECK
  OPTIONS RENAME ECHO WAIT SCREENSHOT TAB CLOSE`.
* Textual selectors (`body:Seat/face:top`, `edge:@x,y,z`, `edges:z=0`, `profile:@x,y,z`,
  `component:X/bodies`, history names...) and `PICK x,y,z` = ray-pick against the model.
* `tests/scripts/chair.fcs` builds the whole `docs/comparison/CHAIR_SPEC.md` chair (all four
  stages, exports) and checks every reference volume/bounding box of the spec;
  `tests/scripts/bracket.fcs` covers lines/arc/circle/dimension/parameters/drawing/exports.
  `tests/scripts/CMakeLists.txt` makes one ctest per `*.fcs` (`fabricad_script_<name>`),
  running the real executable in `build/script-out/<name>/`.
* `src/app/tests/test_scripting.cpp`: runner, selectors, CHECK, verbs, routing, tabs.

## Shared-file edits (expect conflicts here)

| File | Change | Conflict risk |
|---|---|---|
| `src/core/Tool.cpp` (`ToolManager::run`, `runCommandLine`) | (1) `keepsActiveTool` instant tools never become the active tool (they run beside, with their own context) so shell verbs/view commands do not clear the selection or prompt; (2) with an active tool, a line whose first word is a command alias starts that command (Fusion: picking another tool ends the current one) unless the word is a document parameter; (3) an alias shared by several tools resolves to the one enabled now (FILLET/MIRROR/POINT/MOVE/DIMENSION/DELETE: sketch vs solid vs drawing), and a disabled command gives "X is not available now". | medium: anyone touching `runCommandLine`. Behaviour (2) changes the documented order in ARCHITECTURE.md section 8 ("text goes to commandLineInput() first") - update that sentence. |
| `src/part/tools/PartTools.cpp` (`exportBodies`) | bodies exported in their component's world placement; `EXPORTSTL path [deflection=] [angle=|0.2rad] [ascii]`; reports the mesh parameters. | low (one function) |
| `src/app/MainWindow.{h,cpp}` | large: tab infrastructure replaces the single document; `connectSignals()` split into `connectShellSignals()` / `connectTabSignals(tab)`; `newDocument/openFile/save*/maybeSave/closeEvent/autosave*` are tab aware; `setWorkspace` opens tabs; `onToolFinished` opens the drawing tab after `drawing.create`; `showMessage` emits `messageShown`. | high with any other MainWindow edit (icons, manipulators...). Prefer re-applying their edits onto this version: the member names (`viewer_`, `browser_`...) are unchanged and still valid for the active tab, but they can now be null in a drawing tab (`viewer_`, `browser_`, `timeline_`, `navBar_`, `palette_`). |
| `src/app/Ribbon.*`, `CommandLine.*`, `CommandSearch.*` | reference -> pointer to the ToolManager plus `setToolManager()`. | low |
| `src/app/main.cpp` | option parsing (`--script`...), script runner start-up, `setInteractive(false)` while scripted. | low |
| `src/app/AppTools.cpp` | `SET name=expr` is undoable (`SetParameterCommand`) and reports the value. | low |
| `src/app/tests/test_main_window.cpp` | adapted to tab semantics (`newDocument` opens a tab; `autosaveNow` covers all tabs; drawing tabs have no timeline/viewer) plus `tabsOwnTheirState`. | low |
| `CMakeLists.txt` | one line: `add_subdirectory(tests/scripts)` in the tests block. | trivial |

New files only: `src/app/DocumentTab.*`, `ScriptRunner.*`, `ScriptTools.*`, `Selectors.*`,
`src/app/tests/test_scripting.cpp`, `tests/scripts/*`, `docs/SCRIPTING.md`, this note.

## Things other agents should know

* `MainWindow::viewer()/browser()/timeline()` return null while a drawing tab is active; code
  that runs in both workspaces must check. `MainWindow::activeTab()` / `tabs()` give the rest.
* Tools that need the window (`win->viewer()`, `win->toolManager()`) get the *active tab's*
  objects; a tool runs inside its own tab's manager, which is the active one except for the
  deferred `TAB`/`CLOSE` verbs.
* `ToolManager::runCommandLine` now treats a typed alias as a command even while a tool is
  active. Tools whose typed keywords collide with aliases (`FRONT`, `TOP` for the drawing base
  view orientation; these are view-command aliases) should take them as arguments
  (`BASEVIEW front`), which already works.
* The `SketchSession` singleton: `DocumentTab::~DocumentTab` and `MainWindow::activateTab/
  closeTab/closeEvent` end a session that belongs to the tab. If the sketch agent makes the
  session per manager this glue can go.
* `hasDialogShell()` tools (`drawing.create`, `NEWCOMPONENT`, Joint...) wait for `OK` even in
  scripts; that is intended (the script says `OK` as the user would). Scripts that want
  one-liners can pass arguments (`NEWCOMPONENT Seat` + `OK`).

## Follow-ups / known limitations

* No quoting in the command line: object and file names with spaces cannot be typed
  (`ToolManager::runCommandLine` tokenises on white space). Adding quote support there is a
  core change.
* Relative/polar typed input (`@dx,dy`, `@len<ang`) accepts numbers only; expressions with
  parameters work in plain values and dimensions (sketch `parseTypedInput`).
* `HOLE <n>` sets the primary numeric (depth); the diameter is `diameter=10`.
* `EXPORTSTEP` is the part exporter (first registered alias); the assembly XCAF export is
  reachable as `assembly.export-step`. Consider renaming one alias (`EXPORTASSEMBLY`).
* `drawing.create` has the alias `DRAWING`, which shadows `app.workspace-drawing`'s `DRAWING`;
  since finishing it now opens the drawing tab this is fine, but the duplicate alias could go.
* Sketch inference in `PICK` uses the viewer's pixel size; in `--offscreen` runs there is no
  GL view, so `nosnap` (Ctrl) is the reliable form for exact points. `SCREENSHOT` is blank
  offscreen.
* `Document::bbox` (`core::bbox`) pads curved faces; the scripting layer uses its own tight
  `BRepBndLib::AddOptimal` box (`Selectors.cpp`). Core could offer a tight variant.
* `README.md`: add the scripting and tabs paragraphs (owner: integrator); `ARCHITECTURE.md`
  section 8 bullet on the command line needs the new routing sentence.
