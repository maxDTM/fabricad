# Roadmap: execution order of the open plans

Written 2026-10-22. Sources: `ui-redesign.md` (newest, wins on UI), `feature-list-timeline.md`, `tutorial.md`,
`docs/uiux/retest/*.md`, `docs/tests/*/retest/*.md`, `docs/comparison/COMPARISON.md` §4. Each step runs as one or
more workflows (worktrees, `-j2` builds, Sonnet GUI testers through `tools/uitest/ui.sh`).

## Order and why

| Step | Content | Why here |
|---|---|---|
| **1** | **UI redesign, phases 0–5**, with the feature list in phase 2 | It rewrites the files most open issues live in (`MainWindow`, `Ribbon`, `CommandDialog`, `ViewerWidget`, `TimelineWidget`, `Theme`, the QSS). Fixing those issues first would be thrown away. |
| **2** | Remaining open issues + performance work | Fixed once, on the new UI. Several are made moot by step 1 (below). |
| **3** | Drawing test round on the new UI | The redesign changes the drawing canvas surround, selection and dialogs; the drawing workspace has had only one tester pass. |
| **4** | Interactive tutorial | It anchors to the final UI and needs the step 2 fixes (its §6 list). Open questions answered (tutorial.md §8). |

### Step 1: redesign (≈ 11 000 lines + feature list 1 500–2 500; 14–18 sessions)

| Phase | Content | Gate to continue |
|---|---|---|
| 0 | colour audit + lint, `tokens.json` + generator + `fabricad_theme`, `ThemeManager`, fonts/caps, **v6 mockup regenerated** (`docs/mockups/custom-ui-extrude-v6-tokens-{dark,light}`, `data-region` ids). Built and merged 2026-10-22 (0.1–0.4, plus themed icons from phase 5) | **Gate 1a: user signs off v6**; `theme_tokens` ctest green. Gates everything else. |
| 0 ∥ | feature-list **core part** (groups, commands, `.fcad`, scripting, `TimelineActions`, fixture `tests/e2e/bracket-v3.fcad`) | unit tests green; must merge before phase 2's widget work |
| 1 | shell: title bar, angled tabs, workspace selector, `MainToolbar` (+ `toolId`/`toolbarTab`/`toolbarGroup` properties), MORE ▾, status bar, `UiSettings`, Preferences Appearance/Layout | all existing tests green; toolbar fits at 1280 px; theme switch < 150 ms |
| 2 | sidebar: `SidebarHost`, FEATURES (`FeatureListWidget`, default), MODEL restyle, SELECTION, `ui/timelineStyle`, `ui/sidebarSide` | feature-list plan tests + toggle tests |
| 3 | command card, step indicator, segmented control, placements, `OverlayLayout` | extrude in every placement; tutorial anchors resolve |
| 4 | `ViewerPalette`, previews, halo, manipulators, hex dial + ViewCube toggle, overlays, `cameraChanged` | layout boxes match v6; no viewer colour literals |
| 5 | light polish (themed icons already merged in phase 0), drawing canvas, harness (`ui.sh` start size/scale, per-display `XDG_CONFIG_HOME`, `prefs`, `cvd`; `compare.py`, `cvd.py`), **gate G** (gold vs warning/error), Sonnet passes T-mockup/T-toggles/T-keyboard/T-cvd/T-light | **Gate 1b: gate G (redesign 4.7) passes** (or teal fallback + re-pass), tester issues fixed |

Phases 2, 3 and 4 run in parallel worktrees once phase 1 has merged (`ui-redesign.md` 10.1).

### Step 2: open issues and performance

Open issues from the retest verdict tables and the new findings in those reports. **Moot** = removed by step 1;
*re-check* = step 1 changes the code, so retest before fixing.

| Area | Ids | Notes |
|---|---|---|
| Made moot by the redesign | UX-05, NEW-SKETCH-2, NEW-ASSEMBLY-DRAWING-5 (ViewCube contrast: token ViewCube, the default again since 2026-10-23; hex dial option); NEW-SOLID-3, NEW-SKETCH-3, NEW-VIEWER-1 (palette over the cube: `OverlayLayout`); UX-17 (dialog placement: command card); NEW-SOLID-1 (unreadable timeline context menu: token QSS); general DOC-11 / GB-12 (tab drag repaint: new tab bar); NEW-SOLID-7, NEW-VIEWER-4 (Hyprland tiling: harness change) | close after the step 1 tester passes confirm |
| Partly covered by the redesign (*re-check*) | UX-39 (SELECT menu check marks; window/freeform/paint modes remain), UX-43 rest (drawing selection colour), NEW-APP-TOOLS-2, NEW-ADVANCED-PART-2 (card reserves the message row), NEW-SKETCH-4 (value box follows the handle) | |
| Viewer / picking | UX-02 rest + NEW-ASSEMBLY-DRAWING-1 (**high**: origin planes win picks; fixed 2026-10-23 in ui2/fix: planes only when nothing else is under the cursor), NEW-PART-CORE-3, UX-10 rest + NEW-VIEWER-3 (oversized, doubled planes), UX-40 (origin eye toggles), NEW-SOLID-6 (window select ignores the filter), NEW-VIEWER-2, NEW-ASSEMBLY-DRAWING-2, NEW-ASSEMBLY-DRAWING-4, NEW-SOLID-4 (Look At on sketch entry) | |
| Sketch | NEW-SKETCH-1 (medium: value box swallows the click), SK-02 rest (stale draw after a constraint), SK-09 (unverified), SK-11 rest, SK-12 (no profile shading after Finish), SK-13 rest, UX-26 rest, NEW-SKETCH-5, NEW-ADVANCED-SKETCH-1 (cmd box keeps focus), NEW-ADVANCED-SKETCH-2, NEW-ADVANCED-SKETCH-3 | |
| Part | PART-08, PART-12 (verify only), PART-10 rest (overlap still allows OK; black bodies), NEW-PART-CORE-1, NEW-PART-CORE-2, NEW-ADVANCED-PART-1 (wheel on combos), NEW-ADVANCED-PART-3 (slow after a modelled thread), NEW-ADVANCED-PART-4, NEW-APP-TOOLS-1, NEW-ASSEMBLY-DRAWING-3 (Box defaults to Join) | |
| Assembly | UX-35 rest (Ground adds no timeline entry), UX-36 rest, ASM-02 rest, ASM-07 (linear clamp), ASM-10 rest, NEW-ASM-CORE-1, NEW-ASM-CORE-2 | |
| Document / app | functional DOC-11 (Open Recent labels collide), DOC-10 rest, DOC-12 rest, APP-10 rest, NEW-DOC-CORE-1, NEW-DOC-CORE-2, NEW-SOLID-2 (log popup lingers), NEW-SOLID-5, NEW-APP-TOOLS-3 | |
| Drawing | NEW-ASSEMBLY-DRAWING-6, NEW-GENERAL-BUGS-1, NEW-GENERAL-BUGS-2 | fold into step 3 if easier |
| Performance (COMPARISON §4, rec. 1–2) | (a) script mode skips or defers the live preview and computes once at OK; (b) partial recompute on `DELETECOMPONENT` and joint `DELETE`; (c) the 2–3 ms per-command settle floor; (d) a true headless runner without the MainWindow | target `chair_min` 2.6 s → ≈ 1.3 s; re-run `docs/comparison/tools/bench.py` |

Tutorial-critical subset (tutorial.md §6): NEW-SKETCH-1, NEW-ASSEMBLY-DRAWING-1, NEW-PART-CORE-3, NEW-SOLID-4,
SK-02, NEW-ADVANCED-PART-1, NEW-ADVANCED-SKETCH-1, NEW-SOLID-2. Do these first.

**Gate 2**: per-fix-group retest reports with no open high/medium item; perf bench recorded.

Dependencies: (d) touches `main.cpp` and the app start, which phase 0 changes, so it waits for step 1 (or at least
phase 0). (a)–(c) are core/script work and may run in parallel with step 1 phases 1–4 if a worktree is free; they
must not touch UI files.

### Step 3: drawing test round on the new UI

Sonnet testers, mouse and keyboard only, dark and light (the sheet stays white with black ink), card placements,
sidebar sheet browser; round1 → triage → fixes → retest, as in `docs/tests/*`. **Gate 3**: no open high/medium
drawing issue.

### Step 4: interactive tutorial (`tutorial.md`)

Open questions answered (tutorial.md §8). Needs steps 1–3 done (anchors on the final UI,
§6 fixes in). Starts with its phase 0 spike on the new UI. ≈ 14.5–19 agent-days. Gate: `fabricad_tutorial_bracket_default`
10 runs in a row in the default preset, 3 each in the `alt` and `light` presets; testers report no blocker.

## Decisions (confirmed by the user, 2026-10-22)

1. **Card docked in the sidebar vs feature picking:** FEATURES expands automatically while a command's feature-selection input is active, then collapses again.
2. **Workspace selector shortcut:** Alt+1 / Alt+2 / Alt+3 for Part / Assembly / Drawing. Ctrl+1..7 stay the standard views.
3. **Tutorial bubble prose:** shown as typed (mixed case). Bubble titles and buttons follow the caps rule.
4. **Tutorial §8 questions 1–12:** answered; see tutorial.md §8.
5. **COMPARISON recommendations 3–9:** added to roadmap step 2 (open issues and performance). STEP assembly-structure import is already in progress on the back-end track.
