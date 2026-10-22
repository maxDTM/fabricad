# Functional test round 1: triaged issues

Source reports: `round1/{sketch-core,part-core,doc-core,asm-core}.md`. 39 reported issues, 39 kept (none caused by the
test harness, none out of scope). SKETCH-CORE-4 is narrowed: the relative angle is how Fusion works, so only the
silent overlapping or duplicate segment is a defect. SKETCH-CORE-3 is kept but marked unconfirmed.

Groups (separate worktrees, ordered by severity):

- **G1 doc**: document lifecycle, autosave/recovery, app shell (`src/app/MainWindow.cpp`, `DocumentTab`, `TimelineWidget`, `Theme`, `src/core/Document.cpp`, `UndoStack`, `Feature.cpp` warnings)
- **G2 part**: part features (`src/part/features/*`, `PartUtils.cpp`, `part/tools/FeatureTool.cpp`, primitives)
- **G3 asm**: assembly (`src/assembly/**`, `BrowserTree.cpp` component nodes, viewer ghosting and picking)
- **G4 sketch**: sketch tools and session, keyboard router, viewer redraw and fit, command dialog placement (`src/sketch/**`, `ShortcutRouter.cpp`, `CommandDialog.cpp`, `ViewerWidget` fit/redraw)

| ID | Aliases | Sev | Title | Group |
|---|---|---|---|---|
| DOC-01 | DOC-CORE-1 | high | Autosave file is keyed by document name: unsaved Untitled docs and separate instances overwrite each other's recovery file | G1 |
| DOC-02 | DOC-CORE-2 | medium | Accepting recovery deletes the autosave and nothing is rewritten until the next edit | G1 |
| DOC-03 | DOC-CORE-3 | medium | Opening a file that is already open can create a duplicate tab (path not canonicalised) | G1 |
| DOC-04 | DOC-CORE-5 | medium | "Re-matched by geometry" warnings flag unrelated features and stay after undo | G1 |
| DOC-05 | ASM-CORE-8 | low | Dirty marker (*) lost after undoing a delete | G1 |
| DOC-06 | DOC-CORE-6 | low | Document name does not follow the file name on Save As or after an import | G1 |
| DOC-07 | DOC-CORE-7 | low | Invalid .fcad shows raw JSON parser text; a load with failed features gives no summary | G1 |
| DOC-08 | PART-CORE-7 | low | Failed downstream feature shows only a red icon; no visible message | G1 |
| DOC-09 | PART-CORE-6 | low | Deleting a feature with dependents from the timeline asks for no confirmation | G1 |
| DOC-10 | PART-CORE-5 | low | Timeline context menu and tooltips unreadable (black background) | G1 |
| DOC-11 | DOC-CORE-4 | low | Same-named tabs and Open Recent entries can't be told apart (no path tooltip) | G1 |
| DOC-12 | DOC-CORE-8 | low | Application menu stays open and swallows Escape/Ctrl+O after hovering Open Recent | G1 |
| PART-01 | PART-CORE-1 | high | Pattern quantity 100000 freezes the UI and uses gigabytes of RAM (no cap) | G2 |
| PART-02 | PART-CORE-2 | medium | Shell thickness larger than the body reports success with no change | G2 |
| PART-03 | PART-CORE-3 | medium | Two-distance chamfer gives a corrupted solid that is accepted as success | G2 |
| PART-04 | PART-CORE-4 | medium | Cut extrude that hits nothing is accepted silently; the default Cut direction points away from the body | G2 |
| PART-05 | ASM-CORE-12 | low | Box defaults to Join for a body that does not touch | G2 |
| ASM-01 | ASM-CORE-1 | high | Scene stays ghosted after activating root; ghosted bodies can't be picked (blocks Joint) | G3 |
| ASM-02 | ASM-CORE-2 | high | Deleting a component leaves its body and a dangling, still drivable joint | G3 |
| ASM-03 | ASM-CORE-3 | high | Paste New creates an empty component; occurrences show no tree children | G3 |
| ASM-04 | ASM-CORE-4 | medium | Move Component: typing a rotation rewrites the typed distance fields | G3 |
| ASM-05 | ASM-CORE-5 | medium | Joint Animate then OK commits the animation's leftover angle | G3 |
| ASM-06 | ASM-CORE-6 | medium | Origin planes pickable during Joint; a face pick becomes "Untitled (Geometry)" | G3 |
| ASM-07 | ASM-CORE-7 | low | Drive Joints accepts values outside the displayed limits | G3 |
| ASM-08 | ASM-CORE-9 | low | Move/Copy ignores components; Move Component clears the preselection | G3 |
| ASM-09 | ASM-CORE-10 | low | Ground: toolbar has no Unground; a grounded component moves without warning | G3 |
| ASM-10 | ASM-CORE-11 | low | Joint dialog polish: clipped Select buttons, stale error, As-built pick flow, silent ComponentOrigin prefill | G3 |
| SK-01 | SKETCH-CORE-1 | medium | Ctrl+Return / Ctrl+KP_Enter does not finish the sketch | G4 |
| SK-02 | SKETCH-CORE-2 | medium | Viewport stale after constraint/fillet until the mouse moves | G4 |
| SK-03 | SKETCH-CORE-3 | medium | Rectangle placed far from the clicks on an angled-face sketch (unconfirmed; likely clicks during the camera animation) | G4 |
| SK-04 | SKETCH-CORE-9 | low | Angled-face sketch orientation differs between create and edit | G4 |
| SK-05 | SKETCH-CORE-6 | low | Command dialog docks left over the profiles (spec: right edge) | G4 |
| SK-06 | SKETCH-CORE-8 | low | Sketch fillet: typed radius ignored, fixed 5 mm, no radius dimension | G4 |
| SK-07 | SKETCH-CORE-10 | low | Driven dimension prompts for a value, then errors | G4 |
| SK-08 | SKETCH-CORE-12 | low | Esc Esc does not always leave the Dimension tool | G4 |
| SK-09 | SKETCH-CORE-11 | low | Status bar shows duplicate or stale DOF labels | G4 |
| SK-10 | SKETCH-CORE-7 | low | Ctrl+A does not select all in a sketch | G4 |
| SK-11 | SKETCH-CORE-5 | low | F6 Fit ignores sketch geometry | G4 |
| SK-12 | SKETCH-CORE-4 | low | Line chain: retracing segment creates an overlapping line and duplicate dimension with no warning | G4 |

Cross-group notes: G1 owns `MainWindow.cpp`, but G4's Ctrl+Return fix stays in `ShortcutRouter.cpp` plus the sketch
SelectTool. G3 and G4 both edit `ViewerWidget.cpp`, in separate functions (ghosting/picking vs. fitAll/redraw). G3 edits
`BrowserTree.cpp` for component activation and occurrence children; G1 should keep DOC-08 out of the browser tree,
in the status bar or timeline. DOC-05 (`UndoStack`/`Document`) may need G3's delete-component undo checked.
