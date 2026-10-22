# General bug hunt, round 1 (display :34, scale 0.6 via wrapper so the whole UI fits)

Window resize during a command could not be tested (no WM; xdotool windowsize has no effect). Screenshots in `general-bugs/`.

## Issues
- GENERAL-BUGS-1 (medium): Browser dock is gone for good after closing the Drawing tab (last tab) -> auto-created Untitled has no Browser, and Ctrl+N tabs neither; no View menu entry to restore. Steps: Create Box, workspace menu > DRAWING, close the drawing tab x, Discard. Shots 23, 24, 26.
- GENERAL-BUGS-2 (medium): Closing the Design tab of a document that also has a Drawing tab (x or Ctrl+W, even with active Fillet/Sketch tool and unsaved changes) closes it silently with no save prompt; the dirty doc lives on hidden behind the Drawing tab, and only later closing the Drawing asks "Save changes to Untitled?" (name of the design). Shots 12-15, 27-30, 81-84.
- GENERAL-BUGS-3 (medium): Undo of sheet deletion in Drawing leaves Sheets panel (2 items), bottom sheet tabs (corrupted: "eet2 | Sheet3 | < | Sheet3 | +") and canvas (3/3) out of sync until a click. Also after deleting Sheet3 and pressing "-" twice more, Sheet1 (the one with the view) was removed, not the selected sheet. Shots 161-167.
- GENERAL-BUGS-4 (low-med): Failed Save (path /proc/nope/x.fcad) shows "Cannot save" but the document is already renamed "x" (tab, browser) and keeps the bad path. Shots 131-132.
- GENERAL-BUGS-5 (low-med): Change Parameters allows removing a parameter used by a feature; Box3 fails with "unknown parameter 'p1'". Fusion refuses/warns. Shots 113-116.
- GENERAL-BUGS-6 (low-med): Suppressing Sketch1 (right-click > Suppress) leaves dependent Extrude1 geometry intact and not flagged. Shot 104. Also deleting Box body: Extrude still builds geometry though Sketch1 failed (shot 70).
- GENERAL-BUGS-7 (low): Stale red status text after fixing the cause (undo): "Sketch1 failed..." / "Box3 failed..." remains in status bar; also "Unknown command: foobar" lingers. Shots 71, 115.
- GENERAL-BUGS-8 (low): Undo of a body delete renames the body Body1 -> Body2. Shot 71/72.
- GENERAL-BUGS-9 (low): Box dialog: invalid expression "f1e2h3" turns the field red but OK stays enabled and silently creates the box with 20 mm. Shots 88-90.
- GENERAL-BUGS-10 (low): SELECT > Filter: Faces then clicking in viewport makes the origin planes appear in the viewport although Origin eye is off in the browser; Fillet toolbar button stays highlighted after a command ended; planes vanish only with Filter: Select All. Shots 150-160.
- GENERAL-BUGS-11 (low): After saving, browser root still shows "t1 *" while tab shows "t1" (shot 94); redo back to the saved state keeps the dirty marker (shot 58).
- GENERAL-BUGS-12 (low): Tab strip stale paint after dragging a tab (two "t1 Drawing *" labels, wrong active indicator) until a click. Shots 138-141.
- GENERAL-BUGS-13 (low): New documents are all titled "Untitled" (no numbering); new Drawing is dirty (*) immediately and with no design yet shows blank page with empty Sheets panel. Shots 11, 107.
- GENERAL-BUGS-14 (low): Rectangular pattern with Quantity 500 and Extent distribution makes coincident bodies that z-fight to solid black; viewport redraw lags ~2 s after undo. Shots 126-130.
- GENERAL-BUGS-15 (low): Marking menu label "Repeat Rectangu..." truncated; timeline item stays framed after cancelling feature edit (shot 90 vs 91).
- Note: quick second click in Rectangle tool 0.3 s after the first was ignored once (shot 42-45); not reproduced.

## Tested and working
Create Box/Cylinder, edit feature via timeline double click while another tool open, rollback marker + insert + go to end, Fillet (incl. too-large radius error), Chamfer/Shell/Combine tool switching, rapid repeated tool clicks, undo/redo of box/sketch/extrude/rename/visibility, Enter to confirm, shortcuts not leaking into text fields, workspace switch mid-tool/mid-sketch, sketch edit from timeline, Delete body confirmation with dependency list, browser rename/visibility/context menu, Save/Save As/Open, bad and empty file errors, STEP export/import, open already-open file, command line and help list, Measure, marking menu, selection filter menu, 8x Ctrl+N and 7x Ctrl+W, wheel zoom extremes + Fit, Change Parameters, Drawing base view and sheet add/delete. No crashes observed.
