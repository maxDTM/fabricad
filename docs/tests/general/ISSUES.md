# General test round 1: triaged issues

Source reports: `round1/{advanced-part,advanced-sketch,app-tools,general-bugs}.md`. 51 reported issues -> 49 canonical
issues (APP-TOOLS-3 and GENERAL-BUGS-9 merged). Dropped: ADVANCED-SKETCH-16 (recovery prompt listed autosaves of
other instances: `orphanedRecoveryFiles()` already skips sessions whose `session.lock` is held, so the listed copies were
real orphans of test instances killed by the parallel testers; harness artifact). Narrowed: ADVANCED-SKETCH-14 (the
945 px window was the harness display; only the palette covering the ViewCube at 1400 px is kept) and ADVANCED-SKETCH-9
(cmd box off-screen part was the narrow harness window; the ignored viewport keystrokes are kept).

Groups (separate worktrees, ordered by severity):

- **G1 doc**: document lifecycle, tabs, save, dirty state, drawing sheets, timeline, status bar (`src/app/MainWindow.cpp`, `DocumentTab`, `TimelineWidget`, `MarkingMenu`, `BrowserTree` root label, `src/core/Document.cpp`, `UndoStack`, `Feature.cpp`, `src/drawing/DrawingWorkspaceWidget.cpp`, `DrawingModel` sheets)
- **G2 app**: parameters, command line, command search, ribbon/TOOLS tab, preferences, appearance command, interference dialog, sketch palette (`ParametersDialog`, `CommandLine`, `CommandSearch`, `ShortcutRouter`, `Ribbon`/`ToolbarModel`/`AppTools`, `SketchPalette`, `src/assembly/tools/InterferenceTool.cpp`)
- **G3 part**: part features and feature dialogs, body copy/paste, profile picking, viewer display (`src/part/**`, `src/assembly/PasteNew*` + copy, `src/app/Selectors.cpp`, `src/viewer/ViewerWidget.cpp` display drawer)
- **G4 sketch**: sketch solver, sketch tools, profiles, typed entry (`src/sketch/**`)

| ID | Aliases | Sev | Title | Group |
|---|---|---|---|---|
| DOC-01 | GENERAL-BUGS-2 | medium | Closing the Design tab of a document that has a Drawing tab skips the save prompt | G1 |
| DOC-02 | GENERAL-BUGS-1 | medium | Browser dock permanently missing after closing the Drawing tab | G1 |
| DOC-03 | GENERAL-BUGS-3 | medium | Sheets panel, sheet tabs and canvas out of sync after undo of sheet deletion; wrong sheet deleted | G1 |
| DOC-04 | GENERAL-BUGS-4 | medium | Failed Save renames the document to the failed filename and keeps the bad path | G1 |
| DOC-05 | GENERAL-BUGS-6 | low | Suppressing a sketch does not affect or flag the dependent Extrude | G1 |
| DOC-06 | APP-TOOLS-11 | low | Timeline drag reorders a feature before its producer without validation | G1 |
| DOC-07 | GENERAL-BUGS-8 | low | Undo of body deletion renames Body1 to Body2 | G1 |
| DOC-08 | GENERAL-BUGS-7 | low | Stale error text in the status bar after the cause is undone or a new command starts | G1 |
| DOC-09 | GENERAL-BUGS-11 | low | Dirty markers out of sync after save / redo to the saved state | G1 |
| DOC-10 | GENERAL-BUGS-13 | low | All new docs named "Untitled"; new Drawing dirty at once, blank page, empty Sheets panel | G1 |
| DOC-11 | GENERAL-BUGS-12 | low | Tab strip repaint glitch after dragging a tab | G1 |
| DOC-12 | GENERAL-BUGS-15 | low | Marking menu label truncated; timeline item stays framed after Cancel | G1 |
| APP-01 | APP-TOOLS-1 | high | Parameters dialog: renaming to an existing name silently overwrites the other parameter | G2 |
| APP-02 | APP-TOOLS-2 | high | Parameters dialog: renaming to an invalid name deletes the parameter | G2 |
| APP-03 | APP-TOOLS-4 | medium | No Preferences/units dialog; TOOLS tab has only Export; "mm" status label inert | G2 |
| APP-04 | APP-TOOLS-5 | medium | Appearance: A shortcut inert, not in MODIFY or search; A in browser starts type-ahead/rename | G2 |
| APP-05 | GENERAL-BUGS-5 | low | A parameter in use can be removed in Change Parameters | G2 |
| APP-06 | ADVANCED-SKETCH-10 | low | Enter in empty cmd box repeats the last command and cancels a pattern prompt ("Enter applies") | G2 |
| APP-07 | APP-TOOLS-6 | low | ":" focuses the command box but also inserts a colon | G2 |
| APP-08 | APP-TOOLS-10 | low | Ctrl+Z in cmd box recalls old text (QLineEdit undo) instead of model undo | G2 |
| APP-09 | APP-TOOLS-7 | low | Documented commands UNITS and ZOOM missing | G2 |
| APP-10 | APP-TOOLS-8 | low | S command search: noisy matches, disabled commands ranked first | G2 |
| APP-11 | APP-TOOLS-9 | low | Interference dialog: volume column truncated, intersection volume not shown | G2 |
| APP-12 | ADVANCED-SKETCH-14 | low | Sketch Palette overlaps and clips the ViewCube | G2 |
| PART-01 | ADVANCED-PART-2 | high | Modeled thread on a plain cylinder fails ("boolean result is not a valid shape") | G3 |
| PART-02 | APP-TOOLS-3, GENERAL-BUGS-9 | medium | Invalid expressions in feature dialogs coerced silently; field red but OK enabled and commits | G3 |
| PART-03 | ADVANCED-SKETCH-8 | medium | Extrude: one profile click previews every profile of the sketch | G3 |
| PART-04 | ADVANCED-PART-1 | medium | Torus dialog opens with invalid sizes (tube 50 > ring 40), OK disabled | G3 |
| PART-05 | ADVANCED-PART-4 | medium | Paste New (Ctrl+Shift+V / Ctrl+V) of a copied body does nothing, no message | G3 |
| PART-06 | ADVANCED-PART-3 | medium | Thread dialog: no size/designation/class fields when Auto size is off | G3 |
| PART-07 | ADVANCED-PART-7 | low | Dialogs pre-fill the previously typed value (Offset Plane, Press-Pull) | G3 |
| PART-08 | ADVANCED-PART-5 | low | 2 mm fillet on a split/drafted box edge fails as invalid shape | G3 |
| PART-09 | GENERAL-BUGS-10 | low | Faces selection filter shows hidden origin planes; Fillet button stays highlighted | G3 |
| PART-10 | GENERAL-BUGS-14 | low | 500 coincident pattern bodies: no overlap warning, black z-fighting, slow redraw | G3 |
| PART-11 | ADVANCED-PART-6 | low | Seam edges drawn on sphere/torus/cylinder/cone | G3 |
| PART-12 | ADVANCED-PART-8 | low | First face click in Offset Plane sometimes not registered | G3 |
| SK-01 | ADVANCED-SKETCH-1 | high | Dragging / dimensioning an ellipse or elliptical-arc centre distorts and rotates it | G4 |
| SK-02 | ADVANCED-SKETCH-6 | medium | Point-to-point dimension negative; editing it flips geometry to the other side | G4 |
| SK-03 | ADVANCED-SKETCH-2 | medium | Trim is a silent no-op on a spline; Break on a spline says "Cannot break here" | G4 |
| SK-04 | ADVANCED-SKETCH-3 | medium | Fit-point spline closed on its first point is not closed (extra point, no profile) | G4 |
| SK-05 | ADVANCED-SKETCH-4 | medium | Offset of an ellipse produces no geometry | G4 |
| SK-06 | ADVANCED-SKETCH-5 | medium | Ellipse cannot be dimensioned; second-entity picks (origin, line) silently ignored | G4 |
| SK-07 | ADVANCED-SKETCH-7 | medium | Polygon's construction circle splits the polygon into extra profiles | G4 |
| SK-08 | ADVANCED-SKETCH-9 | medium | Viewport typed entry ignored in Ellipse/Pattern/Scale/Chamfer/Text prompts; letters switch tools | G4 |
| SK-09 | ADVANCED-SKETCH-11 | low | Text dialog field ignored when text was typed in the cmd box; overstruck preview | G4 |
| SK-10 | ADVANCED-SKETCH-12 | low | Origin axes cannot be picked as the mirror line | G4 |
| SK-11 | ADVANCED-SKETCH-15 | low | Stale sketch status/UI ("8 selected" after Delete, Extend message in Break, old Sides box) | G4 |
| SK-12 | ADVANCED-SKETCH-17 | low | Finished sketch shows no profile shading | G4 |
| SK-13 | ADVANCED-SKETCH-13 | low | ux.md sketch tools missing (slot variants, polygon by edge, conic, intersect, include 3D, project to surface, spline handles) | G4 |

Dropped: ADVANCED-SKETCH-16 (harness: orphaned autosaves of killed test instances).

Cross-group notes:
- G2 owns `CommandLine.cpp` / `ShortcutRouter.cpp` (APP-06..08); SK-08 should fix typed entry in the sketch tools and `ValueInputOverlay` and only add a minimal router hook if needed.
- SK-07 (`SketchProfiles.cpp`) and PART-03 (profile refs / picking in `Selectors.cpp`, `PartUtils::profileFaces`) both change Extrude profile behaviour; verify together after merge.
- PART-09's stuck toolbar highlight may need `Ribbon.cpp` (G2); PART-02 adds an accept-disable path in `PropertyPanel`/`FeatureTool` only.
- APP-01/02/05 should stay in `ParametersDialog` plus Document parameter API calls; G1 owns `Document.cpp` (DOC-05..07). APP-04's browser type-ahead fix touches `BrowserTree.cpp`, also used by DOC-09 (root label).
- DOC-08 (status clearing in `MainWindow`) and SK-11 (sketch tool messages) both address stale status text.
- PART-04 and PART-07 may share one root cause (values carried over between dialogs; Torus defaults in `Primitives.cpp` are 40/10).
