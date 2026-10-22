# Round 1 - Solid modelling and history (tester: solid, display :33)

Screenshots: docs/uiux/round1/solid/ . Window must be sized to 1395x855 at 0,0 (harness).
Workflow done: XY sketch rectangle (typed 40/30) -> Extrude (profile picked from browser) -> Box primitive -> manipulator drag -> edit Extrude from timeline -> Shell(body from browser) -> undo/redo -> rollback marker -> rename -> save -> new tab -> S search -> marking menu -> ViewCube.
Not covered: Split Body, Move/Copy, Revolve/Hole/Pattern/Mirror/Fillet/Chamfer execution (blocked by SOLID-1); dialogs opened only.

SOLID-1 (critical) Faces, edges and sketch profiles cannot be picked by left click in the viewport (extrude profile, fillet edges, box-select; no hover highlight). Only browser clicks select (Sketch1 gave "1 selected"). Blocks Fillet/Chamfer/Hole/Split/Rotate. Shots 22,23,26,35,36c.
SOLID-2 (high) Light-on-light text: Extrude/Revolve/Fillet/etc dialog labels and disabled-looking fields, Create menu items, display-settings menu, browser context menu, sketch palette options, cmd> and S-search typed text, ViewCube face labels. Black checkboxes. Shots 20b, 40b, 45, 69b, 09b, 37b.
SOLID-3 (high) Viewport does not repaint after model/visibility changes (eye toggle, Edit Feature OK, rollback marker drag) until camera moves. Shots 46,48,53,56,65.
SOLID-4 (high) ViewCube cannot be dragged to rotate; drag leaves a stuck cyan face highlight; edge/corner hit areas tiny. Shot 06.
SOLID-5 (medium) Rectangle second click in viewport ignored; rubber band frozen; only typed values commit. Typed first number goes to Height, not Width. Shots 12,15,18.
SOLID-6 (medium) No live 3D preview of Extrude while editing/dragging distance; arrow manipulator stays at profile; body disappears during Edit Feature. Shots 51,52.
SOLID-7 (low) Manipulator tooltip "Distance: 30 mm" uses serif font. Shot 52.
SOLID-8 (low) Document tabs: black background, active tab not distinct (only red x); sketch tab-scroll arrows black. Shot 72c.
SOLID-9 (low) Status bar selection shows stale name "Body1" after rename to Bracket. Shot 71.
SOLID-10 (low) Selected body renders translucent blue showing interior; unselected top face nearly same colour as background. Shot 31,46.
SOLID-11 (low) Help dialog: row "Cancel command / clear sele..." misaligned/no icon; log popup extends past window; help output only "> app.help". Shots 39c,40.
SOLID-12 (low) Marking menu and some popups render on opaque black (no compositing; may be harness). Shot 24.
SOLID-13 (low) Marking menu in Extrude offers "Repeat 2-Point Rectangle". Shot 24.
SOLID-14 (low) Sketch stays visible after extrude (Fusion hides consumed sketch); Origin planes not shown when choosing sketch plane (must pick in browser). Shot 28.
Works: timeline right-click menu, rollback drag, undo/redo, rename (double-click), save, tabs, S search, Shell on body, Box manipulator.
