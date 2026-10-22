# General bugs, retest (merged main d2278a1, display :34, QT_SCALE_FACTOR=0.6)

Partial retest (time and a cramped, shared display; the app painted only ~840x517 px). Screenshots in `general-bugs/`. Input via xdotool only. The Hyprland tiling made the display small; stale repaint remnants in the screenshots are harness artefacts.

## Verdicts
| Issue | Verdict | Evidence |
|---|---|---|
| DOC-01 (GB-2) close Design tab with Drawing tab | FIXED | 09: "Save changes to Untitled 2?" prompt appears; Cancel keeps doc |
| DOC-02 (GB-1) Browser after closing Drawing tab | FIXED | 08: Browser dock back after closing Drawing tab |
| DOC-03 (GB-3) sheet delete/undo sync, wrong sheet | FIXED | 38-43: panel, tabs, canvas in sync after delete and undo; "-" asks for the selected sheet (Sheet1, then Sheet2). A transient tab-scroll arrow shows right after undo (41) and is gone later (42) |
| DOC-04 (GB-4) failed save renames doc | FIXED | 12: "Cannot save" and the name stays "Untitled 2" |
| DOC-05 (GB-6) suppress sketch | FIXED | 25: Extrude1 flagged "failed: input 'Sketch1' is suppressed", body gone, timeline item red |
| DOC-07 (GB-8) undo body delete renames | FIXED | 17: Body1 stays Body1 |
| DOC-08 (GB-7) stale status error | FIXED | 26: error cleared after undo |
| DOC-09 (GB-11) dirty markers | FIXED | 13: root "rt" clean after save; 14/15: undo gives "rt *", redo to saved state gives clean |
| DOC-10 (GB-13) untitled naming / new drawing | MOSTLY FIXED | "Untitled 2" numbering OK; new Drawing in a fresh doc is not dirty (56). Sheets panel is still empty until "Create Drawing..." is pressed (by design, no default sheet) |
| DOC-11 (GB-12) tab drag repaint | INCONCLUSIVE | xdotool drags (44-46) did not reorder tabs, so the glitch could not be provoked; no glitch seen |
| DOC-12 (GB-15) marking menu label / timeline frame | PARTIAL | 33/34: timeline frame cleared after Cancel (fixed). 32: marking menu labels ("Repeat Box", "Create Sketch", "Move/Copy") are complete, but the long "Repeat Rectangular Pattern" label was not triggered |
| APP-05 (GB-5) remove used parameter | FIXED | 31: "'p1' is used by Box2: removing it would break them", parameter stays |
| PART-02 (GB-9) invalid expression | FIXED (minor) | 05, 22: Box Length "f1e2h3" and Extrude Distance "abc+": red field, text kept, OK disabled. No error text was visible in the status bar or panel |
| PART-09 (GB-10) Faces filter / Fillet highlight | FIXED | 48: no origin planes appear with Faces filter and Origin hidden; 49: Fillet button not highlighted after Escape |
| PART-10 (GB-14) 500 coincident pattern bodies | PARTIALLY FIXED | 54: dialog shows "pattern instances overlap (spacing 0.04008 mm...)" warning. OK is still allowed and bodies still render solid black (55); redraw lag not measured |

Counts: 11 fixed, 1 mostly fixed, 2 partial, 1 inconclusive, 0 not fixed, 0 regressed.

## Regression check of round 1 "working" list
Checked and OK: Box creation, sketch + rectangle + Extrude, undo/redo, Save/Save As, error dialog on bad path, Delete body confirmation with dependency, browser context menu, timeline edit (double click), Ctrl+N, workspace switch, Change Parameters, Rectangular Pattern, marking menu, SELECT filter menu, drawing base sheet add/delete. Not rechecked: Open, STEP, Measure, Chamfer/Shell/Combine, wheel zoom, command line. No crashes.

## New defects
- NEW-GENERAL-BUGS-1 (low): Sheets panel list showed blank right after "Create Drawing" OK until the mouse moved over it (36 vs later hover); likely repaint lag. Status bar did show the sheet created.
- NEW-GENERAL-BUGS-2 (low): the Create Drawing options panel in the Drawing workspace rendered blank at this small window size (35 area, bottom right); OK via Return still worked.

## Not done
G3 PART-03 / G4 SK-07 joint profile retest, SK-12, and the DOC-05 side effects on other dependent features are not covered here.
