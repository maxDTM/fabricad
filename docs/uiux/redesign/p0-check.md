# Redesign phase 0 - GUI check of the merged build (dark and light)

Method: private display :62 (ui.sh), mouse and keyboard only (xdotool), screenshots in `p0-check/`. Build: `build/bin/fabricad` at 0c87560.
Display note: the Xwayland screen came up 1900x490 (not 1600x1000), so the Extrude dialog scrolls and the timeline is cramped; items marked (env) may be caused by that.
Theme was switched with the command line (`theme dark|light|system`); Preferences has no Appearance page yet (planned phase 1), so the "via Preferences" path could not be tested.

## What passed
- Live switch dark <-> light without restart (07, 29), including browser, ribbon, timeline, viewport, view cube, status bar.
- Persistence: `ui/theme=light` written to FabriCAD.conf; restart opened light (31). `theme system` resolved to dark with GNOME `prefer-dark` and was persisted (32).
- Menus (app, Open Recent submenu, Show Panels submenu, help, browser context, timeline context), dropdown popups, tooltips, Preferences dialog, file open/save dialogs, Extrude/Fillet dialogs, sketch palette: legible in both themes (02,03,05,09b,10,11,17,19,22,23,25,27,28,30,34,36,38).
- Text typed by the user keeps its case: command line "theme light" (06), distance "25 mm" (19), tab/browser/document names "p0check-light" (24).
- Icons visible in both themes; light set uses darker strokes with the same accents.
- Quick flow in light: sketch, rectangle, finish, extrude 25 mm, fillet 2 mm, save, reopen in dark: all worked, no regressions seen (13-24, 35, 37).

## Issues
| # | Sev | Where | Issue |
|---|-----|-------|-------|
| 1 | Medium | Light, view cube (07, 21, 26) | Cube faces and edges are white on a near-white viewport; only the FRONT/RIGHT/TOP labels are visible, and in sketch mode the cube is a white blob with "FRONT" (13). Mockup light cube has a grey outline and tinted faces. Needs face fill/edge tokens for light. |
| 2 | Medium | File dialogs (23, 34) | Caps styling is applied to user data: file and folder names in the list and in the completer popup are uppercased ("POCHECK-LIGHT.FCAD", "/TMP/CLAUDE-1000/..." in Look in), while the File name field shows the real case. Caps should apply to chrome labels only. |
| 3 | Medium | Status bar (24) | Message "SAVED /TMP/CLAUDE-1000/P0CHECK-LIGHT.FCAD" uppercases the path (user data). Same class as 2; also "THEME: LIGHT". |
| 4 | Medium | Timeline dock (01, 07, 08, 25) | After startup the Timeline is ticked in Show Panels but not visible (zero height from restored state); toggling it off and on shows it (26). May be (env), but the checked-yet-hidden state is wrong. |
| 5 | Low | Extrude/Fillet dialogs (17, 22, 36) | Selection button ("SELECT"/"1 SELECTED") is a saturated pure blue, not the purple accent used by OK, tabs, checkboxes; looks off-palette in both themes. Label "1 SELECTED" is clipped to "1 SELECTE[ ]" in Extrude (19). |
| 6 | Low | Viewport (21, 29, 35) | Grid lines draw over the lower side faces of the body (grid not depth-occluded), in both themes. Likely pre-existing renderer behaviour. |
| 7 | Low | Dark viewport (36) | On-canvas distance/radius chips ("RADIUS 2 mm", "DISTANCE 25 mm") are white boxes in dark theme; legible but not themed. Axis letters Z/Y in dark are dim grey on dark grid (29). |
| 8 | Low | File dialog columns (23, 34) | Truncated columns and sidebar labels: "COMPU...", "88...KIB", "PLA...ENT", dates "10/22/...:25 PM". |
| 9 | Low | Ribbon Modify group, both themes | Fillet/Chamfer icons are nearly flat grey squares with little detail next to the coloured neighbours (08, 04). |
| 10 | Info | Follow system | Flipping GNOME color-scheme while the app ran in `system` mode did not change the app within 3 s (33). Could be the private Xwayland session not delivering the portal/theme signal; needs a check on the real session. |
| 11 | Info | Preferences | No Appearance page / theme control (expected, phase 1); theme is only reachable via the THEME command. |
| 12 | Info | Screenshot 27 | Screen geometry briefly changed size around a context menu; harness artefact (env). |

## Feel vs mockups (v6 tokens dark/light)
Colours, mono caps type, purple accent, green Finish Sketch and amber/orange feature icons match the token mockups. Not matching yet, as expected: layout (feature tree, breadcrumbs, filter chips, params panel), view cube style, selection colour (mockup amber, build lavender/purple), dialog layout (mockup has stepper, segmented controls).
