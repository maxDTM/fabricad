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
