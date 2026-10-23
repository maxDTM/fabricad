# Tutorial round 2: new-user walkthrough (mouse and keyboard only)

Tester: Sonnet, private display :98 (ui.sh), fresh config (`FABRICAD_NO_TUTORIAL=0`), main at 9aba37b.
Window note: the app drew a 1400x860 area inside the 1900x990 display (see TUTORIAL2-6).

## Result
The whole 66-step tour ("Build a mounting bracket", welcome card to "Tour complete") was completed with the mouse
and keyboard only, from first launch to STL export. Every step advanced when its action was done; no step got stuck.
Anchors were correct everywhere except the small points below. F7, Alt+Left, Alt+Right, Esc, Restore chapter and
"Show me" were all tried.

Files (inspected with a parser, not produced by it): `bracket.fcad` (saved by step 61), `bracket.stl` (step 66).

### STL check
- Binary STL, 1940 triangles (deflection 0.02 / 10 deg as the bubble asked), header "STL Exported by Open CASCADE Technology".
- Bounding box X -40..40, Y -25..25, Z 0..50 mm = 80 x 50 x 50: plate 10 mm (after the history edit 8 -> 10) + flange 40 mm. The flange
  and sketches followed the 8 -> 10 edit, so the parametric history edit works.
- Volume 66 902 mm3; closed manifold (2910 edges, every edge shared by exactly 2 triangles).
### Reopened part
Ctrl+S "bracket", close tab, Ctrl+O, open: 14 features (Sketch1, Extrude1 10 mm, Sketch2, Extrude2 40, Fillet1 10, Sketch3, Extrude3 all,
Hole1 o6.6, Mirror1, Sketch4, Extrude4, Chamfer1 2, Sketch5, Rib1 6), geometry identical, tab named "bracket" (screens 150, 154).
"Tour complete: bracket.stl is ready for your slicer" card shown (159).

## Things that worked well
Right-drag orbit / Home / middle-drag / wheel step (03); "Show me" cursor demo and F7 (106, 109); the grey "Can't see it? Orbit, or press Home"
hint appearing after a dwell; Help > Tutorials menu exists (168); "Restore chapter (new tab)" asks first and keeps the current tab (165-167);
the bubble follows the dialog/tool and never hid its own target except in the cases listed.

## Issues

### TUTORIAL2-1 (medium) Back (Alt+Left) undoes the model, Next (Alt+Right) does not redo it
- Repro: reach step 48 (Chamfer: "Type 2 and press Enter"), apply the chamfer (step 49 "Rib: Click Create Sketch"). Press Alt+Left, then Alt+Right.
- Expected: Back returns to step 48 with the chamfer command usable; Forward returns to step 49 with the chamfer present (or the bubble tells you to redo).
- Actual: Alt+Left goes to step 48 and removes Chamfer1 from the feature list (Redo lit) but no Chamfer dialog is open, so "Type 2 and press Enter"
  has nothing to type into (118). Alt+Right jumps to step 49 while Chamfer1 is still missing and Redo is still lit (119). Continuing the tour would silently
  produce a bracket without the chamfer; I recovered with Ctrl+Shift+Z (120).
- Shots: 117, 118, 119, 120.

### TUTORIAL2-2 (medium) Measure bubble contradicts the table
- Repro: step 59/60, pick the two counterbore rims.
- Expected: bubble and table agree.
- Actual: step 60 says "The table shows the distance between the two rims: 60 mm, centre to centre", but the row named DISTANCE shows 49 mm (rim to rim)
  and only CENTRE DISTANCE shows 60 mm (143, 145, 150-series). A new user will read 49 as "wrong".
  Suggest: "Look at CENTRE DISTANCE: 60 mm" (and mention DISTANCE is the gap between the rims).

### TUTORIAL2-3 (low-medium) Picking the counterbore rim at the Home view needs several tries
- Repro: step 59 at the Home view, click the larger circle of a counterbore.
- Actual: the first clicks selected the top face (area 2586 mm2, 139), the hole's small floor circle (r 5.5, 142) or an EDGE / FACE pair (143)
  instead of the rim; the bubble does not advance and only the long hint explains "click a wrong edge again to drop it".
  Worked after zooming in with the wheel. Suggest the tour zooms the view to the holes, or sets the edge filter, in this step.

### TUTORIAL2-4 (low) Command History pop-up stays open across steps
- Repro: step 64, Ctrl+9, type `LIST features`, Enter, then click Next / continue to step 65 and 66.
- Actual: the history panel stays over the lower left (154, 156 area) and covers the FEATURES/MODEL panels; nothing in the bubble says to close it.

### TUTORIAL2-5 (low) Step 65 wording does not match the UI
- "Open Export STL: it is in the Tools menu at the end of the toolbar (under More when the window is narrow)". At 1400 px there is no Tools button;
  the anchor is the MORE group label and the item sits in the TOOLS section at the very bottom of the long More list, partly off-screen (155).
  Suggest "Open More, then Export STL (under Tools)".
- Related: the step-66 dialog still shows the placeholder "out.stl (empty: ask ..." and an empty small box at the right of the File field (no icon on the browse button) (156).

### TUTORIAL2-6 (low, possibly harness only) App content did not fill the window
- The window is 1900x990 but the UI drew into 1400x860 (black margin right/below, 01-first-launch.png; geometry dump AppBar w=1400). Popup menus
  (Help menu) extend over the black area (168). Not tutorial specific; could not tell if it is the 1900x990 display left over from an earlier session.

### TUTORIAL2-7 (low) Command search finds neither the tour nor the tutorials
- Ctrl+K "tutorial" and "tour" give no result; "help" gives only Keyboard Shortcuts. The only way back is the ? menu (Tutorials), which the welcome card does mention.
  The project convention is a search entry for every tool/feature; add "Start tutorial".

### TUTORIAL2-8 (low) Restore chapter tab name and Save dialog folder
- Restore chapter opened a tab titled "bracket welcome" (167) (named after the saved doc plus a suffix; "Tutorial: start" would be clearer).
- Ctrl+S opened the save dialog in the application's launch folder (the repo directory), not Documents (147).

### TUTORIAL2-9 (info) Show-me discoverability
- F7 works (first press: demo cursor, later presses: repeat, then the "Can't see it?" hint expands), but neither the SHOW ME button nor the bubble shows "F7".

### TUTORIAL2-10 (low) Step 23 bubble target vs. selection filter
- At step 23 (Fillet, "click the two front corner edges") the first click selected the whole top FACE ("1 FACE", Fillet1 2 mm preview, 36) because the face filter was
  active after the previous steps; the step did not complain and the later edit to 10 mm still produced a correct fillet.
  A reader following the text exactly can end up with a face fillet. Suggest switching the filter to EDGES at this step.

## Other checks
- Esc: cancels the active tool/dialog each time; the bubble stays on its step (no auto-skip). Esc in the Measure and Export dialogs behaved as the bubbles say.
- Bubble overlap: at steps 12, 15, 23, 26 and 52 the bubble partly covers the model but never its click target.
- Rebuild readout, DOF readout ("0 DOF", "FULLY CONSTRAINED") matches the bubbles from steps 8 and 15-19.

Screenshots in this folder are a selection of the ~170 taken (file name = capture order).
