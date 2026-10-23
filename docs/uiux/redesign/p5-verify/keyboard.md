# P5 T-keyboard verification (keyboard only, dark and light)

Tester: Sonnet verifier. Build: main fefa14c (build/bin/fabricad). Private Xwayland :87, xdotool key/type only (no mouse
events), QT_SCALE_FACTOR 0.75, display 945x490 or 1900x490 (the shared Hyprland tiles the Xwayland window, so the display
size was not controllable; exact-size grabs were not done). Screenshots in `keyboard/` (numbering = run order).

Harness notes (not app bugs): there is no window manager on :87, so X input focus was set with `xdotool windowfocus`
on the main window or on a dialog (Preferences, recovery dialog) as a WM would. Screenshots after focus changes were
sometimes stale (see P5-KEYBOARD-1), so a 1 px own-window resize ("nudge") was used to force a full repaint before the
ring checks.

## Result: FAIL (extrude flow cannot be done keyboard-only; several controls unreachable)

| Area | Result |
|---|---|
| Search (S), BOX via search, Enter/Enter to apply | pass (17, 18) |
| Command line route (`extrude`) | pass, opens card (51) |
| Extrude card Tab traversal, focus rings | FAIL (KEYBOARD-2, 3) |
| Extrude profile pick, apply | FAIL, no keyboard way found (KEYBOARD-4) |
| FEATURES list: Up/Down, Space suppress, F2 rename | pass (43, 44, 46: Box1 suppressed "off" then restored, renamed "Base") |
| Dial: Tab to it, ring, arrows orbit, Home resets | pass with note (59-*, KEYBOARD-8) |
| Preferences: open via S "prefer", Tab order, tabs via arrows, theme radio via Right | pass, light applied live (60-63) |
| Focus rings dark | visible on rows, headers, dial, command line, combos, spin; weak on icon buttons, CLOSE, tab bar |
| Focus rings light | same pattern (64-l-tab*); weak on icon buttons and Preferences labels |

## Tab order of the main window (no command active)
cmdline, FEATURES header, search button, filter button, feature row(s), MODEL header, eye button, MODEL rows, SELECTION header,
dial, back to cmdline (12 stops, 57-tab*). NOT reachable by Tab: main toolbar buttons (EXTRUDE etc.), document tabs, title-bar search
box, undo/redo/save, ISO/TOP/FRONT/RIGHT, the view-tool column, quick toolbar.

## Issues

**P5-KEYBOARD-1, medium: focus rings left behind / stale paint.**
Repro: start app, Tab 4x (Origin row, MODEL header, ..., SELECTION header) with no mouse; screenshot at once (48-tab4).
Expected: one ring on the focused widget. Actual: rings remain on the previous Base row, MODEL header and SELECTION header at the same
time; they disappear only after a forced full repaint (48-nudged). Related: after Escape / Space+F2 the screen showed the old state until
the next key (46-s: card already closed, rename already applied but shot showed card). Could partly be Xwayland, but widgets do not
invalidate the old ring on focus-out.

**P5-KEYBOARD-2, high: Tab does nothing inside the extrude card.**
Repro: S, "extrude", Enter (or command line `extrude`); press Tab 6-9 times (40-tab*, 52-*, 53-*).
Expected (status bar says "TAB NEXT FIELD"): focus walks START OFFSET, direction segments, EXTENT, DISTANCE, TAPER, FLIP, OPERATION, CANCEL.
Actual: no visible change; Space on each stop does nothing; digits always land in DISTANCE (41-type7, 54-digit5). START OFFSET, direction
segmented control, EXTENT combo, TAPER, FLIP, OPERATION buttons and CANCEL are unreachable by keyboard (Esc and Enter only).

**P5-KEYBOARD-3, high: no focus ring on the field that receives card input.**
Repro: as above, type `7`: DISTANCE shows "7 mm" with no ring or caret highlight (41-type7). Expected a visible ring (token accent.focus).

**P5-KEYBOARD-4, high: extrude cannot be completed without a mouse.**
Repro: with a box present, open EXTRUDE; PROFILES stays "SELECT" and APPLY stays disabled; no keyboard method (Tab, arrows, Space, Enter, F6, command line prompt "Select Profiles") picks a face/profile (19-enter, 51-cl-extrude). Expected a keyboard way to pick (e.g. cycle candidates, or select in MODEL/FEATURES then extrude).

**P5-KEYBOARD-5, high: toolbar, tabs, view controls are not in the Tab order.**
Repro: Tab through a whole cycle (57-tab1..12). Only sidebar, dial and command line are visited. No keyboard access to EXTRUDE/other toolbar buttons, ISO/TOP/FRONT/RIGHT, view column, undo/redo/save, tabs (only S search and the command line substitute).

**P5-KEYBOARD-6, medium: weak focus rings on small controls and Preferences.**
FEATURES search/filter buttons and MODEL eye button show only a faint fill, no 2 px accent.focus ring (57-tab3, 57-tab5, 57-tab8, 64-l-tab3). In Preferences the CLOSE button, tab bar and radio/checkbox labels show a pale tint only, nearly invisible in light (61-tab4, 61-tab5, 63-l-tab4, 63-l-tab5). Plan 2.x: 2 px ring, 1 px offset.

**P5-KEYBOARD-7, medium: global shortcuts depend on focus; plan keycap mismatch.**
S opens command search only when no text-like widget has focus: with a panel header focused (after Tab) S, typed text and Return did nothing/toggled collapse (34-extrude-open: FEATURES collapsed by Return, no search). At a fresh start, with nothing focused, S and Ctrl+K did nothing until a Tab was pressed (36-s; may be the missing WM activation). Ctrl+K (plan 4.x keycap "CTRL K") does nothing; the box shows "S".

**P5-KEYBOARD-8, low: dial step sizes.**
Right pressed twice from AZ 315 EL 35 gave AZ 350 EL 30 (not 345/35); Up then gave AZ 357 EL 44 (59-dial-*). Plan says 15 degree steps. Home restores ISO correctly.

**P5-KEYBOARD-9, low: Space on a FEATURES row during an active extrude suppresses the feature under edit context**
(43-shift+Tab: Box1 "off", body vanishes while the card is open). Probably intended, but no confirmation and rollback state is invisible.

**P5-KEYBOARD-10, low: F2 rename during an active command.** Editor opened (44-f2-typed) and accepted text "Base" (applied on a later repaint, 46-s). No issue except KEYBOARD-1 staleness.

## Not covered
Exact 1920x1080 / 1280x800 layouts (display was tiled to 945x490 or 1900x490), the card's pass through steps 2-3, undo/redo by keyboard, FEATURES rollback bar keyboard move (not reached; arrow keys beyond the list not tried), dark-theme dial ring checks at scale 1.
