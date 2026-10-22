# Plan: Onshape-style feature list with groups

Status: planned, not started. Executed as **phase 2 of `docs/plans/ui-redesign.md`** (worktree `ui/sidebar`), with
the UI-independent core work (groups, commands, scripting, `TimelineActions`) allowed to start earlier. Where this
plan and the redesign differ, the redesign wins; this file was reconciled with it on 2026-10-22. Order of all plans:
`docs/plans/ROADMAP.md`.

## Goal

Add an Onshape-style **feature list** (the **FEATURES** panel of the redesigned sidebar) as the **default** history
view. The bottom icon timeline (the strip) stays as an option. Features can be gathered into **groups** (folders),
and a whole group can be suppressed or unsuppressed in one step. A toggle in **Preferences > Layout > Timeline
style** chooses between *Feature list (sidebar)* and *Bottom timeline (icons)* (redesign 6.1, setting 7).

## Scope

1. **Setting** (owned by the redesign's `UiSettings`, phase 1).
   - Preferences **Layout** page (redesign 6.1), not the General page.
   - QSettings key `ui/timelineStyle`, values `featureList` (default, redesign decision 1) and `strip`. There is no
     older key to migrate: no `prefs/*` timeline key exists in the code.
   - Read and written through `UiSettings` (`changed(key)`), not directly through `AppState`.
   - The switch applies live to all open document tabs; no restart. In `featureList` style `#TimelineDock` is hidden;
     in `strip` style FEATURES is hidden and the strip is shown (redesign 6.2).

2. **Feature list panel** (new `src/app/FeatureListWidget.*`, objectName `FeatureList`).
   - A model and view over `Document`'s feature order.
   - Placement: the FEATURES section of `SidebarHost`, the vertical splitter [card slot, FEATURES, MODEL, SELECTION]
     inside `#BrowserDock`, with one FEATURES view per document tab in a `QStackedWidget` (redesign 6.2). FEATURES
     sits above MODEL (the restyled `#Browser`).
   - The sidebar can be on the **left or right** (`ui/sidebarSide`, redesign setting 4). The widget must not assume a
     side: drag feedback, the rollback bar, indentation and the value column work the same on both.
   - When the command card is docked into the sidebar, FEATURES collapses to its header plus the live row and
     restores when the command finishes (redesign decision 6, 6.4).
   - **Header** (redesign 5.2): 32 h, `FEATURES <count>` in the `header` text style, search and filter icons,
     collapsible.
   - **Rows** (redesign 5.2, 4.6): 28 h, 18 px indent per level, 16 px icon, name, right value column (`value` style,
     `text.tertiary`). Names are shown as typed (decision 3); all other text is caps through the font, never by
     changing strings. Name column elides in the middle, full name in the tooltip (3.4).
   - **States** (redesign 4.6), every one with a non-colour cue:
     - selected: `accent.selFill` + 3 px `accent.indicator` bar;
     - live or edited: `live.fill` + 3 px `live.base` bar + `live.text`;
     - error: 3 px `status.error` bar + octagon icon; warning: 3 px `status.warning` bar + triangle;
     - suppressed: `text.disabled`, struck through, icon at 40 %, unchecked box, `off` in the value column;
     - rolled back (below the bar): `text.tertiary`, icon at 55 %, no strike.
   - **Rollback bar** (redesign 5.2): 4 h `text.primary` line with a `ROLLBACK` tag; `live.base` line and gold tag on
     hover and drag; ±6 px drag area. Release applies one `SetRollbackCommand`. Keyboard: the bar takes focus and
     moves with ↑/↓.
   - **Insert-at-rollback button**: `+ INSERT FEATURE AT ROLLBACK`, opens the command search scoped to the rollback
     position.
   - **Reorder:** drag and drop uses `Document::canMoveFeature` and `MoveFeatureCommand`. A refused move shows the
     reason (toast or inline tip, redesign 5.2/5.3).
   - **Mouse and keyboard:** click selects (synced with the viewer, MODEL and SELECTION panels), double-click edits,
     F2 renames, ↑/↓ move, **Space toggles suppress** (redesign 10.2 T-keyboard). A visible focus ring
     (`accent.focus`) on every focusable item.
   - **Context menu:** the same actions as the strip, plus the group actions. Menus use the token QSS (this also
     retires NEW-SOLID-1, the unreadable timeline context menu).
   - **Painting:** colours come only from `theme::Tokens` at paint time, both themes, repaint on `themeChanged`; no
     colour literals (colour lint, redesign 2.4).

3. **Groups** (core data, saved in `.fcad`).
   - A group is a named, collapsible range of consecutive features. Groups can nest, as in Onshape.
   - Stored in `Document` as part of the timeline, with undoable commands: create from selection, rename, ungroup,
     collapse.
   - Rules:
     - Groups stay consecutive: moving a feature out of, into or past a group is validated together with the dependency check.
     - Deleting a group's last feature removes the group.
     - The rollback bar can sit inside a group.
   - **Group row** (redesign 5.2): chevron, folder icon, name (as typed), member count, tri-state checkbox 14 × 14 at
     the right edge (`border.control`; checked = `accent.solid` + white check). A suppressed group shows the inline
     tip "GROUP SUPPRESSED / Tick to include its N features again".

4. **Group suppress.**
   - One `MacroCommand` sets suppression for every member, giving a single undo step.
   - The group checkbox is tri-state: all, none or some members suppressed.
   - Later features that depend on suppressed ones turn red; this is existing behaviour.

5. **Shared code.**
   - Move the strip's context-menu and command logic into a small `TimelineActions` helper.
   - Both views use it, so behaviour stays identical.
   - The bottom strip (`strip` style) is restyled from tokens by the redesign (2.4 audit row `TimelineWidget.cpp`)
     and draws group brackets read-only in `accent.indicator`.

6. **Scripting:** `GROUP <name> <feat>..`, `UNGROUP`, `SUPPRESS <group|feature>`, `UNSUPPRESS ...`, documented in `docs/SCRIPTING.md`.

7. **Fixture:** `tests/e2e/bracket-v3.fcad` (13 features, groups, one suppressed group), used by the redesign's
   T-mockup and T-light checks (10.2). It needs the group data, so the core part of this plan lands before redesign
   phase 5.

8. **Tests.**
   - Unit tests: group invariants under move/delete/undo, save/load round-trip, tri-state suppress, rollback inside a group.
   - App tests: `ui/timelineStyle` switches the panel live and round-trips through QSettings; FEATURES works with
     `ui/sidebarSide` left and right; the FEATURES collapse while the card is docked.
   - GUI check by a Sonnet tester with mouse and keyboard only (`tools/uitest/ui.sh`), in dark and light, sidebar
     left and right, both timeline styles; part of the redesign's T-toggles and T-keyboard runs.

## Not in scope

- Per-group colours.
- Configurations (Onshape-style variants).
- Groups in the bottom strip beyond read-only brackets.

## Risks

- **Panel sync:** FEATURES, MODEL and SELECTION must stay in sync for selection and rename; all listen to the same
  document and `SelectionModel` signals.
- **Move rules:** combining the group-boundary and dependency rules must not block legitimate reorders. Cover the edge cases with tests.
- **Sidebar space:** FEATURES shares the `SidebarHost` splitter with the card slot, MODEL and SELECTION. Splitter
  sizes persist in `ui/sidebarSplit` and the per-panel collapse state in `ui/panels` (redesign 6.5); this plan adds
  no keys of its own.
- **Width:** bold mono caps are about 25 % wider (redesign 3.4); the sidebar default is 320 px, minimum 260.

## Estimate and order

About 1,500–2,500 lines including tests (counted separately from the redesign's ≈ 11,000, redesign 10.3).

1. **Core part** (groups data, commands, `.fcad`, scripting, `TimelineActions`, fixture): one worktree, any time
   from redesign phase 0 on, since it touches no UI files the redesign rewrites.
2. **Widget part** (`FeatureListWidget`, restyle, setting wiring): in redesign phase 2 (`ui/sidebar`), after the
   phase 1 shell (`UiSettings`, the dock host API, the Preferences Layout page) has merged and the core part is in.

Workflow: plan check (Opus) → implement in worktrees (Fable or Opus) → GUI test (Sonnet, within the redesign's
verification) → fix and merge.
