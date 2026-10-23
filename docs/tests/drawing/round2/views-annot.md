# Drawing round 2: views and annotations (mechanical drafter, mouse + keyboard only)

Area: Drawing workspace on the redesigned UI (creating views from the part/assembly, sheets, base / projected / section /
detail / break views, dimensions, callouts, GD&T, tolerances, editing and deleting, associativity, both themes).

Method: private Xwayland display :94 (`tools/uitest/ui.sh start 94 ... 1920x1080 0.8`; the compositor tiled the window to
1900x490, so the sheet was worked at 39 to 80 % zoom). Only real X input (xdotool mouse and keyboard) and screenshots; the model is
the round-1 repro assembly (`round1/views-annot/repro-model-asm-drw.fcad`, plate + boss + 3 hole types + 4 fillets in
component `Plate`, a pin in `Pin`), copied to the scratchpad and opened with Ctrl+O. It already contained a round-1 drawing
(Sheet1, "legacy" below); all new work was on a new Sheet2 made with `New Drawing` from the command search. The plate
was edited in the Part tab (Extrude1 24 to 30 mm) for the associativity check. Both themes were used (dark first, then Light via the
`Theme` command). Nothing was saved; no project file was changed.

Screenshots: `docs/tests/drawing/round2/views-annot/NN-*.png`. They are whole-display grabs; the app window is 1900x490.

## Issues

| id | sev | title |
|---|---|---|
| DRW2-VIEWS-ANNOT-1 | high | Drawing canvas shows every change 5 to 40 s late (clicks, wheel zoom, hover coordinates, Esc, Enter); stale band after sheet switch |
| DRW2-VIEWS-ANNOT-2 | medium | Scale change (1:1 to 2:1) leaves projected and section views outside the sheet and the front view over the frame |
| DRW2-VIEWS-ANNOT-3 | medium | Switching to third angle moves the left-hand projected view off the sheet |
| DRW2-VIEWS-ANNOT-4 | medium | Dimension Fit (ISO 286) typed while Tolerance is None is silently ignored |
| DRW2-VIEWS-ANNOT-5 | medium | Lower-case shaft fit `g6` is printed as `G6` |
| DRW2-VIEWS-ANNOT-6 | low | Leader line starts inside the note text and strikes through it |
| DRW2-VIEWS-ANNOT-7 | low | Parts List default position prints over the A-A label and the section view |
| DRW2-VIEWS-ANNOT-8 | low | Break, ordinate, baseline, chain, edit sheet, new sheet, move, delete, datum, revision exist only in the command search |
| DRW2-VIEWS-ANNOT-9 | low | Cards clip their content on the right (Edit View, Edit Sheet, Feature Control Frame, Edit Annotation) |
| DRW2-VIEWS-ANNOT-10 | low | Combo popups are cut to 1.5 to 7 rows (Edit Sheet standard, Dimension type) |
| DRW2-VIEWS-ANNOT-11 | low | Legacy sheet: vertical 60 and 42 dimension texts overprint ("6Ø2"); section label A prints over the 100 tolerance text |
| DRW2-VIEWS-ANNOT-12 | low | Aligned dimension text "50" sits on its own extension line |
| DRW2-VIEWS-ANNOT-13 | low | Tapped-hole callout lists the drill first (`Ø5 ↓10 / M6X1 - 6H ↓10`), the legacy one lists the thread first |
| DRW2-VIEWS-ANNOT-14 | low | Sheets panel list and the +/- buttons are unstyled and unlabelled in both themes |
| DRW2-VIEWS-ANNOT-15 | low | Active tool survives a sheet switch; zoom and pan reset on every sheet switch; `New Drawing` adds a sheet |

### DRW2-VIEWS-ANNOT-1 (high) canvas repaint latency
Repro: open the repro model, Drawing workspace, start Base View, click the sheet; or Ctrl+wheel on the sheet; or hover the sheet.
Expected: ghost, placed view, zoom and the `X/Y mm` readout follow the pointer at once, Enter/Esc finish the command.
Actual: the canvas (and the card state, status line, `X/Y` readout) updates 5 to 40 s later, sometimes only after more input. Examples:
Base View click showed nothing until the pointer moved; Esc and a sheet-tab click did not show for ~10 s; the section line
clicks were visible only 15 to 25 s later; wheel zoom applies in jumps. The ribbon, tooltips, the Sheets list, the command search
and the Part workspace answered within a second, so it is the drawing canvas. After switching sheets a strip at the top of the
canvas kept pieces of the previous sheet until the next input (`screenshot 03/09` show the lag; the strip was seen live).
Caveat: this is a hidden private Xwayland display; it may be the compositor throttling frame callbacks, round 1 saw the same
("stale band", VIEWS-ANNOT-18). The harness note says the app presents without vsync, which does not seem to be enough for the
drawing canvas. Whoever owns it should check on a visible window before treating it as an app defect.

### DRW2-VIEWS-ANNOT-2 (medium) scale change pushes views off the sheet
Repro: Sheet2 with front, right, top, section A-A, detail B at 1:1. Select the front view, Edit View, Scale `2:1`, Enter.
Expected: views re-laid out inside the frame (or refused / warned when they cannot fit; A3 is 420 mm wide).
Actual: right and section views are placed at x beyond the sheet (drawn black on the dark canvas), the top view runs through the
bottom frame, the front view crosses the left frame line; no warning. Ctrl+Z restores. Screenshot `10`.

### DRW2-VIEWS-ANNOT-3 (medium) third angle: left view leaves the sheet
Repro: same sheet, Edit Sheet, Standard `ASME (THIRD ANGLE)`, Enter.
Expected: views mirror to the third-angle arrangement inside the frame.
Actual: top view goes above the front view (correct), but the projected view that was on the right goes to the left of the front
view and lands outside the paper (x < sheet edge, its lines are invisible on the dark pasteboard). Screenshot `09`.

### DRW2-VIEWS-ANNOT-4 (medium) Fit ignored without Tolerance = Fit
Repro: double-click a linear dimension (Edit Dimension), type `H7/g6` in FIT (ISO 286), leave TOLERANCE `NONE`, Enter.
Expected: the fit is shown, or Tolerance switches to Fit, or the field is disabled.
Actual: the card closes, the dimension still reads `100`; reopening shows the fit text kept and Tolerance None. Setting
Tolerance to Fit then shows it.

### DRW2-VIEWS-ANNOT-5 (medium) shaft letter upper-cased
Repro: as above, Tolerance `FIT`, Fit `H7/g6`. Actual text `100 H7/G6` (the legacy sheet's Ø30 reads `H7/G6` too).
Expected: `H7/g6` (lower case = shaft, ISO 286). Screenshots `08`, `12`.

### DRW2-VIEWS-ANNOT-6 (low) leader through its text
Repro: Leader, click the arrow tip right of a view, click the text point left of it, type `SEE VIEW A-A`, Apply.
Actual: the leader line starts at the middle of the last word and crosses `A-A`. Expected: it starts at the text box edge. Screenshot `05`.

### DRW2-VIEWS-ANNOT-7 (low) parts list default position
Repro: Parts List, press Enter (default position above the title block) on a sheet with a section view near the title block.
Actual: header row `DESCRIPTION` is printed over the `A-A (1:1)` label and the table touches the section view. Screenshot `06`.

### DRW2-VIEWS-ANNOT-8 (low) commands only in the search
In the Drawing ribbon the MODIFY group holds only Edit View and DIMENSIONS only Dimension. Break View, Ordinate / Baseline /
Chain Dimension, Edit Dimension, Edit Sheet, New Sheet, Move, Delete, Datum Identifier, Add Revision are found only with Ctrl+K.

### DRW2-VIEWS-ANNOT-9 (low) clipped card content
Edit View: checkbox text `EXACT HIDDEN-LINE REMOVAL (SLOW` cut, combo chevrons missing. Edit Sheet: chevrons cut at the card edge.
Feature Control Frame: title shows `FEATURE CONTROL F` before the pin button. Edit Annotation: label `CHARACTERISTI`.

### DRW2-VIEWS-ANNOT-10 (low) combo popups
Edit Sheet > Standard opens a popup of 1.5 rows (second entry half hidden, scroll arrow). Dimension > Type shows 7 of 8 entries.
Keyboard Down + Return works.

### DRW2-VIEWS-ANNOT-11 (low) legacy sheet text collisions
Sheet1 (loaded from the file): the left 60 dimension and its chained 42 print on top of each other (`6Ø2`), the datum A leader
crosses them; the A-A section label overprints the `100 +0.10/-0.05` text. Screenshot `04`. The text collision fixes of round 1 (VIEWS-ANNOT-4) covered the horizontal case.

### DRW2-VIEWS-ANNOT-12 (low) text on the extension line
Aligned dimension (two picks 50 mm apart, text placed to the right): `50` is drawn on the right extension line. Screenshot `08`.

### DRW2-VIEWS-ANNOT-13 (low) tapped-hole callout order
New Hole Callout on the tapped hole: `Ø5 ↓10` then `M6X1 - 6H ↓10`. Legacy callout on Sheet1 (made in round 1) shows the thread first.

### DRW2-VIEWS-ANNOT-14 (low) Sheets panel styling
The panel is a plain list with a bare `SHEETS` caption and two stock grey `+` / `-` buttons without text; other docks (Features, Model) use
the themed header and buttons. Same in light and dark. Screenshot `12`.

### DRW2-VIEWS-ANNOT-15 (low) tool and view state across sheets
Projected View left active when switching Sheet2 to Sheet1 (then asks for the parent view on the other sheet). Zoom and pan reset to
fit on each sheet switch. `New Drawing` inside an open drawing creates `Sheet2` rather than a drawing.

## What worked
* Open model, Drawing workspace (Alt+3 / dropdown), `New Drawing` card (standard, size, scale), Sheet tabs, Sheets list, sheet switching.
* Base View (Front, visible edges, 1:1, assembly with both parts): placed, selectable; Projected View right and top, auto-moved
  inside the frame (top view first sat on the frame line); Section A-A (two clicks + side click) with cutting-plane marks, hatching in different directions per part (round-1 fix holds);
  Detail B (2:1) with circle and label placed next to the circle (fix holds); Break View collapses a span with zigzag lines, undo restores it.
* Dimensions: linear horizontal and vertical (100, 24), diameter (Ø30), radius (R6), angle (`90°`, fix holds), aligned (50), edit card
  with prefix, suffix, precision, tolerance modes (None, Symmetric, Deviation, Limits, Fit, Basic, Reference); Fit works once the mode is set.
* Annotations: multi-line Text with live ghost, Leader, Hole Callout (`Ø6 THRU ⌴ Ø11 ↓6` correct, `Ø8 ↓20`), Feature Control Frame
  (`position dia 0.05 M A B` drawn correctly), edit it (Characteristic, Tolerance, Ø zone, Modifier, Datums), Delete key removes it,
  Surface Texture (`RA 3.2`), Center Mark, Centerline between two holes, Parts List with 2 rows, Balloon.
* Associativity: Extrude1 24 to 30 in the Part tab: both sheets updated (thickness dimension 30, views, section) without orphans; Sheet1 old annotations all kept.
* Deleting a parent view asks `Deleting VIEW1 also deletes 4 dependent views and 15 annotations. Continue?` (YES/NO), No keeps everything (screenshot `11`).
* Undo (Ctrl+Z) reverted third angle, scale and break view.
* Theme: white paper with black lines in both themes, chrome follows the theme, cards and the confirm dialog themed; light theme
  text card and ribbon checked (screenshot `12`).

## Not covered
Ordinate / baseline / chain creation (only the legacy results were inspected), PDF/DXF output (other tester), Delete key in the
light theme (one attempt showed no confirm dialog within 25 s; the same key worked in dark, probably the canvas lagging, see 1).
Exact 1920x1080 geometry (window was 1900x490 at scale 0.8).
