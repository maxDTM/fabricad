# Phase 2 + 3 GUI check (sidebar and command card), dark and light

Checker: Sonnet, mouse and keyboard only on private display :68, binary `build/bin/fabricad` (main at 84befe3), fixture `tests/e2e/bracket-v3.fcad`.
Private config (`XDG_CONFIG_HOME`, `XDG_DATA_HOME` in the scratchpad), so no user settings or autosaves were touched.
Screenshots: `p2p3-check/` (numbers below refer to the file prefixes). Reference: `docs/mockups/custom-ui-extrude-v6-tokens-{dark,light}.png`.

Environment note: Hyprland tiled the private Xwayland window to **945 x 490**. The app window was shrunk to 940 x 480 with xdotool on the private display (no compositor action) and the tall cases (sidebar card, status bar) were seen by offsetting the app window inside the private display. Full-size sidebar comparisons use the offscreen grab at 1920x1080 (`80-offscreen-*`). Screen grabs on Xwayland lag one input event behind, so every shot nudges the pointer first.

## Result summary

| Area | Dark | Light | Notes |
|---|---|---|---|
| FEATURES / MODEL / SELECTION headers vs rows | pass | pass | 36 px band, strong rules, accent tick, caps 12 px label; clearly different from rows (80, 69). Light is subtler (band 233,236,241 vs rows 243,245,248) but the rules carry it. |
| Group collapse / expand (chevron) | pass | n/a | 02/03 (see issue 7) |
| Tri-state suppress checkbox | pass | pass | Hole1 Space gives a dash on Base plate (04, 72); click on the dash re-includes all; click on a ticked group suppresses all, strike-through, `off`, dependants turn red (06, 73); one undo restores (07). |
| GROUP SUPPRESSED tip | pass | not seen | 08 (tip wraps to 3 lines, see issue 8) |
| Rollback bar drag | pass | n/a | snaps to gaps, ghost bar at the old place (10, 11) |
| Roll Marker Here / to Group End | pass | n/a | 14, 19 |
| Drag reorder, valid | pass | n/a | Sketch3 above the Slots group (22, 23), one undo (24) |
| Drag reorder, refusals | pass | n/a | red drop line plus tip `WOULD MOVE BEFORE ITS INPUT 'SKETCH3'` / `'FILLET2'` (17, 20) |
| F2 rename (group and feature) | pass | n/a | 15, 25 |
| Space suppress | pass | pass | 04, 72 |
| Context menus (feature, group, strip) | pass | pass (feature) | 12, 13, 37, 74 |
| MODEL eyes (body, master) | pass | n/a | 27 |
| SELECTION panel | pass | pass | 28; type, name, volume, area; feature selection shows state and position (37) |
| Sidebar left / right | pass | n/a | live from Preferences > Layout (33); persisted |
| Timeline style feature list / strip | pass | n/a | live (36); strip has group brackets and the right-click menu (37) |
| Card float right | pass | pass | 40, 48, 77 |
| Card float left | pass | n/a | 61, 64 |
| Card in sidebar (FEATURES collapsed) | pass | pass | 50, 52, 53, 79 |
| Card free / where dragged, drag memory | pass | pass | drop targets highlight while dragging (49, 65); reopens at the dropped place (67) and again after a restart (75) |
| FEATURES auto-expands during Mirror object pick, card docked | pass | n/a | 57, 58: list open with the live `Mirror` row, pick works, Esc restores (59) |
| Extrude end to end (E, pick profile, 25, Enter) | pass in all 4 placements with a profile picked **in the viewport** | float right and sidebar and free | see issue 1 for the pick-from-list path |
| Settings persist after restart | pass | pass | theme, sidebar side, card placement, free position, `panels` (MODEL stays collapsed), `sidebarSplit`, timeline style all read back (70, 75) |

## Issues

| # | Severity | Issue | Evidence |
|---|---|---|---|
| 1 | **High** | Enter in the FEATURES list steals the command's Enter. With a creating command active (Extrude, profile chosen by clicking the *Sketch3 row* in FEATURES), typing `25` reaches the distance field, but Enter is handled by the list (`Enter edits`), so Sketch3 opens for editing and the Extrude is lost instead of applied. Reproduced in dark (float right) and light (float right). Picking the profile in the viewport works. Fix: while a command is active, the list must not consume Enter (or must hand focus back to the viewport/card after a pick). | 41, 42, 43-BUG, 78-BUG |
| 2 | Medium | Docking the card in the sidebar makes the main window grow: 940x480 became 940x866 (dark) and 940x922 (light) as soon as the card docked. The card plus the three panel minimums set a window minimum height near 900 px, which does not fit a 768 px laptop screen. Resizing back to 480 afterwards works, so it is an auto-grow, not a hard minimum. Consider shrinking panel minimums while a card is docked or falling back to floating when the window is short (6.4 already falls back when the sidebar is hidden). | 55, 79 |
| 3 | Medium | At 480 px height the docked card shows no Apply / Cancel row (it needs scrolling); only the viewport pick, typing and Enter finish it. Same root cause as 2. | 50, 52 |
| 4 | Low | Pressing Enter with focus in the viewport and no command repeats the last command (opened Extrude again). Probably by design (repeat last), noted because it looks like a surprise next to issue 1. | 54 |
| 5 | Low | With a narrow window (945 px) the card drop targets FLOAT LEFT and FLOAT RIGHT overlap and their captions print over each other. | 49 |
| 6 | Low | MODEL tree has no `ACTIVE` badge and no component row (`bracket-v3 > Bracket`) as in the mockup; it shows ORIGIN / BODIES / SKETCHES under the part. Mockup rows also read `Sketch 1`, ours `Sketch1` (fixture names). | 80 vs mockup |
| 7 | Low | Collapsing a group marks the document modified (tab dot) because the fold state is in the document. Acceptable if intended; otherwise store it in the view state. | 02 |
| 8 | Low | The GROUP SUPPRESSED tip wraps `TICK TO INCLUDE ITS 2 / FEATURES AGAIN` onto two lines at the sidebar edge where the mockup keeps one line, and it stays visible behind an open context menu (13). The plain tooltip for a suppressed row also repeats itself (`POCKET1 (SUPPRESSED) / SUPPRESSED`). | 08, 13, 14 |
| 9 | Low | Strip-style context menu lacks `Insert Feature After`, which the list menu has. | 37 vs 12 |
| 10 | Low | Preferences > Layout: the explanatory text under the options is cut off ("... The toolbar"), and the little layout preview does not show the strip nor the card in the sidebar. | 31, 60 |
| 11 | Info | In strip style FEATURES is hidden and a collapsed MODEL leaves an empty block under it. | 36 |

## Compared with the mockups

Sidebar (dark and light, `80-offscreen-*` against the mockups): row structure, group chevron / folder / count / tri-state box, value column, `off` for suppressed rows, live row, ROLLBACK bar with its label, dashed insert button, SELECTION key/value grid, MODEL eyes and caption meta match. Deliberate differences: panel headers are bands with strong rules and an accent tick (the mockup headers are thin), per the user feedback in 5.2. Card (`40`, `61`, `77`, `79`): title with key chip, step tabs `01 PROFILES / 02 DISTANCE / 03 CONFIRM`, field list, Cancel (Esc) / Apply (Enter) match; the 480 px window shows it scrolled.

## Fixes (ui/app4, 2026-10-22)

| # | Status | Change |
|---|---|---|
| 1 | fixed | While a command runs, Enter typed with the focus in FEATURES goes to the command (Apply). A click on a row during a command also hands the keyboard back to the viewport. Checked on display :82: Extrude, Sketch3 picked in the list, `25`, Enter gives Extrude4 at 25 mm. Test: `test_command_card enterInFeatureListAppliesTheCommand`. |
| 2, 3 | fixed | A docked card has no fixed height. Its minimum is the chrome plus about 2 body rows, its body scrolls, and Cancel / Apply stay visible. MODEL and SELECTION may shrink to their headers, and they fold when fewer than 2 rows fit. A sidebar too short for that makes the card float (the 6.4 fallback, setting unchanged), and it docks again when the window grows. Checked on :82 at 945x490 (docked, footer visible, window not grown) and 945x400 (floats). Test: `dockedCardFitsShortWindows`. |
| 4 | by design | Enter with no command repeats the last one (Fusion). |
| 5 | fixed | Drop-target captions that would overlap move to another corner of their target. |
| 6 | data | The ACTIVE badge and component rows appear when the design has components. The fixture has none. |
| 7 | open | The fold state is document data (core). |
| 8 | fixed | The tip text stays on one line when it fits in 300 px, and the tip hides when a context menu opens. A row tooltip no longer repeats the state as its status. |
| 9 | fixed | The strip's context menu has Insert Feature After. |
| 10 | fixed | The Layout note is complete. The preview draws the strip, the panel headers and a docked card. |
| 11 | not reproduced | Strip style with MODEL collapsed: SELECTION takes the rest. |
