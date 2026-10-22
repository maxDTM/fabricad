# Drawing round 1: assembly drawings and output (DRW-ASM-OUTPUT)

Tester: mechanical drafter, mouse and keyboard only on private display :72. Artifacts in `asm-output/`.
Env note: the Xwayland window is tiled by the host compositor and the app needs about 1900 px width; I floated/resized the host window to 2000x1050 (otherwise the ribbon and dialogs are clipped).

## Model used
Three components built by hand: Plate (80x60x10, Ø6.6 hole), Block (30x20x15), Pin (Ø8x12); Pin copy/pasted once (BOM qty 2).
Drawing: A3 ISO first angle, sheet scale 1:2, views at 1:1 (top, projected rear, front), iso on sheet 2.

## What I tested and what worked
- New drawing dialog, base view (front/top/iso, visible / visible+hidden), projected view, balloons (3), linear/diameter dimensions: values correct (80, 60, Ø6.6, 30; after model edit 100).
- Parts list: Item/Qty/Part Number from components, Pin qty follows the pasted occurrence (2). BOM CSV export OK.
- Title block dialog: all fields incl. Company; text with `& < > "` is preserved in PDF, SVG (escaped, well-formed XML) and DXF. Clearing Rev makes the block follow the revision table (C).
- Revision table: Add Revision x3 (A, B, C), shown in PDF/SVG/DXF.
- Multiple sheets: Sheet N / M renumbering (1/2, 2/2), per-sheet export of SVG/DXF, PDF has one page per sheet with its own page size (A3 + A4 verified).
- PDF: scale exact (100 mm = 394 px at 100 dpi), embedded fonts, selectable text, hidden lines short-dashed, line weights 0.5 visible / 0.25 hidden / 0.18 dimension.
- DXF (R12): parses; layers VISIBLE/HIDDEN/DIM/BORDER/TEXT, HIDDEN linetype 3/-1.5, geometry in paper mm matches 1:1 views; `%%c` diameter text.
- Re-export after model edit (plate width 80 to 100): PDF/SVG/DXF all show the new size, dimensions follow.

## Defects
See the list below (same as returned to the workflow).

### DRW-ASM-OUTPUT-1 (Medium) SVG hidden lines render effectively solid
Repro: sheet with a Visible-and-Hidden view; Export > Output SVG; open/render (rsvg-convert). Expected: dashes comparable to PDF (about 3 mm / 1.5 mm). Actual: `stroke-dasharray="12,6"` inside a group scaled to mm, so dash is 12 mm / gap 6 mm; hole/pin hidden lines of 10-20 mm look solid, small hidden circles show as a single arc. Evidence: svg-v1-hidden-lines-solid.png vs pdf-v1-1.png, asm-v1.svg lines 81, 109.

### DRW-ASM-OUTPUT-2 (Medium) Parts list falls off the page after changing sheet size; view crosses frame
Repro: sheet with views + parts list on A3 landscape; Modify > Edit Sheet > A4, Portrait, OK; export PDF. Expected: table re-snapped above title block, views kept inside frame. Actual: table stays at old absolute position, entirely outside the A4 sheet, missing from PDF page 2; iso view touches/crosses the left frame line. Evidence: 03-a4-sheet2-bom-offpage.png, pdf-v3-2.png.

### DRW-ASM-OUTPUT-3 (Medium) Balloon leader tips are not associative
Repro: balloon on plate left edge (view top), then change plate sketch width 80 to 100 and return to the drawing. Expected: tip stays on the edge (or balloon turns orphaned). Actual: view re-centres, tip stays at the old absolute point and floats inside the plate; balloon 3 now lands on the block edge instead of the pin. Evidence: pdf-v2-1.png vs pdf-v1-1.png.

### DRW-ASM-OUTPUT-4 (Low) Hidden edges coincident with visible edges are emitted
Repro: export DXF of the top view. Actual: 13 of 33 HIDDEN lines exactly coincide with VISIBLE lines; through-hole circle emitted on VISIBLE and again on HIDDEN; pin hidden circle emitted twice. Expected: hidden segments hidden behind/under visible ones removed. Effect: grey/dashed overdraw on outlines in the app view (plate outline looks dashed) and DXF viewers, double plotting.

### DRW-ASM-OUTPUT-5 (Medium) No way found to edit revision rows (description, approved)
Repro: Tables > Add Revision; double-click or right-click the revision table; Modify menu has no revision item. Actual: double-click opens Edit Sheet; Description/Approved cells stay empty in all outputs; date is fixed to today. Expected: edit dialog for each revision.

### DRW-ASM-OUTPUT-6 (Low) Title block data is per sheet only
Repro: fill title block on Sheet1, add Sheet2. Actual: Sheet2 shows Title "Untitled", empty company/drawn by/part number/material (also in PDF page 2). Expected: drawing-level fields shared or copied.

### DRW-ASM-OUTPUT-7 (Low) New-sheet title block scale ignores chosen scale
Repro: New Drawing with Scale 1:2 -> title block "Scale" shows 1:1 until the first view is placed (then 1:2). Also the title-block scale stays at the sheet scale (1:2) while views were placed 1:1 on the same sheet, so the block disagrees with the view.

### DRW-ASM-OUTPUT-8 (Low) SVG/DXF export only the active sheet, silently
Repro: two sheets, export SVG/DXF. Actual: only the active sheet is written, default file name `Untitled.*` has no sheet name, no message; PDF writes all sheets. Expected: sheet name in default file name or prompt/option.

### DRW-ASM-OUTPUT-9 (Low) DXF R12 limitations
No lineweights (visible/hidden/dimension weights are lost, only colours), `$EXTMAX` missing (only `$EXTMIN 0,0,0`), `$INSUNITS` in an AC1009 header, dimensions as exploded lines + TEXT (not associative), balloon circles/text on layer TEXT.

### DRW-ASM-OUTPUT-10 (Low) PDF document title ignores title-block Title
PDF Title metadata is "Untitled" while the title block says "Plate & Block <Assy> "A1"" (SVG `<title>` also "Untitled - Sheet1").

### DRW-ASM-OUTPUT-11 (Low) BOM Mass/Material columns always empty
BOM CSV has Mass and Material columns, parts list shows Material, all empty even though the solids have volume and the title block has a material; no way to set per-part description/material seen in the Drawing workspace.

### Observation outside this area (unverified)
Block and Pin sketches were started by clicking the plate's top face in Top view but ended up at z=0 (block spans z 0..15, overlapping the plate 0..10) instead of z=10. Not investigated.
