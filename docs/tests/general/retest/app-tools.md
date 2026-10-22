# APP-TOOLS retest (main d2278a1, display :33, real X input only)

Screenshots: docs/tests/general/retest/app-tools/ (note: the harness Xwayland window was tiled tiny by the compositor; I floated/resized my own :33 host window via hyprctl to get a usable 1500x950 display. No source changes.)

## Verdicts
| Issue | Verdict | Evidence |
|---|---|---|
| APP-01 (APP-TOOLS-1) rename to existing name | FIXED | 14.png: "A parameter named 'b' already exists", both rows unchanged |
| APP-02 (APP-TOOLS-2) rename to invalid name | FIXED | 15.png: "Invalid name 'my var'...", row z kept |
| APP-03 (APP-TOOLS-4) Preferences/units | FIXED (minor gap) | 04-06.png: TOOLS tab has Parameters + Preferences (General/Document tabs: default units, reverse zoom, doc length/angle/precision); status-bar "mm" opens the Document units tab; S "pref" finds Preferences. Gap: no "Scripts" entry in TOOLS (ux.md lists it) |
| APP-04 (APP-TOOLS-5) Appearance A | FIXED | 07.png Appearance (A) in MODIFY menu; 17c S "appear" finds it with shortcut A; 40.png A with viewport selection opens colour dialog; 41.png A with browser focus opens the dialog, no type-ahead/inline rename |
| APP-07 (APP-TOOLS-6) ':' inserts colon | FIXED | 09c.png: ':' focuses cmd box, field empty; ":FIT" typed as "FIT" |
| APP-08 (APP-TOOLS-10) Ctrl+Z in cmd box | FIXED | 12.png: after commands, Ctrl+Z in focused cmd box leaves it empty (no recalled text); the 3 parameters still exist (13.png) |
| APP-09 (APP-TOOLS-7) UNITS / ZOOM | FIXED | 10.png UNITS opens the Preferences units dialog; ZOOM accepted, no "Unknown command" (11.png) |
| APP-10 (APP-TOOLS-8) S search noise | MOSTLY FIXED | 16.png "pref": enabled Preferences ranked first, only 2 hits; "appear": single hit Appearance. Remaining: disabled "Edit Dimension" still matches "pref" (ranked last) |
| APP-11 (APP-TOOLS-9) Interference dialog | FIXED (minor) | 50.png: Volume column fully visible (4000.00), summary "1 interfering pair(s), total 4000.000 mm3", intersection drawn as red common volume in viewport. Remaining nit: Body 1/Body 2 names still elided ("Body5 (Body..."). Note: in "All components" mode, bodies inside the same (root) component are not tested ("0 pairs tested", 46.png); needs separate components |
| DOC-06 (APP-TOOLS-11) timeline reorder | FIXED | 38/39.png: dragging Extrude1 before its Sketch1 is rejected, red marker + tooltip "Cannot move: would move before its input 'Sketch1'", order unchanged. (A Join Box dragged before earlier boxes, which has no explicit reference, was still accepted, 27/28.png; Ctrl+Z reverted it. That is arguably by design since the join target is implicit.) |
| PART-02 (APP-TOOLS-3) invalid expressions | FIXED | 20.png Box Length "5 foo": red border, text kept, message "Length: unexpected 'f' at 2", OK disabled; 34.png Extrude taper "1/0": "Taper angle: division by zero", OK disabled; valid value re-enables OK (35.png, 21.png) |

Counts: 10 issues retested: 9 fixed, 1 mostly fixed (APP-10), 0 not fixed, 0 regressed (APP-03 and APP-11 have minor leftovers noted).

## Regression check of previously working features
Working: Measure (face: area/perimeter/centroid/type/normal, Copy all, OK/Cancel, 51.png), Interference dialog and compute (50.png), parameters via SET and Parameters dialog with units (13.png), cmd box (LIST features, SET, HELP via F1, UNITS, ZOOM, autocomplete popup), marking menu (right-click, 52.png), F1 shortcut table (54.png; note F1 is not handled while the cmd box has focus, 53.png - pre-existing/by design), S search, E shortcut opens Extrude, I opens Measure, Home view, browser context menu incl. Create Components from Bodies, timeline hover tooltip/selection, Box tool dialog (New Body/Join), sketch rectangle + extrude, undo of timeline move. No crashes. Not re-tested for lack of time: Physical Material, component Properties, document tabs/Save As/recent files, Roll marker.

## New defects
- NEW-APP-TOOLS-1 (low, functional): Box tool "Operation" combo remembers Join from a previous use rather than defaulting to New Body; with Join and nothing touching, results are confusing (first Box dialog after restart is New Body, later ones Join - 22.png). Also a Join Box at Position X=0 silently merged Body2 into Body1 (26.png), which is expected for join but surprising since "Objects to Cut/Join" was empty.
- NEW-APP-TOOLS-2 (low, visual): the Box dialog height changes when the error line appears/disappears (OK button moves 681 -> 704, 21.png), so a click aimed at OK right after fixing a field can miss.
- NEW-APP-TOOLS-3 (low, functional): No "Scripts" entry in TOOLS > UTILITIES although ux.md lists it (see APP-03).
