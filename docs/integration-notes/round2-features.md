# Round 2: part and assembly feature gaps (branch `feat/features`)

## What was added

| Feature | Where | Notes |
|---|---|---|
| Box / Cylinder / Sphere / Torus / Coil primitives | `src/part/features/Primitives.*` | Placed on a plane, sketch or planar face at a typed X/Y position. New Body / Join / Cut / Intersect, like Extrude. Coil types: revolutions+height, revolutions+pitch, height+pitch. Sections: circular, square, triangular. Section position, taper angle, clockwise. Named faces (`Box1/Top`, `Coil1/Start`...). |
| Thread | `src/part/features/Thread.*`, ISO table in `src/part/ExtraUtils.*` | ISO 261 metric coarse/fine table. Works on rods and holes. Auto size from the diameter, or a size picked from the table. Cosmetic by default; can be modelled as a helical cut with the ISO 68-1 profile. Full length, or length + offset + flip, left hand. `threads()` returns callouts such as `M6x1 - 6H`. |
| Hole: tap type + click on face | `src/part/features/Hole.*` | Tap type Simple / Clearance (ISO 273 close/normal/loose) / Tapped (tap drill D-P, cosmetic or modelled thread, full or given depth). Each click on a planar face is a separate hole (`refsDistinguishedByPoint`). Picking the same point again removes it. |
| Variable-radius fillet | `src/part/features/Fillet.*` | `radiusType` is Constant or Variable. The radius goes from `radius` to `endRadius` along each chain, with optional intermediate radii (`radiusPoints`, a text property like `0.5:6; 0.25:3`) and `flipVariable`. |
| Rib | `src/part/features/BodyOps.*` | From an open sketch curve. Thickness can be symmetric, one side or the other side. Depth is To Next (grows to whichever side meets material) or a given Depth. Joins the result. |
| Thicken | BodyOps | Faces become a solid, one side (the sign picks the side) or symmetric. New Body by default; Join / Cut / Intersect also work. |
| Scale | BodyOps | Uniform, or non-uniform per axis, about a point (default: the origin). |
| Split Face | BodyOps | Splits the picked faces with a plane, face (optionally extended) or body. The body stays one solid. Faces are split one at a time and the history is merged. |
| Split Body | `src/part/features/Shell.cpp` | Pieces are now ordered along the plane normal, so the piece behind the plane keeps the original body id. New `extendTool`. The tool resolution is shared with Split Face (`resolveSplitTool`). |
| Move/Copy: Free Move | `Shell.cpp` (MoveBodyFeature) | New move type with typed X/Y/Z distances and X/Y/Z angles. The pivot is the selection centre, the origin, or typed coordinates. Rotations are applied X, then Y, then Z, then the translation. |
| Components pattern type | `src/part/features/Pattern.*` | Rectangular and Circular pattern accept `patternType = Components`. Each instance is an occurrence of the source's definition, listed in the hidden Json property `instances`. Occurrences are created or removed on OK (`prepareCommit`, inside the same undo macro as the feature command). Recompute writes their placements, so distance and axis edits are parametric. Mirror rejects Components. |
| Paste New | `src/assembly/PasteNew.*` | `assembly.paste-new` (Ctrl+Shift+V, also in the browser's component context menu). Makes a deep copy of the component sub-tree plus every timeline feature of the copied definitions. Feature, body and component ids are new, and history names are renamed (`Box1/Top` becomes `Box2/Top`). It is one undo step (`DocumentSnapshotCommand`), and Move starts afterwards like in Fusion. |

All of these are serialized through the normal property JSON. Every feature has a generated property panel; a headless test opens each dialog.

## Edits to shared files (expect conflicts here)

- `src/part/PartFeature.h/.cpp`: adds `Outcome::placements`, which commit() writes into components. Adds the virtuals `refsDistinguishedByPoint()`, `wantsCommitPreparation()` and `prepareCommit()`.
- `src/part/tools/FeatureTool.cpp`:
  - `addRefToInput`: point-distinguished refs. World pick points are converted to the body's local frame.
  - `accept()`: wraps prepareCommit() and the Add/Modify feature command in one undo macro.
- `src/part/PartModule.cpp`: two calls (`registerExtraFeatureTypes`, `registerExtraPartTools`). All new registrations live in `src/part/ExtraRegistration.cpp`, which keeps `PartTools.cpp` mostly untouched.
- `src/part/tools/PartTools.cpp`: tooltip text only (Hole, patterns, Fillet, Move).
- `src/assembly/AssemblyModule.cpp`: registers `assembly.paste-new`.
- `src/app/BrowserTree.cpp`: adds the "Paste New" entry to the component context menu.
- `src/part/features/{Fillet,Hole,Pattern,Shell}.*`: new properties are appended, and older documents without them load with the old behaviour.

## Things the integrator should know

- **Primitive naming fix.** Named faces must come from the same `BRepBuilderAPI_Transform` as the solid. If the solid and its faces are transformed separately, their `TopLoc_Location`s differ and lookups by name fail silently: the refs are re-matched by geometry, and a Warning appears.
- **OCCT boolean workaround.** Cutting a hole wall with a bare internal helical cutter can return the input unchanged with no error. `placeThread()` therefore fuses the hole's own core (air) into the cutter. Keep this if the code is refactored.
- **Bounding boxes in tests.** `core::bbox()` adds tolerance gaps and uses B-spline poles. Tests that check exact extents use `BRepBndLib::AddOptimal` (`tightBox` in the tests).
- **Hole clicks need a pick point.** Click-on-face placement relies on the viewer's world pick point in `ShapeRef::point`; the viewer already provides it.
- **New tests:** `src/part/tests/test_primitives_thread.cpp`, `test_bodyops.cpp`, `test_chair_split.cpp` (with `support/ChairFixture.h`, which follows `docs/comparison/CHAIR_SPEC.md`), and `src/assembly/tests/test_paste_new_pattern.cpp`.

## Follow-ups

- Drawings could render cosmetic threads and hole callouts from `ThreadFeature::threads()` and `HoleFeature::threads()`. No drawing code was touched.
- The main window's Edit menu has no Paste New entry yet. The tool is `hidden` and reachable by its shortcut, the command line (`PASTENEW`) and the browser menu.
- Component patterns:
  - Changing the quantity outside the dialog (for example from a parameter) only warns about missing occurrences. OK in the dialog re-syncs them.
  - Mirror of components is not supported.
- Variable fillet: intermediate radii apply only to single-edge chains. Longer tangent chains fall back to a linear law, with a warning.
- Rib: the growth direction is perpendicular to the chord of the curve chain. Strongly curved ribs are approximate.
- Modelled threads are capped at 200 turns.
- No new SVG icons; the new tools use the id-based icon fallback like the other part tools.
