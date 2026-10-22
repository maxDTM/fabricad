# Round 1 - Sketch area UI/UX findings

Tester: hands-on via xdotool + screenshots only (display :32, 1800x990). Screenshots in `sketch/`.
Note on harness: the window was re-tiled to 945 px wide by Hyprland twice; I floated/resized it. Not an app defect.

Overall pattern: the viewport and many widgets repaint one input event late (see SKETCH-2), which makes a lot of things look
broken. Where I could, I confirmed the defect by sending a further mouse event and re-shooting.

## Verified user-reported items
- (1) light text on light background: CONFIRMED, widespread (SKETCH-1)
- (2) ViewCube: CONFIRMED (SKETCH-9)
- (3) eye buttons need another action: CONFIRMED (SKETCH-3)
- (4) edges/faces/profiles not selectable: CONFIRMED for sketch profiles in Extrude (SKETCH-4); origin planes in Create Sketch also did not pick by click (SKETCH-5)

## Issues
| id | sev | title |
|---|---|---|
| SKETCH-1 | high | Palettes, menus, dialogs: near-white text on white |
| SKETCH-2 | high | Viewport/UI repaints only after the next input event; torn frames |
| SKETCH-3 | high | Browser eye (hide) does not hide the sketch |
| SKETCH-4 | critical | Clicking a sketch profile in Extrude does not select it, so no body can be made |
| SKETCH-5 | high | Create Sketch: clicking origin plane in viewport does not start a sketch (browser row does) |
| SKETCH-6 | critical | Sketch Fillet on a dimensioned rectangle corner destroys geometry, sketch over-constrained |
| SKETCH-7 | high | Editing a dimension moves unrelated geometry (hidden alignment constraints) |
| SKETCH-8 | medium | Edit Dimension field and Tab-typed values: typed text lands nowhere / invisible |
| SKETCH-9 | high | ViewCube: not draggable, tiny hit areas, flat white-on-white label, saturated cyan hover that sticks |
| SKETCH-10 | medium | Constraint glyphs: "H"/"V" plain letters, Fix shows tofu boxes, dimension text struck through |
| SKETCH-11 | high | Offset tool: DOF changes by +10 but no geometry visible |
| SKETCH-12 | medium | Profiles: construction circle acts as profile boundary; pentagon outside circle unfilled; "Show Profile" off by default |
| SKETCH-13 | medium | Browser double-click on sketch opens Edit Sketch AND an inline rename box |
| SKETCH-14 | medium | Toolbar tab strip: black scroll arrows over tabs, SOLID tab clipped; black document tabs |
| SKETCH-15 | medium | Line start on origin gives no coincident constraint (DOF too high) |
| SKETCH-16 | low | Stale dashed inference line after chain closes; fillet on regular polygon distorts it |
| SKETCH-17 | medium | Ctrl+Z after Offset left sketch mode |
| SKETCH-18 | low | Creating a dimension does not open the value editor (spec 7.7); tooltips use a huge different font |
| SKETCH-19 | low | Sketch palette floats over geometry and cannot be moved; unchecked checkboxes render as solid black squares |

## Details

### SKETCH-1 light text on light (high, visual)
Steps: Create Sketch > XY/XZ plane > look at Sketch Palette; open CREATE dropdown in SKETCH tab; start Extrude; relaunch app after
a crash (recovery dialog); look at the bottom command box placeholder; type in the Edit Dimension field.
Expected: dark text (#222) on light surfaces, visible disabled states.
Actual: palette option labels, whole CREATE menu (all items incl. shortcut hints), Extrude dialog labels/values, the
"FabriCAD did not close cleanly" dialog text, command box placeholder, and typed text in the Edit Dimension field are
almost white on white. Unchecked checkboxes are solid black squares.
Shots: sketch/07-palette-lowcontrast.png, sketch/27-menu-lowcontrast.png, sketch/26c.png, sketch/41-extrude-profile.png,
sketch/79-recovery-dialog-low-contrast.png.

### SKETCH-2 repaint lag / torn frames (high, functional)
Steps: any action by mouse or keyboard (click a browser expander arrow, press Enter to commit a dimension, type in boxes),
then screenshot without further input, 1-13 s later.
Expected: UI updates immediately.
Actual: nothing changes until a later mouse event arrives; then everything appears at once (e.g. fillet undo and an Origin
collapse showed up only after an unrelated toolbar click). Rubber-band previews lag one cursor position. After a ViewCube
edge click the viewport was left torn: left 2/3 showed the old frame, right 1/3 the new (sketch/71-vc-top-edge.png,
sketch/72-vc-after.png) and stayed frozen even after many mouse moves/keys. Same root cause as the user's eye-button report.
Shots: sketch/46-eye-immediate.png vs sketch/47-eye-after-flush.png; sketch/09-rect-preview.png (preview behind cursor).

### SKETCH-3 browser eye does not hide sketch (high, functional)
Steps: Finish sketch; click the eye of Sketch1 in the browser (247,242); wait; pan with middle drag; click empty canvas.
Expected: sketch disappears at once.
Actual: icon switches to "hidden" but the sketch stays drawn (and stays in selected light-blue); it only vanished later, after
Create Sketch was clicked (sketch/50b.png).
Shots: sketch/46-eye-immediate.png, sketch/49-eye-after-deselect.png, sketch/50b.png.

### SKETCH-4 profile click selects nothing (critical, functional)
Steps: draw rect/circle/slot, Finish Sketch, Extrude (183,82), hover then click inside a shaded profile (several profiles, with hover
pauses, mousedown/up).
Expected: profile highlights, count shown, OK enabled, Extrude gives a body.
Actual: no preselect highlight, no selection; dialog keeps "select a profile to extrude", OK disabled. Because no body can be created I
could not test sketching on a body face.
Shots: sketch/42b-profile.png, sketch/43-profile-multi.png, sketch/70-extrude-click-slot.png.

### SKETCH-5 plane pick in viewport (high, functional)
Steps: Create Sketch, hover/click the translucent orange plane in the isometric view (sketch/01-create-sketch.png).
Expected: hover highlight + click starts sketch, camera Look At. Planes should also be labelled.
Actual: no hover, click does nothing (first attempt); planes are unlabeled and very large. Clicking the "XY/XZ Plane" row in the browser
works and aligns the camera correctly (sketch/51-sketch-xz.png). Note: with display lag (SKETCH-2) I could not fully exclude a late
response, but several flushes did not show one.

### SKETCH-6 fillet destroys sketch (critical, functional)
Steps: 2-point rect 40x25 via Tab typing; chain of 4 lines with H/V + dimension 60; Fillet tool; click top edge then left edge of the
chain rectangle.
Expected: corner rounded (R5), rest unchanged.
Actual: both rectangles vanish (dimension labels 60/40/25 stay), sketch turns red "Sketch1: Over-constrained", browser shows warning icon.
Ctrl+Z restores. Fillet on the (unconstrained) pentagon works but skews the regular polygon (sketch/38-fillet-pent.png).
Shots: sketch/36-fillet-overconstrained.png, sketch/37-undo-fillet.png.

### SKETCH-7 dimension edit moves other geometry (high, functional)
Steps: draw rect A (40x25) at x=500 and chain B starting at the same screen x; Dimension tool on B top edge (47.845); double-click the
dimension; set 60.
Expected: only B changes (anchored corner), A untouched.
Actual: A's left edge also moved 43 px left (to align with B), i.e. an invisible alignment constraint was created between separate shapes
while drawing (inference tracking line becomes a constraint with no glyph).
Shots: sketch/11-rect-done.png (before), sketch/24-dim-60.png (after).

### SKETCH-8 edit boxes (medium, functional)
Edit Dimension panel: after double-clicking a dimension, typed digits did not enter the field until the field was clicked; when they do
the digits are near-invisible (SKETCH-1). Shot sketch/21-dim-dblclick.png. Tab/Enter value boxes work (100, 40/25, 20, R18) but typed text
is not visible in the on-screen boxes before commit (see SKETCH-2).

### SKETCH-9 ViewCube (high)
Steps: drag from (1715,205) to (1790,270) with LMB; click the top edge sliver; look at the cube.
Expected: drag orbits; faces, edges, corners clickable; clear cube.
Actual: drag does nothing (sketch/45-viewcube-drag.png); in sketch/top views the cube collapses to a flat white "TOP" box on a white-grey
background; hit zones for edges are ~10 px slivers; hover highlight is saturated cyan (#00FFFF) and stays after the pointer leaves
(sketch/81-vc-click-top.png); clicking the top sliver started a transition that left a torn frame (sketch/71-vc-top-edge.png).

### SKETCH-10 constraint glyphs (medium, visual)
Fusion-like: small icons. Actual: plain serif letters H/V/T/=, tiny; Fix constraint renders as an empty rectangle (missing glyph) and
the fixed point turns orange-red, not green; dimension numbers are crossed by their own dimension line (sketch/12-rect-glyphs-zoom.png,
sketch/68-fix-glyph-zoom.png).

### SKETCH-11 offset invisible (high, functional)
Steps: slot; Offset tool (473,82); click slot; move 50 px outward; click.
Expected: offset loop at 9.2 mm outside, dimension.
Actual: status goes 4 DOF -> 14 DOF, "4 Sketch Curves", but nothing is drawn, even after pan. Shot sketch/57b.png.

### SKETCH-12 profiles (medium, functional)
Pentagon overlapping a rectangle with a construction circle: shaded profile follows the dashed construction circle; the pentagon corners
outside it are not filled (sketch/41-extrude-profile.png). Construction curves must not bound profiles. "Show Profile" defaults to
unchecked though spec says on; fill is yellow-green, spec says light orange.

### SKETCH-13 (medium) double-click Sketch2 in browser (92,419): sketch enters edit, and the row also turns into an inline rename editor
(sketch/62-edit-sketch.png). Fusion: double-click = edit only; rename via F2/slow second click.

### SKETCH-14 (medium) With SKETCH tab active the SOLID tab is clipped to "ID" and two black scroll buttons sit on the tab strip
(sketch/08-tab-crop.png). Document tab ("Untitled", "Untitled Drawing") are black with white text and a red/grey x; toolbar group
arrows/black chips inconsistent with the light theme.

### SKETCH-15 (medium) Line started on the origin snap shows the red origin marker but 1st point has no coincident constraint:
horizontal 100 mm line = 2 DOF instead of 0 (sketch/06-after-esc.png).

### SKETCH-16 (low) After closing a line chain on its start point a dashed inference line is left on canvas (sketch/ chain screenshot removed;
visible in repro: close the 4-line loop). Fillet on a 5-gon distorts the polygon.

### SKETCH-17 (medium) Ctrl+Z right after an Offset exited the sketch to the SOLID tab (grid and palette disappeared; sketch remained).
Uncertain because of repaint lag; reproduce: slot, Offset, click, Ctrl+Z.

### SKETCH-18 (low) Dimension tool: placing a dimension creates it with an arbitrary value (47.845) and no value editor opens
(spec 7.7: edit box immediately). Tool tooltips use a very large, different font (Fillet/Line tooltips).

### SKETCH-19 (low) Sketch palette is fixed at the right and covers geometry (the 100 mm line ran under it); no drag handle.

## What worked
Create Sketch via browser plane row aligns camera (Top/Front) and shows SKETCH tab, palette, status "Specify first point";
line chains with H/V inference, Tab/Enter length+angle boxes; 2-pt rectangle width/height boxes; circle diameter box; polygon (Tab sides/radius);
slot with T/= glyphs; 3-pt arc preview; fit-point spline; trim; Fix constraint (DOF 10 -> 6); Edit Dimension dialog; Finish Sketch keeps camera;
undo/redo of fillet.

## Not covered
Mirror, Project, tangent arc by dragging, extend, sketch on a body face and Look At (no body could be created, SKETCH-4), construction toggle (X).
