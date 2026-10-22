# Round 1: 3D viewer, navigation and selection

Tester: hands-on, X events only, display :31 (1400x900 after floating the Xwayland window).
Model used: one box primitive (Body1). A bracket could not be built to completion because of VIEWER-2/13.
Screenshots: docs/uiux/round1/viewer/ (name given per issue).

## Issues

- VIEWER-1 (critical) The viewport is not redrawn after scene-state changes; the change appears only after a navigation event (MMB pan, wheel, orbit).
  Seen with: new body after Box OK (box invisible until a 1px MMB pan: s13/s14 vs s15), browser eye on Body1 (s16/s17/s18 vs s19), context-menu Hide (ctx_hide), Origin eye (origin_eye_on vs origin_eye_on_nudged), display style Wireframe/Hidden Line (disp_wire vs disp_hidden_after_nudge), Layout Grid toggle (grid_off vs grid_off_nudged).
  Selection changes from the browser and empty-click deselect DO redraw immediately, so only the "visibility/display" paths miss the update-view call.
- VIEWER-2 (critical) Bodies, faces, edges and vertices cannot be picked in the viewport at all, and there is no preselection highlight.
  Tried: Select tool default and each of Body/Face/Edge/Vertex priority (prio_161, selface_*), click on face centre, edge midpoint, vertex, a 20px grid sweep of 400 clicks across the body inside Fillet (fillet_sweep, grid_after), Move/Copy (move_clicked), hover in all of them. Dialog inputs stay "Select" (0 selected), status bar shows no count.
  Window-select drag draws the blue rubber band (boxselect_drag) but selects nothing (boxselect_done).
- VIEWER-3 (high) Consequence of VIEWER-2: no tool can take a viewport selection. Fillet "Edges" can never be filled; Move/Copy "Bodies" works only when Body1 is clicked in the browser (move_browser_sel); rotate/move of a face/edge-driven reference is impossible. Move shows only an "X distance" value box, no triad.
- VIEWER-4 (high) Dragging on the ViewCube does not orbit the view (vc_drag_mid, vc_drag_end). Press-drag-release also clears the current body selection as if it were an empty-space click. Expected: drag = free orbit, cube follows the camera.
- VIEWER-5 (high) Light text on light background across the UI: all dropdown and context menus (CREATE menu s04, SELECT menu sel_menu, ViewCube right-click vc_rmb, browser context menu browser_ctx, navbar display/grid/views/settings menus nav_*), every command dialog label and field (HOLE s03, BOX s05-s11, FILLET fillet_hover, MOVE/COPY move_tool), command box placeholder and "mm"/"cmd>" in the status bar. Menu items and dialog fields look disabled (about #E0E0E0 on white) even though they are active; the box dialog fields did not accept typed input (VIEWER-13).
- VIEWER-6 (medium) ViewCube hover gives no feedback (vc_hover_montage: nothing highlighted for face, edge, corner). The highlight (saturated cyan, text unreadable on it) appears only after a click (vc_hit_montage). Expected: soft blue highlight on hover, as in docs/research/ux.md 5.1.
- VIEWER-7 (medium) ViewCube hit areas: the cube is about 90px and white-on-white (vc_iso_zoom) so faces/edges are hard to see. Clicking at the visual corner (1276,183 / 1358,183 / 1315,252 in the 1400x900 window) produced edge views, not corner (isometric) views; true corner views are practically unreachable. Edges and faces hit correctly (vc_hit_montage). Make the cube larger, higher contrast, with generous edge/corner hit zones.
- VIEWER-8 (medium) ViewCube is missing the home button, the 90-degree rotate arrows and the adjacent-face arrows (ux.md 5.1). Only the right-click menu has Go Home / Orthographic / Perspective / Set as Home / Reset Home.
- VIEWER-9 (medium) Framing: Home / F6 / double-MMB fit leave the body clipped at the bottom, running behind the navigation bar (vc_home, key_home, after_dblmmb). Clicking the FRONT face on the cube zooms so that the body fills the whole viewport edge to edge (vc_click_front). Fit needs margins and should account for overlays; a cube click should keep the zoom.
- VIEWER-10 (medium) After a LMB orbit (nav_orbit_tool) or Shift+MMB orbit (orbit_shift_mmb) the model and grid show strong perspective convergence although the Views menu has Orthographic checked and Home view was truly orthographic. Camera projection is switched or not honoured by orbit.
- VIEWER-11 (medium) A hidden (eye off) body that is selected is still drawn as a translucent blue ghost after redraw (s19). Hidden objects must not render or be selectable.
- VIEWER-12 (medium) Origin planes: while the Box/Hole "Plane" input is active the origin planes are hidden and not pickable in the viewport (s05-s09); the plane has to be clicked in the browser. Fusion shows and highlights the planes during plane input. When the Origin eye is turned on, only one large orange plane plus axes shows and the planes are far larger than the part and clipped by the viewport (origin_eye_on_nudged).
- VIEWER-13 (medium) BOX dialog Length/Width/Height (and Position) fields did not accept typed values (triple-click, ctrl+a, type, Tab: nothing changed, s11). Only the on-canvas Height box was editable. Final body size did not match the values typed.
- VIEWER-14 (low) SELECT menu offers only Select and 4 priority modes; no window / freeform / paint selection, no selection filters (ux.md 2.1). Priority mode has no visible state (no check mark on the active item).
- VIEWER-15 (low) The 4th promoted CREATE icon (Hole) uses a plain cube icon, indistinguishable from the Box entry in the CREATE menu (s03/s04).
- VIEWER-16 (low) Browser Origin children (axes, planes, point) have no individual eye toggles; only the folder has one (origin_eye_on).
- VIEWER-17 (low) Document tab is a solid black rectangle with white text and a red close box, visually heavy and unlike the rest of the light theme (s02).
- VIEWER-18 (low) Navigation settings menu starts with an empty separator row (nav_settings_menu); Views menu shows "Home" label and "Home" shortcut (nav_views_menu).
- VIEWER-19 (low) Floating command dialog sits on the right and covers part of the model during edge/face picking (fillet_hover, move_tool); it also changes height when the first selection is made (move_tool vs move_browser_sel).

## Verified working
Wheel zoom at cursor, MMB pan, Shift+MMB orbit, LMB orbit tool, double-MMB fit, Ctrl+5 Top, Home key, F6, ViewCube face and edge clicks and Go Home, browser hover preselect, browser click select, empty-click deselect (redraws), nav bar menus open.

## User-reported items
(1) confirmed = VIEWER-5; (2) confirmed = VIEWER-4/6/7/8; (3) confirmed and wider = VIEWER-1; (4) confirmed = VIEWER-2/3.
