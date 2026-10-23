# tools/uitest: GUI harness, mockup compare and colour-vision simulation

Tools for checking the redesigned UI (docs/plans/ui-redesign.md sections 8.3, 4.7 and 10.2). Everything here
is bash plus Python 3 standard library (no pip packages). `ImageMagick` (`import`) and `xdotool` are needed for
the X11 runs, `rsvg-convert` only when a mockup PNG is missing.

| File | What it does |
|---|---|
| `ui.sh` | private rootful Xwayland display per tester, launch, settings, screenshots, compare, CVD |
| `compare.py` | screenshot vs token mockup, region by region (layout boxes, colour, SSIM, HTML report) |
| `cvd.py` | protanopia / deuteranopia / tritanopia simulation of a PNG (Machado 2009, as the `theme_tokens` ctest) |
| `pngio.py` | the stdlib PNG reader and writer both scripts use |
| `selftest.py` | ctest `uitest_tools`: PNG round trip, CVD vs `colormath.py`, compare.py on the mockup itself |
| `card-shots.sh` | offscreen grabs of the command card in every placement and theme |
| `readme-shots.sh` | regenerates `docs/screenshots/fabricad-*.png` (dark, and `-light`) on a private display |
| `tutorial_run.py` | real-input GUI run of a guided tour through the app's read-only TestProbe (opt-in, see below) |

## Safety

The harness never touches the real desktop: each tester gets its own Xwayland server (`:N`, pick an unused
number such as 90-99), and only the FabriCAD window on that display is moved or resized. Settings and data are
private per display. For exact-size checks prefer the offscreen grabs below, which need no display at all.

## ui.sh

```sh
ui.sh prefs 91 ui/theme=light ui/cardPlacement=sidebar    # QSettings for display :91 (before start)
ui.sh start 91 build/bin/fabricad 1920x1080 1.5           # display :91 at 1920x1080, QT_SCALE_FACTOR 1.5
DISPLAY=:91 xdotool mousemove 640 400 click 1             # drive it with real X input only
ui.sh shot 91 /tmp/shot.png                               # screenshot of the whole display
ui.sh geom 91 /tmp/geo.json                               # widget boxes of the running app
ui.sh compare 91 docs/mockups/custom-ui-extrude-v6-tokens-dark.svg /tmp/cmp   # shot + geom + compare.py
ui.sh cvd /tmp/shot.png /tmp/shot-deut.png deut           # CVD simulation (prot | deut | trit | all)
ui.sh reset 91                                            # forget :91's settings and data
ui.sh stop 91
```

* `start <N> [bin|-] [WxH] [scale]`: `-` (or nothing) uses `$UI_BIN` or the main checkout's build, `$UI_ARGS`
  is appended to its command line. A display that is already running keeps its size; `stop` it first to
  change the size. The compositor may tile the rootful Xwayland window, and the display then follows the
  tile (`start` prints a note). For exact-size checks use the offscreen grab below, or `FABRICAD_SCREENSHOT`
  with `FABRICAD_WINDOW_SIZE` (the app grabs its own window, 3D view included; see `readme-shots.sh`).
* Each display gets `XDG_CONFIG_HOME=$UI_LOGDIR/cfg-N` and `XDG_DATA_HOME=$UI_LOGDIR/data-N`
  (`UI_LOGDIR` defaults to `/tmp/fabricad-uitest`), so tests never read or change the user's settings,
  recent files or autosaves.
* Repaints: the app presents without waiting for vsync (swap interval 0; OCCT no longer sets 1 on the window),
  so a hidden or unfocused private display no longer freezes the window or shows input one step late. Older
  notes ask for `vblank_mode=0`; it is not needed any more. `FABRICAD_VSYNC=1` brings vsync back.
* `prefs <N> group/key=value ...` merges into `cfg-N/FabriCAD/FabriCAD.conf` (QSettings INI). Keys are those of
  `UiSettings` (Preferences > Appearance / Layout), e.g. `ui/theme=dark|light|system`, `ui/cardPlacement`,
  `ui/sidebarSide`, `ui/toolbarArea`, `ui/quickToolbar`, `ui/orientation=hexDial|viewCube`,
  `ui/timelineStyle=featureList|strip`, `ui/fontScale`, `ui/glow`, `ui/scanlines`.
* The app is started with `FABRICAD_GEOMETRY_DUMP=$UI_LOGDIR/geo-N.json` and rewrites that file every second;
  `geom` and `compare` read it.

`ui.sh start` sets `FABRICAD_NO_TUTORIAL=1` (fresh per-display settings would show the first-launch tour
welcome card in every screenshot); export `FABRICAD_NO_TUTORIAL=0` to see it.

## tutorial_run.py (guided tours, docs/plans/tutorial.md section 5)

```sh
FABRICAD_GUI=1 tools/uitest/tutorial_run.py --bin build/bin/fabricad \
    --tour resources/tutorials/sample/tutorial.json [--layout default|alt|light] [--out build/tutorial-out]
FABRICAD_GUI=1 ctest --test-dir build -L gui --output-on-failure     # fabricad_tutorial_gui_sample
```

Opt-in: without `FABRICAD_GUI=1` (or `FABRICAD_GL_DISPLAY`) it exits 77 (ctest: skipped). Backends: an
existing private display (`FABRICAD_GL_DISPLAY=:N`), else `Xvfb`, else a private rootful `Xwayland :N` only with
`FABRICAD_GUI_ALLOW_DESKTOP=1`. It starts the app with temporary `XDG_*` folders and `FABRICAD_TEST_PROBE`,
starts the tour by clicking the welcome card (or Help > Tutorials), and for each step moves the real cursor
to the anchor the probe reports, sends the step's `do` input with xdotool, waits for each X event to reach
the app (`inputSeq`) and for the tour to advance, runs the `assert` CHECKs and takes a step PNG. Artefacts:
`<out>/<run>/` (report.json, app.log, probe.txt, step PNGs, checkpoints/, and `run.mp4` only on failure or
with `FABRICAD_UITEST_VIDEO=1`). `--update-checkpoints` copies a passing run's chapter checkpoints into the
tour folder.

## Offscreen grabs (exact size, any scale, no display)

```sh
XDG_CONFIG_HOME=$(mktemp -d) QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=1.5 FABRICAD_WINDOW_SIZE=1920x1080 \
  FABRICAD_SCREENSHOT=/tmp/shot.png FABRICAD_SCREENSHOT_DELAY=4000 FABRICAD_GEOMETRY_DUMP=/tmp/geo.json \
  build/bin/fabricad --script extrude.fcs --stay -q
```

The 3D view is blank offscreen (no GL); all chrome, overlays and the card are real. The PNG is in device
pixels (2880x1620 at scale 1.5); the geometry JSON stays in logical pixels.

## compare.py

```sh
tools/uitest/compare.py shot.png docs/mockups/custom-ui-extrude-v6-tokens-dark.svg \
    --geometry geo.json --report /tmp/cmp [--tol 4] [--only CommandDialog,OkButton] [--json out.json] [--no-ssim]
```

The mockup raster `<mockup>.png` counts as current when `docs/mockups/mockup-png.sha256` lists the SVG's content
hash (mtimes are ignored: a checkout or merge rewrites them). After editing a mockup SVG run `compare.py ...
--refresh-mockup` (re-renders the PNG with `rsvg-convert` and records the hash); the `uitest_tools` ctest fails
while a listed SVG no longer matches its hash.

Regions are the `<rect data-region="ID">` boxes of the token mockups (`AppBar`, `RibbonGroup:CREATE`,
`FeatureList`, `CommandDialog`, `OrientationDial`, `StatusBar` and so on). For each region it prints:

* **layout** (with `--geometry`): the matching widget's box must be within `--tol` px (default 4) of the mockup
  box on the region's gated edges. A miss, or a gated region with no visible widget (`missing`), makes the exit
  status 1. Widgets are found by objectName (or the rect's `data-widget`, e.g. `FeatureList` ->
  `SidebarPanel_features`, as the mockup box includes the panel header), `RibbonGroup:NAME` by the
  `toolbarGroup`/`section` property, `ViewToolColumn` by the `overlay` property, `StatusBar` by class and
  `Viewer` by the viewer widget class.
* **gate** (`data-gate` on the rect, shown in the GATE column): the edges that must match, `ltrb` by default.
  An edge that follows content is left out (`ltb`: the breadcrumb's width follows its crumbs, the document
  tabs their count, the card its body rows; `tb`: the ribbon groups, whose tool sets differ, P5-MOCKUP-10).
  `none` regions are reported as `info` only: the value box sits at the 3D handle, `RollbackBar` and
  `ViewButtons` are painted inside other widgets. `data-why` says why. The v6 boxes were regenerated from the
  app geometry where the app is the agreed layout (the 6 px dock separator before the 3D view, the dial with
  its button row, the card's 16 px padding, decision 2's workspace selector), so a correct 1920x1080 GL grab
  passes (29 of 29) and a layout regression fails.
* **colour**: the dominant colour of the region in the shot and in the mockup PNG, and their CIEDE2000
  difference (`!` above 3). `<circle data-sample="token" cx cy>` points in the SVG are sampled too.
* **SSIM**: mean structural similarity of the luminance (8x8 blocks). Informative only: the mockup has a model
  in the view and invented data, so a low SSIM in the viewer is expected.

`--report DIR` writes `DIR/index.html` with the shot and mockup crop of every region side by side. The shot may
be at any scale; regions are scaled by the shot width / 1920.

## cvd.py

```sh
tools/uitest/cvd.py shot.png shot-deutan.png deut          # prot | deut | trit | normal
tools/uitest/cvd.py shot.png outdir all                    # normal/protan/deutan/tritan PNGs + a 2x2 sheet
tools/uitest/cvd.py shot.png out.png prot --severity 0.6   # anomalous trichromacy
```

The matrices are the ones in `tools/theme/colormath.py` (Machado, Oliveira and Fernandes 2009), applied in
linear RGB, so the gate G screenshots (4.7, step 3) and the numeric `theme_tokens` CVD rows agree. A
1920x1080 shot takes about 3 s per simulation.
