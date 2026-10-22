# doc-core round 1: document reliability

Tester: QA (mouse/keyboard on display :33). Date 2026-10-22. Screenshots in `doc-core/`.

## Defects

| Id | Sev | Title |
|---|---|---|
| DOC-CORE-1 | medium | Two unsaved "Untitled" tabs share one autosave file, only one is recoverable (and the recovery dir is shared by all running instances) |
| DOC-CORE-2 | low | Recovered document is not re-autosaved until the next edit; a second crash loses it |
| DOC-CORE-3 | medium | Opening a file that is already open sometimes creates a duplicate tab of the same file |
| DOC-CORE-4 | low | Tabs / Open Recent are indistinguishable for same-named files (no tooltip, basenames only) |
| DOC-CORE-5 | medium | "re-matched by geometry" warning flags stay on 12 features after the causing edit is undone |
| DOC-CORE-6 | low | Document name does not follow the file name on Save As unless the doc was "Untitled" |
| DOC-CORE-7 | low | Corrupt/invalid .fcad errors are raw parser messages; a file with failing features opens with no notice |
| DOC-CORE-8 | low | Menu stays open and swallows Ctrl+O / Escape after hovering Open Recent submenu |

Details with repro are in the structured result returned to the orchestrator.
Evidence screenshots: twountitled.png, recov1b.png, recov2.png, recov4.png, dupb-c.png, ed-undo.png, del2.png, warn-tip-c.png, imp-moved.png, corr2-empty.png, corr2-half.png, sem-bogustype.png, dupb.png, recent.png.

## Tested and working

New/Open/Save/Save As (Ctrl+N/O/S/Shift+S), overwrite prompt, unicode/space file name, permission-denied save (clear dialog),
save+close+reopen identical (timeline, 17 sketches, hidden state, camera projection), 45-50 step undo/redo across sketches and
extrudes in both directions, undo/redo inside an open sketch (dof status correct), rename body (F2/double click) undoable,
timeline rename, tab close with Save/Discard/Cancel, close last tab, cancelling Save As on close keeps the tab, File>Exit with
dirty doc prompts, STEP export (selected/all), STL export (default and 0 deflection), STEP/STL re-import (embedded, survives
moving the source file), export empty doc (clear message), import bad/missing file (clear message), future-version file (clear message),
autosave + recovery after kill -9 (recovers c.fcad with hidden state), 20-30 feature model responsiveness (undo 50x at 5 Hz keyboard rate, no lag),
dimension edit with invalid expression (clear inline error, dialog stays), parametric change propagating to 17 dependent sketches.
No crash occurred.
