# ui2 round: summary

## 2026-10-23: fix2 (findings of `user-ui-fixes.md` and `tutorial.md`, merge of ui2/san)

User decisions of 2026-10-23 (right-drag orbit, ViewCube default, sketch origin, dropdowns anchored under their
label, right-click on a split button opens its dropdown, the Extrude card body) were retested in
`user-ui-fixes.md` and all pass. This round fixed what those two runs found:

| Id | Fix |
|---|---|
| UI2-USER-UI-FIXES-1 | Command card: Tab walks the body rows in layout order (segmented rows no longer come last), then Cancel, Apply (`CommandCard::orderFooterFocus`, also after a relayout). Test in `test_command_card`. |
| UI2-USER-UI-FIXES-2 | Marking menu on X11: the window is shaped to the hub, slots and list, so no black rectangle without a compositor. |
| UI2-USER-UI-FIXES-3 | A number with a unit ("10 mm", what a field shows) is stored as a value, not an expression: no `fx 10 mm = 10 mm` row, the card no longer grows while tabbing. Test in `test_tools`. |
| UI2-USER-UI-FIXES-5 | `ui.sh stop` drops the display's private autosaves (`KEEP_RECOVERY=1` keeps them). |
| UI2-TUTORIAL-1 | Floating bubble (beside an open menu) stays inside the main window when it fits there. |
| UI2-TUTORIAL-2 | No off-script hint while a tool is still running (Sketch Dimension commits before its card takes the value). |
| UI2-TUTORIAL-3 | After the Open / Save As dialogs the window is activated and the view gets the focus; Ctrl+9 focuses a shown but unfocused command line instead of hiding it. |
| UI2-TUTORIAL-4 | `code` in bubble text renders as code; the finish card expands `{timeline}`. |
| UI2-TUTORIAL-5 | Hints are text.secondary; only off-script corrections use status.warning. |
| UI2-TUTORIAL-6 | "Can't see it? Orbit, or press Home" waits 4 s into the step. |
| UI2-TUTORIAL-7 (part) | The bubble keeps clear of the on-canvas value boxes (step 7). |
| UI2-TUTORIAL-8 (part) | Show me with a hidden target says how to reveal it; a step without actions says so. |
| UI2-TUTORIAL-9 | Measure step text no longer claims a fixed 60 mm whatever was picked. |
| UI2-TUTORIAL-10 | Back (Alt+Left) goes past steps that skip themselves (step 2 "already Part"), so Alt+Left from step 3 works; Alt+Right skipping step 2 was that same auto-skip. F7 on a step without actions now shows a hint. |

ui2/san (sanitizer builds, fuzz drivers, damaged STEP/STL/.fcad hardening, `docs/TESTING.md`) is merged.

Still open:
* Real-Wayland dropdown placement: not reproducible on rootful Xwayland, needs the owner's live session
  (`FABRICAD_POPUP_DEBUG=1` logs each placement).
* UI2-USER-UI-FIXES-4 (info): the sketch origin dot has no hover highlight.
* UI2-TUTORIAL-11 (info): Save dialog starts in the working directory when no folder was used before (the GUI
  runner relies on it; a default to ~/Documents would write into the real home during runs).
* Tutorial step 4 rest: tester rounds (T2 off-script, T3 docs-only from `docs/tutorials/bracket.md`).

## 2026-10-23: tut/finish (tutorial leftovers)

| Id | Fix |
|---|---|
| UI2-TUTORIAL-7 (rest) | "Edit the history": the bubble keeps off the FEATURES list / strip / MODEL tree around a row target. Measure: the window corners are extra places, and covering the target or the next click target always loses, so the second rim stays clear. |
| UI2-TUTORIAL-8 (rest) | Rib: a separate "Click Create Sketch" step before the YZ plane step, so the anchor never points at a plane that is not shown yet. |
| UI2-TUTORIAL-9 (rest) | Measure completes only with the two top rims (selectors at z = 10); a floor circle keeps the step waiting, with a hint. |

Also: the model target glows in the view (tutorial highlight style), the bubble's ⋯ menu gives the keyboard back,
the Markdown copy `docs/tutorials/bracket.md` and the `fabricad_tutorial_offscript` GUI scenario
(`docs/plans/tutorial.md` §9 part 3).
