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
  side of its parent, keeping what it shows. Attached annotations follow.
* **VIEWS-ANNOT-8**: on a scale edit (`viewDefinitionChanged`), projected children and sections
  drawn at the parent scale take the new scale. Their offsets scale with it, and the auto layout
  then restores the gaps, clears other views, keeps the views in the frame and off the title
  block.
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

Outside the drawing area: the three design-workspace observations in views-annot.md, and the
asm-output tester's unverified note about sketches started on the plate's top face ending up
at z = 0. Both are listed in backend-deferred.md for the design/app owners; none was
investigated here.
