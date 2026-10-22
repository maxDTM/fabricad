# Backend work deferred to the UI branches

Items found while fixing non-UI open issues whose fix lies in paths owned by the UI redesign
(`src/app/**`, `src/viewer/**`, `resources/**`, `src/main.cpp`). Each section names the place
to change and what the backend already provides. Append new sections at the end.

## be/drawing (2026-10-22): drawing round 1

Status table: `docs/tests/drawing/ISSUES.md`. These items were not fixed on `be/drawing`.

### DRW-VIEWS-ANNOT-13: hole tool notches material above the placement face (src/part)
- Root cause (verified on the tester's model): a through-all Hole (`extentType = All`) builds its
  tool with `topExtension = 1.0` (`src/part/features/Hole.cpp`, `HoleFeature::compute`). For a
  counterbore or countersink, the extension has the counterbore/countersink radius. On the repro
  model the Ø11 counterbore at (0, -20) overlaps the Ø30 boss by 0.5 mm, so the tool cuts a
  1 mm-high, about 4 mm-wide notch into the boss above the plate face. The drawing shows that
  notch faithfully: it is the "1 mm rectangle" in the front view, and its edges are the
  "seam-like" tangent line. It also made the counterbore cylinder face 7 mm long, which is why
  the hole callout read ↓7. The drawing now takes the depth from the feature (VIEWS-ANNOT-2).
- Fix: build the part of the extension above z = 0 at the drill radius only. Profile: top
  (0, ext) → (r, ext) → (r, 0) → (cr, 0) → (cr, -cboreDepth) …, or subtract the ring
  r..cr × 0..ext from the tool. Not done here because it adds two faces to the proto hole,
  which renumbers the `Hole<n>/H<k>/F<i>` face names that saved documents reference. That
  needs a naming migration, or new faces appended after the existing ones. Also update the
  `test_dressup.cpp` volume check for `topExtension`.

### DRW-VIEWS-ANNOT-16 (rest): symbol pickers for datum, surface texture, hole callout
- Done in `src/drawing`: Edit Annotation shows a GD&T characteristic picker plus tolerance, Ø,
  modifier and datum fields for feature control frames, and a per-row form for revision tables.
- Open (UI design): a symbol palette for surface texture (process, lay symbols) and the hole and
  thread callout options. All of them work through `EditCalloutTool::applySetting` keys
  (`src/drawing/tools/CalloutTools.cpp`), so a dialog only needs to call those.

### DRW-VIEWS-ANNOT-18: stale band after wheel zoom
- Not reproduced in-process. `DrawingCanvas` already uses
  `QGraphicsView::FullViewportUpdate`, and a wheel zoom re-renders the whole viewport. The band
  was the top ~40 px of the canvas. If it shows up again under the new shell, check whether an
  overlay or toolbar widget of the main window (`src/app/MainWindow.cpp`, `layoutOverlays`)
  covers the canvas top without repainting, rather than the canvas itself.

### DRW-ASM-OUTPUT-9 (rest): DXF lineweights
- R12 (AC1009) has no lineweights, so visible, hidden and dimension lines differ only by layer
  and colour. Done: `$EXTMAX`/`$LIMMAX` written, `$INSUNITS` dropped (not valid in AC1009).
  Real lineweights (group 370) need an AC1015 (R2000) writer with handles, an OBJECTS section
  and so on. That is a larger `DxfWriter` change, not done in this round.

### DRW-ASM-OUTPUT-11: BOM Mass / Material empty
- The BOM already fills Material and Mass from a body's physical material and density
  (`buildBom` in `src/drawing/Annotations.cpp`). The tester's bodies had none set.
- UI: the only way to set a material is Browser > body > Physical Material
  (`src/app/BrowserTree.cpp`). The drawing workspace has no entry point, and the parts list has
  no per-row override. Suggest a "Physical Material" item in the drawing's parts-list context
  menu, or a hint in the Parts List tool prompt.

### Design workspace observations from the drawing testers (not investigated)
- `SELECT body:Body1` + `CFB` (component from body) then `NEWCOMPONENT Pin` leaves component
  `Body1` empty and puts the body into `Pin`.
- The first Extrude in a new empty component defaults to Join (docs say New Body).
- `EDITFEATURE <name>` needs the feature selected first, although SCRIPTING.md shows it taking
  the name.
- asm-output tester, unverified: Block and Pin sketches started by clicking the plate's top face
  ended up at z = 0.
