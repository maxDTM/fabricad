# Drawing round 2: assembly drawings and output (DRW2-ASM-OUTPUT)

Tester: mechanical drafter, real X input only (xdotool mouse and keyboard) on private display :94, build `build/bin/fabricad`
(main checkout, built 2026-10-22 22:02, with the round-1 drawing fixes). The window was 1900x490 (host-tiled Xwayland), scale 0.8.
Model: the round-1 repro assembly (`round1/views-annot/repro-model-asm-drw.fcad`: component Plate with boss, holes and fillets, component Pin),
copied to the scratchpad and opened with Ctrl+O; nothing was saved. Exports went to the scratchpad and were inspected there with
`pdftoppm` / `pdftotext` / `pdffonts`, `rsvg-convert`, `xmllint`, and a small python parser for the DXF (plus a throw-away DXF to SVG
renderer to look at it). Screenshots and the exported files are in `docs/tests/drawing/round2/asm-output/`.

Env note: with `vblank_mode` unset the canvas showed every step 5 to 40 s late (known: DRW2-VIEWS-ANNOT-1). After restarting the app
with `vblank_mode=0` in the environment it was usable (still a one-step lag now and then in dialogs).

## Issues

| id | sev | title |
|---|---|---|
| DRW2-ASM-OUTPUT-1 | medium | Typed scale in a scale combo is auto-completed to a longer scale (`1:1` becomes `1:100`, `1:2` becomes `1:20`) and Enter accepts it |
| DRW2-ASM-OUTPUT-2 | medium | Edit Sheet to A4 portrait: view touches the frame, balloon 1 leaves the frame, revision table overprints balloon 2 and the pin |
| DRW2-ASM-OUTPUT-3 | low | A second Parts List on the same sheet is placed exactly on top of the first and prints twice |
| DRW2-ASM-OUTPUT-4 | low | Double-click on revision row A or B opens the form for the last revision (C) |
| DRW2-ASM-OUTPUT-5 | low | PDF prints every text in capitals, SVG and DXF keep the typed case; the capital `APPROVED` header overflows its column and the frame |
| DRW2-ASM-OUTPUT-6 | low | Title block Date is "today at render time": a re-export on another day changes the date of an old drawing |
| DRW2-ASM-OUTPUT-7 | low | BOM CSV Part Number column is empty while the parts list shows the name there; Material and Mass stay empty although the title block has them |
| DRW2-ASM-OUTPUT-8 | low | PDF/SVG/DXF export dialogs open in the working directory (project root), not the document's folder or the last export folder |
| DRW2-ASM-OUTPUT-9 | low | Raw internal names in the UI: base view orientation `ISOTOPRIGHT`/`ISOTOPLEFT`, title block fields `PARTNUMBER`, `DRAWNBY`; PDF Author metadata empty |

### DRW2-ASM-OUTPUT-1 (medium) scale combo autocompletes to the wrong scale
Repro: Drawing workspace, command search `new drawing`; click the Scale field, Ctrl+A, type `1:2`, press Enter. Same in Edit View: select a view,
Edit View, click Scale, Ctrl+A, type `1:1`.
Expected: the scale I typed (1:2 / 1:1).
Actual: the field shows `1:2` plus a selected completion `0` (`1:20`); for `1:1` it shows `1:1` plus selected `00`. The first Enter only accepts the
completion (field becomes `1:20` / `1:100`, the dialog stays open, nothing visible happens), the second Enter applies it. Edit View then applied 1:100
(the iso view shrank to a dot and the title block scale read 1:100). Backspace/Delete after typing removes the completion and Enter then works, but nothing
tells the drafter. A wrong scale on a drawing is silent damage.
Evidence: `04-new-drawing-scale-autocompletes-1to20.png`, `05-scale-typed-1to1-completes-1to100-selected.png`, `06-after-enter-scale-1to100.png`.

### DRW2-ASM-OUTPUT-2 (medium) Edit Sheet to a smaller sheet does not re-fit the content (round-1 ASM-OUTPUT-2 only partly holds)
Repro: Sheet2 (A3 landscape: three views at 1:1, parts list above the title block, balloons 1 and 2, revision table top right, title block).
Command search `edit sheet`, Sheet size A4, Orientation Portrait, Apply, then PDF.
Expected: everything inside the frame, nothing overprinting (round-1 fix claims "refitted inside the frame").
Actual (PDF page 2, A4 595x842 pt): the parts list and title block re-snap fine, but (a) the right-hand projected view ends exactly on the frame line, (b) balloon 1 is
outside the frame (in the zone margin), (c) the revision table sits over balloon 2 and its leader and over the top of the pin of the front view
(`REV A`/`FIRST RELEASE` row struck by the leader). Ctrl+Z restores A3 cleanly.
Evidence: `03-a4-portrait-sheet2-overlaps.png` (PDF page 2 at 100 dpi).

### DRW2-ASM-OUTPUT-3 (low) duplicate Parts List
Repro: Sheet3 with a view; click PARTS LIST, Enter (default position); later click PARTS LIST, Enter again.
Expected: a warning, or the existing table is moved/replaced (one BOM per sheet).
Actual: a second identical table is placed at exactly the same position; PDF prints both (bold, doubled glyphs in header and rows), no message.
Evidence: `07-sheet3-parts-list-twice-overprinted.png`.

### DRW2-ASM-OUTPUT-4 (low) revision row hit test
Repro: revision table with rows A, B, C (Add Revision x3). Double-click the description cell of row A (or B).
Expected: the Edit Annotation form opens on that row.
Actual: the form always opens with `REVISION` = C (the last row); the row must be changed with the combo, whose popup shows only 2 rows (B, C)
and needs the keyboard (Up) to reach A. The form itself works: description/approved edits land in the right row in the canvas, PDF, SVG and DXF.
Evidence: `08-revision-row-A-click-opens-row-C.png`.

### DRW2-ASM-OUTPUT-5 (low) text case differs by output; header overflow in the PDF
Repro: title block Company `Acme Machines`, Title `Flange Assy & Pin <"A">`, revision rows `First release`; export PDF, SVG, DXF.
Expected: the same text in all outputs (or an explicit "caps" setting).
Actual: PDF page and the canvas show `ACME MACHINES`, `FIRST RELEASE`, `PLATE`; the SVG and the DXF keep `Acme Machines`, `First release`, `Plate`.
In the PDF the bold capital header `APPROVED` of the revision table is wider than its column and runs over the right frame line (`01-pdf-sheet2-first-export.png`);
in the SVG it fits (`02-svg-sheet2-render-mixed-case.png`).

### DRW2-ASM-OUTPUT-6 (low) title block date
Repro: Date is read-only (greyed) in Edit Title Block. Export a PDF before midnight, again after: sheets 1-3 were exported at 23:5x (all `2026-10-22`),
re-exported at 00:0x: page 1 and page 3 print `2026-10-23`, page 2 prints `2026-10-22` three times (revision rows keep their own dates) next to `2026-10-23`.
Expected: the drawing date is stored (or editable) so a released drawing does not change when re-plotted.
Evidence: `09-sheet3-2to1-date-rolled-to-10-23.png`, `12-pdf-sheet1-title-block-copied.png` (10-22 version).

### DRW2-ASM-OUTPUT-7 (low) BOM CSV vs parts list (related to the deferred round-1 item 11)
Repro: `Export BOM (CSV)` from the command search on the two-component assembly.
Actual: `Item,Name,Part Number,Description,Material,Qty,Mass [g]` / `1,Plate,,,,1,` / `2,Pin,,,,1,`. The on-sheet parts list prints PLATE/PIN in its PART NUMBER column, CSV leaves Part Number empty and puts the name in Name.
Material and Mass are empty in both although the title block was filled (`Al 6061`, `1.2 kg`). No way found in the Drawing workspace to set part number/description/material per component.
Evidence: `asm-v2-bom.csv`.

### DRW2-ASM-OUTPUT-8 (low) export dialog directory
Repro: any PDF / SVG / DXF export, also a second one after exporting elsewhere. The dialog opens in `<repo>` (the process cwd) with `asm.pdf`; the document lives elsewhere.
Expected: the document's folder, or the last export folder. Risk: a quick Enter writes the file into a source tree.
Evidence: `10-export-dialog-default-dir-project-root.png`.

### DRW2-ASM-OUTPUT-9 (low) raw names
Base View/Edit View orientation list: `FRONT, TOP, ... ISOTOPRIGHT, ISOTOPLEFT, ISOBOTTOMRIGHT, ISOBOTTOMLEFT` (enum names, no spaces or caps style).
Edit Title Block labels `PARTNUMBER`, `DRAWNBY` (SVG shows `Part number`, `Drawn by`). PDF metadata Author is empty although Drawn By is filled (Title is correct).

## What worked (and the round-1 fixes that still hold)

Checked by inspecting the exported files:
- New Drawing (A3, ISO first angle), Base View (Front, visible and hidden), Projected View (top below, left-hand view to the right: first angle correct), Parts List default position above the title block, Balloon on plate and pin (items 1 and 2 match the list), Edit Title Block, Add Revision x3, Edit Annotation forms for revision rows, New Drawing for a third sheet, Edit View scale.
- PDF: 3 pages in one file, each with its own page size (A3, A3; A4 after the edit), embedded DejaVu fonts, selectable text, Title metadata = title block Title (round-1 ASM-OUTPUT-10 holds); `& < > "` preserved in PDF, SVG (well-formed XML, `xmllint` ok) and DXF.
- Scale: plate 100 mm measures 99.8-100.1 mm in the PDF at 100 dpi (1:1); height edit 24 to 30 mm appears as 30.0 mm (118 px); iso view 1:1 is 108.17 x 86.94 mm in the DXF, after Edit View 2:1 it is 216.33 mm wide: ratio 2.0000004. The title block scale follows the first base view (1:2 sheet with a 1:1 view prints 1:1, then 2:1; round-1 ASM-OUTPUT-7 holds).
- Re-export after a model edit (Plate Extrude1 24 to 30 in the Part tab, then back to the drawing): views, section A-A, the `24` dimension (now `30`) and the parts list followed; balloon 1 and 2 tips stayed on their edges (round-1 ASM-OUTPUT-3 holds); the exported PDF shows it.
- Title block shared across sheets: fields typed on Sheet2 appear on Sheet1 and on a new Sheet3, Scale/Sheet/Size stay per sheet (ASM-OUTPUT-6 holds); Rev follows the revision table (C) on its sheet.
- Revision form edits per row (ASM-OUTPUT-5 holds), Description/Approved print in all three formats.
- SVG: `stroke-dasharray="3,1.5"` for hidden lines (ASM-OUTPUT-1 holds), `<title>` = `Flange Assy & Pin <"A"> - Sheet2`. File name suggests the sheet (`asm-Sheet2.svg`), status bar says `Exported DXF to ... (sheet Sheet2 only; activate another sheet to export it)` (ASM-OUTPUT-8 holds).
- DXF (R12): layers VISIBLE/HIDDEN/DIM/BORDER/TEXT, `$EXTMIN 0,0 / $EXTMAX 420,297 / $LIMMAX`, geometry in sheet mm matches the PDF. No hidden line coincides with a visible line (0 of 57; ASM-OUTPUT-4 holds). Iso curves are POLYLINE/VERTEX on layer VISIBLE. Dimensions are exploded (LINE/SOLID/TEXT), balloons on DIM/TEXT.
- Edit Sheet A3 to A4 portrait then Ctrl+Z: restored exactly. Parts list re-snaps above the title block on A4 (that half of ASM-OUTPUT-2 holds).

Not tested: component copy (qty 2 in the BOM; selecting a component in the cramped 490 px high tree was not practical), removing a sheet, Section views on sheet 2, ANSI/third angle (other tester).
