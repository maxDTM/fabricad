# G4-sketch fix plan: sketch tools, key routing, viewer redraw/fit, dialog placement

Status: plan only, nothing implemented. Sources: round1/sketch-core.md and the screenshots, plus a code read at 8e20c27.
Ownership: G4 owns ShortcutRouter.cpp, the sketch module, CommandDialog.cpp, and in ViewerWidget.cpp only fitAll, lookAt and redraw/hover. MainWindow.cpp belongs to G1 and must not be edited here.

## SK-01 (medium): Ctrl+Return / Ctrl+KP_Enter does not finish the sketch
- Root cause:
  - `routeKey` sets `r.toolFirst = ctx.toolActive` before its switch (ShortcutRouter.cpp:72). Ctrl+Return is therefore offered to the tool first.
  - Sketch `SelectTool::keyPress` eats every Return/Enter, whatever the modifiers (SketchModeTools.cpp:402). MainWindow.cpp:1484 then returns before the shortcut lookup.
  - Keypad Enter gives `keySequenceText` = "Ctrl+Enter". MainWindow.cpp:1520 compares it with `normalizeShortcut("Ctrl+Return")`, so the strings do not match.
- Fix (router and sketch only):
  - In the Return/Enter case, when `mods == Ctrl`, set `r.toolFirst = false` and `r.sequence = keySequenceText(Qt::Key_Return, mods)`. Keypad Enter then maps to "Ctrl+Return".
  - In `SelectTool::keyPress`, eat Return/Enter only when no Ctrl/Alt/Meta modifier is held.
  - Optionally: in the `!canvasFocus` branch, rewrite the sequence the same way, so Ctrl+KP_Enter in a dialog field also finishes the sketch.
- Effects and how to avoid them:
  - The active tool no longer sees Ctrl+Return. No tool uses it (grep: ValueInput already ignores combos, ValueInput.cpp:388).
  - A value typed but not yet committed in a line or rectangle box is dropped, because `SketchSession::leave` cancels the tool. This matches Fusion. If that is not wanted, commit the locked boxes in `FinishSketchTool::activate` first.
  - In the drawing workspace, `sketch.finish` is disabled, so the lookup fails and `handleKeyPress` returns false. The key then reaches the DrawingCanvas as before.
  - With the browser focused (not a text field), Ctrl+Return now finishes the sketch.
  - The plain-Return rename the tester saw: BrowserTree already has `NoEditTriggers` (BrowserTree.cpp:61, G3 file). Re-check it on the new binary and do not touch it here.
  - Tests: test_app_logic.cpp:97 still passes. Add cases for `routeKey(Key_Enter, Ctrl, toolCtx)`: `toolFirst == false` and `sequence == "Ctrl+Return"`. Add a sketch test: with the select tool active, `keyPress(Ctrl+Return)` returns false.

## SK-02 (medium): viewport stale after a constraint or fillet until the mouse moves
- Root cause (not confirmed):
  - `commitEdit` calls `render()`, which removes and re-adds every preview, then calls `requestRedraw()` (SketchSession.cpp:401-414). That leads to `OcctView::redrawAll` (Invalidate + update).
  - In shots 71 and 72 the whole frame is old (the new fillet is missing) while the status text has already updated. So this is more than a hover leftover.
  - Candidates:
    - (a) `render()` runs inside `paintGL`/`FlushViewEvents`, so the `update()` it calls is coalesced or lost.
    - (b) A detected owner of a removed preview stays in the immediate layer.
    - (c) A harness timing artifact: the screenshot is taken before the GL widget recomposites.
- Fix:
  1. First reproduce under Xvfb with a log in `paintGL` and `requestRedraw`, and wait 500 ms before the screenshot to rule out (c).
  2. Sketch side, always safe: at the end of `SketchSession::render()`, also post `QTimer::singleShot(0, viewer widget, requestRedraw)`, coalesced with a pending flag.
  3. Viewer side, in redraw/hover only: in `removePreview`/`clearPreviews`, call `ctx->ClearDetected(false)` and reset `hoverRef_` when the removed object is the detected one.
- Effects and how to avoid them:
  - One extra full redraw per sketch edit. Coalesce it so drags (live solve calls `render` on every move) do not double the frame cost.
  - Clearing hover makes the highlight blink off until the next move. Only clear it when the detected object is the one being removed.
  - ViewerWidget.cpp is shared with G3. Touch only `removePreview`, `clearPreviews` and `requestRedraw`.
  - Headless tests use `NullViewer` (`d_->ready()` is false). Guard every OCCT call with `ready()`.

## SK-03 (medium, unconfirmed): rectangle lands far from the clicks on an angled-face sketch
- Root cause (likely):
  - `enterSketchMode` calls `lookAt(plane, true)`, a 0.3 s animation (ViewerWidget.cpp:1949, 2021).
  - Clicks during the animation are projected with the camera mid-animation by `buildEvent` → `screenRay` (ViewerWidget.cpp:~1131).
  - Second suspect: the viewer keeps `sketchPlane_` from `enter()`, but `SketchSession::render()` re-reads `plane_` from the feature (SketchSession.cpp:408). The grid snap and the plane intersection can then use a different frame from `toSketch`. This links to SK-04.
- Fix:
  - On mouse press while a camera animation runs, jump to the end camera first. Add `OcctView::finishAnimation()`, which copies the end camera and stops the animation. OcctView is in no group, so tell G3 about it. Call it at the top of `onMousePress` and `buildEvent` for press/release events.
  - In `SketchSession::render`, when `plane_` changes, call `viewer_->enterSketchMode(plane_, false)` (or a new `setSketchPlane`) so the viewer plane stays in sync.
- Effects and how to avoid them:
  - Clicking during any view animation (ViewCube, Home, Fit) now snaps the camera to its target. That is acceptable and matches Fusion.
  - `enterSketchMode(..., false)` must not re-run `lookAt` or reset the grid each time. Add a cheap setter instead.

## SK-04 (low): angled-face sketch is oriented differently on create and on edit
- Root cause:
  - The view up vector is `plane.YDirection()` (ViewerMath.cpp:135).
  - `facePlane()` builds the axes from the surface parametrisation and flips the normal when the face is REVERSED, keeping X (ShapeUtils.cpp:120-139). So Y depends on which face instance is returned.
  - On create, `resolvePlane` → `resolveShape` takes the face from `TopExp::MapShapes` of the body (GeometryRefs.cpp:93-95, oriented as in the solid).
  - On recompute, `ctx.resolve` returns the history-name face `named` (Feature.cpp:274-285), whose orientation can differ.
  - Shots 85 and 95: Z stays right while Y flips, so the eye is on the other side of the plane. That is a flipped normal, not just a rotation.
- Fix (sketch side):
  - In `SketchFeature::recompute`, take the face instance that is in the owner body: after `ctx.resolve`, use the `ref.index` it refreshed and re-resolve with `resolveShape(doc, ref, state)`.
  - Even better, ask G1 to return `map(idx)` instead of `named` at Feature.cpp:285, a one-line change in a G1 file.
  - Make the axes deterministic:
    - normal = the outward normal of the face as it sits in the solid;
    - X = world X projected on the plane (fall back to world Y, then Z, when nearly parallel);
    - Y = N × X.
  - Store the axes in the sketch JSON (`planeAxes`). On later recomputes, keep the stored X projected onto the new plane, and keep the sign of the normal consistent with the stored one.
- Effects and how to avoid them:
  - Changing the axes rotates or mirrors the local coordinates of existing face sketches, so saved documents would move their geometry.
  - Apply the canonical rule only when `planeAxes` is missing and the sketch is new. For a loaded file without `planeAxes`, store the axes the current code computes on first recompute.
  - Origin planes and construction planes are not affected (that path does not go through `facePlane`).
  - Run test_feature, test_e2e and chair.fcs (its sketches are on offset planes).
  - `core::facePlane` is shared with part features (PartUtils `planeOf`, G2). Do not change `facePlane` itself.

## SK-05 (low): command dialog docks on the left, over the profiles
- Root cause: `CommandDialog::place()` defaults to `QPoint(kLeftMargin, kTopMargin)` (CommandDialog.cpp:181). ux.md says "docked to viewport right edge" (ux.md:62).
- Fix:
  - Default x = `area.width() - width() - kRightMargin`.
  - Default y = just below the ViewCube. The cube centre is at `kCubeOffset` (115 device px) from the top-right corner, size 80, so y ≈ 115/dpr + 40 + 8. Expose the cube's bottom edge from ViewerWidget, or keep a constant of about 180 logical px.
  - Clamp the height to the space left.
  - The double-click on the title docks back to the right, so update the comment at :242.
- Effects and how to avoid them:
  - SketchPalette also docks top-right and avoids the dialog only when they overlap (SketchPalette.cpp:248-258). It then stacks below the dialog, or moves left of it when there is no room.
  - Check the sketch dialogs (offset, dimension value) together with the palette on a 945 px tall window. Update the stale comment at SketchPalette.cpp:248.
  - All command dialogs move, including the assembly ones (G3 ASM-10 is inside the JointTool content, not the frame). Tell G3. Check the clamp on narrow viewports (< 300 + margin).
  - The less body height below the cube means more scrolling. Keep the `kMaxBodyHeight` cap based on `vpH - top`.

## SK-06 (low): sketch fillet ignores a typed radius, is fixed at 5 mm, and adds no radius dimension
- Root cause:
  - `FilletTool` has only `commandLineInput` (ModifyTools.cpp:186) and no `defineValues` box. Canvas digits go to shortcuts, and Enter accepts or exits.
  - `apply()` adds no Radius constraint (ModifyTools.cpp:205-235).
- Fix:
  - `defineValues({lengthField("radius")})`, always visible, with the value set to `radius_` near the cursor.
  - `commitValues` locks `radius_ = lastRadius_` and applies the fillet when two picks exist. The preview uses `lockedValue("radius")`.
  - Keep the `commandLineInput` override; test_tools.cpp:951 uses `runCommandLine("2")`.
  - After a successful fillet, inside the same edit before `commitEdit`, add Radius(arc) = radius through `addLockedDimension` (driven if redundant).
  - For later fillets in the same tool run, add Equal(arc, firstArc) instead. This matches Fusion and avoids clutter.
- Effects and how to avoid them:
  - Each fillet removes one DOF, so DOF counts and constraint counts change in test_tools.cpp:943-958, test_scripting, and the chair.fcs SFILLET 20 x2 (geometry unchanged).
  - Do not add the dimension inside `Sketch::fillet`; test_fillet_dimensions.cpp adds Radius itself at :68-69 and would become over-constrained.
  - Esc must still clear the box first and exit second (base `handleValueKey` already does this).

## SK-07 (low): driven dimension asks for a value and then errors
- Root cause: in `DimensionTool::place`, after the "make driven" answer, the code falls through to `phase_ = Phase::Value` and opens the edit dialog (ModifyTools.cpp:1132-1143). `applyDimensionText` then rejects driven dimensions.
- Fix: after `driven = true; solve()`, call `session()->commitEdit("Dimension")`, `reset()`, `setPrompt` and `refreshToolDialog`, then return without entering Value. The task lists ConstraintTools.cpp, but the code is in ModifyTools.cpp.
- Effects and how to avoid them:
  - Headless tests pass `askYesNo == true` (no host), so they also skip the value phase. Check test_tools for a dimension test that types a value after an over-constrained placement.
  - Undo: still one "Dimension" command.

## SK-08 (low): Esc Esc does not always leave the Dimension tool
- Root cause:
  - In the Value phase the value QLineEdit in the CommandDialog has focus.
  - The first Esc reaches `CommandDialog::keyPressEvent` → `cancelled` → tool `cancel` → `finishValue`. That commits the value and dismisses the dialog (`refreshToolDialog` → `dismiss`, CommandDialog.cpp:125).
  - Focus then moves to the next widget in the chain, often the command line, which is a text input. The second Esc is routed `None` (ShortcutRouter `!canvasFocus`) and never reaches the tool, so the tool stays active.
- Fix: in `CommandDialog::dismiss()`, when `isAncestorOf(QApplication::focusWidget())`, call `viewport_->setFocus(Qt::OtherFocusReason)` before `hide()`. The second Esc then goes to the canvas, `cancel()` sees `picks_` empty, and `ctx.cancel()` runs.
- Effects and how to avoid them:
  - Every tool whose dialog closes now gives the focus back to the viewer. That is right for canvas shortcuts.
  - Never steal focus that is outside the dialog, such as the command line while the user types there.
  - test_main_window key tests: check that `focusWidget` after OK/Cancel is the viewer.

## SK-09 (low): status bar shows duplicate or stale DOF labels
- Root cause: the DOF text goes to three places:
  - prompts: `setPrompt(statusText())` at SketchModeTools.cpp:250, 332 and 395, and ModifyTools.cpp:302;
  - messages: SketchSession.cpp:249, ConstraintTools.cpp:347, ModifyTools.cpp:1148 and 1231;
  - the MainWindow `sketchLabel_` (refreshed on `recomputeFinished`).
  Undo does not refresh the prompt copy.
- Fix:
  - Replace those prompts with a static hint (for the select tool: "Select sketch geometry; drag to move"). Drop the DOF `showMessage` calls, or replace them with a neutral "Dimension added" / "Constraint added".
  - Emit `SketchSession::changed()` from the session's `recomputeFinished` handler too, so the label follows undo and redo.
- Effects and how to avoid them:
  - Tests use `statusText()` directly (test_tools.cpp:748, 1160), so they are unaffected. grep for prompt or message assertions that contain "DOF" before merging.
  - ScriptTools.cpp:507 and Selectors.cpp:1416 keep using `statusText()`.
  - No MainWindow edit is needed.

## SK-10 (low): Ctrl+A does not select all in a sketch
- Root cause: no tool has the Ctrl+A shortcut. `isStandardEditKey` reserves it only for text fields (ShortcutRouter.cpp:42).
- Fix:
  - New instant tool `sketch.select-all` (shortcut Ctrl+A, tab SKETCH, `keepsActiveTool = true`, enabled only with a session) in SketchModeTools.cpp.
  - It sets `ctx.selection` to every non-external geometry ref and calls `session()->render()`.
  - Selecting all bodies outside a sketch needs a Design-workspace tool. Ask G1 to add `select.all` in AppActions.cpp; do not add it here.
- Effects and how to avoid them:
  - `triggerShortcut` already ranks SKETCH tools first in sketch mode, and disabled tools are skipped.
  - With `keepsActiveTool`, a running line tool is not cancelled.
  - Ctrl+A with the browser focused now selects sketch geometry instead of the tree's own select-all, which is acceptable.
  - Delete after Ctrl+A removes everything; this is undoable.

## SK-11 (low): F6 Fit ignores sketch geometry
- Root cause: `fitAll` uses `hasGeometry()` (`!objects.empty()`) and `sceneBox()`, which cover document bodies only (ViewerWidget.cpp:219-243, 2041-2052). The active sketch is hidden from the scene by `setFeatureVisibilityOverride` and drawn as previews.
- Fix:
  - In `fitAll` only, build the box from `sceneBox()` plus, in sketch mode, the bounding box of every preview object (`PrsMgr_PresentableObject::BoundingBox`). Skip `AIS_TextLabel` and other zoom-independent previews.
  - Use the 250-scale fallback only when that combined box is void.
  - Leave `hasGeometry()` itself unchanged; it also drives Home/Iso and the initial fit (:476, :490, :2037).
- Effects and how to avoid them:
  - Glyphs and arrows are sized in pixels and only re-rendered after a wheel, release or resize (SketchSession.cpp:131-150). After F6 they keep the old size.
  - Add a re-render after the fit animation (session hook on a viewer `cameraChanged`, or a delayed check in the session).
  - Transient tool previews (rubber band) also count, which is harmless.
  - Edit only `fitAll` because ViewerWidget.cpp is shared with G3.

## SK-12 (low): line chain retrace silently makes an overlapping line and a duplicate dimension
- Root cause:
  - `LineTool::addPoint` accepts a segment that is collinear and opposite to the previous one (CreateTools.cpp:~515).
  - Angle 180 gives `addRightAngleInstead` → Parallel. The endpoint is inferred coincident with the previous start, so the typed length is redundant, and `addLockedDimension` adds it as a driven "(20)" with only a message (SketchToolBase.cpp:276-282).
- Fix:
  - In `addPoint`, when `lastLine_ >= 0` and the new direction is antiparallel to `lastDir_` (|cross| < eps, dot < 0), reject the segment with an error message ("segment would retrace the previous line"). Keep the chain alive and do not commit.
  - In LineTool only (not the shared `addLockedDimension`), skip a typed dimension that is redundant when its measured value already equals the typed value.
- Effects and how to avoid them:
  - A deliberate back-and-forth construction line is no longer possible in one chain. The user ends the chain and starts again, as in Fusion.
  - Rectangle and polygon tools are unaffected because the base helper is untouched.
  - Check test_tools line-chain tests for any 180-degree typed angle.

## Order and risk
- Order: SK-01, SK-08, SK-07, SK-09, SK-05 (small and low risk), then SK-06, SK-10, SK-12, SK-11, then SK-03, SK-02 (needs a repro), and SK-04 last (data compatibility).
- Build with -j2. Run the sketch, app and e2e ctest suites and chair.fcs after each group of changes.

## Done (branch fn/G4-sketch)

Verification: full ctest (QT_QPA_PLATFORM=offscreen) 56/56 passed, including chair.fcs and test_e2e. GUI checks used real X input on a private Xwayland :44. Screenshots are in docs/tests/functional/fixes/G4-sketch/ in the main checkout.
Harness note: Hyprland tiled the rootful Xwayland window to about 945x490 or smaller, so the app ran with QT_SCALE_FACTOR 0.3-0.5. Because the device pixel ratio was below 1, the ViewCube was drawn far too large. That is a harness artefact. Xvfb is not installed.

- **SK-01**: In ShortcutRouter.cpp, keypad Enter now gives the same shortcut text as Return ("Ctrl+Return"), also from text fields, and Ctrl+Return/Enter skips the tool (`toolFirst = false`). In SketchModeTools.cpp, `SelectTool` eats Return/Enter only when neither Ctrl nor Alt is held. Tests: test_app_logic `routeCanvasKeys`, test_tools `ctrlEnterAndSelectAll`, test_main_window `ctrlEnterFinishesSketch` (Return and keypad Enter through `MainWindow::handleKeyPress`). GUI: Ctrl+KP_Enter finished the sketch (sk01-ctrl-kp-enter.png).
- **SK-02** (root cause still not confirmed): At the end of `SketchSession::render()`, a coalesced `QTimer::singleShot(0)` asks for one more full redraw. This happens only when the viewer has a widget, so headless runs are unchanged. `ViewerWidget::removePreview`/`clearPreviews` clear the AIS detected object when it is the preview being removed. I could not reproduce a stale frame in the GUI. After a fillet, a dimension and an undo, the frame was current without moving the mouse (sk06-fillet-r8.png, sk09-undo-dof-label.png). Seen once: a value-box widget left a ghost at its old position until the next mouse move. That is a repaint of the overlay QWidget over the GL widget, not sketch geometry.
- **SK-03**: Added `OcctView::finishAnimation()`, which jumps a running camera animation to its end camera. `ViewerWidget::buildEvent` calls it for press and release events, so tool clicks are projected with the final camera. ViewCube and control clicks return before `buildEvent` and are unaffected. `SketchSession::render` calls `enterSketchMode(plane, false)` when a recompute moved the sketch plane, which keeps the viewer's copy in sync. OcctView is in no group (tell G3). GUI: I clicked the two rectangle corners immediately after a ViewCube roll click, and the rectangle landed exactly under the clicks in the final view (sk03-clicks-during-animation.png).
- **SK-04**: `SketchFeature` now stores `planeAxes` (normal and X, plus `planeAxesMirrored` for left-handed frames) in its JSON. Every face-plane recompute aligns the plane to the stored axes: same normal side, stored X projected onto the plane, same handedness. A non-parallel new face (plane reference changed) re-stores the axes.
  - `createSketch` builds an **outward**, right-handed plane for a new face sketch (`outwardFacePlane`). It classifies a point just off the face against the body.
  - Deviation and finding: on a chamfer face, `facePlane` returned an *inward* normal and a *left-handed* frame (INFO showed x×y = −normal). New sketches on such faces were therefore viewed from inside the material and mirrored. They are now outward and right-handed, so Extrude on a new face sketch goes outward.
  - Older files without `planeAxes` store what their first recompute computes, handedness included, so their geometry does not move.
  - I did not use the plan's "world X projected" rule for X; the plane's own X is kept, to limit coordinate changes. `core::facePlane` and Feature.cpp are unchanged, so no G1 change is needed.
  - Test: test_feature `storedPlaneAxesStayStable`. GUI: the chamfer-face sketch had the same orientation on create and on edit, seen from outside with Z up (sk04-create-orientation.png, sk04-edit-orientation.png).
- **SK-05**: `CommandDialog` docks to the viewport's right edge (12 px margin) at y = 150, the same top as the sketch palette and below the ViewCube. On viewports shorter than 500 px it uses y = 12. Height caps use the space below the top. Double-clicking the title docks it back to the right. The palette comment is updated, and the palette already stacks below the dialog.
  - Deviation: test_main_window `commandDialogDocksLeftAndKeepsSize` asserted the old UX-17 left dock. It is renamed and now asserts the right dock. This is an app test file outside the plan's list; tell G1 and G3.
  - GUI: sk05-dialog-right.png.
- **SK-06**: `FilletTool` has a "Radius" value box, always visible at the cursor. Digits typed in the canvas go into it, and Enter sets the radius and keeps the tool running. `commandLineInput` is kept. After a fillet, in the same undo step, the first arc of the tool run gets a Radius dimension (via `addLockedDimension`, so a typed expression is kept). Later fillets with the same radius get Equal to the first arc instead. Tests in test_tools: the existing fillet test now checks one Radius = 2, plus a new canvas-typing case with radius 3. GUI: typed 8 + Enter, then a fillet with R8 and its dimension (sk06-*.png).
- **SK-07**: In `DimensionTool::place`, the "make driven" answer now commits the dimension right away ("Driven dimension added") without the value phase. The code is in ModifyTools.cpp. GUI: sk07-driven-no-prompt.png.
- **SK-08**: `CommandDialog::dismiss()` moves the focus to the viewport when the focus was inside the dialog; focus outside the dialog is never taken. `present()` only swaps the body and does not move focus. Test: test_main_window `dialogDismissReturnsFocusToViewer`. GUI: after typing a value and Enter, the first Esc already left the Dimension tool (sk08-esc-esc-exits.png).
- **SK-09**: Sketch prompts no longer carry the DOF text. The select tool uses the static hint "Select sketch geometry; drag to move", and Trim keeps its own prompt. `commitEdit` no longer calls `showMessage(statusText())`. Constraint and dimension tools show "Constraint(s) added", "Dimension added" or "Dimension changed". The session emits `changed()` after each `recomputeFinished`, so the MainWindow DOF label follows undo and redo. GUI: after undo the label went from 3 DOF back to 4 DOF (sk09-undo-dof-label.png).
- **SK-10**: New instant tool `sketch.select-all` (Ctrl+A, SKETCH tab, hidden, `keepsActiveTool`) with a new icon resources/icons/sketch-select-all.svg. It selects all non-projected sketch geometry. Body select-all outside a sketch is left for G1 (`select.all`). Test: `ctrlEnterAndSelectAll` (the line tool stays active). GUI: "4 sketch entities selected" (sk10-ctrl-a.png).
- **SK-11**: `ViewerWidget::fitAll` adds the bounding boxes of the sketch previews in sketch mode, skipping text labels. It falls back to the 250 scale only when the box is void. `hasGeometry()` is unchanged. The session polls the pixel size every 300 ms, so glyphs are re-rendered after F6, ViewCube and other animations. GUI: F6 fit the sketch rectangle (sk11-f6-fit.png).
- **SK-12**: `LineTool::addPoint` rejects a segment antiparallel to the previous one ("Segment would retrace the previous line: not added"), and the chain stays alive. Typed chain dimensions go through `lineDim`, which skips a dimension that is redundant and whose measured value already equals the typed one. `addLockedDimension` is untouched. Test: test_tools `lineRetraceRejected`. GUI: sk12-retrace-rejected.png.
