# Integration notes

Module agents may not edit shared files (top-level `CMakeLists.txt`, `cmake/`, `src/core/`,
other modules' directories). When you need something there, append a dated entry to **your**
module's file in this directory and work around it locally in the meantime (an adapter,
wrapper or extension in your own directory). The integrator processes these files, applies
the change in core (with tests) and marks the entry done.

Files: `sketch.md`, `part.md`, `assembly.md`, `drawing.md`, `ui.md`.

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
