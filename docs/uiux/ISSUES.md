# FabriCAD UI/UX issues, round 1 (deduplicated)

Sources: `docs/uiux/round1/{viewer,sketch,solid,assembly-drawing}.md`. Original tester ids are kept as aliases.

Fix groups (they touch largely separate files, so they can be fixed in parallel worktrees):

- **G1 viewer**: `src/viewer/**` (ViewerWidget, occt/OcctView, SceneModel, ManipulatorPrs, PrimitivePrs)
- **G2 theme**: theme and app chrome: `resources/theme-light.qss`, `resources/icons`, `src/app/main.cpp`, DocumentTab, NavBar, Ribbon, ToolbarModel, CommandDialog, CommandLine, SketchPalette, MainWindow
- **G3 sketch**: `src/sketch/**`
- **G4 commands**: commands, browser and workspaces: `src/part/**`, `src/assembly/**`, `src/drawing/**`, `src/app/{BrowserTree,PropertyEditor,AppTools,MarkingMenuModel,Selectors}`

| Id | Aliases | Sev | Title | Group |
|---|---|---|---|---|
| UX-01 | VIEWER-1, SKETCH-2, SKETCH-3, SOLID-3, ASSEMBLY-DRAWING-2, ASSEMBLY-DRAWING-8 | critical | Viewport is not redrawn after scene changes (visibility, new body, display style, grid, edit, rollback, move) until the next navigation; torn or stale frames; Move triad too small | G1 |
| UX-02 | VIEWER-2, SKETCH-4, SOLID-1, ASSEMBLY-DRAWING-3, ASSEMBLY-DRAWING-4 | critical | Nothing can be picked in the viewport (body, face, edge, vertex, profile, joint position) and there is no preselection highlight | G1 |
| UX-13 | VIEWER-5, SKETCH-1, SOLID-2, ASSEMBLY-DRAWING-1, ASSEMBLY-DRAWING-9 | critical | Light text on light backgrounds; black checkboxes, combo popups, tables and file dialog (the system's dark Qt palette leaks through) | G2 |
| UX-22 | SKETCH-6 | critical | Sketch fillet on a dimensioned rectangle deletes the geometry and over-constrains the sketch | G3 |
| UX-04 | VIEWER-4, SOLID-4, SKETCH-9, ASSEMBLY-DRAWING-5 | high | Dragging the ViewCube does not orbit and clears the selection | G1 |
| UX-32 | VIEWER-3 | high | Move/Copy has no triad (only an X distance box) and does not accept viewport picks | G4 |
| UX-23 | SKETCH-7 | high | Editing a dimension moves unrelated geometry (hidden inference alignment constraint) | G3 |
| UX-24 | SKETCH-11 | high | Offset adds 10 DOF but draws nothing | G3 |
| UX-05 | VIEWER-6, VIEWER-7 | medium | ViewCube is small and low contrast; no hover feedback; garish cyan highlight that sticks; corner and edge hit zones too small | G1 |
| UX-06 | VIEWER-8 | medium | ViewCube has no home button, rotate arrows or step arrows | G1 |
| UX-07 | VIEWER-9 | medium | Fit/Home leaves the model under the nav bar; a ViewCube face click zooms in too far | G1 |
| UX-08 | VIEWER-10 | medium | Orbit shows perspective convergence while Orthographic is checked | G1 |
| UX-09 | VIEWER-11 | medium | A hidden body that is still selected is drawn as a translucent ghost | G1 |
| UX-10 | VIEWER-12, SKETCH-5 | medium | Origin planes are not shown or pickable during plane input; they are oversized and have no labels | G1 |
| UX-14 | VIEWER-17, SOLID-8, SKETCH-14 | medium | Document tabs are black with no clear active tab; ribbon tab strip shows black scroll arrows and clips the SOLID tab | G2 |
| UX-17 | VIEWER-19, ASSEMBLY-DRAWING-10 | medium | Command dialogs cover the model and ViewCube and change height; Dimension hint text is clipped | G2 |
| UX-25 | SKETCH-8, SKETCH-18 | medium | Dimension value editing: no editor on creation, Edit Dimension field not focused, Enter ignored | G3 |
| UX-26 | SKETCH-12 | medium | Profile detection uses construction curves; Show Profile is off by default; wrong fill colour | G3 |
| UX-28 | SKETCH-10 | medium | Constraint glyphs are plain letters; Fix glyph shows as empty boxes; dimension text is struck through by the line | G3 |
| UX-29 | SKETCH-15 | medium | A line started at the origin gets no coincident constraint | G3 |
| UX-30 | SKETCH-17 | medium | Ctrl+Z after Offset leaves sketch mode | G3 |
| UX-31 | SOLID-5 | medium | Rectangle: the second click does not commit; dynamic input focuses Height before Width | G3 |
| UX-33 | VIEWER-13 | medium | Box dialog Length/Width/Height fields ignore typed values | G4 |
| UX-34 | SOLID-6 | medium | Edit Feature on an extrude shows no live preview and hides the body | G4 |
| UX-35 | ASSEMBLY-DRAWING-6 | medium | Undo does not affect assembly operations; Move and Ground add no timeline entry | G4 |
| UX-36 | ASSEMBLY-DRAWING-7 | medium | Assembly context menus incomplete; Ground shows no pin; component naming | G4 |
| UX-37 | ASSEMBLY-DRAWING-11 | medium | Parts list shows only one component and overflows the sheet frame | G4 |
| UX-38 | SKETCH-13 | medium | Double-clicking a sketch in the browser both edits it and starts a rename | G4 |
| UX-11 | SOLID-10 | low | Body shading is close to the background; the selected body looks translucent | G1 |
| UX-12 | SOLID-7 | low | 3D text labels (manipulator values, glyphs) use a serif font | G1 |
| UX-15 | VIEWER-15 | low | The Hole ribbon icon is the same cube as Box | G2 |
| UX-16 | VIEWER-18 | low | Nav bar menus: blank first row in Settings; "Home Home" in Views | G2 |
| UX-18 | SKETCH-19 | low | The sketch palette covers geometry and cannot be moved | G2 |
| UX-19 | SOLID-11 | low | Help dialog row misaligned; command log popup overflows; first Enter swallowed | G2 |
| UX-20 | SOLID-9 | low | Status bar keeps the old body name after a rename | G2 |
| UX-27 | SKETCH-16 | low | Dashed inference line stays after the chain closes; fillet skews a regular polygon | G3 |
| UX-39 | VIEWER-14 | low | SELECT menu has no window/freeform/paint select, no filters and no check on the active mode | G4 |
| UX-40 | VIEWER-16 | low | Origin child items have no eye toggles | G4 |
| UX-41 | SOLID-13 | low | Marking menu offers "Repeat 2-Point Rectangle" during Extrude | G4 |
| UX-42 | SOLID-14 | low | The consumed sketch stays visible after Extrude (the origin-plane part is UX-10) | G4 |
| UX-43 | ASSEMBLY-DRAWING-12 | low | Drawing annotations collide; views stay cyan after a command; status shows "View of View" | G4 |

Dropped as test-harness artefacts: **SOLID-12** (opaque black marking menu: the rootful Xwayland test display has no compositor, so translucent windows show black). The black area below or beside the window and the window not filling the display come from the harness too, and no report depends on them. The dark-palette leak (UX-13) is **not** a harness artefact. It reproduces with the user's own qt6ct (noctalia) dark palette and `adw-gtk3-dark` GTK theme.
