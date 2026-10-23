# Changelog

All notable changes to FabriCAD are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/) (0.x: the file format and the UI may still change).

## [Unreleased]

### Added

- **Guided tour**: "Your first part: a mounting bracket", offered on first launch and in Help > Tutorials:
  from a first sketch to an STL in 66 steps (sketch on faces, dimensions, extrude, fillet, slot, counterbored
  holes, mirror, chamfer, rib, a history edit, measure, save and reopen, export), with chapter checkpoints for
  Restore chapter. The target face, edge or plane glows in the view; a printable copy with screenshots is in
  `docs/tutorials/bracket.md` (generated from a real-input run by `tools/tutorial/export_md.py`).
- Tutorial tests: an off-script GUI scenario (`fabricad_tutorial_offscript`: Show me, a detour and its hint, Undo
  to step start, Back, Restore chapter).
- **Typed sketch values create dimensions**: typed `x,y` points get DistanceX/DistanceY to the origin (or a
  coincidence with it at `0,0`) next to the typed lengths, angles and sizes, so a typed sketch ends fully
  defined; redundant ones become driven. Sketch palette toggle, Preferences > General, `TYPEDDIMS on|off`.
- **Sketch dimensions**: point-to-curve distance (point + circle/arc), circle-to-circle gap and **Arc Length**
  (`⌒`, dimension dropdown, Alt + `D` on an arc, `ARCLENGTH`).
- **Mass Properties** (INSPECT, `MASSPROPS`): volume, area, mass, centre of gravity, inertia about the CoG and
  the origin, principal axes and moments, radii of gyration, a CoG marker in the view, CSV/Markdown report.
  `CHECK mass` and `CHECK cog` in scripts.
- **Material library**: standard, yield and cost fields, a searchable picker (Physical Material > Choose...),
  custom materials saved to `~/.config/FabriCAD/materials.json`, `MATERIAL "Aluminum 6061"`.
- **Component Properties** (`COMPPROPS`): part number, description, material, revision, vendor and cost;
  revision, vendor and cost are new `.fcad` component keys (absent = empty).
- **BOM**: flat or structured (indented, item 2.1, quantities per parent) and selectable columns (mass total,
  volume, revision, vendor, cost, cost total) with `EXPORTBOM a.csv structured columns=...`; the default output
  is unchanged.
- **Title block tokens**: `${material}`, `${mass}`, `${mass:kg,2}`, `${partNumber}`, `${revision}`,
  `${description}`, `${vendor}` refresh with every drawing update; empty Rev fields fall back to the root
  component's revision.

- **Holes for production**: ANSI inch holes (UNC / UNF #0 to 1 1/2") beside ISO metric; hole
  type Clearance (ISO 273 / ASME B18.2.8 close, normal, loose), Tapped (table tap drills, thread
  class, inch callouts `1/4-20 - 2B`) and Tap drill only; counterbores and countersinks sized from
  screw head tables (SHCS, hex, low head, button, flat countersunk 90° / 82°, pan) with head
  clearance and offset; drill point presets; thread depth readout. Old files keep their typed sizes.
- **Pocket Hole** (`POCKETHOLE`): angled stepped screw holes for board joints (Kreg 15°, Castle 6°,
  custom), thickness measured or typed, screw length and step depth from the jig chart, exit point at
  mid-thickness, optional pilot in the mating board.
- **Insert Fastener** (`FASTENER`): socket head cap screws, hex bolts, hex and nyloc nuts, washers,
  heat-set inserts, dowel pins and set screws (ISO and ANSI tables, generated on load, no stored
  geometry), size and length from the picked hole, washer / nut stack, as-built rigid or revolute
  joint, shared definitions with part number, description and material for the BOM.
- **Hole Table** (`HOLETABLE`) in drawings, and a "FOR M6 SHCS" line on hole callouts of
  fastener-sized counterbores.

### Fixed

- Dropdown lists of combo boxes show all their items (the last one hid behind a scroll arrow).
- The command card keeps the keyboard focus after a choice rebuilds it, so Enter applies the command.
- A cut with Extent All goes into the material by itself, like a Distance cut.
- A fully dimensioned slot on projected edges no longer reports 30 DOF.
- Tour: the Measure step accepts only the two top rims of the counterbores; the bubble keeps off the FEATURES
  rows and off the second rim; the Rib chapter asks for Create Sketch before pointing at the YZ plane; the tab's
  close glyph is located exactly; after the bubble's ⋯ menu the keyboard goes back to the window (a shortcut typed
  right after Undo to step start did nothing).

## [0.1.0] - 2026-10-23

First public release.

### Added

- **Sketch**: lines, rectangles, circles, arcs, polygons, slots, points, ellipses, splines and
  text; fillet, chamfer, trim, extend, break, offset, mirror, project, sketch patterns; driving
  and driven dimensions with expressions and document parameters; geometric constraints solved
  by the vendored FreeCAD PlaneGCS solver, with conflict and redundancy reporting; Fusion-style
  on-screen value boxes and inference.
- **Part**: extrude, revolve, sweep, loft, hole (simple / counterbore / countersink, ISO 273
  clearance, tapped), fillet, chamfer, shell, draft, press pull, combine, split, move/copy,
  scale, patterns, mirror, rib, thicken, thread; box, cylinder, sphere, torus and coil
  primitives; construction planes, axes and points; on-canvas manipulators; history-based
  topological naming so references survive upstream edits; Measure / Inspect; physical
  materials and mass properties; STEP and STL import/export.
- **Assembly**: components and occurrences, grounding, in-context editing, rigid groups;
  rigid, revolute, slider, cylindrical, pin-slot, planar and ball joints with limits and
  offsets; tree + Levenberg-Marquardt solver (closed loops); copy/paste of components;
  interference check; BOM CSV; XCAF STEP export.
- **Drawing**: ISO / ASME sheets with title block; base, projected, section (stepped,
  aligned), detail and break views with hidden-line removal on a worker thread; associative
  dimensions with tolerances and ISO 286 fits; GD&T frames, datums, surface texture, hole and
  thread callouts, ordinate/baseline/chain dimensions, balloons, parts lists, revision tables;
  PDF, SVG, DXF (R12) and CSV export.
- **Application**: dark and light themes from one set of design tokens (WCAG and colour-vision
  checked); captioned toolbar, sidebar feature list with rollback bar, model tree, selection
  panel, command card with live preview, marking menu, command search (S), command line,
  orientation widget, document tabs, Fusion / SolidWorks / AutoCAD / Inventor navigation
  presets; scripting and batch mode (`fabricad --script file.fcs`); autosave and crash
  recovery; `.fcad` JSON documents.
- **Packaging**: CMake install rules (binary, `.desktop` file, AppStream metainfo,
  `application/x-fabricad` MIME type for `*.fcad`, hicolor icons rendered from one SVG,
  licences), an AUR `PKGBUILD` (`fabricad`, `fabricad-git`) and a GitHub Actions CI workflow.

[Unreleased]: https://github.com/maxDTM/fabricad/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/maxDTM/fabricad/releases/tag/v0.1.0
