# SKETCH-CORE functional test, round 1

Tester: automated QA pass via real X input on display :31. Screenshots in `sketch-core/`. No crashes observed.
Note: display width changed from 945 to 1400 px mid-run (harness), so early shots clip the ribbon.

## Issues
| id | sev | title |
|---|---|---|
| SKETCH-CORE-1 | medium | Ctrl+Return does not finish the sketch (README says it does); Return goes to the focused browser tree and opens rename. Shots 19-22, 134 |
| SKETCH-CORE-2 | medium | Viewport not repainted after Horizontal/Vertical constraint or fillet until the mouse moves (stale geometry). Shots 71 vs 72, 97 vs 99 |
| SKETCH-CORE-3 | medium | Rectangle drawn on a sketch on an angled face landed 225 px (about 45 mm) away from the clicked corners once (not reproduced). Shot 86 |
| SKETCH-CORE-4 | low | Typed Angle in line-chain dynamic input is relative to the previous segment; 20 @180 retraces the previous line, silently creating an overlapping line plus a duplicate driven "(20)" dimension, no warning. Shot 122 |
| SKETCH-CORE-5 | low | F6 Fit ignores sketch geometry (10 m rectangle/line not framed). Shots 78, 80, 115 |
| SKETCH-CORE-6 | low | Extrude dialog opens over the left of the viewport and covers sketch profiles; must click profiles to the right of it. Shot 18, 25 |
| SKETCH-CORE-7 | low | Ctrl+A does not select all sketch geometry. |
| SKETCH-CORE-8 | low | Sketch fillet radius cannot be typed before picking (fixed 5 mm, no radius dimension created); user must dimension arc afterwards. Shots 61, 69 |
| SKETCH-CORE-9 | low | Sketch on an angled face opens with Y down / X left, but Edit Sketch of the same sketch shows Y up. Shots 85, 95 |
| SKETCH-CORE-10 | low | Driven (reference) dimension still opens the "enter value" dialog; Enter then errors "driven dimensions are measured, not set" and dialog stays. Shots 14-16 |
| SKETCH-CORE-11 | low | Status bar shows up to three DOF fields; middle one goes stale ("1 DOF" vs "Fully constrained"). Shot 131 |
| SKETCH-CORE-12 | low | Esc,Esc does not always leave the Dimension tool (still active, started dimensioning on next drag). Shot 125 |

## Worked correctly
Line chain with auto H/V inference and close-on-start (auto ends chain, shaded profile); 2-point rectangle (live W/H boxes); center-diameter circle; 3-point circle; 3-point arc, center-point arc, tangent arc (auto tangent constraint); dimension typed value, expression (2*10+5 -> fx 25), zero rejected, text rejected with message; over-constraining dimension prompts to make it driven; conflicting perpendicular rejected with red message; Horizontal/Vertical, Tangent, Equal, Fix (Fully constrained shown), Construction toggle (X); trim (undo/redo ok); offset with chain selection and side pick; fillet; zero-length line, zero-diameter circle and zero-size rectangle ignored; 0.01 mm line/circle and 10000 mm line/rectangle accepted; box select + Delete; delete single line removes its dims; drag of fully dimensioned rectangle translates it; undo mid-tool (ends tool, redo restores); 25x undo / 30x redo no crash; nested profile (circle in rectangle) extrudes with hole; editing sketch dimension after extrude updates body; deleting circle in sketch after cut updates pocket; sketch on angled face + cut extrude works.
