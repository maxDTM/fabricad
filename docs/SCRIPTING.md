# FabriCAD command scripts (`.fcs`)

FabriCAD can be driven without a mouse: everything a user can type into the command line
(`cmd>` box in the status bar, Ctrl+9) can also come from a script. A script is a text file
with one command-line entry per line; `fabricad --script file.fcs` feeds every line through
`CommandLine::submit()` -> `ToolManager::runCommandLine()`, exactly the path a typed line
takes, logs what each line did and how long it took, and exits non-zero when a line fails.

```sh
fabricad --script tests/scripts/chair.fcs --offscreen --log chair.log   # headless
fabricad --script part.fcs --stay                                        # run, then keep the window
fabricad --script part.fcs --keep-going                                  # do not stop at the first error
```

| Option | Meaning |
|---|---|
| `--script <file>` (`-s`) | run the script after start-up and exit with its status |
| `--offscreen` (`--headless`) | `QT_QPA_PLATFORM=offscreen`: no window, no OpenGL view; all modelling works, only screenshots are blank |
| `--log <file>` | write the log to a file (it also goes to stdout unless `--quiet`) |
| `--keep-going` (`-k`) | continue after a failed line (default: stop there) |
| `--stay` | leave the window open after the script (interactive inspection, needs a display) |
| `--step-delay <ms>` | after every command (not CHECK lines) fit the 3D view (home orientation outside sketches) and keep the event loop running for `<ms>` so each step is painted: watch or screen-record a run (default 0; timings in the log exclude the pause) |
| `--quiet` (`-q`) | no stdout copy of the log when `--log` is given |

Exit status: `0` every line ok, `1` a line failed (unknown command, rejected input, an error
message from the command, a failed `CHECK`), `2` the script could not be read. Files given
before `--script` (`fabricad model.fcad --script edit.fcs`) are opened first. Relative paths
in the script (`SAVE out.fcad`, `EXPORTSTL a.stl`) are relative to the working directory.

The script is always driven in a non-interactive window: no modal dialogs appear, questions
(`askYesNo`) take their default answer, file dialogs are skipped (give the path as argument),
and unsaved changes are discarded on close.

## Log format

```
# FabriCAD script chair.fcs, 2026-10-20T02:18:30, 242 lines
   5  ok    NEWCOMPONENT Seat                                      9.5 ms
        info: Created component 'Seat'
  23  ok    SELECT profile:@0,0,430                                3.4 ms
        info: Selected 1: Profile of Sketch1 at (0, 0, 430)
  62  ERROR CHECK body:Seat volume ~ 8565976 0.05%                 2.3 ms
        error: CHECK body:Seat volume ~ 8565976 0.05%: FAILED (got 9.01598e+06)
        line 62 failed: CHECK body:Seat volume ~ 8565976 0.05%: FAILED (got 9.01598e+06)
# 217 commands, 217 ok, 0 failed, 3832.4 ms total
```

Each command line gets one row: script line number, `ok`/`ERROR`, the text as submitted, wall
time (including the event-loop settle after the line) and every status-bar message the command
produced (`info:` / `warn:` / `error:`). The same messages appear in the command history pane
(F2 in the command line) of an interactive session.

## Syntax

* One command per line. Blank lines are skipped. `#`, `//` and `;` start a comment, also after
  a command when preceded by white space (`EXTRUDE 10  # join`). `#` inside a word is not a
  comment (`body:#18`).
* Words are separated by spaces. `"..."` or `'...'` keeps spaces inside one word and the quotes
  are dropped (`RENAME body:#44 "Leg Frame"`, `SELECT body:"LegFrame (2)"`, `SAVE "my chair.fcad"`).
* Command names (aliases) are case-insensitive: `EXTRUDE`, `extrude` and `part.extrude` (the
  tool id) all work. Values are typed as in the UI: `10`, `10 in`, `width/2 + 3 mm`, `x,y`,
  `@dx,dy`, `@len<ang`, `w,h`.
* An empty line typed interactively repeats the last command; in a script blank lines do nothing.

### How a line is routed (the user's command line does exactly the same)

1. If the first word is a **shell verb** (`OK`, `CANCEL`, `SELECT`, `PICK`, `INPUT`, `INFO`,
   `LIST`, `CHECK`, `OPTIONS`, `RENAME`, `ECHO`, `WAIT`, `SCREENSHOT`, `TAB`, `CLOSE`) or a
   **view command** (`FIT`, `FRONT`, `TOP`, `ISO`, `WIRE`...), it runs beside the active
   command without disturbing it.
2. Otherwise, if the first word is a **command alias** (`EXTRUDE`, `FINISH`, `C`...), that
   command starts and the active one is cancelled, as in Fusion when you pick another tool.
   A document parameter that shares its name with an alias is not treated as a command.
3. Otherwise the line is **typed input for the active command**: a value for the primary
   number (`10` to Extrude), `name=value` for one of its properties (`operation=Cut`,
   `extentType=All`, `flip=true`), a coordinate for a sketch tool (`40,30`, `@10,0`,
   `@25<45`), a keyword the prompt asks for (`REVOLUTE`, `FLIP`), or a name
   (`NEWCOMPONENT` prompt).
4. With no active command an unknown word is an error (`Unknown command: X`).

An alias shared by several tools (`FILLET` is the sketch fillet inside a sketch and the solid
fillet outside, `MIRROR`, `POINT`, `MOVE`, `DIMENSION`, `DELETE`) resolves to the one that is
enabled in the current context. `EXPORTSTEP` is the part exporter (bodies); use the tool id
`assembly.export-step` for the XCAF assembly export with components and placements.

## Shell verbs

| Verb | Does |
|---|---|
| `OK` (`ACCEPT`, `ENTER`) | accept the active command (its dialog's OK). Fails when no command is active or OK is disabled because the input is incomplete; the message says why. |
| `CANCEL` (`ESC`) | cancel the active command; with none active clears the selection |
| `SELECT <selector> ...` (`SEL`) | set the selection from selector text (below). `SELECT none` clears. `+sel` or a trailing `+` adds instead of replacing. Items the active command's filter does not accept are reported and the line fails. A selection made *before* a command becomes its preselection (`SELECT body:Seat/edge:@0,-200,460` then `FILLET 6`); made *while* a command runs it goes to the command's active input (what the dialog highlights). |
| `PICK x,y` / `PICK x,y,z [options]` (`CLICK`) | a left click. In an open sketch `x,y` are sketch coordinates and `x,y,z` world coordinates projected onto the sketch plane. Outside a sketch `x,y,z` is a ray-pick against the model: the sketch profile / vertex / edge / face / body at that point, honouring the active command's selection filter. Options: `nosnap` (Ctrl: exact point, no sketch inference), `+` / `shift` (add), `ctrl` (toggle), `double`, `tol=<mm>` (default 0.5), `dir dx,dy,dz` (ray direction; default: the view direction, i.e. the nearest entity). |
| `INPUT <property> [selectors]` (`INTO`) | make a selection input of the active feature command the target (`INPUT splitTool plane:YZ`, `INPUT axis body:Seat/edge:@0,200,430`, `INPUT targets body:LegRight`); `OPTIONS` lists the inputs. Without selectors it only switches the input. |
| `OPTIONS` (`?`) | describe the active command: prompt, active input, validity, every property as `name=value` with its enum options. These names are what `name=value` input accepts. |
| `INFO [selectors]` (`DESCRIBE`) | describe entities (default: the selection; with nothing selected, the document and the active command/tab). Bodies: component, volume, area, counts, tight bounding box, centroid, mass. Faces/edges: type, size, centre, normal/direction, radius. Sketches: plane origin, normal and axis directions, profiles with seed points (sketch and world), geometry and dimensions. Components: placement (translation + XYZ angles), parent, children, bodies, bounding box. |
| `LIST [bodies\|features\|components\|sketches\|joints\|params\|selection\|tabs\|tools [filter]\|all]` (`LS`) | list document objects with their names and ids (what selectors accept); `LIST tools` prints every alias with its tool id; `LIST features` is the timeline with status and marker; `LIST components` is the component tree (indented children). |
| `CHECK ...` (`ASSERT`, `EXPECT`) | assert a fact; a failed check fails the line (see below) |
| `RENAME <selector> <name>` (`REN`) | rename a body, component or feature (undoable) |
| `ECHO text` (`REM`) | write text to the log / history |
| `WAIT ms` (`SLEEP`) | wait, processing events |
| `SCREENSHOT file.png [viewport]` (`SNAP`) | grab the window (or the 3D view); blank in `--offscreen` runs |
| `TAB <n\|title\|next\|prev>` (`GOTO`) | activate a document tab (1-based number or title); `TAB` alone lists them |
| `CLOSE [tab]` (`CLOSETAB`) | close the active tab (or the named one); unsaved changes are discarded in scripts |

## Selectors

Selectors name geometry in text the way a user would point at it. They are used by `SELECT`,
`INPUT`, `INFO`, `CHECK` and `RENAME`. Coordinates are world coordinates (mm), so `top` is +Z
of the assembly even for moved components. A chain is written with `/`.

| Selector | Meaning |
|---|---|
| `body:Seat`, `body:#18`, `Seat` | body by name (case-insensitive) or id; a bare name tries body, component, then feature |
| `body:@x,y,z` | the visible body nearest that point |
| `component:Lever`, `comp:#5`, `component:root` | a component; `component:Lever/bodies` all its bodies |
| `feature:Extrude1`, `sketch:Sketch1`, `joint:Joint1`, `feature:#20` | a timeline feature (by name or id) |
| `plane:XY` `plane:XZ` `plane:YZ`, `plane:OffsetPlane1`, `plane:Sketch1` | origin plane, construction plane or a sketch's plane |
| `axis:X` `axis:Y` `axis:Z`, `axis:Axis1` | origin or construction axis; `origin` = the origin point; `point:Point1` = a construction point |
| `body:B/face:top` | planar face whose outward normal points `top/bottom/left/right/front/back` (= +Z −Z −X +X −Y +Y), the outermost one. `face:front@0,175,700`: the one nearest a point. |
| `body:B/face:#3`, `body:B/face:@x,y,z`, `face:plane@...`, `face:cylinder@...`, `face:largest`, `faces:all`, `face:Extrude1/End` | by index, nearest to a point (optionally of a surface type: plane, cylinder, cone, sphere, torus), largest/smallest, all, or history name |
| `body:B/edge:@x,y,z`, `edge:#7`, `edges:z`, `edges:line`, `edges:circle`, `edges:horizontal`, `edges:z=0`, `edges:top`, `edges:all`, `edge:Extrude1/Side/c1` | edges: nearest to a point, by index, parallel to an axis, by type, horizontal, lying at a coordinate (`z=0` = both ends at z = 0), bounding a face side, all, or history name |
| `body:B/face:top/edges` | the edges bounding the selected face(s) (`.../edges:line` filters them) |
| `body:B/vertex:@x,y,z`, `vertices:all` | vertices |
| `profile:@x,y,z` | the sketch region containing that world point; searches every sketch whose plane contains the point and takes the innermost region. `sketch:Sketch1/profile:@x,y` uses sketch coordinates, `profile:#2`, `profile:largest`, `profiles:all`. |
| `point:@x,y,z`, `curve:@x,y,z`, `sketch:Sketch1/curve:#3`, `sketch:S/curves:all`, `sketch:S/points:all` | sketch points and curves (geometry ids as `INFO sketch:` prints them) |

Singular kinds (`face:`, `edge:`) must match exactly one entity, otherwise the error says how
many matched and suggests the plural form or `@x,y,z`. Without a `body:` prefix a face/edge
selector searches all bodies and must be unique.

## CHECK

`CHECK` makes a script a test. A failed check is an error for the line (and the run, unless
`--keep-going`). Comparisons: `== != < <= > >=` and `~` (approximately: default tolerance
0.1 %, or an explicit absolute `0.01` or relative `0.05%` third value).

```
CHECK bodies == 2                  CHECK features > 3            CHECK components == 3
CHECK sketches == 1                CHECK joints == 0             CHECK params == 2
CHECK selection == 4               CHECK tabs == 2               CHECK dof == 0        (open sketch)
CHECK rollback == 5                CHECK undo == 1 (can undo)
CHECK status ok                    every active feature is Ok (also: warning, error)
CHECK command none                 CHECK command part.extrude (the active command)
CHECK param width == 80            CHECK file out.step exists | missing | size > 1000
CHECK body:Seat volume ~ 8565976 0.05%       CHECK body:Seat exists      CHECK body:Old missing
CHECK body:Seat zmax ~ 840.619 0.01          CHECK body:LegLeft faces == 30
CHECK component:LegLeft xmin ~ -205 0.01     CHECK body:Seat/face:top area == 1200
CHECK feature:Extrude1 status ok             CHECK body:Seat/edges:z=0 count == 8
```

Metrics on a selector: `volume`, `area`, `length`, `mass` (g, needs a material), `faces`,
`edges`, `vertices`, `xmin xmax ymin ymax zmin zmax`, `dx dy dz` (size), `cx cy cz` (centroid),
`radius`, `profiles`, `geometry` (sketch), `bodies`, `x y z` (component: body count, world origin),
`count` (matched entities).
Bounding boxes are tight (no tolerance padding), so `0.01` works.

## Commands by task

Every tool registered in the ribbon has aliases (`LIST tools` prints them, `HELP` / F1 shows
them in the UI). The ones a typical part needs:

### Sketching

```
SKETCH XY                     sketch on an origin plane (XY, XZ, YZ)
SELECT plane:OffsetPlane1     then SKETCH: on a construction plane
SELECT body:Seat/face:front@0,175,700     then SKETCH: on a planar face
INFO sketch:Sketch2           plane origin, normal and the directions of the sketch x/y axes
FINISH                        leave the sketch (FINISHSKETCH, Ctrl+Return)
EDITSKETCH <id>               reopen a sketch
```

Inside a sketch the create tools stay active for repeats (type the next command to switch):

```
LINE  / L        points: 0,0  @80,0  @0,30  @25<45 (polar)  12 (length along the cursor)
                 a point on the first point closes the chain; ESC/CANCEL ends it
RECT / REC       corner then size: -210,-200 then 420,400 (w,h) or @420,400; REC3 (3 points), RECC (centre)
CIRCLE / C       centre then diameter: 40,15 then 10; CIRCLE2 (2 points), CIRCLE3 (3 points)
ARC / A          three points: start, end, then a point on the arc (PICK 80,30 nosnap ...); ARCC (centre), ARCT (tangent)
POLYGON / POL, SLOT, POINT / PO
SFILLET 20       sketch fillet: then PICK a point on each line, on the part to keep (not inside the corner zone)
TRIM / TR, EXTEND / EX, BREAK / BR, OFFSET / O, MIRROR / MI, PROJECT / PROJ, CONSTRUCTION / X
DIMENSION / D    PICK the entity (or two), PICK where the text goes, then type the value or
                 an expression (width/2); OK keeps the measured value
DIMEDIT <id> <value>
```

Typed `x,y` are sketch coordinates (see `INFO sketch:` for the axes); `PICK x,y,z nosnap`
places points by world coordinates, which is the easy way on faces and offset planes.
Relative (`@dx,dy`) and polar input take numbers; plain values and dimensions take
expressions with parameters.

### Solids

```
SELECT profile:@20,15,0 profile:@70,35,0      then
EXTRUDE 30                    value = distance; then name=value lines and OK:
  operation=Join|Cut|Intersect|New Body   direction=One Side|Two Sides|Symmetric
  extentType=Distance|To Object|All       symmetricMeasure=Whole Length   flip=true
  distance2=... taperAngle=...            INPUT targets body:X (bodies to cut/join)
  The operation defaults to Join when the active component (or a child of it) has a body, else to
  New Body; without `targets` only bodies of the active component's tree take part in a boolean
  (Fusion behaviour), so an extrude in a new component never merges into another part it touches.
REVOLVE / REV, SWEEP, LOFT, SHELL
HOLE             PICK/INPUT face body:X/face:@x,y,z then PICK x,y,z nosnap, diameter=10, extentType=All;
                 cuts the body the hole is placed on (INPUT targets body:Y ... to cut others too)
FILLET 6         with edges/faces selected (tangentChain=true by default)
CHAMFER 3        chamferType=Equal Distance|Two Distances|Distance and Angle, distance2=, angle=
SPLIT            SELECT body:X first, then INPUT splitTool plane:YZ, OK
MOVE / COPY      bodies: moveType=Translate dx= dy= dz= | moveType=Rotate INPUT axis <edge/axis> angle= |
                 Point to Point; createCopy=true for a copy
COMBINE, OFFSETFACE, DRAFT, PRESSPULL / PP, RECTPATTERN, CIRCPATTERN, MIRROR
OFFSETPLANE 430  with a plane selected (flip=true for the other side); PLANEANGLE, MIDPLANE, PLANE3, AXIS, POINT
EDITFEATURE <id> opens the feature's command in edit mode (same inputs, OK pushes one undo step)
SET width=80 mm  create/change a parameter (undoable; the model follows; does not interrupt a command); PARAMETERS opens the dialog
                 SET width=  removes it (refused while features or parameters use it); invalid names are refused
UNITS in [deg]   document units (mm cm m in ft, deg rad; undoable); plain-number parameters used as lengths keep
                 their size ("10" becomes "10 mm"). UNITS alone opens Preferences > Document; PREFS opens Preferences
ZOOM E | ZOOM 2 | ZOOM 0.5 | ZOOM W   fit, zoom about the centre by a factor, drag-to-zoom mode (view command)
APPEARANCE #ff8800   colour of the selected/preselected bodies or components (A without a colour opens the picker)
MEASURE          with one or two entities selected
UNDO / U, REDO, DELETE (selection), HIDE / SHOW, SHOWALL, COMPUTE
```

A feature command needs its inputs complete before `OK` works (`OPTIONS` shows `valid:`).
Extrude's direction follows the sketch normal; use `flip=true` when the material is on the
other side (an `All` cut into nothing leaves the body unchanged, `CHECK` for it).

### Components, joints, assembly

```
NEWCOMPONENT Seat    then OK (activates it: new bodies go into it)
SELECT none / ACTIVATE          activate the root again (ACTIVATE with a component selected activates it)
RENAME component:Seat/bodies Seat    name the body of a component
SELECT body:LegLeft body:LegRight / CFB    one component per selected body (named after the bodies)
SELECT component:Seat / GROUND  (idempotent; GROUND off | UNGROUND frees, GROUND toggle toggles)
SELECT component:A component:B / ASBUILTJOINT / OK     rigid as-built joint (type: REVOLUTE etc. as typed input);
                 faces/edges may be selected instead of the components (they name the mating geometry)
JOINT REVOLUTE   then SELECT/PICK a snap on each component (face, edge, vertex), FLIP, OK
SELECT component:LegLeft / MOVECOMPONENT / -15,0,0 / OK     typed x,y,z translates, @30 rotates about Z;
                 x= y= z= rx= ry= rz= set one dialog field; pivot=0,200,430 is the world point the
                 angles turn about (rx=-5 then rotates -5 deg about +X through it; the dialog has Pivot fields)
DRIVEJOINT Joint1 45, RIGIDGROUP, INTERFERENCE, INSERTDESIGN file.fcad, COPYCOMPONENT / PASTE
SELECT feature:Joint1 / DELETE
SELECT component:LegFrame / DELETECOMPONENT    bodies and child components move to its parent (children keep
                 their world placement), joints on it are removed; LIST components shows the tree
```

### Drawings and export

```
DRAWING / OK           create a drawing of the design: it opens in its own tab (TAB 1 goes back)
BASEVIEW front 1:1 100,150 / OK      orientation, scale, sheet position (mm); hidden, tangent
PROJECTEDVIEW / OK, SECTIONVIEW, DETAILVIEW, DIMENSION, TEXT, LEADER, PARTSLIST, BALLOON
EXPORTPDF a.pdf, EXPORTSVG a.svg, EXPORTDXF a.dxf, EXPORTCSV a.csv
EXPORTSTEP a.step                    selected bodies (or all) in their component placement
assembly.export-step a.step          XCAF assembly: components, occurrences, names, colours
EXPORTSTL a.stl [deflection=0.05] [angle=20|0.2rad] [ascii]      selected bodies or all (world placement);
                 typed exports always start from the built-in 0.05 mm / 20 deg / binary; every export
                 meshes afresh, so earlier exports or the display mesh never leak into the file.
                 In the GUI, EXPORTSTL without arguments opens the dialog (file, deflections, format)
IMPORT part.step / OK  (or IMPORT, filePath=part.step, OK): insert a STEP/STL as bodies (string
                 properties are typed as name=value like the others); IMPORTFILE part.step inserts at once
SAVE a.fcad / SAVEAS a.fcad, OPEN a.fcad (new tab), NEW (new tab), CLOSE
```

## Tabs

Every design and every drawing is a tab in the application bar, with its own document, undo
stack, selection, active command, camera, browser and timeline. `NEW`/`OPEN` make a tab,
`DRAWING` opens the drawing tab linked to the design's document (one document, two tabs: the
unsaved-changes prompt comes with the last of them), `TAB n` switches, `CLOSE` closes.
Switching tabs cancels the command of the tab you leave and finishes its open sketch, like
leaving a Fusion document.

## Writing a reliable script

* `LIST features`, `LIST bodies` and `INFO sketch:...` tell you the names the tools generate
  (`Sketch1`, `OffsetPlane1`, `Extrude1`, `Body1`; bodies renamed with `RENAME`, components
  created by `NEWCOMPONENT <name>`). Prefer world points (`profile:@x,y,z`, `edge:@x,y,z`,
  `body:@x,y,z`) over indices, which change with upstream edits.
* After each feature put a `CHECK status ok` and a `CHECK ... volume` to catch a wrong
  direction or an empty boolean early; `--keep-going` plus the log shows everything at once.
* Picks in a sketch fillet or trim decide which part to keep: pick on the part that stays.
* `OK` is only accepted when the command is valid; `OPTIONS` says what is missing.
* `tests/scripts/chair.fcs` (the chair benchmark of `docs/comparison/CHAIR_SPEC.md`: sketches
  on offset planes and faces, join/cut/new-body extrudes, fillet chains, split body, components,
  as-built joint, chamfer, hole, move body/component, STL/STEP export), `tests/chair/chair.fcs`
  (the same model as a test user typed it, with CHECKs of every reference value, a .fcad round
  trip and a STEP read-back; `ctest -R fabricad_chair_spec`) and
  `tests/scripts/bracket.fcs` (lines, arc, circle, dimension, parameters, drawing views, PDF/SVG)
  are complete examples; `ctest -R fabricad_script` runs them.
