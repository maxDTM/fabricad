# Backend work deferred to the UI branches

Items found while fixing non-UI open issues whose fix lies in paths owned by the UI redesign
(`src/app/**`, `src/viewer/**`, `resources/**`, `src/main.cpp`). Each section names the place
to change and what the backend already provides. Append new sections at the end.

## be/perf (2026-10-22): script performance

### Batch mode should be set by the app, not detected from argv
- Done in the backend: `core::isBatchMode()` / `core::setBatchMode()` (`src/core/Tool.h`).
  `part::FeatureTool` defers a new feature's live preview in batch mode and computes it once at
  `OK` (or when `OPTIONS` / `isValid()` asks). Interactive behaviour is unchanged.
- Stopgap: with no explicit value, core reads `FABRICAD_BATCH=1/0`, else the process command
  line (`--script` without `--stay` and without `--step-delay` > 0).
- App follow-up: in `src/app/main.cpp` call `fabricad::core::setBatchMode(scripted && !opts.stay
  && opts.stepDelay == 0)` next to `window.setInteractive(false)`, and
  `setBatchMode(false)` where `--stay` hands the window back to the user. A cleaner variant is an
  `IAppHost::isInteractive()` virtual that `MainWindow` overrides with `interactive_`. Then
  remove the argv sniffing in `detectBatchMode()` (`src/core/Tool.cpp`).

### Per-command settle floor (ScriptRunner)
- `ScriptRunner::runLine` (`src/app/ScriptRunner.cpp`) calls `settle()` after every line:
  `sendPostedEvents()` plus three `processEvents(AllEvents, 10)`. On its own this costs < 1 ms
  (a `CHECK` line takes 0.7 ms). The 2 to 5 ms seen on `SELECT`, `RENAME`, `GROUND` lines is the
  app reacting to document/selection signals during that settle (browser tree, timeline,
  property editor and status-bar repaints of the offscreen window). The core cannot remove it.
- Suggested fixes, all in `src/app` / `src/viewer`:
  1. In batch mode, run one `processEvents` pass instead of three, or skip the settle for verbs
     that need no event delivery (`CHECK`, `INFO`, `LIST`, `ECHO`, `OPTIONS`); keep it for
     `WAIT`, `SCREENSHOT`, tab switches and queued `CLOSE`.
  2. Coalesce browser/timeline rebuilds: let them listen to `Document::recomputeFinished` and
     rebuild once from a zero-timer, instead of on every `featureStatusChanged` / `bodyChanged`.
  3. With `--offscreen` and no step delay, hide the main window (`setUpdatesEnabled(false)`
     on the central widget) so widget repaints are not rendered into the offscreen backing
     store at all.

### Headless runner (COMPARISON.md recommendation 2)
- Not done: a runner that executes a `.fcs` script without building `MainWindow` needs
  `src/app/main.cpp` and a host that does not need the window. `ScriptRunner` is bound to
  `MainWindow` (command line, messages, tabs), and the shell verbs in `src/app/ScriptTools.cpp`
  (`SELECT`, `CHECK`, `OK`, `OPTIONS`, `INPUT`, exports, tabs) take a `MainWindow*`.
- Suggested shape: move the verbs to a `ScriptHost` interface (document, tool manager, message
  sink, optional tab list) implemented by `MainWindow` and by a small `HeadlessHost` built on
  `core::NullAppHost` + `core::NullViewerContext`; `fabricad --batch file.fcs` then creates a
  `QCoreApplication`-like `QGuiApplication` with the offscreen platform, the module registries
  and one `Document`, and never constructs a widget. Call `core::setBatchMode(true)` there.

## be/logic (2026-10-22)

### SK-12 (general round): no profile shading after Finish Sketch
- Still open. The shading is drawn by the viewer, not by the sketch module:
  `src/viewer/SceneModel.cpp` adds `SceneStyle::Profile` items only when `opt.showProfiles` is
  set ("profiles (only while a tool picks them)").
- Fix: show the closed profiles of visible, finished sketches as well (Fusion shades them
  lightly), for example with `showProfiles` on for every visible sketch outside a tool, or a
  separate option the app sets after Finish Sketch. The sketch side already gives everything
  needed (`IProfileSource::profiles()` / `profileFace()`).

### DOC-06 caveat: opening a STEP file in an Untitled tab keeps the name "Untitled"
- `MainWindow::openFile` (STEP/STL branch, around "Fusion: Open on a STEP/STL makes a new
  design") runs `part.import-file` but never renames the design. The `.fcad` branch does
  `if (isAutoDocumentName(name)) setName(completeBaseName)`; do the same after a successful
  import.

### STEP import with assembly structure: app follow-ups
- Done in the backend: `part.import-file` (what File > Open of a .step runs) and the Insert
  STEP/STL dialog now create components, occurrences and named bodies from an assembly STEP
  (`ImportFeature` `keepStructure`, on by default). One undo step.
- The browser should refresh its component nodes after the import (it already listens to
  `componentAdded`), and the "Keep assembly structure" checkbox shows in the Import dialog as a
  plain bool row; no app change is required, but a check in the redesigned dialog is worth it.

### NEW-PART-CORE-1: "Shell1 failed: ..." stays in the status bar after Ctrl+Z
- `MainWindow::reportNewFailures` posts the failure message; nothing clears it when the
  failure is gone. Clear (or replace) the message after an undo/redo/recompute when the feature
  it names is no longer failed.

### Functional DOC-11 (partial): Open Recent entries for same-named files collide
- The entries read "chair.fcad (chair)" for different folders. Use the parent folder name as
  the suffix (and more path components while labels still collide). `RecentFiles` in
  `src/app/AppState.h` / the menu builder in `MainWindow`.

### APP-10 (mostly fixed): disabled commands still match in S search
- `src/app/CommandSearch.cpp` ranks disabled tools last but still lists them ("Edit Dimension"
  for "pref"). Drop disabled tools from fuzzy (non-prefix) matches.

### NEW-ASM-CORE-2: Assemble menu says "Ground" for a grounded component
- The ground tool toggles; the label is static. The menu/ribbon should show "Unground" when
  every selected component is grounded (the state is `core::Component::grounded`).

### Compiler warning in a GUI test (status report: "2 in a GUI test")
- `src/app/tests/test_main_window.cpp:269`: `Qt::ControlModifier | (k == Qt::Key_Enter ? Qt::KeypadModifier : 0)`
  mixes an enum and an int in the conditional (-Wextra, plus an "ISO C++ says that these are
  ambiguous" note). Fix: `Qt::ControlModifier | (k == Qt::Key_Enter ? Qt::KeypadModifier : Qt::NoModifier)`.
- The other 4 warnings of the report (-Wmaybe-uninitialized on ShapeRef via
  `SelectionModel::setPreselection`, `src/core/Selection.cpp`) are fixed on be/logic.
