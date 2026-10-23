# Production-features test (tester run, display :98, mouse and keyboard only)

Build: main @ f910908 (`build/bin/fabricad`). Scope: holes by standard, pocket holes, Insert Fastener, BOM, mass
properties and materials, typed sketch values, drawing title block / hole table, and the new exports. Exported
files were inspected with independent readers (ezdxf, trimesh, a STEP/IGES text parse). Files are in `exports/`.

Note on evidence: an accidental clean-up deleted the first set of screenshots. The 12 screenshots kept here were
re-captured in a second pass; some findings below (typed sketch values, STEP/3MF/glTF/OBJ/IGES export dialogs) are
therefore backed by the exported files and by the log, not by a screenshot.

## What worked (production view)

| Area | Result |
|---|---|
| Hole card, ISO | Clearance M6 normal = 6.6 mm, counterbore for M6 SHCS = 11 x 6 mm. STEP cylinders confirm radii 3.3 and 5.5. Correct. |
| Hole card, ANSI | 1/4-20 tapped = 5.105 mm (#7 drill), cylinder radius 2.5527 in the STEP. Correct. |
| Pocket hole | 19 mm board, Kreg 15, 1 1/4" screw, step 9.5, pilot 3.6, 2 holes per edge. Result cuts the rail; the preview is clear. Usable once the thickness is typed (PRODUCTION-3). |
| Insert Fastener | M6x10 8.8 SHCS seats in the counterbore, joint created, identical fasteners merge to one BOM row with quantity. Size from a 6.6 hole = M6 is right. |
| Mass properties | Aluminum 6061: 46627.1 mm3 x 2.70 = 125.893 g (matches); PETG custom (1.27) 10498.4 mm3 = 13.333 g (matches). Inertia, principal axes in the Markdown / CSV report are plausible. |
| Custom material | PETG saved to `materials.json` with category, standard, yield, cost; the picker filter on category ("plastic") works. |
| BOM | Flat and structured, selectable columns, part number / revision / vendor / cost / mass / total mass all correct. |
| Typed sketch values | Typing 50 Tab 30 in the rectangle gives two dimension constraints and "fully constrained"; dragging then does nothing, editing the dimension to 60 resizes. Circle diameter typed gives a diameter dimension. Works as in Fusion. |
| Title block tokens | `${vendor}`, `${description}`, `${mass:g,1}` resolve; mass follows material and fasteners (0.135 kg). Part number and revision fill from the component. |
| Face DXF | R2000, `$INSUNITS` 4 (mm), 60 x 30 plate: OUTER closed LWPOLYLINE 0..60 x 0..30, INNER CIRCLE r=4 at (25,15). 1:1, origin lower-left. Audit: 0 errors. Correct for laser / waterjet. |
| 3MF | Unit millimetre, two objects with names and colours, components in build items, both watertight, volumes 46629.4 and 687.8 mm3 (exact 46627.1: tessellation). Slicer-ready. |
| glTF (.glb) | Metres, Y-up (80 x 12 x 50 mm plate gives 0.08 x 0.012 x 0.05), node per component with the fastener placed by translation, two materials. Valid per pygltflib. |
| OBJ + MTL | Millimetres, groups per body "Part/Body", materials per body; watertight at 1e-3 vertex merge. |
| STEP | AP214, `SI_UNIT(.MILLI.,.METRE.)`, product id = part number ("BRK-0001"), name and description written; assembly export has the nested products. Dimensions match. |
| IGES | Units MM, entities 186/514 (BRep), 858 lines. Not re-read by an independent reader (none available here). |

## Issues

### PRODUCTION-1 (high) Insert Fastener Auto picks the wrong size for an inch tapped hole
Repro: Hole card > ANSI INCH > Tapped > 1/4-20, apply (hole 5.105 mm). Insert Fastener, select the hole wall, leave Size = Auto.
Expected: ASME B18.3 1/4-20 (the tap drill 5.105 mm belongs to 1/4-20; screw major diameter 6.35).
Actual: "1 x ASME B18.3 #8-32 x 1/2 ... (hole o5.11)" is offered and inserted. A #8 screw (4.17 mm major diameter) would
rattle in a 1/4-20 tapped hole, and the BOM then orders the wrong fastener. Choosing 1/4 by hand works.
Screenshot: `04-fastener-auto-tapped-hole.png`. The size match appears to choose by nearest diameter against the screw
table (5.11 is within a tolerance of the #8 clearance drill) instead of reading the Hole feature's thread size.

### PRODUCTION-2 (high) Hole table drops the tapped hole when a fastener sits in it
Repro: plate with Hole1 (M6 clearance + counterbore, SHCS inserted) and Hole2 (1/4-20 tapped, 10 deep, SHCS inserted),
new drawing, base view TOP, HOLETABLE.
Expected: two rows (A1 o6.6 cbore, B1 1/4-20 - 2B).
Actual: "Hole table: 1 hole(s)", only A1 listed (`10-drawing-hole-table.png`). In an earlier run with the same two holes
and a fastener in each (both rows present, B1 `1/4-20 - 2B DEEP 10 / o5.105 DEEP 10`) it showed 2 holes, so the
loss is state dependent (this run: fasteners inserted after both holes, drawing created afterwards). A
production drawing that silently omits a threaded hole is a scrap risk.

### PRODUCTION-3 (medium) Pocket Hole "Measure thickness" fails on a plain parallel board
Repro: board 200 x 60 x 19 (Extrude from a sketch), second body (stile) touching its end. CREATE > Pocket Hole, entry
face = the 200 x 60 underside (Body1), exit edge = the end edge at the stile.
Expected: thickness 19 measured from the opposite face (the preview and Apply enabled).
Actual: red "No face opposite the entry face: type the thickness", Apply disabled; I had to untick Measure thickness and
type 19 (`12-pocket-measure-thickness-error.png`, result `13-pocket-hole-result.png`). Reproduced twice. The
default workflow of the feature (measure from the model) is therefore broken for a simple board next to a touching
body. Also: the exit-edge picker accepted an edge of the other body (Body2) without a message and only failed at
Apply time.

### PRODUCTION-4 (medium) A freshly inserted standard fastener reports interference with its own hole
Repro: through hole with counterbore for M6 SHCS (Hole card, Clearance M6 normal, Counterbore, Fastener SHCS), Insert
Fastener M6x10 8.8, INSPECT > Interference > Compute.
Expected: no interference (head 10 in o11 counterbore, shank 6.0 in o6.6).
Actual: "2 interfering pair(s), total 83.8 mm3": Body1 vs ISO 4762 M6x10 = 56.55 mm3 (86.4 mm3 in another run), pink
overlay at the head (`06-interference.png`). The second pair (#8-32 in the 5.1 mm tapped hole, 27.3 mm3) is expected
for threads, but there is no way to filter threaded fasteners out of the report, so real clashes are buried.
Probably the head sits about 0.7 mm into the counterbore floor (cbore depth 6.0 = head height 6.0, no
clearance on the head seat). Not verified with a section view.

### PRODUCTION-5 (medium) Auto length for a blind hole rounds up past the hole bottom
Repro: Hole 1/4-20 tapped, depth 10 mm, Insert Fastener 1/4 with Length = Auto.
Expected: length no longer than the hole depth + head.
Actual: 1/4-20 x 1/2" (12.7 mm) is chosen (the Fastener feature list shows 12.7 mm), longer than the 10 mm hole, so
the tip penetrates the hole bottom. Rounding up is right for through holes (so a nut engages), wrong for blind holes.

### PRODUCTION-6 (low) Component Properties card lacks the mass promised in the guide
USER_GUIDE section 8: "the card also shows the computed mass". The card (`09-component-properties.png`) has only the
six fields. Mass is available through BOM and Mass Properties, so this is a doc / feature mismatch. The Material (BOM)
field is also blank for a body that has Aluminum 6061 assigned, so it cannot be seen that the BOM falls back to the
body material.

### PRODUCTION-7 (low) Long title-block text overflows the box
Edit Title Block > Title = `Mounting plate - ${vendor} - ${description}` renders past the title cell and over the
sheet border (`11-title-block-tokens.png`). No clipping or shrink-to-fit. Tokens expand to the long form, so users
will hit this.

### PRODUCTION-8 (low) Library gaps for the stated workflows
Built-in materials have no PETG, nylon-CF, PA12 or MJF resin, and woods are only oak, pine, plywood and MDF
(no maple, walnut, birch ply by thickness). Custom materials work, but 3D-printing users will define PETG first.
Also the BOM sums Cost total only for rows that have a cost: fasteners have no price, so the total is understated
without a warning (`bomB.csv`: Cost total 12.50 with 3 rows).

### PRODUCTION-9 (low) Typed sketch dimension labels collide
Typing a line length and angle (40, Tab, 30) leaves "40" and "30 deg" printed over each other, and after editing a
rectangle dimension the other dimension line stays at its old offset (it measures the old position). Cosmetic.

### PRODUCTION-10 (low, usability) Hole card with a preselected face creates two placement points
Select the top face, press H, then click on the face: card says "Placement points: 2 faces" and the feature list shows
`o6.6 x 2`. Clicking without a preselection gives one hole. Expected one hole per click.

### Not reproduced (dropped)
A first drawing showed an empty Part number and `${vendor}` resolved to nothing although the component had both set; a
second drawing from the same data filled both correctly. Possibly the drawing was created before the component
properties were committed; mention only for completeness.

## Not tested
Inch documents for DXF (`units=in`), `schema=ap242` write with material (only AP214 files inspected), IGES `faces`
mode, pocket holes into a mating part (`Also drill the mating part`), Castle jig, tapped modelled threads.
