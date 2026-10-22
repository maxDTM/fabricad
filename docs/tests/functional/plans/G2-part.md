# G2-part plan: part modelling validation, limits and defaults

Status: plan only, no code changed. Source: docs/tests/functional/round1/part-core.md (PART-CORE-1..4) and asm-core.md.
Alias note: the Box default issue is **ASM-CORE-13** in asm-core.md. ASM-CORE-12 is "pasted instance shows no children", which belongs to G3.

## PART-01: pattern quantity 100000 freezes the UI (high)
- **Root cause:**
  - `Pattern.cpp:384 linearOffsets()` only rejects `quantity < 1`. Circular (`:476`) only rejects `n < 2`. `addInt` (`:362/:366/:461`) sets no upper bound.
  - `PropertyPanel.cpp:193-201`: the Int spinbox has a range of +/-100000 and emits `valueChanged` on every keystroke, skipping `debounce_`. Typing "100000" therefore recomputes at 1, 10, 100, 1000, 10000 and 100000 instances, synchronously on the UI thread.
- **Fix:**
  - Add `constexpr int kMaxPatternInstances = 1000` in Pattern.cpp.
  - In `linearOffsets()`, error when `quantity > kMax`. In the rectangular compute, check the total `off1.size()*off2.size()` (symmetric doubles it) before building transforms: `ctx.setError("pattern too large (max 1000 instances)")`. Apply the same check to the circular `n`. The check lives in compute, so it also guards the component pattern, scripts and loaded files.
  - UI:
    - Add `std::int64_t intMin = INT64_MIN, intMax = INT64_MAX` to `core::PropertyFlags` (Property.h).
    - Set `flags().intMax = 1000` (and intMin 1 or 2) on the quantity props.
    - Make the PropertyPanel spinbox use these flags.
    - Call `spin->setKeyboardTracking(false)`, so the value commits on Enter, focus-out or arrow keys only.
- **Possible effects / avoid:**
  - Property.h is a core header. Nobody else lists it, but every target rebuilds (build -j2, rebase on G1 first). The default `operator==` stays valid. Do not serialise the new fields in Json.cpp (G1).
  - `src/app/PropertyEditor.cpp:173` has its own spinbox. Make it honour `intMax` too (one line, no group owns that file), or leave it, since compute still errors.
  - keyboardTracking(false) means there is no live preview while typing ints, only on commit. Headless tests that call `setValue()` still emit. Check `test_tools` / `test_paste_new_pattern` (G3 test, quantities 2-4, unaffected).
  - 1000 instances can still take ~30 s (100 instances ~3 s per the tester). Consider 500, or keep 1000 and accept that. Async recompute is out of scope.

## PART-02: shell thickness larger than the body succeeds with no change (medium)
- **Root cause:** `Shell.cpp:73-81` only checks `mk.IsDone()`. MakeThickSolidByJoin returns the input or a degenerate solid, and `replaceBody` (PartUtils.cpp:398) only runs BRepCheck. The hollow-body branch (`:84-96`) has the same gap: a collapsed inward offset gives a Cut that changes nothing.
- **Fix:**
  - After each branch, compute `v0 = core::volume(g.bodyShape)` and `v1 = core::volume(result)`.
  - Reject the result with `ctx.setError("shell failed: thickness too large for the body")` when any of these holds:
    - `!isValidShape(result)`
    - `v1 <= eps`
    - `|v1 - v0| < 1e-6*v0` (unchanged)
    - Inside direction and `v1 >= v0`
    - In the removed-faces branch, a removed face is still present (`mk.IsDeleted` on every removed face, or face count unchanged)
  - Optional extra guard: `t >= 0.5 * min bbox extent` gives an error early.
- **Possible effects / avoid:**
  - With the Outside direction the volume can be larger or smaller than v0, so only require "changed and positive" there.
  - Volume costs one GProp per body, which is negligible.
  - Run the shell tests in `src/part/tests` (thin walls near the limit must still pass). Use a relative tolerance, not an absolute one.

## PART-03: two-distance chamfer gives a corrupted solid accepted as success (medium)
- **Root cause:** `Fillet.cpp:32 healed()` returns any ShapeFix output that passes BRepCheck. `ChamferFeature::compute` (`:291`) and Fillet (`:200`) accept it with no topology or volume sanity check. The tester saw missing or inverted faces and an open shell.
- **Fix:**
  - Add `bool sameKindSolid(orig, r)` next to `healed()`. It requires:
    - the solid count equals the original's
    - each shell is closed (every edge has exactly 2 faces via `MapShapesAndAncestors`, or `BRepCheck_Shell::Closed`)
    - `volume(r) > 0`
  - Chamfer: also require `v1 < v0` and `v1 > 0.5*v0` (a chamfer only removes material).
  - Fillet: require `|v1-v0| < 0.5*v0` (concave fillets add material).
  - When a check fails, run ShapeFix, re-check, and then error with "chamfer produced an invalid shape (distance too large?)".
  - Also run the check on the un-healed path, because BRepCheck-valid output can still be inverted (negative volume means inverted).
- **Possible effects / avoid:**
  - Fillet shares the helper, so PART-CORE-8 (partially intersecting rounds) may now be rejected. That is acceptable and more consistent, but check the fillet tests and the chair fixture (`test_chair_split`, `ChairFixture.h`) still pass.
  - Keep the 0.5 ratio loose to avoid false failures on small parts with big fillets.

## PART-04: cut that hits nothing accepted silently, and Cut defaults away from the body (medium)
- **Root cause, accepted cut:** `PartUtils.cpp:308-309 participants()` uses `shapesTouch`, so a tool that only shares the top face counts as a participant. The Cut loop (`:375-394`) then accepts a result identical to the input. Line 349 only fires when nothing touches.
- **Root cause, default direction:**
  - `Extrude.cpp:171-173` uses the profile face normal (outward on a face sketch) unless `flip` is set.
  - `FeatureTool::autoOperation` (`:431-465`) only switches Join<->Cut by material fraction at drag end. When the user picks Cut explicitly (`:604/:647` set operationAuto_=false), nothing flips the direction.
- **Fix:**
  - (a) In `applyTool`'s Cut loop, compare `volume(base)` with `volume(r.shape)` (relative 1e-7). Skip unchanged participants: do not call `addModified`, so names and history stay untouched. If no participant changed, `ctx.setError("the cut does not remove material from any body")`. Intersect stays as it is.
  - (b) In FeatureTool, when the user sets `operation` to Cut on a new Extrude (not editing), with OneSide, extent Distance, the user not having touched `flip`/distance sign, and a tool that is ~0% inside material: toggle `flip`, recompute, and keep the flip only if the material fraction becomes > 0, otherwise revert.
    - Factor the inside-volume calculation out of `autoOperation` into `materialFraction(ctx)` and reuse it.
    - Track `flipAuto_` so a later manual flip wins.
- **Possible effects / avoid:**
  - (a) is shared by Extrude, Revolve, Sweep/Loft, BodyOps (`:200`), Hole (`:274`) and Primitives.
    - Hole always cuts into material, so it is fine.
    - Downstream features that now miss after an upstream edit turn red instead of passing silently. This is Fusion behaviour, but run the full ctest (chair fixture, timeline tests) and fix any fixtures that relied on no-op cuts.
    - Volume of large bodies (patterns) adds some cost per cut. Compute it only for Cut.
  - (b) must not run in edit mode or for scripted `operation=Cut` via `runCommandLine` unless the tests expect it. Restrict it to the panel or command change path and check `test_tools` extrude cases.
  - Not touching PartTools.cpp (G3) or core/Feature.cpp (G1).

## PART-05 (ASM-CORE-13): Box defaults to Join for a body it does not touch (low)
- **Root cause:**
  - `PartFeature.cpp:84-91 applyDefaultsForNewFeature` sets Join whenever the active component has any body. That overrides `Primitives.cpp:45` NewBody.
  - `FeatureTool::autoOperation` returns early for anything other than Extrude (`:436`).
  - `applyTool:338` already turns a non-touching Join into a new body, so the result is right but the dialog shows Join.
- **Fix:** In `autoOperation`, add a branch for primitive types (Box, Cylinder, Sphere, Torus, ...; check `dynamic_cast<PrimitiveFeature*>`). If the operation is auto and the tool touches no body in scope (`shapesTouch` over `bodiesInComponentTree`), set New Body. If it touches and the operation is currently auto New Body, set Join. Run it on each preview update while `operationAuto_` is set (not only at drag end), and rebuild the panel only when the value actually changes.
- **Possible effects / avoid:**
  - Leave `applyDefaultsForNewFeature` unchanged: `test_tools.cpp:473/487` relies on it for Extrude.
  - Picking an operation by hand still stops the auto switch (`:604/:647`).
  - Panel rebuild on every preview may cause flicker or lose focus. Rebuild only when the value changes.
  - Saved documents are unaffected (edit mode sets operationAuto_=false).

## Cross-group
- Files touched:
  - Pattern.cpp, Shell.cpp, Fillet.cpp, PartUtils.cpp, FeatureTool.cpp, PropertyPanel.cpp
  - core/Property.h (header-only flag, coordinate with G1)
  - optional app/PropertyEditor.cpp
- Not touched: PartTools.cpp (G3), core/Feature.cpp and GeometryRefs (G1).
- Verify with `ctest` (part + assembly suites), then retest in the app: qty 100000 shows an error, shell 50 on a 20 mm box shows an error, chamfer 3/8 shows an error, the face cut shows an error and auto-flips, the box shows New Body.

## Done

Branch `fn/G2-part` (worktree `fabricad-wt/fn-G2-part`). Full `ctest` (QT_QPA_PLATFORM=offscreen): 56/56 pass.
GUI retest on display :42 with the worktree binary; screenshots in
`fabricad/docs/tests/functional/fixes/G2-part/`.

**PART-01: pattern quantity cap**
- `Pattern.cpp`: `kMaxPatternInstances = 1000`. Compute rejects each direction > 1000, the total
  `off1.size()*off2.size()` (symmetric counted) > 1000, and the circular count (symmetric type: 2n-1)
  with "pattern too large (max 1000 instances)". It covers component patterns, scripts and loaded files.
- `core/Property.h`: `PropertyFlags::intMin/intMax` (header only, not serialised, defaults ±1e6).
  The quantity properties get 1..1000 (circular 2..1000).
- `PropertyPanel.cpp` and `app/PropertyEditor.cpp`: spinbox range from the flags, `setKeyboardTracking(false)`.
- Deviation: kept 1000 (not 500). 1000 body instances at 20 mm spacing previewed in under 4 s in the GUI.
- Tests: `patternQuantityIsCapped` (test_construction_pattern), `patternQuantitySpinboxBounded` (test_tools).
- GUI: typing 100000 clamps to 1000 and the app stays responsive at about 230 MB RSS.
  1000 x 2 gives the red error and OK disabled.
- Seen, not fixed (not in this group): after picking Direction 2 the Quantity 2 / Distance 2 rows
  only appear after the next enum change. Selecting a ref does not rebuild the panel when
  hidden flags change.

**PART-02: shell too thick**
- `Shell.cpp`: a `plausible()` check on both branches (faces removed and hollow body). Requires a
  BRepCheck-valid result, `sameKindSolid`, positive volume, a volume change (relative 1e-6), v1 < v0 for
  Inside, and no removed face still present. Otherwise error "thickness too large for the geometry".
- Test: `shellTooThickIsError` covers 50 mm on a 20x20x10 box, hollow 50 mm, and a 4.5 mm wall that still works.
- GUI: 50 mm on a 20x20x30 box with the top face removed shows the error and OK disabled. 2 mm still previews.

**PART-03: corrupted chamfer accepted**
- `PartUtils`: new `sameKindSolid(before, after)` (same solid count, closed shells, every solid with
  positive volume) and `hasSelfIntersection()` (BOPAlgo_ArgumentAnalyzer, self-intersection only).
- `Fillet.cpp`: chamfer and fillet results must pass both checks. Chamfer also needs 0.5·v0 < v1 < v0
  and fillet |v1-v0| < 0.5·v0.
- Deviation: the reported repro (two-distance 3/8 on a top edge next to a 5 mm fillet) passes
  BRepCheck, is closed and has a plausible volume. The chamfer runs along the tangent chain (top
  edge, fillet arc, vertical edge) and the result folds over itself. Only the self-intersection
  check catches it, so I added that check on top of the plan.
- Tests: `sameKindSolidRejectsBrokenResults` (inverted, open shell, extra solid, null) and
  `twoDistanceChamferNextToFillet` (the repro must be an error or a clean solid; the same chamfer
  on the opposite edge must give the exact volume). test_chair_split and the chair script/spec pass.
- GUI: before the fix the result had a visible folded patch and was accepted. Now the dialog says
  "chamfer produced an invalid shape". With Flip on, the chamfer gives a clean result.

**PART-04: cut that removes nothing; default Cut direction**
- `PartUtils.cpp applyTool`: for Cut, a participant whose volume does not change (relative 1e-7) is
  left alone. If no body changes, error "the cut does not remove material from any body".
  Volumes are computed only for Cut.
- `FeatureTool`: `materialFraction()` is factored out of `autoOperation`. `autoFlipForCut()` runs
  only when the user picks Cut in the dialog (not in edit mode, not for `operation=Cut` from the
  command line). It applies only to a one-side distance Extrude whose tool is under 1% inside material:
  it sets Flip and keeps it only if the tool then goes into material. `undoAutoFlip()` clears an
  automatic flip when the operation leaves Cut. A manual Flip toggle (`flipUser_`) disables both.
- `PropertyPanel::refresh()` now also syncs enum combos and bool checkboxes, so programmatic
  changes (auto flip, auto operation) show without a panel rebuild.
- Tests: `extrudeCutTouchingOnlyIsError` (test_extrude_revolve) and `cutOnTopFaceFlipsIntoBody`
  (test_tools; covers the scripted path with no flip plus error, the dialog auto flip, undoing the flip
  on Join, and the volume after accept). No existing fixture relied on a cut that changed nothing.
- GUI: box below XY, circle sketch on XY (the box top face). Extrude defaults to Join. Picking Cut
  ticks Flip and shows a valid red cut. Unticking Flip by hand gives "the cut does not remove
  material from any body".

**PART-05 (ASM-CORE-13): Box default operation**
- `FeatureTool::autoPrimitiveOperation()`: for `PrimitiveFeature`, while the operation is still
  automatic and no explicit targets are set, it sets New Body when the shape touches no body in
  scope and Join when it does. This runs in `updatePreview` and recomputes once when the value
  changes. The combo is synced through `refresh()` with no rebuild, so focus is not lost.
  `applyDefaultsForNewFeature` is unchanged.
- Test: `boxOperationFollowsContact` covers overlap (Join), far away (New Body), face contact
  (Join), and a chosen operation that is kept.
- GUI: a Box over the existing body shows Join. Position X = 100 switches it to New Body.

Harness notes: the scratchpad directory is shared between agents. Another agent overwrote my
first helper script `shot.sh`, and running it once wrote a stray `s7.png` into
`fixes/G1-doc/` (screenshot only, nothing else touched). The recovery autosave directory is also
shared. I started the app with a private `XDG_DATA_HOME` after a recovery prompt offered another
agent's autosave.
