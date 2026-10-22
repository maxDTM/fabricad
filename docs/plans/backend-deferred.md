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
