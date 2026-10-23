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
- **Production interop** (production-features.md P3): STEP schema choice AP203 / AP214 / AP242 on both STEP
  exporters (`schema=`), per-body names and colours on the bodies exporter, part number / description /
  material (and revision) as STEP product properties, read back into the components on import; a STEP / IGES
  import report (entities, solids, shells, faces, failed faces, B-rep check) with *Sew faces* / *Fix shape* /
  *Unify same-domain faces*; IGES import and export (`EXPORTIGES`, BRep or trimmed surfaces); the **Export
  Mesh** card for STL, OBJ (+ .mtl), glTF / GLB and 3MF (`EXPORTMESH`, `EXPORTOBJ`, `EXPORTGLTF`, `EXPORT3MF`)
  sharing the STL quality presets; **Export Face DXF** (`EXPORTDXF` in the design workspace): a 1:1 DXF of a
  planar face, coplanar faces or a sketch with OUTER / INNER / ETCH layers, arcs as polyline bulges, splines or
  spline-to-arcs, mm or inches, R12 or the new R2000 (AC1015) writer; Preferences > General export defaults.
  STEP files with AP242 PMI that OCCT types differently (the NIST CTC samples) are no longer refused as damaged.
- Tutorial tests: an off-script GUI scenario (`fabricad_tutorial_offscript`: Show me, a detour and its hint, Undo
  to step start, Back, Restore chapter).

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
