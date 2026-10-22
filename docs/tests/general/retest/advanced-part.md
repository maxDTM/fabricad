# ADVANCED-PART retest (round 1 fixes, main d2278a1), display :31

Screenshots: docs/tests/general/retest/advanced-part/. Harness note: the private display was only 945x490 (tiled compositor), narrower than the app's minimum layout, so the app was launched with QT_SCALE_FACTOR=0.55 on :31 to make the whole window (incl. right-hand dialogs) visible. Input was real X events only. Screenshots are therefore small.

## Verdicts
| Issue | Verdict | Evidence |
|---|---|---|
| PART-01 modeled thread on plain cylinder | FIXED | c1/d1/d2: 10 mm cylinder, Thread, Auto size + Modeled, OK: helical modeled thread built, no error. (Also built with a mismatched M12x1.5 designation: feature accepted, dialog shows a warning "M12x1.5 does not match the rod diameter 10 mm".) |
| PART-04 torus invalid defaults | FIXED | a2/a4: dialog opens 40/10, OK enabled. Typed tube 50 (OK disabled, correct), cancelled, reopened: back to 40/10, valid. |
| PART-05 Paste New of a body | FIXED | e1: select Body1 in browser, Ctrl+C, Ctrl+Shift+V over viewport: "Body1 (1)" appears, status "Pasted Body1 (1)", Move/Copy opens. |
| PART-06 thread size/designation/class | FIXED | c1/c2: Class shown; unchecking Auto size reveals Size (M10), Designation (M10x1.5) and Class. |
| PART-07 dialogs reuse last value | FIXED (Offset Plane, Press-Pull) | g5: Offset Plane after -12 reopens with 10 mm. j5: Press-Pull after -8 reopens with 5 mm. |
| PART-08 2 mm fillet on drafted box | PARTIALLY VERIFIED / likely fixed | l1/l2: box drafted 10 deg, 2 mm fillet on vertical edge succeeds. The split step of the original repro was not reproduced (my splitting plane did not intersect the box, m2), so the exact split+draft case is untested. |
| PART-11 seam lines on curved primitives | FIXED | b2/c7 cylinder, n6 sphere (zoomed), o4 torus: no seam line drawn. |
| PART-12 first face click lost in Offset Plane | LIKELY FIXED (intermittent) | First click on a face registered "1 selected" in 3 of 3 attempts (g1, g2, g3, g5). Intermittent bug, so not conclusive. |

Counts: 6 fixed, 1 likely fixed (intermittent), 1 partially verified, 0 not fixed, 0 regressed.

## Regression check of round-1 "working" list
Re-exercised and OK: Box, Cylinder, Sphere, Torus, Thread (modeled), Move/Copy (via paste), Press-Pull, Draft, Fillet, Offset Plane, Undo (Ctrl+Z of Press-Pull), browser listing of bodies, fit-view. Not re-tested (time): Coil, Hole, variable fillet, Scale, Sweep, Loft, Rib, Thicken, patterns, components, other construction planes. No crashes; no regressions seen in what was exercised.

## New defects
- NEW-ADVANCED-PART-1 (low): Mouse wheel over the Thread dialog scrolls combo values instead of scrolling the panel (Designation changed M10x1.5 -> M12x1.5 unintentionally, c3), and wheel over a non-combo area zoomed the viewport instead of scrolling the dialog (c5/c6). Standard Qt issue; suggest ignoring wheel on unfocused combos.
- NEW-ADVANCED-PART-2 (low): Press-Pull dialog changes height after typing a negative value (an extra message row appears), moving the OK button down (j1 vs j4); easy to mis-click, causing a silently ignored OK in my first attempt.
- NEW-ADVANCED-PART-3 (low/perf): With a modeled thread in the document every subsequent feature (Box, Sphere, Torus, Press-Pull) takes several seconds to apply and the UI is unresponsive in the meantime (clicks queued/lost, menu first-click dropped).
- NEW-ADVANCED-PART-4 (low, observation): Press-Pull preview arrow/ghost for a pull on a 2 mm plate points downward with a positive 5 mm distance (j5); unclear if intended.
