# Production features test: summary

## 2026-10-23: pf/fix (findings of `report.md` and `../tutorial2/report.md`)

All high and medium findings of the production-features tester run and the second tutorial walkthrough are
fixed, plus the cheap low ones.

| Id | Fix |
|---|---|
| PRODUCTION-1 (high) | Insert Fastener reads the thread of a tapped hole (Hole tapped / Thread feature on the same axis, `HoleSite::thread`) and sizes the screw from it before the diameter: a 1/4-20 hole (5.105 mm, also the #8 loose clearance) gets 1/4-20. Test `tappedHoleSizeAndBlindLength`. |
| PRODUCTION-2 (high) | The head of the screw in the tapped hole hid the hole's whole circle from the view. The Hole Table adds the holes of the view's bodies that open towards the viewer but have no circle in the view (`occludedHoleCircles`), and never lists bores of standard parts (fasteners, washers, nuts). Test in `test_drawing_hole_table` (a screw in each hole, 2 rows). |
| PRODUCTION-3 (medium) | The resolved entry face lacked its orientation in the body, so an underside "pointed" into the material and no opposite face was found; the body's own copy of the face is used now. An exit edge of another body gives a warning. Unit test `measuredThicknessFromUnderside` and a case in `tests/scripts/holes_fasteners.fcs` (failed on main). |
| PRODUCTION-4 (medium) | Thread engagement is not a clash: a standard screw in a tapped hole, a nut on its bolt or an insert in its hole (every edge of the overlap within tap drill .. major diameter about the part's axis) is counted apart ("N thread engagement(s) not counted"); *Include thread engagement* / `INTERFERENCE THREADS` lists them marked `[thread]`. The head-in-counterbore overlap did not reproduce: an M6x10 seated in its M6 counterbore reports nothing (test `seatedFastenerNoInterference`), nor does the tester's `exports/part.fcad`. |
| PRODUCTION-5 (medium) | A blind hole in a thin plate counted as through (the far probe came out of the opposite face). Openness is tested just past each end, a coaxial drill-point cone closes it. Auto length: the longest standard length within the hole (within the thread when one fits there); too shallow for every length gives an error asking for `length=`. |
| PRODUCTION-6 (low) | MODEL > Properties... opens the COMPPROPS card (with the mass) instead of a separate dialog without it; an empty Material field shows the bodies' material as placeholder. |
| PRODUCTION-7 (low) | Title block values are measured with the sheet font and shrink, break onto two lines, then get an ellipsis. Test `longTitleFitsItsCell`. |
| PRODUCTION-8 (low) | PETG, ASA, TPU 95A, Nylon CF, PA12 (SLS/MJF), SLA resin, PP, HDPE; Baltic birch plywood, hard maple, walnut, birch, beech, cherry, OSB. BOM export with a cost column reports the cost total and the rows without a cost. |
| PRODUCTION-9 (low) | A line's angle to the horizontal is labelled in its own arc at the start point, off the length label; placed dimension labels follow their geometry after every solve (a dimension edit), as during a drag. |
| PRODUCTION-10 (low) | The point of a preselected face is provisional: the first click into Placement points replaces it (one click, one hole). |
| TUTORIAL2-1 (medium) | Back that undid a step's result re-enters the step's context (reopens the command; sketch contexts are not replayed); Next after Back redoes up to the next step's recorded undo index. |
| TUTORIAL2-2 (medium) | Step 11.2 points at CENTRE DISTANCE (60 mm) and explains DISTANCE. |
| TUTORIAL2-3 | New step key `"pick"` (selection priority during the step): 4.1, 8.1 and 11.1 pick edges first; 11.1 says to zoom in on the counterbores. |
| TUTORIAL2-4 | Step 12.4 says to press Esc to close the command line again. |
| TUTORIAL2-5 | Step 13.1 names More / Tools, the TOOLS section and Ctrl+K; mesh and face DXF cards have a labelled Browse button (was an empty 28 px box). |
| TUTORIAL2-7 | **Start Tutorial** (`TUTORIAL`, `TOUR`) in command search. |
| TUTORIAL2-8 | A restored chapter is named "Tutorial - <chapter>". Open / Save As start in the last folder used, else Documents; `FABRICAD_FILE_DIALOG_DIR` overrides it and `ui.sh` / `tutorial_run.py` set it so test runs still save beside their reports (closes UI2-TUTORIAL-11 too). |
| TUTORIAL2-9 | The button reads "Show me · F7". |
| TUTORIAL2-10 | Step 4.1 picks edges first; its hint says how to drop a face picked by mistake. |

Still open:

* TUTORIAL2-6: the UI drew into 1400x860 on a 1900x990 display; most likely the harness start size, not the app.
  Recheck on a fresh display.
* PRODUCTION-4 head overlap: not reproduced; needs the `.fcad` of a run that shows it.
* Not tested in the round: inch face DXF, AP242 write with material, IGES `faces` mode, "Also drill the mating
  part", the Castle jig, modelled threads; IGES still has no independent reader.

Verification: Release build, full ctest (98: 93 pass, 5 GUI-runner entries skipped as opt-in) and the colour lint
green; on display :99 a plate with an M6 counterbore and a 1/4-20 tapped hole, a screw in each (Auto picked
`ASME B18.3 1/4-20 x 3/8`), gives "No interference · 1 thread engagement(s) not counted" and a two-row hole table
(`fix-holetable-occluded.png`). `tests/scripts/holes_fasteners.fcs` fails on the previous main (pocket hole from the
underside) and passes now.
