# ASM-CORE round 1 (assembly basics), tester run on display :34

Screenshots in docs/tests/functional/round1/asm-core/ (defect-*.png are the key ones).

## Issues
- ASM-CORE-1 (high): after creating a nested component and re-activating root, whole scene stays ghosted/transparent and ghosted geometry cannot be picked (Joint picks fail). Screens: defect-ghost-after-root-activate.png, defect-ghost-blocks-picking.png
- ASM-CORE-2 (high): deleting a component keeps its body (moved to root Bodies) and leaves a dangling Joint1 (red error icon) that Drive Joints still lists/drives. defect-delete-comp-dangling-joint.png
- ASM-CORE-3 (high): Paste New (Ctrl+Shift+V) creates "Body2 (1):1" with no body/geometry, no children; joint to it is accepted. t14.png, t18.png
- ASM-CORE-4 (medium): Move Component typed Z angle changes X/Y distance fields to odd values (-15.377/-41.411); typed X later overwritten. defect-move-typed-values-mangled.png, t13.png
- ASM-CORE-5 (medium): Joint Animate then OK stores animated angle (212.4, outside -180..180) instead of typed 45. defect-animate-leftover-angle.png
- ASM-CORE-6 (medium): origin planes drawn during Joint and are pickable; picking over them yields "Untitled (Geometry)" instead of body face. defect-joint-originplane-pick.png
- ASM-CORE-7 (low): Drive Joints accepts values outside shown range (500 vs -100..100 mm); range label misleading.
- ASM-CORE-8 (low): dirty "*" disappears after undoing a delete although doc modified.
- ASM-CORE-9 (low): Move/Copy (M) does not accept components; stale selection cleared when tool starts; browser component click ignored.
- ASM-CORE-10 (low): toolbar Ground disabled for grounded component (no Unground there); moving a grounded component allowed silently.
- ASM-CORE-11 (low): Joint dialog "Select" buttons clipped when comp label long; stale error "Component 2 must differ" remained until next change; As-built joint comp2 needs Select click first; Component 1 auto-prefilled with ComponentOrigin.
- ASM-CORE-12 (low): Pasted instance Body1:2 shows no children in browser tree.
- ASM-CORE-13 (low): Box with Operation default Join when not touching; app window larger than small display gives clipped UI (harness).
