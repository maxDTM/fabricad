# Phase 4 GUI check (viewport), dark and light

Checker: Sonnet, mouse and keyboard only on private display :83 (`tools/uitest/ui.sh start 83`), binary `build/bin/fabricad` (main at f351af1), fixture `tests/e2e/bracket-v3.fcad`, private `XDG_CONFIG_HOME` / `XDG_DATA_HOME`. No Hyprland output, window or setting was touched.
Screenshots: `p4-check/` (file prefixes below). Reference: `docs/mockups/custom-ui-extrude-v6-tokens-{dark,light}.png`.

Environment notes: the host tiles the Xwayland root to 1900 x 490, so the app window was set to 1900 x 1000 (and 880 x 700, 1900 x 480 / 540 / 1080 for the short and narrow cases) on the private display and shifted with xdotool to see the lower half (`-t` = top band, `-b` = bottom band). Root grabs lag one input event, so every shot nudges the pointer first. Exact 1920 x 1080 grabs are offscreen (`offscreen-1920x1080-{dark,light}-{idle,ext}.png`, no GL there, so no 3D content; ext = `SELECT sketch:Sketch3/profile:largest`, `EXTRUDE 25`).

## Result summary

| Area | Dark | Light | Evidence |
|---|---|---|---|
| Body, edge lines, hover tint, selected-face tint + purple edge, body selection halo | pass | pass (see M1) | dark-01, light-14 |
| Gold extrude preview: edge, fill, handle, handle hover glow, value box (tag + gold frame) | pass (glow faint, L6) | pass | dark-04..07, light-04..06 |
| Hex dial: tip click (again = opposite), ISO/TOP/FRONT/RIGHT, vertex click, inside drag = orbit, ring drag = azimuth only, AZ/EL readout | pass | pass (tip, button) | dark-02, dark-03, dark-03b, light-17 |
| ViewCube option (Preferences > Layout > Orientation) live switch, readout + buttons under it | pass | pass, contrast see M2 | dark-11, light-01, light-02 |
| ViewCube corner click from FRONT gives ISO (AZ 315 EL 35) | pass | pass | dark-11, light-16 |
| Breadcrumb (document > Bracket, command chip), filter bar (segments toggle, tool-restricted segments hide / light, wraps to two rows at 880 px) | pass | pass | dark-13, light-13, light-15 |
| Right view-tool column (fit, orbit, pan, zoom, display menu, views menu) | pass | pass | light-08 |
| Readouts X/Y/Z (pick point / ground plane) and scale bar (20 mm at close zoom) | pass | pass | dark-01-bottom, light-03 |
| Sketch profile shading after Finish Sketch | pass, neutral grey (L5) | pass, very faint (L5) | dark-10, light-09 |
| Toolbar top / left / right | pass (L3) | pass | dark-12, dark-14, light-09 |
| Quick toolbar left / right / off | pass | pass | dark-12, dark-14, light-09, light-11 |
| p2 regression: Extrude, click Sketch3 row in FEATURES, type `25`, Enter | pass (Extrude4, 25 mm) | pass (Extrude5, 25 mm) | dark-08, light-07 |
| p3 regression: docked card keeps Apply visible and does not grow the window | pass at 480 (falls back to float, window stays 480), 540 (docked, MODEL folded), 1000 | pass at 480 | dark-15, dark-17, light-12 |
| Nothing overlaps the card (float right / left, sidebar, toolbar left / right, quick toolbar left / right) | pass (but M3, M4) | pass | dark-13, dark-14, light-10, light-11 |

## Issues

| # | Severity | Issue | Evidence |
|---|---|---|---|
| M1 | Medium | Light theme bodies are washed out. Top face (227,231,235) on a viewport background (245,247,249) is about 1.2:1, only the edge lines separate body from background; the mockup body is a mid grey (191,195,200 top, 173,176,181 wall). Dark is lighter than the mockup too: top (178,185,195) against (155,160,169). Suggest lowering the default body lightness in `ViewerPalette` (about 15 L in light, 8 in dark). | light-03, light-05 vs mockup |
| M2 | Medium | Light ViewCube contrast (p0/p1 M1) is only half fixed. Face-on (FRONT) the edge ribbons give a clear outline, but in the ISO view the left and right faces (240,244,250 / 234,239,245) have no visible boundary against the viewport (245,247,249): the cube reads as a floating "Y" of ribbons with faint labels. Needs a darker face fill or a 1 px outline on the cube silhouette. Dark cube is fine. | light-02, light-16 |
| M3 | Medium | The value box is drawn over the command card when the handle sits behind the card (float right, default view after fit): DISTANCE tag and field cover the "OBJECTS TO CUT/JOIN" row. The value box should clamp away from the card region or go under it. Also in the offscreen grabs (no 3D view) it lands at the top-left, covering the breadcrumb and the command chip. | dark-05, offscreen-*-ext |
| M4 | Medium | ViewCube option, short window (1900 x 480): the view-tool column is placed "beside the dial" but the cube is wider than the dial, so the column overlaps the left side of the cube (FRONT label cut). With the hex dial at 480 the column clears it. | dark-16 vs light-12 |
| M5 | Medium | Card docked in the sidebar: FEATURES shows only the live row (no rows above it, wheel does not scroll, chevron only folds MODEL), at 1000 and 1080 px windows. The "click a FEATURES row to pick the profile" flow therefore cannot be done while docked (it works in the float placements, p2 fix verified there). After dragging the panel splitter a blank 70 px gap opens between card and FEATURES. Decide: auto-expand FEATURES for profile picks like Mirror, or document it. | light-18, dark-15 |
| L1 | Low | Handle hover label "Distance: 25 mm": first letter hidden under the arrow ("istance"), black backdrop in dark, and in light the text is white on a light background (almost unreadable). Should use the value-chip tokens. | dark-07, light-06 |
| L2 | Low | Value box shows `25` without the unit and spinner; the mockup has `25 mm` with a stepper and an `fx boss_height = 25 mm` line. | dark-06 |
| L3 | Low | Vertical toolbar (left / right) drops the label of long names (PRESS PULL, NEW COMP, INTERFERE; MIDPLANE / AS-BUILT / COMBINE sit in the same state); icon only, so those tools lose their name. | dark-12, dark-14 |
| L4 | Low | Sketch profile shading after Finish Sketch is a neutral slate in dark (73,77,90) and nearly invisible in light (only the dark-blue outline reads). Picks for a tool (Extrude) turn it purple, which is clear. | dark-10, light-09 |
| L5 | Low | Live preview glow is faint: dark and light show the gold edge and a peach / tan fill, the 35 % glow only reads on the handle hover. The mockup preview is a stronger amber glow. | dark-06, light-05 |
| L6 | Low | Default ISO azimuth is 315 (X toward lower right); the mockup shows AZ 045 (X to the upper right, Y left). | all vs mockup |
| L7 | Low | Offscreen 1920 x 1080 vs mockup: positions match (breadcrumb 16/16, dial 1702-1870 x 288, column 1868 x 330, readout 336 x 1010, scale bar 1704 x 1018, card right edge ~1646). Content differences remain: fewer toolbar tools (no PROJECT, SWEEP, LOFT, PATTERN, MIRROR, SECTION, SURFACE), search box has no CTRL K chip, no second and third document tabs, card has the older form (START OFFSET, TAPER ANGLE, FLIP) not the mockup's PROFILE chip / DIRECTION icons / fx line, MODEL tree lacks the ACTIVE badge. Mostly out of phase 4 scope. | offscreen-* |
| I1 | Info | The right-click marking menu has an opaque black backdrop on this display (no compositor, translucent window); not reproduced as an app bug. | dark-18 |
| I2 | Info | Opening Preferences cancels a running command (card closed); fine, but a shot of a command and the dialog together is not possible. Prefs dialog opens centered on the window, not the screen. | |

No High findings. The p2 and p3 regressions hold in every placement tried, except for the profile-from-list flow while the card is docked (M5).
