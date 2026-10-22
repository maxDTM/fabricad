# Drawing round 1: views and annotations (mechanical drafter, mouse + keyboard only)

Area: Drawing views (base, projected, section full/stepped, detail, break, hidden/tangent, scale,
move/delete) and annotations (dimensions of all kinds, tolerances, callouts, centre marks/lines,
GD&T, datum, surface finish, notes, edit/delete, associativity after model edits).

Method: private Xwayland display :71 via `tools/uitest/ui.sh`, real X input (xdotool) and screenshots.
The design (plate + boss + 3 hole types + 4 fillets in component `Plate`, a pin in component `Pin`) was
typed into the in-app command line (that is the app's keyboard UI); every drawing operation was done with
mouse clicks, dialogs and shortcut keys. The model is saved as `views-annot/repro-model-asm-drw.fcad`
(open it, switch the workspace dropdown to DRAWING; plate is 24 mm thick in that file because of the
associativity test; sheet standard is ASME third angle because of test DRW-VIEWS-ANNOT-7).

Test-environment notes (not product defects): the shared compositor resized my display repeatedly
(945x490, 945x990, 1900x490...), so the window was clipped; I ran with `QT_SCALE_FACTOR=0.65`/`0.55` and a
private `XDG_CONFIG_HOME` to keep everything on screen. The binary `build/bin/fabricad` was rebuilt by
someone else at 13:25 (new dark theme); the first ~two thirds of the session ran on the old light-theme
binary (built 09:22), the rest on the new one. No drawing-module behaviour difference was seen.

Screenshots: `docs/tests/drawing/round1/views-annot/NN-*.png`.

## Issues

| id | sev | title |
|---|---|---|
| DRW-VIEWS-ANNOT-1 | medium | Projected view auto-placement overlaps its parent view when space is short |
| DRW-VIEWS-ANNOT-2 | medium | Hole callout reports counterbore depth 7 while the model (and a drawing dimension) say 6 |
| DRW-VIEWS-ANNOT-3 | medium | Aligned dimension text is drawn upside down |
| DRW-VIEWS-ANNOT-4 | medium | Chain dimension segment text collides with the base dimension text (60 / 42 print as one glyph blob) |
| DRW-VIEWS-ANNOT-5 | medium | Delete key does not delete selected drawing objects (Backspace does) |
| DRW-VIEWS-ANNOT-6 | medium | Plate-thickness dimension orphans (red ?) when an unrelated cut feature is deleted |
| DRW-VIEWS-ANNOT-7 | medium | Switching the sheet to ASME third angle leaves existing projected views in first-angle arrangement |
| DRW-VIEWS-ANNOT-8 | medium | Changing the base view scale rescales children but does not re-layout: views overlap each other and the title block |
| DRW-VIEWS-ANNOT-9 | low | A section view placed by clicking near the title block overlaps the title block, its label overprints the title |
| DRW-VIEWS-ANNOT-10 | low | Detail label ("B") is drawn in the middle of the parent view, not next to the detail circle |
| DRW-VIEWS-ANNOT-11 | low | Adjacent parts in a section (plate, pin) get identical hatch direction and pitch |
| DRW-VIEWS-ANNOT-12 | low | Angle dimension prints "90.000deg" while every other dimension uses document precision |
| DRW-VIEWS-ANNOT-13 | low | Unexplained 1 mm high rectangle above the plate top face in the front view, and a grey seam-like line in the boss with tangent edges on |
| DRW-VIEWS-ANNOT-14 | low | Ordinate: Enter with no features picked errors but leaves an orphan origin label "0" |
| DRW-VIEWS-ANNOT-15 | low | Break view: break lines run through the whole view bounding box, far beyond the part outline |
| DRW-VIEWS-ANNOT-16 | low | GD&T / callout / surface editing is a free-text `key=value` box (no symbol picker, hard to discover) |
| DRW-VIEWS-ANNOT-17 | low | Deleting a parent view silently deletes all child sections/details and their dimensions (no confirmation) |
| DRW-VIEWS-ANNOT-18 | low | Stale (unrepainted) band after mouse-wheel zoom in the drawing canvas (may be Xwayland related) |

### DRW-VIEWS-ANNOT-1 (medium) projected view overlaps parent
Repro: new A3 ISO first-angle drawing, Base View (Front, 1:1, visible edges), click the sheet low, about 90 mm above
the bottom frame line. Press P, move below the base view, click, Enter.
Expected: gap of parent half-size + child half-size + 15 mm as in the README (or both views shifted / refused).
Actual: the top view is clamped into the frame and pushed up into the front view: the top view's upper edge
overlaps the front view's lower 10 mm (and during the preview the ghost view crosses the frame). Screenshot `01`.

### DRW-VIEWS-ANNOT-2 (medium) counterbore depth in callout
Repro: counterbore hole (thru d6, cbore d11, depth 6) in a 20 mm plate; Section A-A through it; Symbols > Hole
Callout, click the hole in the top view, place.
Expected: `Ø6 THRU ⌴ Ø11 ↓6`. Actual: `Ø6 THRU ⌴ Ø11 ↓7`. A linear dimension picked on the cbore floor line and the
plate top line in the section view reads 6, so the geometry is right and the callout is wrong. Thread callout
(`M6x1 - 6H ↓10 / Ø5 ↓10`) was correct. Screenshot `11`.

### DRW-VIEWS-ANNOT-3 (medium) aligned dimension text upside down
Repro: Dimension (D), Type Aligned, pick the left through-hole circle then the tapped-hole circle (centres 41.34 mm
apart, line rising to the right), place the text on the upper-left side.
Expected: "41.34" readable from the bottom or right (ISO 129). Actual: text is rotated about 212 deg, i.e. upside down.
Screenshot `07`.

### DRW-VIEWS-ANNOT-4 (medium) chain dimension text overlap
Repro: linear dimension 60 (top and bottom plate edges of the top view), Dimensions > Chain Dimension, pick the 60
dimension, then the counterbore circle, then the tapped hole circle, Enter.
Expected: chain segments readable (10 and 42 on their own dimension line, or offset from the 60).
Actual: segments 10 and 42 are drawn on the same line as the 60; "42" prints on top of "60" (reads "6Ø2").
Screenshot `08`.

### DRW-VIEWS-ANNOT-5 (medium) Delete key
Repro: click a dimension (R6), a note, or a view in the drawing canvas so it highlights, press Delete.
Expected (README keyboard table: Del = Delete): object removed. Actual: nothing happens (Delete from the command
line says "NOTHING SELECTED"); Backspace removes the selection and Ctrl+Z restores it. Delete key was sent as the X
keysym `Delete`.

### DRW-VIEWS-ANNOT-6 (medium) thickness dimension orphaned by unrelated delete
Repro: in the right-side view add a linear dimension (two clicks on plate top and bottom edges) = plate thickness
(20, later 24). In the design tab: `SELECT feature:Extrude3` (the cut that makes the two plate holes), `DELETE`, answer
Yes. Back in the drawing tab: that thickness dimension is a red `?` (Measured "?") although the plate top and bottom
edges are untouched; baseline/ordinate/aligned dimensions to the removed holes are orphaned too, which is correct.
Ctrl+Z in the design brings every dimension back (good). Screenshot `13`.

### DRW-VIEWS-ANNOT-7 (medium) third angle does not re-derive views
Repro: drawing with a front view, a top view below it and a right-hand side view (first angle). Modify > Edit Sheet,
Standard = ASME (third angle), OK. Expected: projected views rearranged (top above, right view on the right) or at
least a warning. Actual: views stay where they were, so the sheet now contradicts its own standard (top view
below the front, left-side view on the right); only newly placed projected views follow third angle. Screenshot `16`.

### DRW-VIEWS-ANNOT-8 (medium) scale change does not re-layout
Repro: double-click the front (base) view, Scale 2:1, OK. Expected: children rescaled and layout re-flowed (the README
says automatic layout keeps views a gap apart, inside the frame, above the title block). Actual: top and side views
inherit 2:1 (the section stays 1:1) but keep their old centres, so views overlap each other, dimensions, the section
view and run over the title block. Undo restores the previous state. Screenshot `17`.

### DRW-VIEWS-ANNOT-9 (low) section view overlaps title block
Repro: Create > Stepped Section View (cut line points left hole x, centre, boss centre; Enter), place the view by
clicking on the title block area. Actual: view bounding box and label "C-C (1:1)" sit on the title block ("ASM").
Expected: pushed above the title block. Screenshot `14`.

### DRW-VIEWS-ANNOT-10 (low) detail label position
Repro: Detail View on the section view around the counterbore. The label "B" is drawn at the middle of the A-A view
(over the pin hatch, ~20 px from the circle) instead of next to the circle. Screenshot `04`.

### DRW-VIEWS-ANNOT-11 (low) hatch of adjacent parts
Section A-A: plate and pin (separate components) are hatched with the same 45 deg direction and spacing; ISO 128-50
asks for alternating direction/pitch between adjacent parts so they can be told apart. Screenshot `03`.

### DRW-VIEWS-ANNOT-12 (low) angle precision
Angular dimension of the plate corner prints `90.000°` (3 decimals) next to `88`, `100`, `41.34` (document precision).

### DRW-VIEWS-ANNOT-13 (low) spurious geometry in the front view
Front view of the part (visible-edges mode, first base view): a small rectangle (about 4.5 mm wide, 1 mm high)
stands on the plate top face at x = 0, i.e. above the solid, in line with the M6 tapped hole / counterbore at x = 0.
It does not appear in the right-side view or in the A-A section. With Tangent Edges = Full a grey vertical line
appears at x = 0 inside the boss (looks like a cylinder seam drawn as a tangent edge). Not root-caused.

### DRW-VIEWS-ANNOT-14 (low) orphan ordinate origin
Dimensions > Ordinate: click origin point, place label, press Enter without picking features. Status bar: "Select a
feature of the same view" (red), but an orphan origin label "0" stays on the sheet (Ctrl+Z removes it).

### DRW-VIEWS-ANNOT-15 (low) break lines too long
Break View on the front view (two vertical lines in the plate part): the pair of zig-zag break lines is drawn from the
top to the bottom of the view's bounding box, i.e. also through empty space above the plate. Dimensioning across the
break correctly reads the true length (100). Screenshot `05`, `18`.

### DRW-VIEWS-ANNOT-16 (low) usability of symbol editing
Datum, FCF, hole/thread callouts, surface texture and revisions are edited through one text box "SETTINGS
key=value; key=value" plus a help paragraph. It works (`tolerance=0.05; datums=A; dia=1` produced `⌖ Ø0.05 A`), but
there is no characteristic/symbol chooser, so GD&T symbols other than the default position symbol are hard to reach.

### DRW-VIEWS-ANNOT-17 (low) cascade delete without confirmation
Create > Delete on the top view removes it together with section A-A, detail B, and all their dimensions and cut
lines; the sheet list lags a second (shows 5 views, then 2). No confirmation or status message; Ctrl+Z restores all.
Screenshot `15`.

### DRW-VIEWS-ANNOT-18 (low) stale repaint
After several mouse-wheel zoom steps the top ~40 px of the canvas kept the previous zoom level's picture (twice);
a mouse move repaints it. Possibly an Xwayland/offscreen artefact, noted for completeness.

## Incidental findings outside the drawing area (design workspace)
* `SELECT body:Body1` + `CFB` (component from body) followed by `NEWCOMPONENT Pin` + `OK` left the new component
  `Body1` empty (0 bodies) and put the body into `Pin`. Doing NEWCOMPONENT first works.
* The first Extrude in a freshly created empty component defaults to Operation = Join (docs say New Body).
* `EDITFEATURE Extrude1` (as in SCRIPTING.md) answers "select a feature to edit"; the feature must be selected
  first (`SELECT feature:Extrude1`, then `EDITFEATURE`).

## What was tested and worked
* Drawing creation (ISO A3), sheet list, base view front/hidden style, projected top and side views (first angle
  placement and hover preview), drag of parent moves children, drag of child is constrained to its axis.
* Hidden-line drawing of drilled, counterbored and tapped holes incl. drill-point cone; hidden/visible switch by
  Edit View, exact HLR.
* Full section A-A through top view: cut-line symbol with arrows and labels, hatch, correct profile of boss, pin,
  counterbore and tapped hole (drill point), view label "A-A (1:1)".
* Stepped section (3 clicks + Enter), section label C-C, cut line chain symbol; delete of a section.
* Detail view B at 2:1 with clipped geometry and circle in the parent.
* Break view with true-length dimension across the break.
* Dimensions: linear (edge length 88, two-edge 100 and 60), diameter Ø30, radius R6, aligned 41.34 (value right),
  angle 90, chain, baseline (15, 85 from origin edge), ordinate (0/15/50/85); tolerances symmetric/deviation
  (`+0.10 -0.05`), fit (`Ø30 H7/g6`), basic (boxed), reference (parentheses); edit by double-click.
* Hole callout (`Ø6 THRU ⌴ Ø11`) and thread callout (`M6x1 - 6H ↓10, Ø5 ↓10`) read from the solid; centre marks
  on circles; datum identifier A (triangle on edge), FCF (`⌖ 0.1`, edited to `⌖ Ø0.05 A`), surface texture Ra 3.2,
  multi-line note (create; removed with Backspace).
* Associativity: plate thickness 20 -> 24 updated dimension (24), section, hole depths and hidden lines; boss and pin
  views followed; deleting the hole cut turned dimensions on removed holes into red `?` and Ctrl+Z restored them;
  save / close / reopen restored all views and annotations.
* Undo/redo of view deletion, scale change and dimension deletion.

## Not tested
Aligned section, ordinate stagger (documented limitation), PDF/SVG/DXF output parity (covered by the output
tester), parts list / balloons / revision table (assembly tester), a true assembly with joints.
