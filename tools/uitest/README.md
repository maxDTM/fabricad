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
| `card-shots.sh` | offscreen grabs of the command card in every placement and theme |
| `readme-shots.sh` | regenerates `docs/screenshots/fabricad-*.png` (dark, and `-light`) on a private display |

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

* `start <N> [bin|-] [WxH] [scale]`: `-` (or nothing) uses `$UI_BIN` or the main checkout's build. A display
  that is already running keeps its size; `stop` it first to change the size.
* Each display gets `XDG_CONFIG_HOME=$UI_LOGDIR/cfg-N` and `XDG_DATA_HOME=$UI_LOGDIR/data-N`
  (`UI_LOGDIR` defaults to `/tmp/fabricad-uitest`), so tests never read or change the user's settings,
  recent files or autosaves.
* `prefs <N> group/key=value ...` merges into `cfg-N/FabriCAD/FabriCAD.conf` (QSettings INI). Keys are those of
  `UiSettings` (Preferences > Appearance / Layout), e.g. `ui/theme=dark|light|system`, `ui/cardPlacement`,
  `ui/sidebarSide`, `ui/toolbarArea`, `ui/quickToolbar`, `ui/orientation=hexDial|viewCube`,
  `ui/timelineStyle=featureList|strip`, `ui/fontScale`, `ui/glow`, `ui/scanlines`.
* The app is started with `FABRICAD_GEOMETRY_DUMP=$UI_LOGDIR/geo-N.json` and rewrites that file every second;
  `geom` and `compare` read it.

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

Regions are the `<rect data-region="ID">` boxes of the token mockups (`AppBar`, `RibbonGroup:CREATE`,
`FeatureList`, `CommandDialog`, `OrientationDial`, `StatusBar` and so on). For each region it prints:

* **layout** (with `--geometry`): the matching widget's box must be within `--tol` px (default 4) of the mockup
  box on every edge. A miss makes the exit status 1. `missing` means no visible widget matched (a region such as
  `RollbackBar` or `ViewButtons` is painted inside another widget and has no box of its own). Widgets are found
  by objectName, `RibbonGroup:NAME` by the `toolbarGroup`/`section` property, `ViewToolColumn` by the `overlay`
  property, `StatusBar` by class and `Viewer` by the viewer widget class.
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
