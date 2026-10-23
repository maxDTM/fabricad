# UI2 user UI fixes: mouse and keyboard retest

Tester: Sonnet, real X input (xdotool) on private Xwayland :98 (1400x860 window), build of main at b585826 (build/bin/fabricad 05:13). Screenshots in `docs/uiux/ui2/user-ui-fixes/`.

## Results

| Check | Result | Evidence |
|---|---|---|
| Right drag orbits (AZ 315/EL 35 -> 298/24) | pass | 13-rdrag-orbit |
| Right click without drag opens marking menu | pass (see -2) | 12-viewport-rclick-menu |
| Middle drag pans | pass | 52-panned |
| ViewCube is the default orientation control | pass | 01-start |
| Click faces FRONT/TOP/RIGHT -> exact views (270/0, 270/90, 0/0) | pass | 21-23 |
| Click edges (top-front, front-right) -> 45 deg views | pass | 32, 33 |
| Click corner / just off the hub (offset 6 px) -> iso corner | pass | 30, 31, 37 |
| Click just outside the cube silhouette | no action (ok) | 35 |
| Hover highlight on cube parts | pass | 31, 38, 91 |
| Drag on cube orbits | pass | 39 |
| Home, roll arrows | pass | 20, 24, 25 |
| Light theme cube (hover, top view) | pass | 91, 92 |
| Sketch origin dot centred on axes | pass (within 0.5 px of both axis lines; no hover highlight seen, see -4) | 41-43 |
| Toolbar split buttons: right click opens dropdown under the button (PLANE, SELECT, SURFACE, INSERT, RECTANGLE, OPTIONS, TOOLS) | pass | 02-05, 44, 46, 47 |
| Right click on a button without dropdown (EXTRUDE) opens nothing | pass | 06 |
| Left click on split arrow opens dropdown; left click on the body runs the tool | pass | 07, 45, 40 |
| Section captions (CREATE, MORE, SKETCH, sketch CREATE, CONSTRAINTS) open under the group | pass | 08-10, 48, 49 |
| Light theme dropdown anchored | pass | 93 |
| Split arrow width | looks wider; not measured numerically | 11 |
| Origin planes do not steal picks from faces (top and side face in iso, planes visible) | pass | 66, 67 |
| Extrude card: DIRECTION order One side / Symmetric / Two sides with icons | pass | 50, 60-62 |
| MERGE WITH chip: click starts body pick (BODIES filter), shows "1 BODY  Body1" | pass | 71, 72 |
| PREVIEW switch toggles the live preview off/on (mouse and Space) | pass | 53/54, 56/57, 75 |
| MORE OPTIONS collapsed, expands to START OFFSET (mouse and Space) | pass | 60, 76 |
| Keyboard: Tab reaches every control, Left/Right changes segmented option, Enter applies | pass (see -1) | 73-tab*, 74-tabB*, 79-81 |

Not testable here: the real Wayland/Hyprland "popup in window centre" case. :98 is a rootful Xwayland, where all popups were anchored correctly; the Wayland path needs the owner's live session.

## Issues

* UI2-USER-UI-FIXES-1 (low): Tab order. Repro: open Extrude with a profile, press Tab repeatedly from PROFILES. Expected: top-to-bottom order. Actual: PROFILES, clear, EXTENT, DISTANCE, TAPER, swap, MERGE WITH, clear, PREVIEW, MORE OPTIONS, then DIRECTION and OPERATION segments, then CANCEL (74-tabB*). DIRECTION and OPERATION come after MORE OPTIONS.
* UI2-USER-UI-FIXES-2 (low, possibly environment): the marking menu (right click) paints a solid black rectangle behind its buttons (12-viewport-rclick-menu), likely a translucent overlay window on Xwayland without a compositor. Expected: transparent background.
* UI2-USER-UI-FIXES-3 (low): focusing DISTANCE or TAPER adds an "fx 10 mm = 10 mm" row under each, so the card grows and rows jump while tabbing (73-tab4..8). Expected: stable layout while moving focus.
* UI2-USER-UI-FIXES-4 (info): the origin dot gets no hover highlight in sketch mode (43); the dot itself is centred.
* UI2-USER-UI-FIXES-5 (info, harness): `ui.sh stop` kills the app without closing it, so the next start on the same display shows the autosave recovery prompt.
