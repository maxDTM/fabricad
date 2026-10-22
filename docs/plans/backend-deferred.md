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
- **Fixed in redesign phase 4 (`ui/viewport`)**: `SceneOptions::shadeProfiles` (on in the viewer) adds a non-pickable `SceneStyle::ProfileShade` tint (`sk.profileFill`) for visible, finished sketches.
- Was open: The shading is drawn by the viewer, not by the sketch module:
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

## be/drawing (2026-10-22): drawing round 1

Status table: `docs/tests/drawing/ISSUES.md`. These items were not fixed on `be/drawing`.

### DRW-VIEWS-ANNOT-13: hole tool notches material above the placement face (src/part)
- Root cause (verified on the tester's model): a through-all Hole (`extentType = All`) builds its
  tool with `topExtension = 1.0` (`src/part/features/Hole.cpp`, `HoleFeature::compute`). For a
  counterbore or countersink, the extension has the counterbore/countersink radius. On the repro
  model the Ø11 counterbore at (0, -20) overlaps the Ø30 boss by 0.5 mm, so the tool cuts a
  1 mm-high, about 4 mm-wide notch into the boss above the plate face. The drawing shows that
  notch faithfully: it is the "1 mm rectangle" in the front view, and its edges are the
  "seam-like" tangent line. It also made the counterbore cylinder face 7 mm long, which is why
  the hole callout read ↓7. The drawing now takes the depth from the feature (VIEWS-ANNOT-2).
- Fix: build the part of the extension above z = 0 at the drill radius only. Profile: top
  (0, ext) → (r, ext) → (r, 0) → (cr, 0) → (cr, -cboreDepth) …, or subtract the ring
  r..cr × 0..ext from the tool. Not done here because it adds two faces to the proto hole,
  which renumbers the `Hole<n>/H<k>/F<i>` face names that saved documents reference. That
  needs a naming migration, or new faces appended after the existing ones. Also update the
  `test_dressup.cpp` volume check for `topExtension`.

### DRW-VIEWS-ANNOT-16 (rest): symbol pickers for datum, surface texture, hole callout
- Done in `src/drawing`: Edit Annotation shows a GD&T characteristic picker plus tolerance, Ø,
  modifier and datum fields for feature control frames, and a per-row form for revision tables.
- Open (UI design): a symbol palette for surface texture (process, lay symbols) and the hole and
  thread callout options. All of them work through `EditCalloutTool::applySetting` keys
  (`src/drawing/tools/CalloutTools.cpp`), so a dialog only needs to call those.

### DRW-VIEWS-ANNOT-18: stale band after wheel zoom
- Not reproduced in-process. `DrawingCanvas` already uses
  `QGraphicsView::FullViewportUpdate`, and a wheel zoom re-renders the whole viewport. The band
  was the top ~40 px of the canvas. If it shows up again under the new shell, check whether an
  overlay or toolbar widget of the main window (`src/app/MainWindow.cpp`, `layoutOverlays`)
  covers the canvas top without repainting, rather than the canvas itself.

### DRW-ASM-OUTPUT-9 (rest): DXF lineweights
- R12 (AC1009) has no lineweights, so visible, hidden and dimension lines differ only by layer
  and colour. Done: `$EXTMAX`/`$LIMMAX` written, `$INSUNITS` dropped (not valid in AC1009).
  Real lineweights (group 370) need an AC1015 (R2000) writer with handles, an OBJECTS section
  and so on. That is a larger `DxfWriter` change, not done in this round.

### DRW-ASM-OUTPUT-11: BOM Mass / Material empty
- The BOM already fills Material and Mass from a body's physical material and density
  (`buildBom` in `src/drawing/Annotations.cpp`). The tester's bodies had none set.
- UI: the only way to set a material is Browser > body > Physical Material
  (`src/app/BrowserTree.cpp`). The drawing workspace has no entry point, and the parts list has
  no per-row override. Suggest a "Physical Material" item in the drawing's parts-list context
  menu, or a hint in the Parts List tool prompt.

### Design workspace observations from the drawing testers (not investigated)
- `SELECT body:Body1` + `CFB` (component from body) then `NEWCOMPONENT Pin` leaves component
  `Body1` empty and puts the body into `Pin`.
- The first Extrude in a new empty component defaults to Join (docs say New Body).
- `EDITFEATURE <name>` needs the feature selected first, although SCRIPTING.md shows it taking
  the name.
- asm-output tester, unverified: Block and Pin sketches started by clicking the plate's top face
  ended up at z = 0.

## be merges (2026-10-22): flaky timing test

### `fabricad_app_test_theme_manager` / `themeSwitchIsFast` fails under machine load
`src/app/tests/test_theme_manager.cpp:192` asserts `worst < 150` ms. With load average ~10 on
8 threads (parallel builds) it measured 158-262 ms and failed about half the runs; it passes
on reruns when quieter. Seen identically after each of the be/perf, be/logic and be/drawing
merges, so not caused by them. Suggest a looser bound, a median instead of the worst run, or
a skip/label (`timing`) so `ctest -LE timing` can exclude it on busy machines.

## ui/app4 (2026-10-22): app-side items done

- **Batch mode set by the app.** `src/app/main.cpp` calls `core::setBatchMode(scripted && !stay &&
  stepDelay == 0)` (and `false` again where `--stay` hands the window back). `detectBatchMode()` in
  `src/core/Tool.cpp` reads only `FABRICAD_BATCH`; the command-line sniffing is gone.
- **Headless runner.** `HeadlessHost` (`src/app/HeadlessHost.*`) runs `--script --offscreen` (and the
  new `--batch file.fcs`) without `MainWindow`: modules, app and script verbs, and one
  `Document` / `ToolManager` / `NullViewerContext` per tab (NEW, OPEN, DRAWING, TAB, CLOSE work).
  The verbs reach the shell through `AppShell` (`src/app/AppShell.h`), which `MainWindow` implements
  too. `FABRICAD_SCRIPT_WINDOW=1`, `--stay`, `--step-delay` and `FABRICAD_SCREENSHOT` use the window.
  Numbers: `docs/comparison/COMPARISON.md` "Headless runner".
- **Per-command settle floor.** Query lines (`CHECK`, `INFO`, `LIST`, `ECHO`, `OPTIONS`) skip the
  settle; batch runs settle in one pass. The browser, timeline, feature list and selection panel
  already coalesce their rebuilds through zero-timers. The rest of the floor (about 4.7 ms per
  `SELECT` in the window) is the window's own reaction and is gone in the headless runner
  (0.7 ms).
- **test_main_window warning** fixed (`Qt::NoModifier`).
- **Theme-switch timing test**: best of six < 150 ms and median < 400 ms. It runs as its own
  ctest entry `fabricad_app_test_theme_manager_timing` with the label `timing`
  (`ctest -LE timing` skips it). The main entry skips it through `FABRICAD_SKIP_TIMING=1`.
- **DOC-06**: a STEP/STL opened into an Untitled design names the design after the file.
- **NEW-PART-CORE-1**: the failure report also runs after every undo-stack change, so an undo that
  removes the failed feature without a recompute clears its message.
- **Functional DOC-11**: `RecentFiles::labels()` adds as many parent folders as needed to tell
  same-named files apart (`chair.fcad (a/chair)` / `chair.fcad (b/chair)`).
- Still open from this file for the app: APP-10 (disabled tools in fuzzy search) and NEW-ASM-CORE-2
  (Ground / Unground label).
