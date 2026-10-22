# P5 verify: layout toggles (10.2 #2), PARTIAL

Verifier: Sonnet. Build `build/bin/fabricad` (19:27, main fefa14c). 18 runs, pairwise over toolbar x sidebar x card x quick toolbar x orientation x theme x timeline (runs 1-12 cover every value pair; 13-18 are the 6.4 rules: edge order, toolbar+sidebar same side, card sidebar + featureList).

## Method and limits (read this first)
- Display :86 was tiled by Hyprland to 466x239 (five parallel testers' rootful Xwayland windows share workspace 1). The window cannot be resized without touching the compositor (forbidden), and there is no Xvfb/Xephyr. So the mouse and keyboard pass was NOT possible.
- What was done instead: offscreen grabs at 1600x1000 (QT_QPA_PLATFORM=offscreen, FABRICAD_WINDOW_SIZE, FABRICAD_SCREENSHOT) with private XDG dirs and a seeded `ui/*` config, the Extrude script (sketch, rect, EXTRUDE 25) leaving the live card open, plus the FABRICAD_GEOMETRY_DUMP boxes for an off-screen scan.
- NOT verified: Extrude completed by hand (Apply, result in model), changing each toggle through Preferences, persistence after restart (only "config is read at start" is shown), 3D view and the AIS ViewCube (blank offscreen). Rerun the GUI half when a tester can get a big display.

## Matrix (screenshots in toggles/)
| Run | toolbar | sidebar | card | quick | orient | theme | timeline | overlap | off-screen | card live in grab |
|---|---|---|---|---|---|---|---|---|---|---|
| r01 | right | left | free | left | hexDial | light | featureList | ok | ok | ok |
| r02 | left | right | sidebar | off | viewCube | dark | featureList | ok | **card clipped by window edge** | clipped |
| r03 | top | left | sidebar | left | hexDial | dark | strip | ok | ok | ok (scrolls) |
| r04 | right | right | sidebar | right | viewCube | light | strip | ok | **card clipped by dock** | clipped |
| r05 | top | right | floatRight | right | hexDial | dark | featureList | ok | ok | ok |
| r06 | right | right | floatLeft | left | viewCube | dark | featureList | ok | ok | ok |
| r07 | left | left | floatLeft | off | hexDial | light | strip | ok | ok | ok |
| r08 | left | left | floatRight | left | viewCube | light | strip | ok | ok | ok |
| r09 | right | right | floatRight | off | hexDial | dark | strip | ok | ok | ok |
| r10 | top | right | free | off | viewCube | light | strip | ok | ok | ok |
| r11 | top | right | floatLeft | right | viewCube | dark | strip | ok | ok | ok |
| r12 | left | left | free | right | hexDial | dark | strip | ok | ok | ok |
| r13 | right | right | floatRight | right | hexDial | dark | featureList | ok (edge order tool col, quick, card) | ok | ok |
| r14 | left | left | floatLeft | left | hexDial | light | featureList | ok | ok | ok |
| r15 | top | left | sidebar | off | hexDial | light | featureList | ok | ok | ok (card scrolls, FEATURES keeps live row) |
| r16 | top | left | floatRight | right | viewCube | dark | featureList | ok | ok | ok |
| r17 | left | right | floatRight | right | hexDial | light | strip | ok | ok | ok |
| r18 | right | left | sidebar | left | viewCube | light | featureList | ok | ok | ok (scrolls) |

File names are `rNN-toolbar-sidebar-card-qQuick-orient-theme-timeline.png`.

Checked OK: toolbar outermost with sidebar on the same side (r06, r07, r09, r12, r13, r14); title bar full width; quick toolbar sits between card and tool column (r05, r11, r13, r16, r17); floatLeft + quick left puts the quick toolbar outermost (r06, r14); card in sidebar with featureList keeps the live row (r02, r15, r16-like r18); strip hides FEATURES and shows the bottom strip; both themes render all combinations; no widget box outside 1600x1000 except in r02. The floatRight card stops left of the dial column, as in the mockup.

## Issues
### P5-TOGGLES-1 (major) Card clipped when the sidebar is on the right and the card docks in it
- Repro: ui/sidebarSide=right, ui/cardPlacement=sidebar, open Extrude at 1600x1000 (r02, r04).
- Expected: the card fits the sidebar slot, or the dock is at least as wide as the card (left sidebar is 320 px, card body 300 px).
- Actual: the right dock is 260 px wide, the card body is 300 px; title shrinks to "EXTR", SYMMETRIC and the APPLY/ENTER button are cut off; in r02 (toolbar left) CardBody ends at x=1648, beyond the 1600 px window. Default right-dock width should match the left (320) or the card slot should set a minimum width.

### P5-TOGGLES-2 (minor) Card in the sidebar slot needs scrolling on a 1000 px window
- Repro: r03, r15, r18 (card sidebar, left): the slot is about 375-425 px tall, the card needs about 470; OPERATION and OBJECTS TO CUT/JOIN are hidden below the scroll, header title "Untitled" is ellipsized.
- Expected (6.4): usable without hunting for fields; Actual: works (Apply visible, scroll bar present) but the Operation group is not visible by default. Judgement call, low priority.

### P5-TOGGLES-3 (process, blocker for the GUI half)
- Parallel verifiers on one Hyprland workspace shrink each rootful Xwayland window (here 466x239), so the mouse/keyboard checks cannot run. Suggest serial GUI runs, or an Xvfb/Xephyr based display in tools/uitest.
