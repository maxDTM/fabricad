# ASM-CORE retest (merged build a110a4f), display :34, mouse/keyboard only
Screenshots in docs/tests/functional/retest/asm-core/ (sNN.png, in order of execution).
The display was only 945x490, so the dialog hides part of the viewport; the app window was resized to fit.

| Issue | Verdict | Evidence |
|---|---|---|
| DOC-05 / ASM-CORE-8 | FIXED | s38: "*" kept after Ctrl+Z of a component delete |
| PART-05 / ASM-CORE-12 | FIXED | s6-s10: Box apart from existing body defaults to New Body (Body2 created); an overlapping box (X=10) switched to Join |
| ASM-01 / ASM-CORE-1 | FIXED | s13 (ghosted on activate), s15 (browser "Activate Component" on root: no ghosting), s16-s18 (ghosted body picked in Joint) |
| ASM-02 / ASM-CORE-2 | MOSTLY FIXED | s36-s37: Joint1 removed with its component; Drive Joints reports "no joints with motion". The body is still kept and moved to root (by design); status bar says "kept 1 body". |
| ASM-03 / ASM-CORE-3, -12 | FIXED | s30-s35: Paste New gives Component2 (1):1 with Bodies/Body2 in the tree and a visible body |
| ASM-04 / ASM-CORE-4 | FIXED | s33-s34: X=50, Y=30 stayed after typing X angle 50 and Z angle 30 |
| ASM-05 / ASM-CORE-5 | FIXED | s24-s26: Animate then OK, Drive Joints shows 45 |
| ASM-06 / ASM-CORE-6 | FIXED | s17-s18: origin planes not drawn in Joint; pick gave a body face, not "Untitled (Geometry)" |
| ASM-07 / ASM-CORE-7 | PARTIALLY FIXED | s27: typing 500 on an angle joint gave 140 (wrapped into -180..180). Linear range clamp not tested. |
| ASM-08 / ASM-CORE-9 | FIXED | s41-s43: M with a component selected hands off to Move Component; the preselection is kept |
| ASM-09 / ASM-CORE-10 | FIXED | s39-s41: Ground toggles ("Grounded"/"Ungrounded"); "'Component1' is grounded. Move it anyway?" prompt |
| ASM-10 / ASM-CORE-11 | PARTIALLY FIXED | s44: Component 1 no longer prefilled. s45-s46: the red "must differ" message cleared after the next click. Select-button clipping and As-built comp 2 click-before-Select not retested. |

## Regression check of round-1 working features
New Component with Activate, Joint (rigid and revolute) with angle offset, Drive Joints dialog with slider, Move Component, Copy, Ctrl+Z restoring the component and joint: all OK.

## NEW defects
- NEW-ASM-CORE-1 (cosmetic): after undoing a component delete, the browser order changes (restored "Component2:1" is listed after "Component2 (1):1"), s38.
- NEW-ASM-CORE-2 (cosmetic): the Assemble menu still says "Ground" when the component is already grounded (it acts as a toggle but the label does not show Unground), s39.
- Harness note: the screenshots are sometimes one action stale, and the display grew to 1900 px wide mid-run (black area in s33+).

## Counts
Fixed 8, mostly or partially fixed 3 (ASM-02, ASM-07, ASM-10), not fixed 0, regressed 0, new defects 2 (both cosmetic).
