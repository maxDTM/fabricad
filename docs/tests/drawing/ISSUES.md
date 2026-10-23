# Drawing round 1: issue status

Findings of the two round-1 drawing testers:
[views and annotations](round1/views-annot.md) (DRW-VIEWS-ANNOT-*) and
[assembly and output](round1/asm-output.md) (DRW-ASM-OUTPUT-*). Fixed on branch `be/drawing`
(drawing module, `src/drawing/**`). Regression tests: `src/drawing/tests/test_drawing_round1.cpp`
(one test per fixed issue, several on the testers' repro model, copied to
`src/drawing/tests/data/round1-views-annot.fcad`). Deferred items are described in
[docs/plans/backend-deferred.md](../../plans/backend-deferred.md) (section "be/drawing").

| id | severity | title | status |
|---|---|---|---|
| DRW-VIEWS-ANNOT-1 | medium | Projected view overlaps its parent when space is short | fixed |
| DRW-VIEWS-ANNOT-2 | medium | Hole callout shows counterbore depth 7 instead of 6 | fixed |
| DRW-VIEWS-ANNOT-3 | medium | Aligned dimension text is upside down | fixed |
| DRW-VIEWS-ANNOT-4 | medium | Chain dimension text overprints the base dimension | fixed |
| DRW-VIEWS-ANNOT-5 | medium | Delete key does nothing in the drawing | fixed |
| DRW-VIEWS-ANNOT-6 | medium | Unrelated feature delete orphans the thickness dimension | fixed |
| DRW-VIEWS-ANNOT-7 | medium | Third angle does not rearrange existing views | fixed |
| DRW-VIEWS-ANNOT-8 | medium | Base scale change does not re-layout the views | fixed |
| DRW-VIEWS-ANNOT-9 | low | Section view and its label overlap the title block | fixed |
| DRW-VIEWS-ANNOT-10 | low | Detail label drawn in the middle of the parent view | fixed |
| DRW-VIEWS-ANNOT-11 | low | Adjacent parts in a section share hatching | fixed |
| DRW-VIEWS-ANNOT-12 | low | Angle dimension prints `90.000°` | fixed |
| DRW-VIEWS-ANNOT-13 | low | 1 mm rectangle above the plate and a seam-like line in the boss | deferred (root cause in the part Hole feature) |
| DRW-VIEWS-ANNOT-14 | low | Ordinate: orphan origin label "0" after Enter with no features | fixed |
| DRW-VIEWS-ANNOT-15 | low | Break lines run through the whole view box | fixed |
| DRW-VIEWS-ANNOT-16 | low | Symbol editing is a free-text `key=value` box | partly fixed (GD&T frame and revision forms); rest deferred (UI) |
| DRW-VIEWS-ANNOT-17 | low | Parent view delete cascades silently | fixed |
| DRW-VIEWS-ANNOT-18 | low | Stale band after wheel zoom | deferred (not reproduced; canvas already repaints the full viewport) |
| DRW-ASM-OUTPUT-1 | medium | SVG hidden lines look solid (dash array in pen widths) | fixed |
| DRW-ASM-OUTPUT-2 | medium | Parts list falls off the page after a sheet size change | fixed |
| DRW-ASM-OUTPUT-3 | medium | Balloon leader tips are not associative | fixed |
| DRW-ASM-OUTPUT-4 | low | Hidden edges coincident with visible edges are emitted | fixed |
| DRW-ASM-OUTPUT-5 | medium | No way to edit revision rows | fixed |
| DRW-ASM-OUTPUT-6 | low | Title block data is per sheet only | fixed |
| DRW-ASM-OUTPUT-7 | low | Title block scale ignores the chosen scale | fixed |
| DRW-ASM-OUTPUT-8 | low | SVG/DXF export only the active sheet, silently | fixed (sheet name in the file name, status message) |
| DRW-ASM-OUTPUT-9 | low | DXF R12 limits | partly fixed (`$EXTMAX`/`$LIMMAX`, no `$INSUNITS`); lineweights deferred (needs R2000) |
| DRW-ASM-OUTPUT-10 | low | PDF and SVG titles ignore the title block Title | fixed |
| DRW-ASM-OUTPUT-11 | low | BOM Mass and Material always empty | deferred (works once a body has a Physical Material; editing it from the drawing is UI work) |

## What changed, per issue

* **VIEWS-ANNOT-1**: `DrawingModel::autoArrange` no longer pulls a view into its parent to keep it
  inside the frame. It moves the whole family (base view and its children) when that fits,
  otherwise it keeps the 15 mm gap.
* **VIEWS-ANNOT-2**: the callout takes the counterbore depth from the Hole feature (measured from
  the placement face). The geometric face was 1 mm too long because of a part-side defect, see
  VIEWS-ANNOT-13.
* **VIEWS-ANNOT-3**: `readableTextAngle()` normalises the angle into (-90°, 90°]. The old code
  added 180° once without normalising, so angles below -270° stayed upside down.
* **VIEWS-ANNOT-4**: a chain segment that overlaps a span already on the line (here the point
  inside the base span) moves to the next line out, one spacing further.
* **VIEWS-ANNOT-5**: `drawing.delete` has the shortcut `Del`. The main window routes Delete
  through the shortcut lookup before the canvas, and in the Drawing workspace this tool outranks
  `app.delete`.
* **VIEWS-ANNOT-6**: the thickness dimension referenced one piece of the boss base circle seen
  edge-on (`{Extrude1/End|Extrude2/Side/c1}#2`). Deleting Extrude3 renumbered the pieces, so
  the name failed. Refs now fall back to the other pieces of the same split edge. A hidden edge
  that is dropped under a visible one (ASM-OUTPUT-4) keeps its identity for refs.
* **VIEWS-ANNOT-7**: `DrawingModel::changeSheetStandard` mirrors each projected view to the other
  side of its parent, keeping what it shows. Section and detail views stay where they were,
  keeping only their alignment. The family is then moved back inside the frame
  (`fitFamilyInFrame`), and free views are moved clear of the others. Attached annotations
  follow.
* **VIEWS-ANNOT-8**: on a scale edit (`viewDefinitionChanged`), projected children and sections
  drawn at the parent scale take the new scale. Their offsets scale with it, and the auto layout
  then restores the gaps, clears other views, keeps the views in the frame and off the title
  block. When the family cannot fit at the new scale (the repro's 2:1 on A3), views extend past
  the frame rather than overlapping each other, and Edit View says so in the status bar.
* **VIEWS-ANNOT-9**: the auto layout uses `DrawingView::layoutBBox()`, which includes the view
  label. A view is moved up or left to clear the title block.
* **VIEWS-ANNOT-10**: the detail label sits 2 mm outside the circle, on the diagonal that points
  away from the parent's centre.
* **VIEWS-ANNOT-11**: each part in a section gets its own hatch: 45°, 135°, then a closer pitch
  (`hatchVariant`).
* **VIEWS-ANNOT-12**: angle text drops trailing zeros, like length text.
* **VIEWS-ANNOT-14**: the ordinate origin label is committed together with the first member.
  Enter with no member leaves nothing behind and says so.
* **VIEWS-ANNOT-15**: break lines span the material crossed at the cut (`breakCutExtent`), plus
  3 mm.
* **VIEWS-ANNOT-16**: Edit Annotation on a feature control frame has a characteristic picker,
  tolerance, Ø zone, modifier and datums. On a revision table it has a form per row.
* **VIEWS-ANNOT-17**: deleting a view that has dependants asks first (default Yes). The status
  bar then names the dependent views and annotations that went with it.
* **ASM-OUTPUT-1**: the SVG `stroke-dasharray` is rewritten from pen widths into sheet mm
  (`fixSvgDashes`), giving 3 / 1.5 mm hidden lines.
* **ASM-OUTPUT-2**: `DrawingModel::changeSheetFormat` maps views and free notes into the new
  frame. A parts list snapped above the title block re-snaps there. Other tables keep their
  offset from the nearest frame corner. Everything is then refitted inside the frame.
* **ASM-OUTPUT-3**: a balloon stores its edge (`DimRef`) and its parameter along the edge. The
  tip is re-resolved after every projection.
* **ASM-OUTPUT-4**: `removeHiddenUnderVisible` drops the hidden parts that lie on a visible line
  or circle, tangentially, within tolerance.
* **ASM-OUTPUT-5**: tables are solid for hit testing, so a double-click on a cell opens the
  revision form instead of Edit Sheet. Keys `row=<rev|n>` and `description/approved/date/rev`
  edit any row.
* **ASM-OUTPUT-6**: a new sheet copies the drawing-level title block fields. Editing a field
  also updates the other sheets that showed the same value. Scale, Sheet and Size stay per
  sheet.
* **ASM-OUTPUT-7**: the title block Scale is the scale of the first base view, or the sheet scale
  until a view exists. A new sheet repaints with the chosen scale.
* **ASM-OUTPUT-8**: for SVG/DXF with several sheets, the suggested file name ends in the sheet
  name and the status message says that only that sheet was written.
* **ASM-OUTPUT-10**: PDF metadata and SVG `<title>` use the title block Title (`documentTitle`).

## GUI verification (display :74, worktree build, mouse and keyboard)

The tester's repro model was opened and the DRAWING workspace selected from the dropdown. The
screenshots are in `round1/be-drawing-verify/`:
- `01`: the hole callout reads `⌴ Ø11 ↓6`, "41.34" is readable and the angle reads `90°`.
- `02`: clicking R6 and pressing the Delete key removed it.
- `03`: Edit Sheet with standard ASME (third angle), then OK. The top view moves above the
  front view, the side view to its left, all inside the frame.
- `04`: front view at 2:1. The children re-flow without overlapping, but the family no longer
  fits A3.

The display kept resizing during the session, as the tester reported, so the app ran with
`QT_SCALE_FACTOR` 0.5-0.6. At that size the Edit Sheet dialog clipped its fields, which is a
shell layout matter, so the standard was entered through the in-app command line with the dialog
open (`standard=asme`). That exposed a bug, now fixed: the dialog's OK re-applied the old
standard, because command-line input did not update the dialog's widgets.

Outside the drawing area: the three design-workspace observations in views-annot.md, and the
asm-output tester's unverified note about sketches started on the plate's top face ending up
at z = 0. Both are listed in backend-deferred.md for the design/app owners; none was
investigated here.

# Drawing round 2: issue status

Findings of the two round-2 testers: [views and annotations](round2/views-annot.md) (DRW2-VIEWS-ANNOT-*)
and [assembly and output](round2/asm-output.md) (DRW2-ASM-OUTPUT-*). Fixed on branch `rm/fix`.
Regression tests: `src/drawing/tests/test_drawing_round2.cpp` (plus the updated tapped-hole order in
`test_drawing_callouts.cpp`).

| id | severity | title | status |
|---|---|---|---|
| DRW2-VIEWS-ANNOT-1 | high | Canvas shows every change 5 to 40 s late; stale band after a sheet switch | fixed (driver vsync off at start-up; full repaint on sheet switch) |
| DRW2-VIEWS-ANNOT-2 | medium | Scale 2:1 pushes views off the sheet, no warning | partly fixed: warning in the card and the status bar; a family that cannot fit still extends past the frame (by design, round 1) |
| DRW2-VIEWS-ANNOT-3 | medium | Third angle moves the left projected view off the sheet | fixed |
| DRW2-VIEWS-ANNOT-4 | medium | Fit typed while Tolerance is None is ignored | fixed |
| DRW2-VIEWS-ANNOT-5 | medium | Shaft fit `g6` printed as `G6` | fixed |
| DRW2-VIEWS-ANNOT-6 | low | Leader starts inside its note text | fixed |
| DRW2-VIEWS-ANNOT-7 | low | Parts List default position over the A-A label and view | fixed |
| DRW2-VIEWS-ANNOT-8 | low | Several commands only in the command search | fixed (Break View, Edit Sheet, Ordinate, Baseline on the ribbon; the rest in the group caption menus) |
| DRW2-VIEWS-ANNOT-9 | low | Cards clip their content on the right | partly fixed (long rows wrap, shorter Edit View check box text) |
| DRW2-VIEWS-ANNOT-10 | low | Combo popups cut to 1.5 to 7 rows | not fixed (the popups are limited by the 490 px display; revision row combo shows 12) |
| DRW2-VIEWS-ANNOT-11 | low | Legacy sheet: vertical 60/42 texts and A-A label overprint | deferred (vertical chain case of round-1 VIEWS-ANNOT-4) |
| DRW2-VIEWS-ANNOT-12 | low | Aligned dimension text on its own extension line | fixed |
| DRW2-VIEWS-ANNOT-13 | low | Tapped-hole callout lists the drill first | fixed |
| DRW2-VIEWS-ANNOT-14 | low | Sheets panel unstyled and unlabelled | fixed |
| DRW2-VIEWS-ANNOT-15 | low | Tool survives a sheet switch; zoom and pan reset | fixed; `New Drawing` adding a sheet is kept (one drawing per document) |
| DRW2-ASM-OUTPUT-1 | medium | Scale combo auto-completes `1:1` to `1:100` | fixed |
| DRW2-ASM-OUTPUT-2 | medium | A4 portrait: view on the frame, balloon outside, revision table over balloon 2 | partly fixed: balloons kept in the frame, a revision table over a view or balloon moves to the clear top corner, the warning tells when views do not fit |
| DRW2-ASM-OUTPUT-3 | low | Second Parts List on top of the first | fixed (placed clear, with a message) |
| DRW2-ASM-OUTPUT-4 | low | Double-click on revision row A or B edits row C | fixed |
| DRW2-ASM-OUTPUT-5 | low | PDF and canvas print text in capitals; `APPROVED` overflows | fixed |
| DRW2-ASM-OUTPUT-6 | low | Title block Date is the render date | fixed for new sheets (the date is stored); older files keep the fallback until a Date is entered |
| DRW2-ASM-OUTPUT-7 | low | BOM CSV Part Number empty | partly fixed (falls back to the name, as the parts list); Material and Mass per component stay deferred (round-1 ASM-OUTPUT-11) |
| DRW2-ASM-OUTPUT-8 | low | Export dialogs open in the working directory | fixed |
| DRW2-ASM-OUTPUT-9 | low | Raw internal names; PDF Author empty | fixed |

## What changed, per issue (round 2)

* **VIEWS-ANNOT-1**: `setupGlPlatform` also sets `vblank_mode=0` (Mesa) and `__GL_SYNC_TO_VBLANK=0`
  (NVIDIA) before any GL context exists, unless the user set them or `FABRICAD_VSYNC=1`. The testers
  found that `vblank_mode=0` cured the lag; the requested swap interval alone did not reach every
  context. A sheet switch repaints the whole viewport.
* **VIEWS-ANNOT-2**: `DrawingToolBase::warnIfOutsideFrame` runs on every live Edit View change, after
  Edit View OK and after Edit Sheet OK (the old check only ran on OK and was hidden by the card closing).
* **VIEWS-ANNOT-3**: `fitFamilyInFrame` moved nothing when the block (projected and aligned views) was
  wider than the frame. Then the projected views alone form the block and aligned sections are fitted
  like free views.
* **VIEWS-ANNOT-4**: typing in FIT while Tolerance is None selects Fit (the combo used to put None back).
* **VIEWS-ANNOT-5, ASM-OUTPUT-5**: the drawing fonts set `MixedCase` explicitly. They inherited the
  app font's `AllUppercase` from the canvas widget and the PDF writer, while the text was measured in
  mixed case (hence the overflow). Text keeps the typed case in every output.
* **VIEWS-ANNOT-6**: the leader starts 0.3 text heights outside the widest line, measured with the
  real font.
* **VIEWS-ANNOT-7, ASM-OUTPUT-3**: Enter places the parts list with `PartsListTool::clearPosition`:
  above the title block when clear of views, tables and other lists, else the nearest clear spot up the
  right, then the left frame edge. A second list for the same view says so.
* **VIEWS-ANNOT-8**: Break View, Edit Sheet, Ordinate and Baseline Dimension are promoted.
* **VIEWS-ANNOT-9**: drawing card forms use `newForm` (wrap long rows).
* **VIEWS-ANNOT-12**: text placed beyond an extension line goes outside it (half the text plus an
  arrow length) on an extended dimension line, instead of being clamped onto the line.
* **VIEWS-ANNOT-13**: a plain hole with thread data reads thread first, then the tap drill.
* **VIEWS-ANNOT-14**: themed sheet rows, labelled `+ New` / `- Delete` panel buttons.
* **VIEWS-ANNOT-15**: a sheet tab or list click cancels a running drawing command; each sheet keeps
  its zoom and pan.
* **ASM-OUTPUT-1**: the scale combo has no completer (and no insert policy).
* **ASM-OUTPUT-2**: `changeSheetFormat` clamps balloons into the new frame and moves a revision table
  that lies over a view or balloon to the clear top corner.
* **ASM-OUTPUT-4**: the canvas double-click passes `--row=<n>` for the row under the pointer.
* **ASM-OUTPUT-6**: `addSheet` stores today's date in the title block.
* **ASM-OUTPUT-7**: `bomToCsv` writes the name when the part number is empty.
* **ASM-OUTPUT-8**: export dialogs start in the last export folder, else the document's folder, else home.
* **ASM-OUTPUT-9**: orientation and title block labels are spaced (`Iso Top Right`, `Part Number`);
  the PDF Author is the Drawn By field.
