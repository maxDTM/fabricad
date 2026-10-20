# Integration notes

Module agents may not edit shared files (top-level `CMakeLists.txt`, `cmake/`, `src/core/`,
other modules' directories). When you need something there, append a dated entry to **your**
module's file in this directory and work around it locally in the meantime (an adapter,
wrapper or extension in your own directory). The integrator processes these files, applies
the change in core (with tests) and marks the entry done.

Files: `sketch.md`, `part.md`, `assembly.md`, `drawing.md`, `ui.md`. Round-2 hand-over notes:
`round2-icons.md`, `round2-features.md`, `round2-sketch-drawing.md`, `round2-multidoc.md`,
`round2-sketch-input.md`, `round2-manipulators.md` (merged into `main` on 2026-10-20, see the
status entry at the end of this file).

Entry format:

```
## 2026-10-19 — short title                      (status: open | done | rejected)
What I need: ...
Why: ...
Workaround in place: ...
Suggested change (header/signature): ...
```

## Integration status (2026-10-19, integrator)

All modules are wired into `fabricad` (`src/app/main.cpp`, `MainWindow::registerModules`):
sketch, part, assembly, drawing tools in the ribbon / search / marking menu / command line, the
Drawing workspace widget as central widget, features listed under their component in the browser,
occurrences displayed by the viewer. Core changes applied (see the `done` entries in each file):
`Document::addComponent(Component)`, `definitionOf/displayedBodies`, `namedSubShape` + name-aware
`RecomputeContext::resolve`, `Feature::component()/editToolId()`, `ToolInfo::keepsActiveTool`,
`ToolManager::toolStarted`, QKeySequence-normalised shortcuts, `fromJson` keeps the undo stack,
stable face-plane origins (`facePlane` projects the world origin), stable extrude/revolve side
names (`Side/c<curveId>`). Tests: `src/core/tests/test_integration_core.cpp`, `tests/e2e/test_e2e.cpp`.

## Integration status (2026-10-20, round 2)

Branches merged in the order icons, features, sketch-drawing, multidoc, sketch-input,
manipulators. Integration changes on top of the branches: icons for every round-2 tool and
feature type (`generate_icons.py`); drawing hole callouts read the part Hole's `tapType` /
`threadSize` / `threadFullDepth`; `Sketch::retargetTrimmedDimensions()` keeps typed (driving)
dimensions valid through sketch fillets / chamfers (virtual sharp) instead of distorting the
sketch; `DIMEDIT d1 <expr>` accepts dimension names; `MainWindow` cancels every tab's command
before tearing down (crash when a script stopped inside a dimension command); manipulator
providers for Box / Cylinder / Sphere / Torus / Coil / Thicken / Rib; the feature value box is
anchored at the manipulator handle; `ISO` alias for the isometric view; `ARCHITECTURE.md`
section 8 / 13 updated for the new command routing and the manipulator API. New tests:
`src/sketch/tests/test_fillet_dimensions.cpp`, `primitivesAndThickenArrows` in
`src/part/tests/test_manipulators.cpp`.
