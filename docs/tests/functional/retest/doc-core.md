# doc-core retest (build a110a4f, Release 06:17)

Retester: mouse/keyboard on display :33, 2026-10-22 ~06:20-06:30. Screenshots in `doc-core/`.
Environment note: the :33 window initially came up tiled at 945x490 (status bar and timeline clipped); I floated/resized it to 1500x950 via the compositor for the DOC-04 checks. Other agents' live FabriCAD instances were running at the same time, which made the multi-instance recovery check real.

## Verdicts

| Id | Verdict | Evidence |
|---|---|---|
| DOC-01 | FIXED | Two unsaved "Untitled" tabs with different content (b.step import, all.step import) each got their own autosave (`recovery/<session-uuid>/Untitled-<id>.fcad`). After `kill -9`, restart offered "2 autosaved copies" and recovered both in separate tabs with correct, different content (`d01-twountitled.png`, `d01-recovery.png`, `d01-recovered.png`). Other live instances' autosave folders (session.lock) were not offered. A session that had died (lock gone) was offered as an orphan on the next start. |
| DOC-02 | FIXED | After accepting recovery, the recovered tabs show modified (`*`) and the new session folder already contained both autosaves with no further edit (`ls` of the new folder). A second `kill -9` straight after recovery offered them again (`d02-second-crash.png`, `d02-recovered-again.png`). |
| DOC-03 | FIXED | Re-opening an open file via `/tmp/doccore/../doccore/x.fcad` and via a symlink path did not create a tab; an Untitled doc saved with Save As and then re-opened by the same path did not create a duplicate (`d03-lnk.fcad.png`, `d03-reopen.png`). |
| DOC-04 | FIXED | c.fcad: editing a hole dimension in Sketch2 (ø4 -> ø6) left no warning icons on Sketch4-15 or timeline (round 1: 12 warned features). After undo, still clean (`d04-c-edited.png`, `d04-c-undone.png`). b.fcad: dragging a plate corner in Sketch3 and undoing also showed none (`d04-edited.png`, `d04-undone.png`). Not tested: reading the tooltip text of a rematched ref after a forced non-exact resolve. |
| DOC-06 | FIXED | Imported b.step into Untitled, Save As rt_imp.fcad -> doc/tab renamed "rt_imp"; Save As again as rt_imp2.fcad -> renamed "rt_imp2" (`d06-saveas.png`, `d06-saveas2.png`). Caveat: could not test an import-derived name that is not "Untitled", since Open of a STEP in an Untitled tab keeps "Untitled". |
| DOC-07 | FIXED | empty.fcad: "the file is empty"; trunc/half.fcad: "the file is damaged or truncated (at byte N of M)" (`d07-empty.png`, `d07-trunc.png`, `d07-half.png`). bogustype.fcad opens with a status-bar notice "Opened bogustype.fcad with problems: 27 features failed to compute: Extrude1, Sketch4, ... and 22 more" (`d07-bogus3.png`). The notice is a transient status-bar message, not a dialog; it was gone after a few seconds. |
| DOC-11 | PARTIALLY FIXED | Tab tooltip shows the full path (`d11-tab-tip.png`); same-named tabs are disambiguated ("b (doccore)", `d07-bogus3.png`). Open Recent entries have a full-path tooltip (`d11-recent-tip.png`) but the label suffix is the document name, not the parent folder, so entries still collide: "chair.fcad (chair)" twice, "bracket.fcad (bracket)" three times (`d12-recent.png`). |
| DOC-12 | FIXED | Hamburger menu -> hover Open Recent -> click in viewport: menu closed; Ctrl+O then opened the file dialog immediately (`d12-recent.png`, `d12-after-click.png`, `d12-ctrlo.png`). |

Counts: 7 fixed, 1 partially fixed, 0 not fixed, 0 regressed.

## Regression check of round-1 "working" list
Spot-checked and still working: Ctrl+N/O/S/Shift+S, Save As with a space in the name ("rt b.fcad"), save+close+reopen identical content, tab close with Save/Discard/Cancel dialog (Discard works), undo/redo of sketch dimension edits, import of STEP (b.step, all.step) into a new doc, open of bad/empty/truncated files with clear messages, autosave + recovery after kill -9 (including hidden state / geometry), Open Recent menu. Not re-run (time/priority): STL export, permission-denied save, future-version file, 45-step undo, File>Exit prompt. No crash occurred.

## New defects
- NEW-DOC-CORE-1 (low, observation): after editing then undoing every edit back to the loaded state, the tab still shows the modified marker ("c *", `d04-c-undone.png`). Probably intended (redo available) but Fusion/SolidWorks clear it at the save point.
- NEW-DOC-CORE-2 (low): the "opened with problems" notice (DOC-07) is only a transient status-bar message; on a file with 27 failed features the user can easily miss it. A dialog or a persistent banner would be safer.
