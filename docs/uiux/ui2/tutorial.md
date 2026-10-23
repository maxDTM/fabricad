# UI2 T-tutorial: first-time user run of the in-app bracket tour

Tester: Sonnet (mouse and keyboard only, xdotool on private display :98, build/bin/fabricad at 8498c92,
window 1400x860, dark theme, fresh settings, `FABRICAD_NO_TUTORIAL=0`). Screenshots: `docs/uiux/ui2/tutorial/`.
The tour file and plan were not read. The run went from the welcome card (step 1/65) to the STL export and
"Tour complete" (step 65/65).

## Result

* Tour completes end to end (65 steps, 7 sections) with real input only. Every step advanced when the bubble's
  instruction was followed; nothing blocked.
* `bracket.fcad` saved from the tour (Ctrl+S, name `bracket`), tab closed, reopened with Ctrl+O: 14 features
  (Sketch1 ... Rib1), one body, same shape; no dirty dot after reopen.
* `bracket.stl` exported (97 084 bytes, binary, linear 0.02, angular 10). `tools/uitest/stlcheck.py`: 1940
  triangles, 1 shell, volume 66 902.9 mm3 (bracket reference 66 901.36, 0.002 %), box X -40..40, Y -25..25,
  Z 0..50. Matches.
* Bubble anchors were right in nearly every step (Sketch button, XY plane circle, Rectangle caption/arrow,
  Finish Sketch, Create/Modify captions, MODEL > planes, Extrude1 row, tab x). The origin marker in the sketch is
  centred on the axes crossing (06). Right-clicking a toolbar button/caption opens its dropdown directly under it
  in the model view and in sketch mode (05, 17, 29, 44, 56); the "..." menu under the bubble opens under its
  button (66).
* Esc closes menus; Show me highlights the anchor and adds an orange hint line (70); Restore chapter asks
  "open the start of this chapter in a new tab?" and opens `bracket sketch` in a new tab, keeping the old one
  (67, 68); Help > Tutorials lists the tours and re-running on a loaded document works (62, 63).

## Issues

### UI2-TUTORIAL-1 (medium) bubble clipped by the window bottom when a dropdown is open
Repro: step 27 (Slot and cut: "Open the Create menu of the sketch toolbar"), right-click the CREATE caption.
Expected: bubble stays fully visible (moves beside or above the tall menu). Actual: the bubble moves down and is cut
by the window edge, only its title and first line show; Back/Skip/"..." are out of reach (17).

### UI2-TUTORIAL-2 (medium) false "That did Dimension" hint while the step is progressing
Repro: step 32 (and again step 44): press D, click the arc centre, click the edge, click to place the text. The
Sketch Dimension card opens with the measured value. Expected: no hint, the user is about to type 25 / 40.
Actual: red hint "That did Dimension. Press Ctrl+Z, or use ... > Undo to step start." appears under the
instruction although the action is correct (21, 38). Typing the value and Enter then advances normally.

### UI2-TUTORIAL-3 (medium) keyboard focus lost after the Open dialog; typed text leaks into shortcuts; Esc dead
Repro: step 62 Ctrl+O, pick bracket.fcad (opens), step 63 press Ctrl+9 and type `LIST features`.
Expected: the cmd line takes the text. Actual (first attempt): Ctrl+9 did not focus the cmd line; the letters ran
tool shortcuts instead (S opened command search, E started Extrude, a 5th Extrude card appeared, the feature list
got "Extrude5" in edit mode) and three Esc presses did not cancel the card (52, 53). Only clicking the card's X
and then the view restored keyboard control; Ctrl+9 then worked and LIST printed all 14 features (54). Suggest
returning focus to the main window / viewer after a file dialog closes, and making step 63 less trap-like.

### UI2-TUTORIAL-4 (low) literal markup in bubble text
* Step 63: "and `LIST features` prints ..." shows the backticks (51).
* Final card (step 65 done): "double-click any feature in the {timeline} to change it." shows the unresolved
  placeholder `{timeline}` (59).

### UI2-TUTORIAL-5 (low) Show me hint is styled like an error
Show me, step 3: "It is the first button of the SKETCH section of the toolbar." is shown in the same red/orange
as the "wrong action" hints (70). Use a neutral/accent colour for help text, keep red for corrections.

### UI2-TUTORIAL-6 (low) "Can't see it? Orbit, or press Home" appears at step entry
Steps 35 (hole, 24) and 47 (chamfer, 40) show the red hint immediately, before the user did anything, because the
card covers the target edge. It reads like a reproach. Show it only after a delay or a wrong click.

### UI2-TUTORIAL-7 (low) bubble covers its own target / the next click
* Step 7 (Center Rectangle, "type 80, Tab, type 50"): the bubble sits right under the cursor and hides the
  width/height boxes it talks about (07).
* Step 58 (Measure two counterbore rims): with the Measure card open, the second rim sits under the bubble after
  Home (47); the user must orbit. The bubble moved out of the way only after an orbit.
* Step 56 (Edit the history): the bubble sits on the FEATURES list, hiding rows above/below the highlighted
  Extrude1 row (46). It is fine once the list is scrolled (the highlight follows the row).

### UI2-TUTORIAL-8 (low) Show me with target off screen gives no pointer
Step 54 (Rib: "Click the line you just drew"): in the iso view the line is mostly hidden by the wall, the anchor
is off screen, Show me says "Nothing to show from here: the target is not on screen yet." (45). Orbiting made
the anchor appear (the bubble repositions), but the first reaction is a dead end. Also step 49 (41): the anchor
square floats in empty space over the model (the YZ plane position) while the text first asks for Create Sketch.

### UI2-TUTORIAL-9 (low) Measure step accepts the wrong edge
Step 58/59: I picked the counterbore FLOOR circle of the second hole instead of its top rim (hover shows the
smaller inner circle). The tour accepted it and the bubble said "the table shows the centre distance: 60 mm" while
the table showed 60.133 (Delta Z 4). Either validate the rim (z = 10) or phrase the text without a fixed number.

### UI2-TUTORIAL-10 (low) Alt+Left / Alt+Right / F7
* Alt+Right at step 1 jumped to step 3 (step 2 skipped without being shown). Alt+Left at step 3 did nothing (65).
  The Back button works. Unclear whether Alt+Left/Right are meant as Back/Next.
* F7: no visible reaction at the welcome step, step 3 of a restored chapter or after the tour (60, 69).

### UI2-TUTORIAL-11 (info) Save dialog starts in the repo directory
Step 60/61: the Ctrl+S dialog opens in the app's working directory (`.../projects/fabricad`), so the tour's
bracket.fcad / bracket.stl land in the source tree for a user who starts the app there. The tour text does not say
where to save. (Not wrong for the CI runner, which relies on it.)

## Step log (screenshots)
01 step 1 (3D view), 02 Home, 04 step 4 plane circle + Show me, 05 Rectangle right-click dropdown, 06/07 rectangle
step, 08 step 8 Finish Sketch, 10 distance, 12 line, 17 Create caption menu with clipped bubble, 19-21 slot dims,
22 extent combo, 23-27 hole, 28 counterbore depth, 29-35 mirror, 36-38 flange sketch, 39 flange profile,
40 chamfer, 41-43 rib sketch, 44-46 rib + history, 47-48 measure, 49-50 save/open, 51-54 Ctrl+9, 55-58 export STL,
59 tour complete, 61-70 Help, Alt+arrows, "..." menu, Restore chapter, F7, Show me.
