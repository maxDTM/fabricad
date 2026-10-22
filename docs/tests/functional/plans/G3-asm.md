# G3-asm fix plan: assembly (ghosting, delete/paste, joints, move)

Plan only, nothing implemented yet. Line numbers are from the tree as of 2026-10-22 05:25.
Evidence: docs/tests/functional/round1/asm-core.md and asm-core/defect-*.png.
Every fix gets a headless test in src/assembly/tests (test_assembly_tools.cpp or test_assembly_features.cpp) or src/viewer/tests.

## ASM-01 (high): scene stays ghosted after activating root; ghosted bodies can't be picked
- Root cause: each caller sets ghosting itself. NewComponentTool::accept (ComponentTools.cpp:93-97) and activateSelectedComponent (ComponentTools.cpp:249-255) call viewer->setGhostedComponents. Other paths change the active component without touching the viewer:
  - the browser "Activate Component" item and paste (BrowserTree.cpp:471, 486, 491)
  - undo of New Component (ComponentOps.cpp:358)
  - undo of Delete (ComponentOps.cpp:478)
  - Document::removeComponent (Document.cpp:607)
  A ghosted item gets no selection modes (ViewerWidget.cpp:777) and gets no overlay (ViewerWidget.cpp:918), so it cannot be picked.
- Fix:
  1. In ViewerWidget::connectDocument (ViewerWidget.cpp:287), connect Document::activeComponentChanged and documentReset to a new private helper, syncGhostingFromDocument(). For the root it calls setGhostedComponents({}). Otherwise it keeps the active component and its descendants, found by walking Component::children (the viewer must not depend on assembly/descendants()). Call the helper from setDocument too.
  2. Keep the tool calls (they now do nothing extra). The NullViewer in tests has no document hookup, and test_assembly_tools.cpp:584/588 checks h.viewer.ghosted().
  3. Picking: let ghosted bodies take selection modes when the tool's effective mask includes ShapeKind::Component, which means assembly tools such as Joint, Move and Ground. Do this in modesFor/applySelectionModes (ViewerWidget.cpp:775, 893). Also let ownerItem (ViewerWidget.cpp:918) return ghosted items in that case, so hover and selection highlights still draw.
- Possible effects:
  - Part tools (filter without Component) keep today's behaviour: ghosted bodies stay unpickable, so no stray cross-component references.
  - The extra activeComponentChanged connection fires during load and undo. It only calls scheduleSync, which is coalesced.
  - G4 also edits ViewerWidget.cpp (fitAll, redraw/hover, lookAt). Touch only connectDocument, modesFor and ownerItem, and rebase on G4's changes before merging. Expect small conflicts at most.
  - The fitAll/origin-size bounding box skips ghosted items (ViewerWidget.cpp:809). Leave that as it is.

## ASM-02 (high): deleting a component leaves its body and a dangling, drivable joint
- Root cause: DeleteComponentCommand::redo removes only the features where referencesComponents() is true (ComponentOps.cpp:438). That function (ComponentOps.cpp:253) checks Component-kind refs and componentA/componentB. A joint whose origin refs are faces or edges of the deleted component's bodies, or whose JointOrigin.component is that component, is missed. This happens when componentA/B was resolved differently, for example to the root through an origin-plane pick (ASM-06). Before fixing, reproduce headless and confirm which field misses.
  - The bodies are moved to the parent on purpose (comment at ComponentOps.cpp:408, test_assembly_features.cpp:557-566). The tool message says so (ComponentTools.cpp:379).
  - DriveJointTool::candidates (DriveJointTool.cpp:50) lists errored joints.
- Fix:
  1. Pass node_.bodies (captured before removal) into referencesComponents. A feature is doomed when any of these is true:
     - any Ref or RefList ref has a B-rep owner among those bodies;
     - it is a joint and originA().component or originB().component is a gone id;
     - it is a joint and an origin ref's owner is one of those bodies.
     Only joint and RigidGroup features (IJointProvider) are doomed through body refs. Part features that use the bodies keep working, because the bodies survive.
  2. Body semantics are a decision for the reviewer:
     - (a) Fusion: delete the component's bodies too, by dooming the features whose component()==comp_ plus the transitive dependentsOf() of each (Document.h:95).
     - (b) Keep the current move to the parent and show "N bodies moved to <parent>" as a warning.
     The plan recommends (a) with the message "Deleted component X and N bodies". It needs test_assembly_features.cpp:557-566 and the result struct (movedBodies becomes deletedBodies) updated.
  3. DriveJointTool::candidates and the joint combo: skip joints with status Error or that are not active.
- Possible effects:
  - With (a), undo has to restore the doomed features at their indices. The existing features_ vector already does this, but ordering matters when there are dependents, so sort by index (already done at ComponentOps.cpp:470).
  - Features in other components that consumed those bodies (Combine, Split) go into error. That matches Fusion, but list them in the message.
  - G1 DOC-05 (dirty flag after undo): keep every change going through Document mutators (removeComponent/addComponent/takeFeature/addFeature, which call touchModified). Don't add a separate modified flag here, so G1's clean-index logic applies.
  - The scripting tests in app/tests/test_app_logic.cpp:371 (deleteComponent) need a re-check.

## ASM-03 (high): Paste New produces an empty component; occurrences show no tree children
- Root cause:
  - deepCopyComponent copies only the features whose f->component() is one of the copied definitions (PasteNew.cpp:146-151). A body that is in the component but was made by a feature somewhere else is skipped, so the new component is empty. This happens after "Create Components from Bodies", New Component "from bodies", or a body moved by reparenting. In the tester's run, the Box feature stayed in the root while Body2 moved into "Body2:1".
  - BrowserTree::fillComponent lists doc_.bodiesOf(compId) (BrowserTree.cpp:179). An occurrence owns no bodies; its bodies are displayedBodies() of the definition (Document.cpp:588), so Body1:2 shows no children.
- Fix:
  1. PasteNew: work out bodiesOf(p.from) for every copied definition. If a body's creating feature is not in feats, either:
     - preferred: add a frozen copy of the body, a small non-parametric "BaseBody" feature (serialised BRep, owned by assembly) placed in the new component, the way Fusion pastes non-parametric bodies;
     - fallback: copy the creating feature and its upstream chain, then drop the extra bodies it creates.
     After the copy, if the source had bodies and res.bodies is empty, fail with a message. Never leave an empty component.
  2. BrowserTree: for c->isOccurrence(), fill the Bodies folder from doc_.displayedBodies(compId). Use keys "occbody:<occ>:<body>" so they don't collide with the definition's "body:<id>" key. Clicking one selects ShapeRef::component(occ), the same ref the scene uses (SceneModel.cpp:183). Make these items read-only: no rename and no eye toggle, or an eye that toggles the occurrence.
  3. JointTool::pick in as-built mode (JointTool.cpp:274-300), and placeOrigin: reject a component whose displayedBodies are empty and that has no child components, with the message "Component 2 has no geometry".
- Possible effects:
  - A new feature type needs FeatureFactory registration (AssemblyModule.cpp:69) and JSON round-trip tests. Old files are unaffected.
  - The browser change affects tree-state restore (expanded keys) and the browser→viewer selection sync. Check that selecting an occurrence body doesn't select the definition's bodies in the viewer.
  - G1 keeps DOC-08 out of BrowserTree.cpp. Coordinate: only fillComponent and the activation lambdas change here.
  - Paste is wrapped in DocumentSnapshotCommand, so undo stays a single step.

## ASM-04 (medium): Move Component: typing a rotation rewrites typed distance fields
- Root cause: setTransform builds delta = T(x,y,z)·T(p)·R·T(-p) (MoveComponentTool.cpp:299-316). applyDelta then reads values_[0..2] back from delta.TranslationPart() (MoveComponentTool.cpp:266-269). That value includes the pivot term p - R·p, so after an angle edit X and Y become -15.377/-41.411, and syncDialog writes them into the fields.
- Fix: when values_ are derived from a delta (triad drag), decompose about the pivot: t = delta.TranslationPart() - (p - R·p), where R is the delta's rotation and p = baseFrame_.Location(). setTransform then assigns values_ from its arguments directly and skips the re-derivation (add a flag parameter to applyDelta). The fields stay the source of truth.
- Possible effects:
  - Drag then type, and type then drag, must agree. Add a round-trip test: setTransform(x, y, z, 0, 0, 30), then read values_ back.
  - setPivot (MoveComponentTool.cpp:318) reapplies values_ and stays correct.
  - The jointed drive path (applyJointDrive) is unchanged.

## ASM-05 (medium): Joint Animate then OK commits the animation's leftover angle
- Root cause: onAnimationTick writes values_[0] (JointTool.cpp:701). accept() stops the timer first (JointTool.cpp:207), so the edit-path reset at JointTool.cpp:215 never runs (timer already stopped), and the create path builds from the animated values_. setAnimating(false) resets the value to 0 instead of the typed value (JointTool.cpp:675). The fmod(...,360) (JointTool.cpp:693) produces 0..360, which gave 212.4.
- Fix: add typedValue_. Store it when the animation starts. Restore it, and run updatePreview, in setAnimating(false), accept(), cancel() and on a joint-type change. Use a single stopAnimation(ctx) helper. Wrap the rotation to (-180, 180]. While animating, don't sync the animated value into valueEdit_, or sync it with the edit disabled.
- Possible effects: JointTool edit mode (restoreEdited) and tests that drive ANIMATE from the command line. Check test_assembly_tools for "ANIMATE".

## ASM-06 (medium): origin planes pickable during Joint; pick returns "Untitled (Geometry)"
- Root cause:
  - The JointTool filter includes kPlaneKinds, kAxisKinds and kPointKinds (JointTool.cpp:135).
  - ViewerWidget::onFilterInputsChanged auto-shows origin planes whenever the mask contains OriginPlane (ViewerWidget.cpp:613).
  - componentOfRef maps origin refs to the root (ComponentOps.cpp:159-161). The large origin planes therefore win the pick over body faces, and the root is labelled "Untitled".
- Fix: keep origin kinds in ctx.selection's filter, so the browser Origin folder can still be picked explicitly. Pass the viewer a mask without OriginPlane/OriginAxis/OriginPoint (ctx.viewer->setSelectionFilter at JointTool.cpp:158/170). The planes then aren't auto-shown or pickable in the viewport. Construction planes, axes and points stay pickable.
- Possible effects:
  - Joint to the root origin still works from the browser.
  - If the user turned the Origin folder on, the planes are visible but still not pickable in the viewport. That's acceptable; mention it in the prompt.
  - No change to viewer code is needed.

## ASM-07 (low): Drive Joints accepts values outside the displayed range
- Root cause: DriveJointTool::setValue (DriveJointTool.cpp:181) writes any value. The range label shows the slider span (DriveJointTool.cpp:110-116, 302) even when no limits are set.
- Fix:
  - When limitsEnabled: setValue clamps through joint_->clampToLimits() (already used at MoveComponentTool.cpp:293).
  - Rotation without limits: wrap to (-180, 180].
  - Distance without limits: accept the value and label it "slider ±100 mm (no limits)".
- Possible effects: command-line "DRIVE J1 500" and scripts get clamped values. Update any test that expects the raw value.

## ASM-08 (low): Move/Copy works on bodies only; Move Component clears the preselection
- Root cause:
  - Move/Copy is featureTool<MoveBodyFeature> (PartTools.cpp:146). It reads only Body refs (PartTools.cpp:80), so a component selection is dropped.
  - MoveComponentTool::activate already reads ctx.preselection (MoveComponentTool.cpp:83). The "cleared" report is probably the Move/Copy path, or the browser giving refs that componentsOf rejects, for example occurrence body items (ToolCommon.cpp:19). Verify headless.
- Fix:
  - In PartTools.cpp, register Move/Copy through a thin subclass. When the preselection holds Component refs and no Body refs, it hands off to "assembly.move" by tool id through ctx.manager (no link dependency on assembly) and passes the preselection on.
  - In MoveComponentTool, accept browser component clicks during the tool (they work once ASM-03's occurrence refs exist).
- Possible effects:
  - G2 must not touch PartTools.cpp or the Move/Copy part of FeatureTool.cpp. Make the change only in PartTools.cpp, not in FeatureTool.cpp (a G2 file).
  - The "M" shortcut stays the same.

## ASM-09 (low): ground handling inconsistencies
- Root cause:
  - The toolbar "assembly.ground" always runs GroundMode::Ground (AssemblyModule.cpp:89). "Unground" is hidden (AssemblyModule.cpp:93).
  - The disabled state seen with a grounded component needs a repro. The predicate hasComponentSelection (AssemblyModule.cpp:48) doesn't check grounded, so suspect the ribbon refresh or the ghosting (ASM-01).
  - MoveComponentTool::setComponents doesn't warn about grounded components (MoveComponentTool.cpp:98-130).
- Fix:
  - Toolbar button: when every selected component is grounded, run the Unground behaviour and update the tooltip/label through the existing enable refresh. Keep the GROUND command semantics (explicit on/off/toggle words) unchanged.
  - Move Component on a grounded component: warning prompt "X is grounded: unground to move", and refuse the move, as Fusion does.
- Possible effects: test_assembly_tools.cpp:532-555 runs "assembly.ground" twice and expects "already grounded". Either keep the command path as-is and toggle only from the ribbon action, or update the test.

## ASM-10 (low): Joint dialog polish (all in JointTool.cpp)
- Clipped Select buttons (JointTool.cpp:736-747): give the label an elided text with a tooltip, or setWordWrap and a minimum width of 0 with QSizePolicy::Ignored. Give the button a fixed sizeHint.
- Stale red "Component 2 must differ": showMessage(...,true) at JointTool.cpp:285/318 is never cleared. Clear it (ctx.showMessage("", false) or the host's clear call) in placeOrigin/pick on success.
- As-built component 2 ignores viewport clicks until Select is pressed: mouseMove returns false outside PickAxis (JointTool.cpp:351), and mousePress may use a stale hoverBest_ from component 1 (JointTool.cpp:368). Reset hoverRef_/hoverBest_ in setStage, and take e.pick when the stage is PickA or PickB. This probably also depends on ASM-01 (ghosted component 2).
- Component 1 silently prefilled with ComponentOrigin: activate() turns a preselected Component ref into snap A (JointTool.cpp:173-181). Prefill only from B-rep refs. When a Component ref is used, label it "Component origin of X" and leave the stage on PickA (or show it clearly).
- Possible effects: the headless joint tests that pass a preselection of two components need checking. Keep that working by still accepting two Component refs, which is explicit enough.

## Order and shared files
1. ASM-01 (viewer/ghosting) unblocks testing of Joint and ASM-10.
2. Then ASM-06, ASM-05, ASM-02, ASM-03, ASM-04, then the low-severity issues.

Shared files:
- ViewerWidget.cpp is shared with G4. G3 touches only connectDocument, modesFor, applySelectionModes and ownerItem.
- BrowserTree.cpp is shared with G1. G3 touches only fillComponent and the activation code.
- PartTools.cpp: only the Move/Copy registration.
- The undo/dirty interplay with G1 DOC-05 is covered under ASM-02.

Run the full ctest (headless) after each step, building with -j2.

## Done (fn/G3-asm, 2026-10-22)

Build: Release, -j2. Full headless ctest (QT_QPA_PLATFORM=offscreen): 56/56 pass. GUI checks ran on
display :43 with the worktree binary, using real xdotool input. Screenshots are in
docs/tests/functional/fixes/G3-asm/ (main tree).

- **ASM-01:** ViewerWidget::connectDocument now listens to `activeComponentChanged`, `documentReset`
  and component add/remove, and setDocument does the same. Each one calls the new
  `syncGhostingToActiveComponent()`, which keeps the active component and its descendants
  (walking `children`) and clears ghosting for the root. Ghosted bodies get selection modes and
  overlays when the tool filter includes Component (`ghostedPickable()`, used in modesFor,
  applySelectionModes and ownerItem). Tools still call setGhostedComponents. Files:
  viewer/ViewerWidget.{h,cpp}. Test: test_viewer_smoke `ghostingFollowsActiveComponent`.
  GUI: browser Activate on Top ghosts Base, browser Activate on root clears it, and the Joint tool
  picked a face on the ghosted Base (asm01-*.png).
- **ASM-02:** referencesComponents also counts joints and rigid groups that hold a B-rep ref on
  the deleted component's bodies, or a JointOrigin whose component or ref points into it.
  DeleteComponentResult has a new `removedFeatures`. Drive Joints skips joints in error.
  **Deviation (body handling):** option (b). Bodies still move to the parent, and the user is now
  warned about it (Delete Component tool message, and a warning on the browser/Del path). Option
  (a) was not done: the creating feature can be a root feature shared by other bodies, and an
  undo would lose the component assignment of re-created bodies. **Real root cause found in the
  GUI:** browser Delete / Del used `app::makeDeleteCommand`, which called
  `Document::removeComponent` directly and never ran the assembly delete. It now adds
  `assembly::makeDeleteComponentCommand` (a new factory in ComponentOps.h). That is a deviation
  into app/AppActions.cpp and a few lines of MainWindow::deleteRefs for the warning. Files:
  assembly/ComponentOps.{h,cpp}, assembly/tools/ComponentTools.cpp,
  assembly/tools/DriveJointTool.{h,cpp} (`candidates` is now public), app/AppActions.cpp,
  app/MainWindow.cpp. Tests: `deleteComponentRemovesJointsOnItsBodies`, test_app_logic
  `deleteComponent` (now with a joint). GUI: deleting Top removed the joint and undo restored it
  (asm02-*.png).
- **ASM-03:** a new non-parametric feature, `BaseBody` (assembly/BaseBodyFeature.{h,cpp}). It is
  registered in AssemblyModule, stores the shape as BRep text and uses the body icon. Paste New
  adds one BaseBody per body of the copied tree whose creating feature is not copied. Paste New
  warns when the copy shows no bodies. The browser lists an occurrence's displayed bodies under a
  read-only "Bodies" folder: kind `KindOccurrenceBody`, key `occbody:<occ>:<body>`, and a click
  selects the occurrence. The Joint tool rejects empty components (no bodies, no children), both
  as-built and in snap mode. Files: assembly/PasteNew.cpp, app/BrowserTree.{h,cpp} (fillComponent
  and refForItem only), assembly/tools/JointTool.cpp. Tests:
  `pasteNewCopiesBodiesMadeOutsideTheComponent` (Create Components from Bodies, then Paste New;
  covers volume, independence, save/load and single-step undo), `jointPickErrorsAndEmptyComponents`.
  GUI: "Body3 (1):1" has Body3, and Base:2 lists Body1 (asm03-*.png).
  Known limit: a feature inside the copied tree that modifies a frozen body is applied again on
  the frozen (already final) shape.
- **ASM-04:** applyDelta takes an optional `fields` array. setTransform passes the typed values,
  so nothing is read back. A triad drag now shows t = TranslationPart − (p − R·p), relative to the
  pivot. Added `fieldValue(i)`. Files: assembly/tools/MoveComponentTool.{h,cpp}. Test:
  `moveComponentTypedFieldsAreKept`, which re-types the read-back fields and gets the same
  placement. GUI: X 10, Y 20, Z angle 30 all stay as typed (asm04-*.png).
- **ASM-05:** added `typedValue_` and `stopAnimation()`, which restores the typed value. It runs on
  Animate off, accept, cancel, deactivate and joint-type change. While animating, the edit field
  keeps showing the typed value. Rotations wrap to (-180, 180] with `wrapAngle180` (a new helper in
  JointTypes) when limits are off. Files: assembly/tools/JointTool.{h,cpp},
  assembly/JointTypes.{h,cpp}. Test: `jointAnimateThenOkKeepsTypedValue`. GUI: typed 45, animated,
  pressed OK, and editing the joint showed 45 (asm05-*.png).
- **ASM-06:** the Joint tool gives the viewer a filter without origin kinds. The selection filter
  keeps them, so the browser Origin folder still works. File: JointTool.cpp. GUI: no origin
  planes appear during Joint (asm06-*.png).
- **ASM-07:** Drive Joints clamps to the limits when they are set (setPrimaryValue already did).
  Without limits, a rotation wraps to (-180, 180]. A distance without limits is accepted, and the
  range label reads "slider ±100 mm (no limits)". Test: `driveJointWrapsUnlimitedRotation`. GUI:
  typing 500 gave 140 (asm07-*.png).
- **ASM-08:** Move/Copy (`part.move`) is now a thin `MoveCopyTool` subclass in PartTools.cpp. With
  only components preselected, it runs `assembly.move`, which takes them as its preselection.
  FeatureTool.cpp is untouched. Headless check: MoveComponentTool already used ctx.preselection,
  so the "cleared selection" came from the Move/Copy path. Test (test_paste_new_pattern):
  `moveCopyHandsComponentsToMoveComponent`. GUI: selecting Top and pressing M opened Move
  Component on Top with the selection kept (asm08-*.png).
- **ASM-09:** **Deviation:** the toolbar button is a new tool, `assembly.ground-toggle` ("Ground",
  with a new icon resources/icons/assembly-ground-toggle.svg). When everything selected is
  grounded, it ungrounds. `assembly.ground` (the GROUND alias) is now hidden and stays idempotent
  as the scripting tests expect. The label does not change dynamically, because the ribbon has no
  hook for that. Move Component on a grounded component asks "Move it anyway?" (default Yes, so
  scripts like chair.fcs, which rotates the grounded Seat, keep working) and shows a warning. "No"
  leaves it out. The plan said "refuse"; it now asks first, because otherwise the chair spec
  breaks. "Ground disabled for a grounded component" did not reproduce with this build (the
  button is enabled; asm09-ground-enabled-on-grounded.png). It was probably the ASM-01
  ghosting/selection state. Test: `groundToolbarButtonToggles`, plus the grounded part of
  `moveComponentTypedFieldsAreKept`.
- **ASM-10:** in JointTool.cpp, the Select-row labels use an Ignored size policy and are elided in
  the middle with a tooltip, and the Select buttons have a fixed size. A valid pick after a red
  pick error replaces the error with an info line ("Component 2: …"). setStage resets the hover
  state, and mousePress uses the hover snap only in snap stages (not as-built PickA/PickB). A
  regular joint no longer prefills from a preselected component (as-built still does), and a
  component-origin snap is labelled "Component origin of X". GUI: on as-built component 2, a
  viewport click on the ghosted Base worked without pressing Select, and the red message was
  replaced (asm10-*.png).
