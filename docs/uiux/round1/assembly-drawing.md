# Assembly / transforms / drawings UI-UX test (round 1)

Tester: assembly-drawing. Display :34. Screenshots in `assembly-drawing/`. Task: two-box bracket (Box 60x30x8 plate + Box 8x30x30 upright, overlapping), Create Components from Bodies, Move Component (triad drag, ring drag, typed), Ground, Joint / As-built Joint, Interference, Drawing workspace (base, projected, section, dimension, parts list, PDF export), plus visual audit.

Legend: kind visual/functional, severity critical/high/medium/low.

## ASSEMBLY-DRAWING-1 (visual, critical) Menu, dialog and context-menu text is near-invisible (light grey on white); enabled/disabled look inverted
Steps: click CREATE label (207,110); or ASSEMBLE (568,110); or INSPECT / TABLES / EXPORT / SELECT / hamburger (19,17); right-click a browser row; open any command dialog (e.g. Box, Move Component, Joint).
Expected: dark readable labels for enabled items, greyed for disabled.
Actual: enabled items are drawn ~#E5E8EC on white (almost unreadable). Disabled items are DARKER than enabled ones: in ASSEMBLE with nothing selected only "Ground" and "Create Components from Bodies" (disabled) are readable, enabled New Component/Joint/Rigid Group are faint but clickable. In the component context menu only Paste / Paste New (clipboard empty = disabled) are readable. Dialog field labels (Plane, Position X, Length...), typed values inside input fields and status hints are equally faint.
Shots: 04-createmenu, 26-assemble, 31-rclick, 84-comp-ctx, 05-box, 06-boxvals, 36-move, 53-inspect, 70-tables, 77-hamburger.

## ASSEMBLY-DRAWING-2 (functional, critical) Viewport does not repaint after commands / browser actions
Steps: Box > OK; or click the eye of Body2; or finish Move Component; or Show again. Take screenshot without touching the view.
Expected: result visible immediately.
Actual: new body not drawn after OK (empty grid; only after a mouse-wheel tick does the box appear). Eye click flips the icon but the body stays visible (hide) / stays hidden (show) until the next wheel tick or a click in the empty viewport; the state shown is always one action behind. Ghost of the previous selection highlight (blue transparent copy of the moved body at its old position) stays on screen after Move OK. Hover never repaints (no pre-highlight).
Shots: 09-body, 11-wheel, 16-eye, 17-eye-hover, 18-eye-after-wheel, 19-deselect, 39, 42-ok.

## ASSEMBLY-DRAWING-3 (functional, critical) Faces / edges / bodies cannot be picked in the viewport
Steps: default Select mode, click on top face of the plate (850,420), edges, vertices; also with SELECT > Face Priority; also inside Joint dialog after pressing Select for Component 1.
Expected: hover highlight, click selects face/edge/body, status bar shows selection.
Actual: nothing is selected (status bar empty, dialogs stay "not set"). Joint / As-built joint snap and axis picking therefore impossible (OK stays disabled); only browser rows select things. After a wheel redraw the large orange origin plane appears highlighted although the Origin folder is hidden, suggesting hidden origin planes are being picked in front of bodies.
Shots: 20-face-click, 24-face-prio, 25-face-prio2, 43-joint, 45-joint-click1, 46-joint-click2, 47, 90-asbuilt-pos.

## ASSEMBLY-DRAWING-4 (functional, high) Joint / As-built joint cannot be completed; Drive Joints untestable
Steps: J, Select (Component 1) then click body; or Shift+J, pick Body2 and Body1 in browser (works), Motion=Revolute, then click viewport for the joint position.
Expected: snap marker on hover, click sets joint origin, OK creates joint, joint appears under Joints in browser and timeline.
Actual: component pick via browser works; viewport joint position click does nothing, OK stays disabled, so no joint is created and Drive Joints cannot be exercised. Joint dialog also draws "Enable" (joint limits) as a solid black square.
Shots: 85-asbuilt, 86-asbuilt2, 88-rev, 89-joint-made, 43-joint.

## ASSEMBLY-DRAWING-5 (functional, high) ViewCube cannot be dragged to orbit
Steps: from iso view press mouse on the cube centre (1300,215), drag left 100 px, release.
Expected: view orbits with the cube.
Actual: view unchanged (only fit/ wheel tick redraws). Face clicks (TOP, FRONT, RIGHT) and the front/top edge do work.
Also: cube is white faces on a white/pale background with no outline, labels tiny and the TOP label is rotated, so faces and hit areas are hard to see. After a cube click the camera is left zoomed far in (not fit).
Shots: 50-iso, 51-cube-drag, 52-cube-drag2, 49-cube-front-click.

## ASSEMBLY-DRAWING-6 (functional, medium) Undo / Ctrl+Z has no visible effect for assembly operations; Move/Ground not in timeline
Steps: Create Components from Bodies x2, Move Component OK (X=20), Ground. Press Ctrl+Z 3 times and click toolbar Undo.
Expected: last operations are undone, timeline shrinks, body moves back.
Actual: nothing changes visibly (timeline still 4 icons, moved body stays, components stay). Move Component and Ground create no timeline entry at all (Fusion records a Position / Ground step).
Shots: 81-undo, 82-undo2, 83-undo-btn, 42-ok.

## ASSEMBLY-DRAWING-7 (functional, medium) Assembly menu and browser context menu incomplete / Ground gives no feedback
Steps: ASSEMBLE menu, right-click a component in browser.
Expected: Fusion-like context menu (Ground, Create Components from Bodies, Isolate, Properties, Activate); CREATE menu lists New Component; grounded component shows a pin icon in browser.
Actual: body context menu has only Hide, Physical Material, Appearance, Rename, Delete. Component menu has Ground, but after Ground the browser row shows no pin and the 3D view is unchanged (only status text "Grounded"). Created components are named like their body ("Body1" component containing "Body1" body) instead of Component1:1, and the empty top-level "Bodies" folder remains.
Shots: 31-rclick, 84-comp-ctx, 55-ground, 35.

## ASSEMBLY-DRAWING-8 (functional, low) Move Component dialog: first drag works, but triad is small and tied to the stale-render bug
Steps: select component in browser, Assemble > Move Component, drag red X arrow tip (866,408) right-down, then drag blue ring.
Result: dragging arrow gives X Distance 19, ring drag gives Z Angle -100, typed 30/20 in fields moves the body: this works. Defects: body is not redrawn while dragging (only triad moves; see -2), triad and ring are tiny (about 60 px) and overlap, so grabbing the intended handle is fiddly.
Shots: 36-move, 37-drag-x, 38-drag-x-up, 40-rotate, 41-typed.

## ASSEMBLY-DRAWING-9 (visual, medium) Inconsistent dark widgets in a light UI (combo popups, tables, lists, file dialog, shortcut list)
Steps: Operation / Motion combo (1283,507), Interference dialog, F1, Output PDF, document tabs, SHEETS +/- buttons and scrollbar in Drawing.
Expected: widgets follow the light theme.
Actual: combo drop-downs, the Interference result table, the F1 command table and the GTK save dialog are pure black with white text; document tabs and "+ / -" sheet buttons and the drawing scrollbar are black; the Interference table clips its columns ("Body1 ...", "Volume [" cut off) and has an unused black body area.
Shots: 13-combo, 87-motion, 54-interf, 91-f1, 74-pdf, 57-drawing.

## ASSEMBLY-DRAWING-10 (visual, medium) Command dialogs overlap the ViewCube area and hide the model; dialog hint text clipped
Steps: open Box, Move Component, Joint, Interference.
Actual: dialogs dock at right, directly under the ViewCube, covering model geometry in the default framing; Dimension dialog hint "Esc clears the current picks; Esc again" is truncated and faint; Joint dialog is 510 px tall and runs to the timeline. Not draggable without a WM was not tested.
Shots: 05-box, 43-joint, 69-dim3, 54-interf.

## ASSEMBLY-DRAWING-11 (functional, medium) Parts list shows only Body1 of a two-component assembly and overflows the sheet frame
Steps: new Drawing, Base View of Body1 (status showed active Body1), Tables > Parts List, click (900,300).
Expected: all components (Body1, Body2) with qty; table inside border.
Actual: only "1 / 1 / Body1"; table's right edge (Material column) is cut by / touches the sheet frame. Description and Material empty.
Shots: 72-parts-placed, 76-exported-pdf.

## ASSEMBLY-DRAWING-12 (visual, low) Drawing annotation collisions and states
Steps: Section View via two clicks through top view then placement; Dimension on top edge of top view.
Actual: the "60" dimension text collides with the section-cut label "A"; views stay cyan (selected colour) after the command ended; status bar shows odd "View of View" for the selected view; the base view preview/selection box is a thin 150x20 px rectangle for the 60x8 front view (correct but gives no feedback of geometry such as hidden lines).
Shots: 66-section3, 69-dim3, 72-parts-placed.

## Verified working (for reference)
- Create Components from Bodies via browser selection + ASSEMBLE menu, Move Component with typed values and ring rotation, Interference (1 pair, 1920 mm3 = 8x30x8, red highlight correct), Drawing: sheet with border and title block, base/projected/section view (A-A, hatching), linear dimension 60, parts list, Output PDF produces correct PDF (`assembly-drawing/drawing-export.pdf`, preview 76-exported-pdf.png).
- ViewCube face clicks.

## User-reported items
1. Light text on light backgrounds: confirmed (ASSEMBLY-DRAWING-1).
2. ViewCube cannot be dragged: confirmed (-5); face clicks do work.
3. Eye buttons delayed: confirmed, root cause is viewport not repainting (-2).
4. Edges/faces not selectable: confirmed (-3); also blocks joints and any face-based rotate.

## Notes
Harness: Hyprland tiling resizes the Xwayland window (display varied 945x490 .. 1900x990); the app window rendered at ~1400x860.
