# Plan: production features (holes, fasteners, mass, sketch intent, interop)

Status: plan, no code (2026-10-23). Follows `feature-gaps.md` step 6 ("everyday quick wins") but picks by what a
designer modelling parts for **prototyping and production** cannot do today, not by vendor parity. Each item says
which job is blocked without it. Three packages P1, P2, P3 run in parallel worktrees with disjoint file ownership;
§6 lists what is deferred and why.

**Sources.** `feature-gaps.md` §1, §2, §5, §7 and §10; `docs/comparison/COMPARISON.md` rec. 5; code on main
@ 8422cee: `src/part/features/Hole.{h,cpp}` (ISO 273 clearance, ISO 261 tapped, cbore/csink typed by hand),
`Thread.{h,cpp}` + `part/ExtraUtils.h` (`IsoThread`, `isoMetricDesignations()`), `src/core/Materials.{h,cpp}`
(26 materials, density + colour), `src/assembly/Bom.{h,cpp}` (CSV `Item,Name,Part Number,Description,Material,Qty,Mass [g]`),
`src/drawing/Sheet.{h,cpp}` (`TitleBlock::fieldNames()`: Title, PartNumber, DrawnBy, Date, Checked, Approved, Scale,
Sheet, Size, Rev, Material, Mass, Company; all user-typed strings), `src/drawing/HoleAnalysis.cpp` +
`Callouts.h` (`HoleInfo`, `HoleCallout`), `src/sketch/ValueInput.h` (locked boxes already become driving
dimensions), `src/sketch/SketchSolver.cpp` (PlaneGCS has `P2CDistance`, `C2CDistance`, `ArcLength` unused),
`src/part/features/Import.{h,cpp}` (STEP AP214 / STL), `src/assembly/StepExport.h` (`schema` option already
accepts "AP242" but is untested), `src/drawing/DxfWriter.{h,cpp}` (R12 sheet writer), `src/part/tools/MeasureTool.cpp`
(centre of mass and mass per entity, no inertia, no report).

Conventions every package follows (from `ROADMAP.md` / the UI redesign): one tool id per new tool
(`<area>.<name>`) with a toolbar or MORE ▾ entry and a search entry, a command card, script verb(s) listed in
`docs/SCRIPTING.md`, a mono icon in `resources/icons/generate_icons.py`, `USER_GUIDE.md` and `CHANGELOG.md`
entries, unit tests in the module's `tests/` and one `.fcs` script test under `tests/scripts/` where the feature
is scriptable. No new heavy dependency; tables are compact C++ arrays or small JSON resources loaded on first use.

## 1. What a production designer needs and why (selection)

| Need | Job blocked today | Package |
|---|---|---|
| Hole standards done properly: ISO + ANSI inch clearance (close/normal/loose), tapped with tap drill, thread spec and cosmetic thread, counterbore/countersink **sized from fastener tables**, drill point angle, callouts | Every bracket, plate and housing has fastener holes. Today cbore/csink sizes are typed from memory, there is no inch series, and a drawing callout cannot say "M6 SHCS counterbore" because the hole does not know its fastener. | P2 |
| Pocket holes (angled stepped screw hole for wood / sheet joinery) | Furniture, jigs, cabinets, shop fixtures: the most common joint in plywood and lumber prototyping; no CAD workaround short of a sketch + angled plane per hole. | P2 |
| Light standard parts library (SHCS, hex bolts, nuts, washers, heat-set inserts, dowel pins) inserted into a hole with a joint, BOM-ready | Assemblies are checked for fit, clearance and reach with real fasteners; the BOM must list them with part number and material for ordering. Downloading vendor STEP per screw is slow and bloats files. | P2 |
| Typed sketch values create constraints; point-to-curve and arc-length dimensions | A typed sketch that is not fully defined moves when edited later (COMPARISON: 2-8 DOF left after a scripted part). Point-to-arc distances (slot to edge) and arc length (belt/strap/flex lengths, sheet developed lengths) cannot be dimensioned at all. | P1 |
| Mass properties (volume, mass, CoG, inertia, principal axes) with a density library feeding the BOM and the title block | Weight estimates for shipping, balance of moving parts, motor sizing, the "Mass" box in the title block that is typed by hand today and goes stale. | P1 |
| Interop: STEP AP242 verified, IGES in/out, OBJ / glTF / 3MF export, 1:1 DXF of a face or sketch | Sending parts to a machine shop (STEP/IGES), a printer (3MF with units and colour), a laser/waterjet (DXF of the flat face at 1:1), a web viewer or renderer (glTF/OBJ). | P3 |

Not chosen although Fusion has it: McMaster-style vendor catalogue browsing, hole "preset" cloud sync, Fusion's
exact Hole dialog layout, cosmetic thread textures, ANSI *fine* series beyond UNF, Fusion's fastener "auto size
from hole" wizard. See §6.

## 2. P1: sketch intent, mass properties, materials, BOM and title block

Size: **M** (6-8 agent-days). Owner areas: `src/sketch`, `src/core/Materials*`, new `src/part/MassProperties*`,
`src/assembly/Bom*`, `src/drawing/Sheet*` (title block fields only), `src/part/tools/MeasureTool.cpp`.

### 2.1 Typed sketch values create constraints (COMPARISON rec. 5)

User-facing behaviour:
* Locked value boxes already create driving dimensions when the geometry is created. P1 extends this to
  **every typed input**: a typed coordinate (`x,y` in the point box, or an absolute `@x,y`) creates
  horizontal/vertical distance dimensions to the sketch origin (DistanceX/DistanceY), a typed length creates a
  Distance, a typed angle an Angle dimension (to +X when alone, between lines when drawn from another line), a
  typed diameter/radius a Diameter/Radius. Rectangle width/height, polygon side count, slot length/width,
  circle/arc radii, ellipse radii follow.
* A sketch preference **Typed values create dimensions** (default on; `sketch/typedDimensions` in `UiSettings`),
  a per-session toggle in the SKETCH palette, and the status bar DOF counter turns `text.secondary` when the
  sketch is fully constrained (it already counts DOF).
* The inferred constraints (horizontal/vertical, coincident, tangent) that `SketchInference` shows while drawing
  are applied as today; typed dimensions are added after them and skipped when they would over-constrain
  (solver reports redundancy, the dimension becomes *driven*, shown in parentheses as now).
* **Point-to-curve distance**: Sketch Dimension `D` with a point and an arc/circle gives the shortest distance
  (`ConstraintP2CDistance`, FreeCAD semantics: point to the circle, signed inside/outside preserved).
  Circle-to-circle distance (`C2CDistance`) with two circles/arcs not concentric; a line and a circle give the
  existing tangent-offset distance (line to the centre minus radius) as `Distance` with the circle as `b`.
* **Arc-length dimension**: Sketch Dimension on one arc with the Alt modifier, or the new MORE ▾ entry
  **Arc Length** in the sketch dimension dropdown; drawn as a dimension line concentric with the arc, with
  extension lines along the radii and the value with the `⌒` prefix (ISO 129-1).
* Both new dimension kinds are editable in place (double-click), driven/driving toggle, expression-capable.

Data / format: `SketchGeometry.h` `ConstraintType` gains `PointCurveDistance` (a=point, b=circle/arc),
`CurveCurveDistance` (a,b circles/arcs), `ArcLength` (a=arc). `.fcad` stores them under their
`constraintTypeName` strings; old files unaffected. `SketchSolver` maps them to
`addConstraintP2CDistance`, `addConstraintC2CDistance`, `addConstraintArcLength` (all present in
`third_party/planegcs`). Dimension rendering in `SketchCurves.cpp`/`SketchSession.cpp` adds the two glyph layouts.

Files owned: `src/sketch/SketchGeometry.{h,cpp}`, `SketchSolver.cpp`, `SketchSession.{h,cpp}` (typed → dimension
rule table), `SketchInference.{h,cpp}` (redundancy check), `ValueInput*.{h,cpp}` (a `createsDimension` flag per
field), `src/sketch/tools/CreateTools.cpp`, `ConstraintTools.cpp`, `SketchCurves.cpp`, `src/app/UiSettings.{h,cpp}`
(the one setting), `src/app/SketchPalette.{h,cpp}` (toggle), `src/app/PreferencesDialog.cpp` (one row).

Script verbs: existing `DIMENSION` (`SKDIM`) accepts a point and an arc; new `ARCLENGTH <arc> [value]`
(alias `SKARCLEN`); `SETPREF sketch/typedDimensions on|off` through the existing preference verb if present, else
a card property `typedDimensions` on `sketch.create`. `CHECK dof 0` already exists for the acceptance scripts.

Tests: `test_solver.cpp` (P2C, C2C, ArcLength residuals, redundancy → driven), `test_value_input.cpp`
(fields flag), `test_tools.cpp` sketch (line typed 50,30 → DistanceX+DistanceY+Distance; rectangle typed → width
+ height, 0 DOF for a rectangle anchored at the origin), `tests/scripts/sketch_typed_full.fcs` (bracket sketch of
the tutorial drawn with typed values ends at `CHECK dof 0`), `test_feature.cpp` round trip of the three new
constraint names.

Acceptance: the tutorial bracket's first sketch typed as in `tests/e2e/bracket-v3.fcad` reaches 0 DOF without a
manual dimension; no existing `.fcad` in `tests/` changes its solve; arc length of a 90° R10 arc reads 15.708;
dimension glyphs match the redesign tokens in both themes (offscreen grab compare with `tools/uitest/compare.py`).

### 2.2 Mass properties and the material library

User-facing behaviour:
* **Mass Properties** (INSPECT, tool id `part.mass-properties`, shortcut none, verb `MASSPROPS`): pick bodies or
  components (default: the active component, or the whole design at root). The card shows volume, surface area,
  mass, centre of gravity (world and component frame), bounding box, inertia tensor about the CoG and about the
  origin, principal axes and moments, radius of gyration; the CoG is drawn as a marker in the viewer with the
  principal axes as a small triad while the card is open. **Copy** puts a tab-separated block on the clipboard;
  **Write report...** saves CSV or Markdown.
* Units follow `core::Units` (mm/g/kg, in/lb·in² when inches are active). Precision from Preferences.
* Measure `I` keeps its summary rows and gains a "Mass properties..." link row that opens the tool with the same
  selection.
* **Material library**: the 26 materials stay as the built-in table and gain the fields below; the Physical
  Material menu becomes a searchable picker (card body: category tabs, filter box, density shown) with
  **Custom...** (name + density + optional appearance) and **Library...** which opens a user library file
  `~/.config/FabriCAD/materials.json` (same schema, merged over the built-ins by name). A component's material
  is the material of its bodies when they all agree, else "mixed".
* Component properties card (**Properties** in the MODEL context menu, `assembly.component-properties`,
  verb `COMPPROPS`): part number, description, material (picker), revision, vendor, cost, plus the computed mass
  (read-only). Today `partNumber`/`description`/`material` exist on `Component` but have no card.

Data / format:
```
Material { name, category, density g/cm3, color, standard ("EN AW-6061", "ASTM A36", "PLA"),
           yield MPa (optional, informative), costPerKg (optional), source ("builtin"|"user") }
```
`Component` gains `revision`, `vendor`, `cost` (strings/double, `.fcad` keys `revision`, `vendor`, `cost`;
absent = empty). `core::MassProperties` struct (volume, area, mass, cog, inertiaCog[6], inertiaOrigin[6],
principalAxes[3], principalMoments[3], bbox) computed with `BRepGProp::VolumeProperties` and
`GProp_GProps::MatrixOfInertia` / `GProp_PrincipalProps`, summed over occurrences with `GProp_GProps::Add`
after `gp_Trsf` of each occurrence. Density 0 bodies contribute volume only and the report says
"n bodies without density".

### 2.3 BOM and title block fed by the model

* BOM gains options (card on **Export BOM**, verb `EXPORTBOM a.csv [flat|structured] [columns=...]`):
  flattened vs structured (level column with indentation in the Name), columns chosen from Item, Name,
  Part Number, Description, Material, Qty, Mass [g], Mass total [g], Volume [cm3], Revision, Vendor, Cost,
  Cost total. Default stays today's seven columns so existing scripts keep their output. The parts list in a
  drawing uses the same column list (sheet setting).
* Standard parts from P2 appear as rows through the same `Component` fields (P2 writes `partNumber`,
  `description`, `material`, `standard` on insert; nothing BOM-specific in P2).
* Title block fields `Material` and `Mass` (and `PartNumber`, `Rev`) get a **source** per field: *typed* (as
  today) or *from model* (`${material}`, `${mass}`, `${partNumber}`, `${revision}`, `${mass:kg,2}` format spec).
  Default for a new sheet: from model; existing sheets keep typed text. Values refresh with the drawing update
  that already re-projects views. `Edit Title Block` shows the token list.

Files owned (2.2 + 2.3): `src/core/Materials.{h,cpp}`, `src/core/Component.h` (+ `Document.cpp` JSON of the
three new fields), new `src/part/MassProperties.{h,cpp}`, new `src/part/tools/MassPropertiesTool.{h,cpp}`,
`src/part/tools/MeasureTool.cpp` (link row), `src/part/tools/PartTools.cpp` (registration lines only),
`src/app/AppearancePicker.{h,cpp}` → material picker widget (or new `src/app/MaterialPicker.{h,cpp}`),
`src/app/BrowserTree.cpp` (menu entries), new `src/assembly/tools/ComponentPropertiesTool.{h,cpp}`,
`src/assembly/Bom.{h,cpp}`, `src/assembly/AssemblyModule.cpp` (BOM card + registration), `src/drawing/Sheet.{h,cpp}`
(field sources), `src/drawing/SheetRenderer.cpp` (token expansion only), `src/drawing/Annotations.cpp` (parts
list columns), `resources/icons/generate_icons.py` (`part.mass-properties`, `assembly.component-properties`),
`docs/USER_GUIDE.md` §6, §7, §8, §9, `docs/SCRIPTING.md`, `CHANGELOG.md`.

Script verbs: `MASSPROPS [report=a.csv|a.md]`, `COMPPROPS partNumber=... description=... material=... revision=...
vendor=... cost=...`, `MATERIAL "Aluminum 6061"` (sets the selection's material; exists as a menu only today),
`EXPORTBOM a.csv [flat|structured] [columns=item,name,partNumber,qty,mass,massTotal]`, `ARCLENGTH`, and
`CHECK mass 123.4 ±0.1`, `CHECK cog x,y,z ±tol` added to the `CHECK` family.

Tests: `src/part/tests/test_mass_properties.cpp` (box 10×20×30 steel: volume, mass 47.1 g, CoG, inertia against
closed forms; two occurrences placed → `Add`; density 0 reporting), `test_document.cpp` (new component fields
round trip, old files load), `test_assembly_features.cpp` (BOM flat vs structured, column selection, mass total,
standard-part rows if P2 is merged), `test_drawing_model.cpp` (title block tokens expand and refresh after a
material change), `tests/scripts/massprops_bom.fcs` (`CHECK mass`, `EXPORTBOM` columns compared to a golden
CSV), `test_app_logic.cpp` (material picker filters, custom material stored in the user library under a
temp `XDG_CONFIG_HOME`).

Acceptance: `MASSPROPS` on the tutorial bracket reports mass within 0.1 % of the closed-form value computed in
the test; inertia tensor symmetric and positive definite, principal moments sorted; BOM CSV default output
byte-identical to today's for `tests/chair`; title block `${mass}` updates after changing the material in
MODEL; user library survives restart; no UI colour literals (theme lint stays green).

## 3. P2: holes, pocket holes, fastener library, insert into hole

Size: **L** (10-14 agent-days; the fastener generators are the long part). Owner areas: `src/part/features/Hole*`,
`Thread*`, new `src/part/features/PocketHole*`, new `src/part/standards/*`, new `src/assembly/FastenerInsert*`,
`src/drawing/HoleAnalysis.cpp`, `Callouts.{h,cpp}`.

### 3.1 Hole standards done properly

User-facing behaviour (Hole card `H`, same tool id `part.hole`):
* **Standard**: `ISO metric` (default, mm) or `ANSI inch` (UNC/UNF; the card switches its size list and the
  value boxes show inches with fractions and decimals, e.g. `1/4-20`, `#10-32`). Preference
  `hole/defaultStandard` follows the document unit.
* **Type** (replaces `tapType`): `Simple` (typed diameter), `Clearance` (fit Close / Normal / Loose from
  ISO 273 or ASME B18.2.8), `Tapped` (tap drill from the table, thread spec + class, cosmetic or modelled thread
  as today, thread depth full / value), `Tap drill only` (the drill without a thread, for parts tapped in the
  shop).
* **Fastener**: for Counterbore and Countersink the head recess is sized from the fastener table: head type
  `Socket head cap screw (ISO 4762 / ASME B18.3)`, `Hex bolt (ISO 4014 / B18.2.1)`, `Low head SHCS (ISO 14580)`,
  `Button head (ISO 7380)`, `Flat countersunk (ISO 10642 / B18.3 flat CSK, 90° metric / 82° inch)`,
  `Pan/cheese head (ISO 1207)`, plus `Custom` (today's typed values). The card fills `cboreDiameter`,
  `cboreDepth`, `csinkDiameter`, `csinkAngle` from the table and keeps them editable (edited = Custom); a
  **Counterbore clearance** (0 / 0.5 / 1 mm head clearance) and **Head below surface** offset.
* **Drill point**: Flat / Angled with angle presets 118° (HSS), 135° (split point), 140° (carbide), or typed;
  for blind tapped holes the usable thread depth is reported in the card ("thread 12 mm, drill 15.3 mm + point").
* **Hole callouts in drawings** read the fastener data: `HoleInfo` gains `standard`, `fastener` (type + size),
  `fit`, `tapDrill`, `threadDepth`, `pointAngle`, `pocket` (3.2); the generated callout follows ISO 15786 /
  ASME Y14.5 (`⌴ ⌀11 ⬇ 6.5 / ⌀6.6 THRU` for an M6 SHCS cbore; `M6x1 - 6H ⬇ 12 / ⌀5 ⬇ 15.3`), with the fastener
  as an optional second line ("FOR M6 SHCS"). A **Hole Table** (drawing, `drawing.hole-table`) lists holes of a
  view by tag, X, Y, size, callout, quantity (uses `HoleAnalysis` recognition so it works for imported parts too).

Data / format: compact tables in `src/part/standards/` as `constexpr` arrays (no runtime file reads):
```
ThreadRow     { designation, standard (ISO261|UNC|UNF), major mm, pitch mm (TPI converted), tapDrill mm, class default }
ClearanceRow  { designation, close, normal, loose }                      // ISO 273, ASME B18.2.8
HeadRow       { headType, designation, headDia dk, headHeight k, csinkAngle, cboreDia (dk + clearance), standards }
PointAngle    { label, degrees }
```
Sizes: ISO M1.6-M36 coarse and fine as in `isoMetricDesignations()` today; inch #0-#12, 1/4-1 1/2 UNC/UNF.
`IsoThread` becomes `ThreadSpec` (keeps the ISO methods; `tapDrill()` reads the table value). `.fcad`: Hole gains
`standard`, `fastener`, `headClearance`, `headOffset`, `tapDrillOnly`; old `tapType` values map onto `type`;
loading an old file keeps Custom head sizes so geometry does not change.

### 3.2 Pocket holes

A separate feature `part.pocket-hole` (verb `POCKETHOLE`, icon: angled stepped hole in a board section) rather
than a Hole type, because its placement is edge-based and its result is a stepped bore at an angle, with a
different manipulator set.

User-facing behaviour:
* Pick the **face** the screw enters (the back of the board), then one or more **exit edges** (the board edge
  the screw leaves) and click positions along them, or type a spacing pattern (`n holes every d mm from the
  ends`); the card shows the hole axis angle preview as a ghosted stepped cylinder exiting the edge face.
* **Preset** by jig: `Kreg 15° (default)`, `Castle 6°`, `Custom angle`; **Material thickness** presets 12 / 15 / 18
  / 19 (3/4") / 25 / 38 (1 1/2") mm or measured from the picked face pair (default: measured), which set the
  jig's depth collar (distance from the entry point to the edge) per the jig's table; **Screw**: coarse / fine
  pocket screw lengths 25 / 32 / 38 / 64 mm (1", 1 1/4", 1 1/2", 2 1/2") sized from the thickness;
  **Pilot diameter** (3.6 mm Kreg, 9.5 mm step/counterbore) and **step depth** editable; **Exit point** offset
  from the edge-face centre line (default: the thickness midpoint, so the screw exits mid-edge), **Edge
  distance** from the board end.
* Geometry: two coaxial cylinders (counterbore then pilot) at the angle about the exit edge direction, cut from
  the body; the pilot continues through the edge face so a mating board receives a pilot when "Also drill the
  mating part" is on and a touching body is selected (optional second body, pilot only). The exit point
  is a named point (`PocketHole1/P<k>`) usable by the P2 insert tool and by joints.
* Drawings: the callout reads `POCKET HOLE 15° ⌀9.5/3.6 Kreg` with the screw; the hole table lists them.

Data: `PocketJig { name, angle, pilot, step, depthCollar[thickness → collar], screwLength[thickness] }` as a
small table; `.fcad` feature type `PocketHole` with the properties above and the edge/face refs through
`GeometryRefs` as other features do.

### 3.3 Standard parts library and insert into hole

User-facing behaviour:
* **Insert Fastener** (ASSEMBLE > INSERT, `assembly.insert-fastener`, verb `FASTENER`, shortcut none): a card with
  **Family** (Socket head cap screw ISO 4762 / ASME B18.3, Hex bolt ISO 4014/4017 / B18.2.1, Hex nut ISO 4032 /
  B18.2.2, Nyloc nut ISO 7040, Washer ISO 7089 (plain) / ISO 7092 (small) / ASME B18.21.1, Heat-set insert
  (M2-M8, standard/short, common 3D-print sizes), Dowel pin ISO 2338 / ASME B18.8.2, Set screw ISO 4026 (flat
  point)), **Size**, **Length** (list from the standard's range, or Auto from the hole depth), **Material**
  (Steel 8.8 / 10.9 / 12.9, A2 / A4 stainless, brass, nylon: sets density and appearance through P1's
  `Material`; works without P1 by writing the density), **Thread representation** (cosmetic (default) /
  modelled through `Thread`), **Hex socket / head recess** modelled (yes; it is cheap and tells the head apart).
* **Insert target**: click a hole (circular edge or cylindrical face, any origin: Hole feature, PocketHole,
  imported). The fastener is created as a **component** (definition reused for identical part numbers, so the
  BOM counts them) placed with its seating plane on the hole's start face and a **rigid joint** to that face
  (or a revolute joint for pins when **Allow rotation** is on). Auto picks the size from the hole table
  (clearance ⌀6.6 → M6; 1/4" clearance → 1/4-20) and the length from the stack height rounded up to the next
  standard length, both overridable. Multiple holes: one click per hole, or **All equal holes of this
  feature**. **Add nut and washer** adds a stack on the far face (washer under the head too, when checked).
* Fastener components carry `partNumber` (`ISO 4762 M6x20 8.8`, `B18.3 1/4-20 x 1 ASTM A574`), `description`
  ("Socket head cap screw"), `material`, `standard`, `mass` from the density; they appear as normal rows in the
  BOM (P1 columns) and are grouped by identical part number. A fastener component is editable through its
  card (double-click): change size/length and it regenerates in place, keeping the joint.
* Fasteners are plain components in `.fcad` with a `standardPart` block (family, size, length, material,
  options) so they are regenerated on load from the table (no mesh or B-rep stored), and exported to STEP as
  solids like any body.

Data / format: generators in `src/part/standards/Fasteners.{h,cpp}` build each part from revolve profiles +
polygon prisms + the existing `Thread` helper (cosmetic by default): tables
```
ScrewRow   { family, designation, dk headDia, k headHeight, s socketSize, lengths[], threadLength rule }
NutRow     { family, designation, s width, m height }
WasherRow  { family, designation, d1 inner, d2 outer, h }
InsertRow  { designation, outerDia, length, holeDia (recommended), short/standard }
PinRow     { designation, dia, lengths[], m6 tolerance }
```
Roughly 10-15 sizes per family, ISO and ANSI, as `constexpr` rows (about 400 lines total). A `StandardPart`
feature (`BaseBodyFeature` subclass) in the fastener component owns the body; Thread cosmetic data is attached
so callouts and the browser show the thread.

Files owned (3.1-3.3): `src/part/features/Hole.{h,cpp}`, `Thread.{h,cpp}`, `src/part/ExtraUtils.{h,cpp}`
(thread table → `standards/Threads.{h,cpp}`), new `src/part/standards/{Threads,Clearance,Heads,Fasteners,
PocketJigs}.{h,cpp}`, new `src/part/features/PocketHole.{h,cpp}`, new `src/part/features/StandardPart.{h,cpp}`,
`src/part/tools/PartTools.cpp` (registration), `src/part/tools/FeatureManipulators.cpp` (pocket-hole angle and
exit handles), new `src/assembly/tools/InsertFastenerTool.{h,cpp}`, new `src/assembly/FastenerInsert.{h,cpp}`
(hole analysis for placement, joint creation), `src/assembly/AssemblyModule.cpp` (registration),
`src/drawing/HoleAnalysis.cpp`, `src/drawing/Callouts.{h,cpp}`, `CalloutRenderer.cpp`, new
`src/drawing/HoleTable.{h,cpp}` + `tools/CalloutTools.cpp` (hole table tool), `resources/icons/generate_icons.py`
(`part.pocket-hole`, `assembly.insert-fastener`, `drawing.hole-table`), `docs/USER_GUIDE.md` §7, §8, §9,
`docs/SCRIPTING.md`, `CHANGELOG.md`. Shared with P1 only through `Component::partNumber/description/material`
(already exist) and `core::Material` lookup by name (exists); P2 must not edit `Materials.cpp` or `Bom.cpp`.

Script verbs: `HOLE` gains `standard=iso|ansi type=simple|clearance|tapped|tapdrill size=M6x1|1/4-20
fit=close|normal|loose fastener=shcs|hex|lowhead|button|csk|pan|custom headClearance=0.5 point=118`;
`POCKETHOLE jig=kreg15 thickness=19 screw=32 pitch=... spacing=...` with face/edge selectors; `FASTENER
family=shcs size=M6 length=auto material=8.8 nut=yes washer=yes` with a hole selector; `HOLETABLE <view>`;
`CHECK holes <n>`, `CHECK fastener "ISO 4762 M6x20"` (BOM contains it).

Tests: `test_tools.cpp` part and new `test_hole_standards.cpp` (every table row produces a valid hole; ISO
and ANSI clearance values spot-checked against the standards: M6 normal 6.6, 1/4 normal 0.281"; cbore for M6 SHCS
⌀11 depth 6 + clearance; tap drill M6x1 = 5.0, 1/4-20 = #7 0.201"; thread depth report), `test_pocket_hole.cpp`
(Kreg 15° in 19 mm: exit point lies on the edge face at mid-thickness within 0.05 mm, pilot exits, step depth
from the collar table; two boards → mating pilot), `test_fasteners.cpp` (each family × 3 sizes generates a
closed valid solid, volume within 5 % of the catalogue mass / density, head and socket present; regeneration
from `.fcad` is identical), `test_assembly_tools.cpp` (insert into a Hole: seating face coincident, axis
coaxial, joint created, BOM row with part number, two equal holes → qty 2), `test_drawing_callouts.cpp`
(callout strings for the five hole kinds, hole table rows), `tests/scripts/holes_fasteners.fcs` (plate with
cbored M6 holes, four SHCS inserted, `CHECK fastener`, STEP export loads back in `test_step_import` with the
screws).

Acceptance: old `.fcad` holes load with unchanged geometry (hash of the tutorial bracket's final body equal
before/after); ANSI inch holes round-trip with inch units on; a 100-hole plate with `FASTENER` all-equal inserts
in < 2 s headless (one definition, 100 occurrences); the drawing callout of a tapped cbore hole reads correctly
in ISO and ANSI form; GUI tester pass (Sonnet, `ui.sh`) covers Hole card standard switch, pocket hole on a 19 mm
board, insert of screw + nut + washer, hole table.

## 4. P3: production interop and export formats

Size: **M** (6-8 agent-days). Owner areas: `src/part/features/Import*`, `src/assembly/StepExport*`, new
`src/part/io/*`, `src/drawing/DxfWriter*` (new flat-face entry point only), `src/part/tools/Export*Tool*`.

User-facing behaviour:
* **STEP AP242 check**: the Export STEP dialog (bodies and assembly) gets **Schema** AP203 / AP214 (default) /
  AP242 with per-face colours, names, and (AP242) product properties `partNumber`, `description`, `material`,
  `revision` written as `product_definition` attributes / `descriptive_representation_item`s; import reads them
  back into `Component`. Validation: round trip in tests, `STEPCAFControl_Reader` of our AP242 output, and a
  check against an external AP242 file in `tests/fixtures/` (the NIST CTC sample `nist_ctc_01_asme1_ap242.stp`,
  MIT-like licence, 1 MB) imports with names and colours. A **STEP import report** (dialog after import and in
  the log: entities, shapes, failed faces, `BRepCheck_Analyzer` result, sewing/fix applied) with options
  **Sew faces**, **Fix shape**, **Unify same-domain faces** (defaults off, on, off).
* **IGES import/export** (`TKDEIGES`, already in the OCCT build): `IMPORT a.igs` through the same Import
  feature (surfaces sewn to solids when closed, else surface shells become bodies; the report says which),
  `EXPORTIGES a.igs [brep|faces]` (BRep entities 186/514 default, or trimmed surfaces 144 for old CAM).
* **Mesh export**: `EXPORTOBJ a.obj` (with `.mtl` for appearance colours, Y-up option), `EXPORTGLTF a.glb|a.gltf`
  (PBR colours from appearances, node per component with names, binary default; `RWGltf_CafWriter`),
  `EXPORT3MF a.3mf` (units mm, one object per body with the body/component name, colour per object, the
  `Metadata` title/designer from the document, components as `components` so a printer slicer keeps parts
  separate). 3MF is a ZIP of XML; OCCT has no writer and lib3mf is a heavy dependency, so the XML is written by
  hand (about 200 lines) and the ZIP by the single-file public-domain `miniz.c` vendored under
  `src/part/io/third_party/miniz/` (no new link dependency, negligible build cost; `tests/fuzz` covers it in the
  sanitizer build). All three share the STL dialog's mesh settings (quality presets, deflection) through a common
  **Export Mesh** card (`part.export-mesh`, the format from the extension; `EXPORTSTL` keeps its card and
  verb for compatibility).
* **DXF 1:1 of a face or sketch** (`part.export-face-dxf`, verb `EXPORTDXF a.dxf` from the design workspace with a
  planar face, a sketch, or several coplanar faces selected; in the drawing workspace `EXPORTDXF` keeps the sheet
  meaning): outer and inner wires as closed `LWPOLYLINE` (lines) / `ARC` / `CIRCLE` / `SPLINE` (fitted; option
  **Approximate splines as arcs** with tolerance for laser controllers), layers `OUTER`, `INNER`, `ETCH` (sketch
  construction lines, optional), 1:1 in mm or inches (`$INSUNITS`), origin at the face's bounding box corner or
  the sketch origin, R12 (today's writer, polylines only) or **R2000 (AC1015)** with `LWPOLYLINE`, `SPLINE` and
  `$INSUNITS` (the minimal AC1015 header/tables/blocks the writer needs; this is the `backend-deferred`
  DRW-ASM-OUTPUT-9 item, done here for the flat-cut use and reused by sheet metal later). Bend-line and etch layers
  are placeholders for the sheet-metal plan.
* Preferences > General gains **Export defaults**: STEP schema, mesh preset, DXF version.

Data / format: `src/part/io/MeshExport.{h,cpp}` (OBJ, glTF via OCCT, 3MF writer), `src/part/io/IgesIO.{h,cpp}`,
`src/part/io/StepIO.{h,cpp}` (moved from `Import.cpp`: reader options, report struct, AP242 properties;
`Import.cpp` keeps the feature), `src/part/io/FaceDxf.{h,cpp}` (face/sketch → 2D wires → `DxfWriter`),
`src/drawing/DxfWriter.{h,cpp}` gains `Version { R12, R2000 }`, `lwpolyline()`, `spline()`, `setUnits()`.
`.fcad`: Import feature gains `format`, `sew`, `fix`, `unify`; export settings live in QSettings only.

Files owned: `src/part/features/Import.{h,cpp}`, new `src/part/io/*`, `src/assembly/StepExport.{h,cpp}`,
`src/assembly/tests/test_step_import.cpp`, `src/part/tools/ExportStlTool.{h,cpp}` → `ExportMeshTool.{h,cpp}`,
new `src/part/tools/ExportFaceDxfTool.{h,cpp}`, `src/part/tools/PartTools.cpp` (registration lines only),
`src/drawing/DxfWriter.{h,cpp}`, `src/app/PreferencesDialog.cpp` (Export defaults rows, a different block from
P1's row), `CMakeLists.txt` (`TKDEIGES`, `TKRWMesh`, `TKDEOBJ`, `TKDEGLTF` link lines; miniz source),
`resources/icons/generate_icons.py` (`part.export-mesh`, `part.export-iges`, `part.export-face-dxf`),
`docs/USER_GUIDE.md` §10, `docs/SCRIPTING.md`, `CHANGELOG.md`, `tests/fixtures/` (small IGES, 3MF, AP242 files).

Script verbs: `EXPORTSTEP a.step [schema=ap203|ap214|ap242] [pcurves=off]` (bodies and `assembly.export-step`),
`IMPORT a.step|a.igs|a.iges [sew=on] [fix=off] [unify=on] [report=a.txt]`, `EXPORTIGES a.igs [faces]`,
`EXPORTOBJ a.obj [yup] [quality=...]`, `EXPORTGLTF a.glb|a.gltf [quality=...]`, `EXPORT3MF a.3mf [quality=...]`,
`EXPORTMESH a.<ext>` (format from the extension), `EXPORTDXF a.dxf [r12|r2000] [units=mm|in] [arcs=0.01]` with a
face or sketch selector in the design workspace; `CHECK file a.3mf exists`, `CHECK mesh a.obj triangles>1000`
in the `CHECK` family.

Tests: `test_inspect_export.cpp` → split `test_export_formats.cpp` (OBJ reopened by `RWObj_Reader`: triangle count
and bbox equal the STL export; glTF reopened by `RWGltf_CafReader`: node names = component names, colours;
3MF: ZIP has `[Content_Types].xml`, `_rels/.rels`, `3D/3dmodel.model`, XML validates against the 3MF core
schema subset in the test, vertex count matches, reopening through our own minimal reader in the test;
IGES round trip of the bracket: volume within 1e-6 relative; STEP AP242 round trip keeps names, colours,
`partNumber`; import report fields; sew/fix options on a damaged STEP from `tests/fuzz` corpora), `test_drawing_export.cpp`
(DXF R2000 header, `LWPOLYLINE` closed flags, `$INSUNITS`, spline-to-arcs tolerance; R12 output unchanged byte
for byte for the sheet export), `test_step_import.cpp` (NIST AP242 fixture: component count, names, no failed
faces), `tests/scripts/export_formats.fcs` (one part exported to every format, `CHECK file` each), fuzz drivers
for the IGES reader and the 3MF writer's mesh input (`tests/fuzz`, sanitizer build).

Acceptance: every export of the tutorial bracket opens in an external tool recorded in the test log (FreeCAD for
IGES/STEP/OBJ, PrusaSlicer or Cura for 3MF, a glTF validator run of `gltf_validator` if installed, LibreCAD
for DXF R2000) — done once manually by the tester and noted in `docs/tests/interop/`; STEP AP242 output passes
`STEPCAFControl_Reader` with zero fails and the NIST fixture imports; the face DXF of a 100×50 plate with four
⌀6.6 holes is 1:1 (LibreCAD measures 100.00), inches when the document is in inches; no new startup cost
(`fabricad --version` time unchanged; all new link libraries are OCCT toolkits already installed).

## 5. Parallel execution, overlap and merge order

| | P1 | P2 | P3 |
|---|---|---|---|
| Worktree | `prod/p1-intent-mass` | `prod/p2-holes-fasteners` | `prod/p3-interop` |
| Exclusive dirs | `src/sketch/**`, `src/core/Materials*`, `src/part/MassProperties*`, `src/part/tools/MassPropertiesTool*`, `src/assembly/Bom*`, `src/assembly/tools/ComponentPropertiesTool*`, `src/drawing/Sheet*`, `SheetRenderer.cpp`, `Annotations.cpp` | `src/part/features/Hole*`, `Thread*`, `PocketHole*`, `StandardPart*`, `src/part/standards/**`, `src/part/ExtraUtils*`, `src/assembly/FastenerInsert*`, `src/assembly/tools/InsertFastenerTool*`, `src/drawing/HoleAnalysis.cpp`, `Callouts*`, `CalloutRenderer.cpp`, `HoleTable*`, `tools/CalloutTools.cpp` | `src/part/features/Import*`, `src/part/io/**`, `src/assembly/StepExport*`, `src/part/tools/Export*`, `src/drawing/DxfWriter*`, `CMakeLists.txt` link lines, `tests/fixtures/**` |
| Shared, append-only (merge by concatenation) | `src/part/tools/PartTools.cpp` registration, `src/assembly/AssemblyModule.cpp` registration, `resources/icons/generate_icons.py`, `docs/SCRIPTING.md`, `docs/USER_GUIDE.md`, `CHANGELOG.md`, `src/app/PreferencesDialog.cpp` (each package adds its own block) | same | same |
| Depends on | nothing | nothing (uses existing `Component` fields; P1's BOM columns show its data once both are merged) | nothing |
| Merge order | 1 (smallest schema change) | 3 (largest; rebase on P1 for the material picker in the fastener card, else keep its own density field) | 2 |

Each package: `-j2` build in its own build dir, `ctest` green (85 today) plus its own tests, a Sonnet GUI pass
through `tools/uitest/ui.sh` on the assigned display, offscreen grabs for the card layouts, `CHANGELOG.md` entry,
commits on its branch only (the orchestrator merges and pushes). Headless tests with `QT_QPA_PLATFORM=offscreen`.

## 6. Deferred (and why)

| Item | Why not now | Where it goes |
|---|---|---|
| Vendor catalogue browsing (McMaster-Carr insert), downloading STEP per part | Network, licensing, slow; the generated library covers the ordering use. | not planned |
| Fasteners beyond §3.3 families (rivets, threaded rods, retaining rings, bearings, T-slot hardware) | Add rows to the same tables once the generators are proven; bearings are not fasteners. | follow-up S items |
| Fusion-style "hole wizard" presets synced in the cloud, hole favourites | Fusion parity only; a user library JSON of hole presets is an S follow-up. | later |
| Cosmetic thread as a texture in the viewer | Visual only; the browser and callouts carry the thread. | not planned |
| Full STEP AP242 PMI/GD&T export of drawing annotations | Needs `XCAFDoc_DimTolTool` mapping from the drawing module; useful for model-based definition but no current user asks. | `feature-gaps.md` §7 keeps it (M) |
| DWG, Parasolid, ACIS, native formats | No free writer/reader. | out of scope, "use STEP" |
| 3MF import, OBJ/glTF import as meshes | Mesh bodies do not exist as a body kind. | after mesh-to-B-rep (`feature-gaps.md` §2) |
| Sheet-metal flat pattern DXF | Needs unfolding; the R2000 DXF writer and the layer scheme from P3 are ready for it. | `feature-gaps.md` step 9 |
| Auto-constrain / "fully define sketch" command | Builds on P1's typed constraints and the DOF query; medium size. | step 6 follow-up |
| Spline/offset dimensions, curvature constraints | Not blocking production parts. | `feature-gaps.md` §1 |
| Assembly-level mass properties with joint positions over time, motion | P1 gives static assembly mass; motion is step 8. | step 8 |
| Costing beyond a per-component cost field (volume-based cost, machining time) | Needs process data the app does not have. | not planned |
| Pocket-hole "auto layout along all edges" and a joinery library (dados, dowels, dominos) | After pocket holes prove the edge-placement UI. | follow-up S |
| Hole table in the drawing for pocket holes with angle columns | Covered by the generic hole table's callout column for now. | with the sheet-metal bend table work |
