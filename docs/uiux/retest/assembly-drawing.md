# Assembly / drawing retest (round 1 fixes, merged build 1530bc7)

Tester: assembly-drawing retester, display :34 (window floated to 1500x960 via hyprctl because tiling shrank it to 945x241). Mouse and keyboard only. Screenshots in `assembly-drawing/`.
Flow: two boxes (60x30x8 plate, 8x30x30 upright, New Body), Create Components from Bodies, Ground, Move Component, As-built Joint, Interference, Drawing (base, section, dimension, parts list, PDF dialog).

## Verdicts

| Issue | Verdict | Evidence |
|---|---|---|
| UX-01 redraw / stale frames / Move triad | FIXED | 06-box-ok, 08-box2-ok, 12-eye-hide, 05-boxvals (live preview), 24-drag-mid (body redrawn during drag), 26/27 |
| UX-02 viewport picking | PARTIALLY FIXED | 13-hover, 14-click-face ("Face of Body1" in status bar), 31/32 joint pick works. But origin planes win picks in Joint mode (see NEW-ASSEMBLY-DRAWING-1, 29) |
| UX-04 ViewCube drag orbits, keeps selection | FIXED | 16-cube-drag, 17-cube-drag-sel (Body2 stays selected) |
| UX-13 light-on-light / black widgets | FIXED | 03-createmenu, 04-box, 18-assemble, 20-comp-ctx, 28 (Enable checkbox light), 34-interf, 36-drawing (tabs, scrollbar, +/-), 48-export (file dialog light). Interference table still scrolls horizontally (Volume column clipped) |
| UX-17 dialog placement / stable size / hint | FIXED | Dialogs dock left (04, 23, 28, 34, 45), ViewCube and model clear, Dimension hint wraps fully (45). Joint dialog 370 px tall, no resize observed |
| UX-35 undo of assembly ops, Move/Ground in timeline | PARTIALLY FIXED | Move: timeline icon added (26), Ctrl+Z moves body back (27), redo works. Ground: Ctrl+Z / redo toggle pin correctly (22), but Ground still adds no timeline entry |
| UX-36 context menus, pin, naming | MOSTLY FIXED | Component menu has Hide, Activate, Ground, Isolate, Copy/Paste, Properties, Rename, Delete (20). Pin icon on grounded Body2:1 (21). Empty top-level Bodies folder gone (19). Body context menu not re-checked. Components made from bodies are named Body1:1 / Body2:1 (after the body, with instance suffix), not Component1:1 (not tested via New Component) |
| UX-37 parts list | FIXED | 41: both Body1 and Body2 with qty, table clamped inside the frame (right edge sits on the frame line, no overflow) |
| UX-43 drawing annotation | MOSTLY FIXED | Views return to black after commands (41, 44), status shows "VIEW1 (Front)" not "View of View", dimension text "60" no longer overlaps the "A" label (47) though it sits right beside it. Parts list stayed cyan until the next command (42) |

Counts: 5 fixed, 2 partially fixed (UX-02, UX-35), 2 mostly fixed (UX-36, UX-43; counted as fixed with caveats in the summary), 0 not fixed, 0 regressed.

## NEW defects

- NEW-ASSEMBLY-DRAWING-1 (high): in As-built Joint / Joint selection mode the origin planes (shown orange, big, although the Origin folder is hidden) are pickable and sit in front of the bodies. Clicking at the middle of the upright set Component 1 to "Untitled" (the root) and a second click gave "Component 2 must differ from component 1" (29). Picks only worked on parts of the bodies not covered by the planes (30, 31). Same root as round-1 AD-3. Planes should not be displayed/activated unless the Origin is shown, or should be lower pick priority than bodies.
- NEW-ASSEMBLY-DRAWING-2 (low): Move Component triad stays at the original pivot while the body is dragged (24); Fusion moves the triad with the body.
- NEW-ASSEMBLY-DRAWING-3 (low): Second Box defaulted to Operation = Join (auto-joined into Body1) rather than New Body, so the bracket needed a redo. Matches Fusion behaviour but surprising for the assembly flow.
- NEW-ASSEMBLY-DRAWING-4 (low): faint dotted artifact along the lower-left edge of the plate in iso view (06, 08, 11).
- NEW-ASSEMBLY-DRAWING-5 (low): ViewCube remains white-on-white with tiny, sparse, rotated labels (16, 17); not covered by the fixes.
- NEW-ASSEMBLY-DRAWING-6 (low): Drawing tab "Untitled Drawing" opened with an empty SHEETS list and no sheet border until the first view was placed (36 vs 39).
