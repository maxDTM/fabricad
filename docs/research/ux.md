# FabriCAD UX Specification (research: Fusion 360, SolidWorks, AutoCAD)

Status: research spec, implementable. Target: Qt6 Widgets + OCCT AIS/V3d.
Primary model: **Autodesk Fusion** (single-window, workspace switcher, toolbar tabs with
dropdown groups, browser tree, bottom timeline, floating command dialogs).
Secondary: SolidWorks (FeatureManager tree, confirmation corner, mouse gestures, S-key toolbox)
and AutoCAD (command line, dynamic input, object snaps, Enter = repeat).

All names below are generic UI vocabulary; FabriCAD must use its own icons/artwork and must not
use Autodesk/Dassault trademarks or branding in the UI. "Fusion-like" describes behaviour only.

Sources: Autodesk Fusion help (help.autodesk.com/view/fusion360), Fusion keyboard shortcut
reference, SolidWorks Help (help.solidworks.com) "Mouse Gestures", "Shortcut Bar", "Confirmation
Corner", AutoCAD help "Command Line", "Dynamic Input", "Object Snaps". Details reflect the
2024-2026 UI generations; where products differ, the FabriCAD choice is stated explicitly.

---

## 1. Main window layout

```
+--------------------------------------------------------------------------------------------+
| APPLICATION BAR  [=] [File v] [Save] [Undo][Redo]   |  Document tab: Part1 *  x | +  |  [?] [user] |
+--------------------------------------------------------------------------------------------+
| [DESIGN v] |  SOLID | SURFACE | SHEET METAL | MESH | UTILITIES |   (SKETCH contextual tab)   |
|  workspace | +--------+--------+---------+--------+---------+---------+---------+--------+  |
|  switcher  | | CREATE | MODIFY |ASSEMBLE |CONSTRUCT| INSPECT | INSERT  | SELECT  |        |  |
|            | | [ico][ico][ico] v  each group: 2-4 promoted icons + dropdown arrow  ...    |  |
+--------------------------------------------------------------------------------------------+
| BROWSER (dock, left)   |                                                   +-----------+   |
| v (o) Part1 v1         |                                                   | ViewCube  |   |
|   > Document Settings  |               3D GRAPHICS VIEW                    |  [FRONT]  |   |
|   > Named Views        |                                                   +-----------+   |
|   > Origin        (o)  |         (grid on XY / ground plane)       +---------------------+ |
|   > Bodies        (o)  |                                           | COMMAND DIALOG      | |
|   > Sketches      (o)  |                                           | EXTRUDE             | |
|   > Construction  (o)  |                                           | Profile   [1 sel] x | |
|   > Joints             |                                           | Direction [One side]| |
|   > Component1:1  (o)  |                                           | Distance  [10 mm]   | |
|                        |                                           | Operation [Join v]  | |
| COMMENTS (collapsed)   |                                           |   [ OK ]  [Cancel]  | |
|                        |                                           +---------------------+ |
+------------------------+-------------------------------------------------------------------+
| |<< < > >>|  [sk][ex][fi][ch][ho]|v rollback marker  [filter]  [gear]   NAV BAR: [orbit][pan]  |
| TIMELINE (feature history, left->right)                [zoom][fit][display v][grid v][views v]|
+--------------------------------------------------------------------------------------------+
| STATUS BAR: prompt text ("Select a profile")   | selection count | units mm | [cmd> ______ ] |
+--------------------------------------------------------------------------------------------+
```

FabriCAD widget mapping:

| Region | Qt implementation | Notes |
|---|---|---|
| Application bar | custom `QWidget` row above toolbar (not QMenuBar) | File menu (New, Open, Save, Save As, Export, Import, Recent), quick-access Save/Undo/Redo, document tabs (`QTabBar`, closable, `*` when dirty), help. A classic `QMenuBar` may be toggled on in prefs. |
| Workspace switcher | `QToolButton` with menu, at left of toolbar | DESIGN, DRAWING (later: RENDER/SIMULATION not planned). Switching swaps the toolbar tab set and the central view. |
| Toolbar tabs | `QTabBar` + stacked pages of group buttons | Tabs per workspace. A contextual tab (SKETCH) appears and auto-activates while editing a sketch, coloured accent. |
| Groups | Each group = `QToolButton` (label with down-arrow, `InstantPopup`) above row of promoted large icons | Menu items: icon, name, shortcut text right-aligned, flyout submenus, "Pin to toolbar" via right-click. |
| Browser | `QDockWidget` + `QTreeView`, left | Eye toggles visibility; light bulb/eye per folder; right-click context menus. |
| Viewport | `QWidget` hosting OCCT `V3d_View` (native window) | ViewCube overlay (`AIS_ViewCube`), top-right. |
| Navigation bar | small floating bar bottom-centre of viewport (overlay `QWidget`) | Orbit (free/constrained), Look At, Pan, Zoom, Fit, Display settings (visual style, environment, effects), Grid & snaps, Viewports. |
| Command dialog | floating `QFrame` tool window docked to viewport right edge, movable | Title, inputs, OK/Cancel. Non-modal; viewport stays interactive. |
| Timeline | `QWidget` bottom strip, horizontal scroll | Playback controls, feature icons, rollback marker, settings gear. |
| Status/command line | `QStatusBar` + optional `QLineEdit` "command box" | AutoCAD-style entry, see 9. |

Default sizes: browser 260 px wide; timeline 40 px tall (icons 24 px); toolbar 64 px (icons 32 px
promoted, 16 px in menus); command dialog 300 px wide.

## 2. Workspaces and toolbar contents

### 2.1 DESIGN workspace tabs

**SOLID tab** (default):

| Group | Promoted icons | Full dropdown (shortcut) |
|---|---|---|
| CREATE | New Component, Create Sketch, Extrude, Revolve, Hole | New Component, Create Sketch, Extrude (E), Revolve, Sweep, Loft, Rib, Web, Emboss, Hole (H), Thread, Box, Cylinder, Sphere, Torus, Coil, Pipe, Pattern > (Rectangular Pattern, Circular Pattern, Pattern on Path), Mirror, Thicken, Boundary Fill, Create Base Feature |
| MODIFY | Press Pull, Fillet, Chamfer, Shell, Combine | Press Pull (Q), Fillet (F), Chamfer, Shell, Draft, Scale, Combine, Replace Face, Split Face, Split Body, Silhouette Split, Move/Copy (M), Align, Delete (Del), Physical Material, Appearance (A), Manage Materials, Change Parameters, Compute All (Ctrl+B) |
| ASSEMBLE | New Component, Joint, As-built Joint | New Component, Joint (J), As-built Joint (Shift+J), Joint Origin, Rigid Group, Drive Joints, Motion Link, Enable All Contact Sets, Motion Study |
| CONSTRUCT | Offset Plane, Plane at Angle, Midplane, Axis | Offset Plane, Plane at Angle, Tangent Plane, Midplane, Plane Through Two Edges, Plane Through Three Points, Plane Tangent to Face at Point, Plane Along Path; Axis Through Cylinder/Cone/Torus, Axis Perpendicular at Point, Axis Through Two Planes, Axis Through Two Points, Axis Through Edge, Axis Perpendicular to Face at Point; Point at Vertex, Point Through Two Edges, Point Through Three Planes, Point at Center of Circle/Sphere/Torus, Point at Edge and Plane, Point Along Path |
| INSPECT | Measure, Interference, Section Analysis | Measure (I), Interference, Curvature Comb, Zebra, Draft Analysis, Section Analysis, Center of Mass, Display Component Colors (Shift+N) |
| INSERT | Insert Derive, Decal, Canvas | Insert Mesh, Insert SVG, Insert DXF, Canvas, Decal, - FabriCAD: Insert STEP/IGES/STL, Insert DXF, Insert SVG, Canvas |
| SELECT | Select | Select, Window Selection, Freeform Selection, Paint Selection; Selection Priority (Select Body / Face / Edge / Component priority); Selection Filters (checkbox list: Bodies, Faces, Edges, Vertices, Sketches, Profiles, Sketch Curves, Sketch Points, Construction, Joints, Components...) |

**SURFACE tab**: CREATE (Extrude, Revolve, Sweep, Loft, Patch, Ruled, Offset, Extend), MODIFY
(Trim, Untrim, Stitch, Unstitch, Thicken, Reverse Normal), ASSEMBLE, CONSTRUCT, INSPECT, INSERT, SELECT.
**SHEET METAL tab**: CREATE (Flange, Bend, Unfold, Refold, Create Flat Pattern), MODIFY, ... (later phase).
**UTILITIES tab** (FabriCAD naming "TOOLS"): Parameters (Change Parameters), Scripts, Preferences, Export.

FabriCAD MVP tabs: SOLID, SURFACE (subset), TOOLS. Sheet metal / mesh deferred (show greyed or hide).

### 2.2 SKETCH contextual tab

Appears on Create Sketch / Edit Sketch; other tabs stay accessible but the sketch tab is active.

| Group | Items (shortcut) |
|---|---|
| CREATE | Line (L), Rectangle > 2-Point Rectangle (R), 3-Point Rectangle, Center Rectangle; Circle > Center Diameter Circle (C), 2-Point, 3-Point, 2-Tangent, 3-Tangent; Arc > 3-Point Arc, Center Point Arc, Tangent Arc; Polygon > Circumscribed, Inscribed, Edge; Ellipse; Slot > Center to Center, Overall, Center Point, 3-Point Arc, Center Point Arc; Spline > Fit Point, Control Point; Conic Curve; Point; Text; Mirror; Circular Pattern; Rectangular Pattern; Project/Include > Project (P), Intersect, Include 3D Geometry, Project to Surface; Sketch Dimension (D) |
| MODIFY | Fillet, Chamfer (sketch), Trim (T), Extend, Break, Sketch Scale, Offset (O), Move/Copy (M), Change Parameters |
| CONSTRAINTS | Horizontal/Vertical, Coincident, Tangent, Equal, Parallel, Perpendicular, Fix/Unfix, Midpoint, Concentric, Collinear, Symmetry, Curvature (G2), Coincident-to-curve. Each a one-click icon in the toolbar group (not hidden in a dropdown). |
| INSPECT | Measure, ... |
| INSERT / SELECT | as Design |
| FINISH SKETCH | large green check button "Finish Sketch" at the right end of the tab |

### 2.3 DRAWING workspace tabs
See section 7.

---

## 3. Browser tree

Root node = active document / root component, with version tag ("Part1 v3"). Radio button at
left of each component = "activate" (only active component receives new features).

```
v (o) Part1 v3                  <- root component, radio = active
  > [gear] Document Settings     Units (mm/in/...; per-doc), Design History (capture on/off)
  > [cam]  Named Views           TOP, FRONT, RIGHT, HOME + user saved views (dbl-click = go)
  > [eye]  Origin                O (point), X, Y, Z axes, XY, XZ, YZ planes (hidden by default)
  > [eye]  Analysis              section analyses etc. (only when present)
  > [eye]  Joints                joints between child components (assembly)
  > [eye]  Bodies                Body1, Body2 ... (eye, rename F2, appearance)
  > [eye]  Canvases
  > [eye]  Sketches              Sketch1 ... (icon shows fully-constrained state)
  > [eye]  Construction          Plane1, Axis1, Point1
  > (o) [eye] Bracket:1          child component occurrence, same sub-folders
  > (o) [eye] Bolt:1 [pin]       grounded shows pin icon
```

Behaviour:

| Action | Result |
|---|---|
| Hover tree item | Pre-highlight the entity in viewport (blue) |
| Click | Select (also selects in viewport) |
| Eye icon | Toggle visibility (folder eye hides contents) |
| Double-click name / F2 | Rename |
| Double-click sketch | Edit sketch |
| Right-click | Context: Show/Hide (V), Isolate, Rename, Delete, Edit Sketch, Activate Component, Ground/Unground, Create Components from Bodies, Properties, Appearance, Find in Timeline, Export... |
| Drag body onto component | Move body into component |
| Error state | Red/yellow icon overlay matching timeline |

Sketch icon states: blue pencil-plane = under-defined; black/locked = fully constrained; red/warn = errors.

---

## 4. Timeline (parametric history)

```
 |<< <  >  >>|  [S1][Ex1][S2][Ex2][Fi1][Ch1][Pl1][Ho1] | [S3][Ex3]      [funnel][gear]
                                                         ^ rollback marker (draggable)
                                         items after marker are greyed/suppressed-from-view
```

| Feature | Spec |
|---|---|
| Items | One icon per feature/sketch/construction/joint/component creation, in creation order. Tooltip = feature name; Hover highlights resulting geometry. Icons coloured by type. |
| Rollback marker | Vertical handle after last item. Drag left to roll back (model recomputes to that point; later items greyed); new features inserted at marker. Playback buttons: go to start, step back, step forward, go to end. |
| Edit | Double-click item -> reopen its command dialog (sketch -> edit sketch with camera alignment). Right-click -> Edit Feature, Edit Profile Sketch, Roll History Marker Here, Suppress/Unsuppress, Delete, Rename, Group selected (Create Group), Find in Browser. |
| Reorder | Drag icons left/right; invalid move (before dependency) shows red insertion line and is rejected. |
| Errors | Warning = yellow background, error = red background; tooltip text; errors propagate downstream as yellow. |
| Groups | Collapsible group icon containing several items. |
| Suppressed | Strikethrough/greyed icon. |
| Capture Design History off | Timeline hidden; direct modeling mode (later). |

Core-model requirement: features stored as an ordered list with `rollbackIndex`; recompute from
first dirty item; each feature exposes `edit()` (re-open dialog prefilled), `suppressed`, `name`,
`status {ok, warning, error, message}`. Testable without GL.

---

## 5. Viewport, ViewCube, navigation bar

### 5.1 ViewCube (top-right)
- Faces: TOP, FRONT, RIGHT, BACK, LEFT, BOTTOM; 12 edges and 8 corners clickable (26 views).
- Hover highlights face/edge/corner (blue). Click animates camera (~250 ms) to that view.
- Drag cube = orbit. Home icon (house) above-left of cube = Home view (iso). Curved arrows
  appear in face-on views to rotate 90 deg; small arrows to step to adjacent faces.
- Right-click cube: Go Home, Orthographic / Perspective / Perspective with Ortho Faces,
  Set current view as Home, Reset Home, Set current view as Front, Set current view as Top.
- Fusion default: Y-up ("Default modeling orientation" pref; Z-up selectable). **FabriCAD
  default: Z-up** (matches OCCT, SolidWorks is Y-up; AutoCAD Z-up). Make it a preference.
- OCCT: `AIS_ViewCube` covers geometry, highlight, animation; customise labels and colours.

### 5.2 Navigation bar (bottom centre)
`[Orbit v (Free/Constrained)] [Look At] [Pan] [Zoom v (Zoom/Zoom Window)] [Fit] | [Display Settings v] [Grid and Snaps v] [Viewports v]`
- Display Settings: Visual Style (Shaded, Shaded with Hidden Edges, Shaded with Visible Edges
  Only (default), Wireframe, Wireframe with Hidden Edges, Wireframe Visible Edges Only),
  Environment, Effects (shadows, ambient occlusion), Object Visibility (origin, sketches,
  construction, joints...), Camera (Ortho/Perspective), Ground Plane offset, Full Screen.
- Grid and Snaps: Layout Grid (on), Layout Grid Lock, Snap to Grid, Grid Settings (adaptive /
  fixed major spacing + minor subdivisions), Incremental Move.

### 5.3 Mouse navigation presets

| Action | Fusion (default) | SolidWorks | AutoCAD | Inventor |
|---|---|---|---|---|
| Orbit | Shift + MMB drag | MMB drag | Shift + MMB drag | F4 / Shift+MMB |
| Pan | MMB drag | Ctrl + MMB drag | MMB drag | MMB drag |
| Zoom | Wheel (zoom at cursor) | Wheel; Shift + MMB drag | Wheel at cursor | Wheel |
| Fit all | Double-click MMB / F6 | F | Double-click MMB (Zoom Extents) | F6 / Home |
| Orbit around point | Shift+MMB click sets pivot (Fusion: click-orbit on geometry pivot) | Click MMB on face then drag | - | - |
| Look At / Normal To | Look At button | Ctrl+8 | PLAN | Page Up |
| Select | LMB | LMB | LMB | LMB |
| Context menu | RMB click (marking menu) | RMB (shortcut bar/context) | RMB | RMB marking menu |

Wheel direction differs between products and all offer a "Reverse zoom direction" preference
(AutoCAD: forward = zoom in; SolidWorks: forward = zoom out by default). **FabriCAD default:
wheel forward = zoom in, zoom about cursor; preference "Reverse zoom direction".**

FabriCAD: preference "Pan, Zoom, Orbit shortcuts" with presets `FabriCAD/Fusion`, `SolidWorks`,
`AutoCAD`, `Inventor`, `Blender`(optional). Implement as a mapping table {button, modifiers} ->
{orbit, pan, zoom, rotate-roll}. Orbit uses `V3d_View::StartRotation/Rotation` around
the picked point if over geometry (gravity point), else scene centre.

---

## 6. Interaction model

### 6.1 Selection and highlighting

| State | Colour (light theme) | Notes |
|---|---|---|
| Pre-selection (hover) | Light blue `#4FA3FF` faces tint ~40%, edges `#3A8DFF` thick 2 px | Updated on mouse move (OCCT `MoveTo`, dynamic highlight). |
| Selected | Darker blue `#0B6CFF` (faces tinted ~50%, edges 3 px) | Fusion uses blue for both, darker for selected. |
| Selected + hover | Lighter selected blue `#5B9BFF` |  |
| Sketch curves under-constrained | Blue `#1E6FD9` | Fusion: blue = free; black = fixed/fully defined. |
| Sketch curves fully constrained | Black `#000000` (dark theme: white/grey) | Sketch browser icon gets a lock. |
| Construction curves | Orange dashed `#E58A00` (Fusion: orange dash) | SolidWorks: centreline dash-dot. |
| Projected/referenced geometry | Purple `#9B30D9` | Fusion projected = purple. |
| Fixed curves | Green `#2E9E2E` | Fusion fixed = green. |
| Over-constrained / conflict | Red `#E02020` | With error dialog listing conflicting constraints. |
| Sketch points | Small filled circles; under-defined blue, defined black | Origin point red. |
| Profiles (closed regions) | Light orange fill `#FFC98A` alpha 0.4 when hover-able in Extrude | Fusion shows profile shading; selected profile = blue tint. |
| Body default appearance | Steel grey `#B9BDC3`, edges dark `#1A1A1A` | Shaded with visible edges. |
| Background | Gradient top `#F5F7FA` to bottom `#C9D2DE` (light grey-blue) | Prefs: solid/gradient/dark. |
| Grid | Minor `#D8DDE3`, major `#B0B8C2`, axes X red / Y green / Z blue | Adaptive spacing; origin axes coloured. |
| Ground / joints | Joint glyph white with black outline; grounded pin icon | |

Selection rules:
- LMB click selects (replace). Ctrl+click or Shift+click adds/toggles (Fusion: Ctrl/Shift both
  add; Ctrl toggles). Click empty space = clear selection.
- Window select: LMB drag left-to-right = window (fully inside, solid outline); right-to-left =
  crossing (touching, dashed outline). Same convention as AutoCAD/SolidWorks.
- Hold LMB on a stack of entities (~0.5 s) = "Select Other"/pick list (Fusion: click-and-hold
  shows list of overlapping items; SolidWorks: Alt / Select Other).
- Selection filter and priority per SELECT menu and command input filters.
- Double-click: edge -> select tangent chain (loop); face -> ... ; sketch curve -> chain.

### 6.2 Command dialog (Fusion "command dialog", SolidWorks PropertyManager)

```
+------------------------------------+
| [ico] EXTRUDE                  [?] |  <- title bar, draggable, info icon
+------------------------------------+
| Type          [Solid|Thin]         |
| Profiles      [ ^ 1 selected  x ]  |  <- selection input: active one highlighted blue,
| Start         [Profile Plane  v]   |     click to make active, x clears
| Direction     [One Side       v]   |
| Extent Type   [Distance       v]   |
| Distance      [ 10 mm      ][fx]   |  <- expression field: "d1*2", "10 in", params
| Taper Angle   [ 0.0 deg       ]    |
| Operation     [ Join          v]   |  <- icon + text combo
+------------------------------------+
| [i] Select a profile               |  <- inline validation / error (red text)
|                 [ OK ]   [Cancel]  |
+------------------------------------+
```

Rules:
- Opening a command: dialog appears, first selection input active; status prompt updated.
- Live preview (OCCT presentation, transparent/orange-ish preview in Fusion: semi-transparent
  green for join, red for cut). FabriCAD: preview body transparency 0.5; join = body colour,
  cut = red `#E04040` alpha, new body = blue-tint.
- Manipulators in viewport: arrow drag handle for distance, ring for angle; dragging updates the
  numeric field; a floating value box near the handle accepts typed values.
- Enter = OK (unless focus in a multi-line field); Esc = Cancel; OK disabled while invalid.
- Fields accept expressions with units and parameter names (user parameters table), e.g.
  `width/2 + 3 mm`. Invalid -> red field border + tooltip.
- Selection inputs show count, allow deselect by clicking selected item, filter entity types.
- Right-click in viewport during command: marking menu with OK (top), Cancel, Repeat.
- After OK: feature appended at timeline marker, dialog closes. Enter/Space/right-click "Repeat
  <Command>" re-runs last command.
- SolidWorks extras worth copying: confirmation corner (OK/Cancel icons top-right of viewport)
  - FabriCAD optional pref.

### 6.3 Marking menu (right-click radial)

RMB click (no drag) in viewport opens radial menu with 8 slots around the cursor plus a linear
overflow menu below. RMB press-drag toward a slot and release = gesture selection (fast).

Default content with nothing selected (Fusion-like):

```
                 [ Repeat <last> ]                 N
   [ Undo ]                         [ Delete ]      NW / NE
 [ Press Pull ]       (+)          [ Extrude ]      W  / E      (W=Press Pull, E=Extrude)
   [ Move/Copy ]                    [ Hole ]        SW / SE
                 [ Create Sketch ]                 S
 -------- overflow (linear) --------
 Measure, Select tools, Show/Hide, Find in Browser, Find in Window, Show All Components/Bodies...
```

Context-sensitive slots: face selected -> Edit Sketch / Press Pull / Offset Plane / Look At;
edge -> Fillet / Chamfer; in sketch -> Line, Circle, Rectangle, Trim, Dimension, Finish Sketch,
Normal to/Look At, Undo; in command -> OK, Cancel, Repeat. Implementation: frameless translucent
`QWidget` painted wedges, 200 px diameter, hit-testing by angle; delay 0 for click, gesture
threshold 15 px.

### 6.4 Toolbox / command search (S key)

S (Fusion "Shortcuts box" / SolidWorks "Shortcut Bar"): popup at cursor containing (a) a
search field with instant fuzzy search over all commands (name, aliases, tooltip) with icons and
shortcuts, Enter runs top hit; (b) a user-pinned grid of commands (drag from search results,
"Pin to Shortcuts"). Context-sensitive pinned set for sketch vs part. Esc closes.

### 6.5 Command box (AutoCAD style, optional)
See 9.

---

## 7. Sketch workflow (precise)

1. **Create Sketch** (toolbar, marking menu or S-box; Fusion has no default key, FabriCAD
   leaves it unbound). If a planar face/plane is pre-selected,
   sketch starts immediately. Otherwise the three origin planes are shown (translucent orange,
   pickable, labelled) along with planar faces; status "Select a plane or planar face".
2. On pick: camera animates to **Look At** the sketch plane (normal, up = plane Y), grid shown on
   the plane, SKETCH contextual tab activates, **Sketch Palette** floats at right:

```
+---------------------------------+
| SKETCH PALETTE                  |
| v Options                       |
|   Linetype          [ / ][ :: ] |  <- normal / construction (X)
|   Look At           [eye]       |
|   Sketch Grid       [x]         |
|   Snap              [x]         |
|   Slice             [ ]         |  (section the model at sketch plane)
|   Show Profile      [x]         |
|   Show Points       [x]         |
|   Show Dimensions   [x]         |
|   Show Constraints  [x]         |
|   Show Projected Geometries [x] |
|   3D Sketch         [ ]         |
| v Constraints                   |
|   [=H/V][coinc][tan][equal][//] |
|   [perp][fix][mid][conc][coll]  |
|   [sym][G2]                     |
|                                 |
|        [ Finish Sketch ]        |
+---------------------------------+
```

3. The origin of the sketch plane is projected automatically (origin point + axes as reference).
4. **Line tool (L)**: click start, click next point; chains continuously until Esc, double-click,
   or closing the loop on the start point (closing ends the chain). **Drag from the last
   endpoint** (press LMB at endpoint, drag, release) creates a **tangent arc** from the previous
   segment; release, then continue with lines. Moving back over the endpoint resets.
5. **Dynamic inputs during creation**: floating boxes near cursor show length and angle (line),
   width/height (rectangle), diameter (circle). Type a number -> fills the focused box; **Tab**
   locks it and moves to the next box (locked value shows lock icon, becomes a dimension on
   commit); Enter commits the point. Same as AutoCAD Dynamic Input (Tab cycles fields).
6. **Inference / auto-constraints** while drawing (glyph shown next to cursor; applied on click):

| Inference | Trigger | Glyph |
|---|---|---|
| Coincident | cursor within 8 px of point/endpoint/curve | small square/dot |
| Midpoint | within tolerance of segment midpoint | triangle |
| Horizontal / Vertical | line within ~3 deg of axis | H / V bar |
| Tangent | arc/line from endpoint tangent direction | tangent circle-line |
| Perpendicular / Parallel | relative to the previous or hovered line | perp / // |
| Equal | length within tolerance of a hovered line (Fusion: on rectangle/polygon) | = |
| Center/Concentric | over circle center | cross |
| Alignment dashed inference lines | horizontal/vertical alignment with existing points (dashed tracking line) | dotted line |
| Grid snap | if Snap on | none |

   Holding **Ctrl** (Fusion: Ctrl while drawing) suppresses inferences/snaps.
7. **Dimension (D)**: click one entity -> linear/diameter/radius dimension; two entities -> distance
   or angle; then place the text by moving and clicking; an edit box appears immediately to type
   the value or expression (e.g. `d1*2`). Dimension named `d<n>` automatically and added to
   parameters. Placing a dimension that would over-constrain asks: "make driven (reference)
   dimension?" (Fusion: Shows warning "This dimension would over-constrain"; SolidWorks: dialog
   Make Driven / Leave Overdefined). FabriCAD: show dialog with Driven / Cancel.
   Double-click an existing dimension edits its value.
8. **Construction (X)**: toggles selected curves between normal and construction (orange dashed);
   with nothing selected, toggles the linetype for the next curves created.
9. **Constraint status**: free geometry blue, fully defined black; under-constrained curves can be
   dragged (live solve); fully constrained cannot. Status bar shows "Sketch: 3 DOF remaining" (FabriCAD
   addition; SolidWorks shows "Under Defined / Fully Defined" in status bar).
10. **Constraint glyphs**: small icons next to geometry; hover highlights affected entities; select +
    Delete removes. Fix constraint = green.
11. **Profiles**: every closed region (including those formed by intersections) becomes a pickable
    profile; shaded light orange.
12. **Finish Sketch**: palette/toolbar button or marking menu (FabriCAD: also `Ctrl+Enter`). Camera does NOT move
    back (Fusion keeps view; option to restore). Timeline adds sketch icon.
13. Edit sketch: double-click sketch in timeline/browser or right-click face > Edit Sketch.

Trim (T): hover a curve segment between intersections -> red highlight preview; click deletes;
drag across multiple = power trim (SolidWorks). Extend: hover shows preview to next intersection.
Offset (O): select chain, drag or type distance, flip direction.
Project (P): select edges/faces/bodies to project onto sketch plane (purple, associative).

---

## 8. Feature dialogs (fields per command)

Common to all: name auto "Extrude1"...; OK/Cancel; preview; manipulators; Enter=OK.

### 8.1 Extrude (E)

| Field | Values | Default |
|---|---|---|
| Type | Solid / Thin | Solid |
| Profiles | sketch profiles and/or planar faces (multi) | preselection |
| Start | Profile Plane / Offset (distance) / Object (face/plane) | Profile Plane |
| Direction | One Side / Two Sides / Symmetric | One Side |
| Measurement (symmetric) | Half Length / Whole Length | Half |
| Extent Type (per side) | Distance / To Object / All (through all) | Distance |
| Distance | expression, negative allowed (flips) | 10 mm |
| To Object options | Extend faces (bool), offset | - |
| Taper Angle | deg | 0 |
| Thin: Wall location | Side1 / Center / Side2, thickness | - |
| Operation | Join / Cut / Intersect / New Body / New Component | auto: Join if touching body, New Body otherwise; Cut if dragged into material |
| Objects to Cut/Join (participant bodies) | checklist | all intersecting |

Manipulators: arrow (distance), small taper wheel. Dragging an arrow into a body switches operation
to Cut automatically (Fusion behaviour).
OCCT: `BRepPrimAPI_MakePrism`, `BRepFeat_MakePrism`/`BRepOffsetAPI_DraftAngle` for taper;
booleans via `BRepAlgoAPI_Fuse/Cut/Common`.

### 8.2 Revolve
Profiles; Axis (sketch line, edge, construction axis, origin axis); Type: Angle / Full / To;
Angle (deg, default 360); Direction One Side / Two Sides / Symmetric; Operation as Extrude.
Ring manipulator. OCCT `BRepPrimAPI_MakeRevol`.

### 8.3 Fillet (F)
Type: Fillet / Rule Fillet / Full Round Fillet. Edges/faces/features (selecting a face adds all
its edges); selection sets (multiple radii groups, "+" to add set); Radius type Constant /
Chord length / Variable; Radius; Tangent chain (bool, default on); Corner type Rolling ball /
Setback; G2 option. OCCT `BRepFilletAPI_MakeFillet`.

### 8.4 Chamfer
Edges; Chamfer Type: Equal distance / Two distances / Distance and angle; Distance(s), Angle,
Flip; Tangent chain; Corner type Chamfer/Miter/Blend. OCCT `BRepFilletAPI_MakeChamfer`.

### 8.5 Shell
Faces to remove / Bodies; Tangent chain; Inside thickness; Outside thickness; Direction Inside /
Outside / Both; Shell type Sharp / Rounded. OCCT `BRepOffsetAPI_MakeThickSolid`.

### 8.6 Hole (H)
Placement: Single hole (on face, at point with dimensions to edges) / From sketch points /
multiple points. Extents: Distance / To / All. Hole type Simple / Counterbore / Countersink.
Hole tap type Simple / Clearance / Tapped / Taper Tapped; Thread type (ISO Metric profile,
ANSI Unified...); Size, Designation, Class, Fit (Normal/Close/Loose for clearance), Direction
(right/left hand), Modeled (cosmetic vs modeled thread). Diameter, Depth, Drill point Flat /
Angle (118 deg), Counterbore diameter/depth, Countersink diameter/angle (90 deg). Placing on a face
shows two reference-edge distance dims to edit. OCCT: cylinder/cone cut solids built from profile
revolve.

### 8.7 Rectangular pattern
Pattern Type: Faces / Bodies / Features / Components; Objects; Axes/Directions (edge, axis,
sketch line); Distribution Extent / Spacing; Quantity, Distance (Direction 1, Direction 2);
Direction type One / Symmetric; Suppress instances (click instance checkboxes in viewport);
Compute Option Optimized / Identical / Adjust. Manipulators: arrows + quantity spinner.

### 8.8 Circular pattern
Pattern Type; Objects; Axis; Type Full / Angle / Symmetric; Quantity; Total angle; Suppress.

### 8.9 Mirror
Pattern Type (Faces/Bodies/Features/Components); Objects; Mirror Plane (plane/planar face);
Operation (for bodies: Join / New Body). OCCT `BRepBuilderAPI_Transform` with `gp_Trsf::SetMirror`.

### 8.10 Combine
Target Body; Tool Bodies; Operation Join / Cut / Intersect; Keep Tools (bool); New Component (bool).

### 8.11 Press Pull (Q)
Context command: select face -> becomes Offset Face (distance); select edge -> Fillet;
select profile -> Extrude. Fast single-field interaction.

### 8.12 Construction
| Command | Inputs |
|---|---|
| Offset Plane | Plane/planar face; Distance; Extent (for display) |
| Plane at Angle | Linear edge/axis/sketch line; Angle (rotates about it, starting from adjacent plane) |
| Tangent Plane | Cylindrical/conical face; Angle; reference plane |
| Midplane | Two planes/planar faces (parallel or not -> bisector) |
| Plane Through Two Edges / Three Points / Along Path (distance or proportion) | as named |
| Axis Through Cylinder/Cone/Torus | face |
| Axis Perpendicular at Point / Through Two Planes / Through Two Points / Through Edge / Perpendicular to Face at Point | as named |
| Point at Vertex / Through Two Edges / Through Three Planes / at Center of Circle/Sphere/Torus / at Edge and Plane / Along Path | as named |
Construction geometry displayed: planes = translucent orange rectangle (`#F5A623` alpha 0.25,
border solid), axes = orange line, points = small orange sphere. Labelled in browser Construction.

### 8.13 Move/Copy (M)
Object type Bodies/Components/Faces/Sketch objects; Move Type Free Move (triad gizmo) /
Translate / Rotate / Point to Point / Point to Position; X/Y/Z distances, angles; Create Copy;
Pivot Set. Triad: arrows for translate, arcs for rotate, planes for 2D drag.

---

## 9. Keyboard shortcuts (FabriCAD defaults, Fusion-compatible)

| Key | Command | Context | Origin |
|---|---|---|---|
| S | Toolbox / command search | any | Fusion (SW: S shortcut bar) |
| E | Extrude | design | Fusion |
| Q | Press Pull | design | Fusion |
| F | Fillet | design | Fusion (SW: F = zoom fit) |
| H | Hole | design | Fusion |
| M | Move/Copy | any | Fusion |
| J | Joint | design | Fusion |
| Shift+J | As-built Joint | design | Fusion |
| I | Measure | any | Fusion |
| A | Appearance | design | Fusion |
| L | Line | sketch | Fusion |
| R | 2-Point Rectangle | sketch | Fusion |
| C | Center Diameter Circle | sketch | Fusion |
| D | Sketch Dimension | sketch | Fusion |
| X | Toggle Construction | sketch | Fusion |
| P | Project | sketch | Fusion |
| T | Trim | sketch | Fusion |
| O | Offset | sketch | Fusion |
| V | Toggle visibility of selection | any | Fusion |
| N | New Component | design | FabriCAD |
| Shift+N | Component colour cycling | design | Fusion |
| 1 | Window selection; 2 Freeform; 3 Paint | any | Fusion |
| Delete | Delete | any | |
| Esc | Cancel command / exit tool / clear selection | any | all |
| Enter | Repeat last command (when idle); OK (in dialog) | any | AutoCAD/Fusion |
| Space | Repeat last command (when idle) | any | AutoCAD |
| Ctrl+Z / Ctrl+Y (Ctrl+Shift+Z) | Undo / Redo | any | |
| Ctrl+S / Ctrl+Shift+S | Save / Save As | any | |
| Ctrl+N / Ctrl+O | New / Open | any | |
| Ctrl+C / Ctrl+V | Copy / Paste (component/body) | any | |
| Ctrl+B | Compute All | design | Fusion |
| F6 | Fit to window | any | Fusion |
| Home | Home view | any | |
| F2 | Rename | browser | |
| F7 | Slice (sketch) | sketch | |
| Ctrl+1..8 | Views (Front, Back, Left, Right, Top, Bottom, Iso, Normal To) | any | SolidWorks-style alt |
| Shift + arrows | Rotate view 15 deg | any | SolidWorks-style |
| Alt (hold) / Ctrl (hold while drawing) | Disable snap/inference | sketch | Fusion: Ctrl |
| Tab | Next dynamic input field (lock value) | while drawing | Fusion/AutoCAD |
| Shift+Tab | Previous field | while drawing | |
| ? / F1 | Help | | |

Rules: single-letter shortcuts are active only when the viewport/browser/timeline has focus (not
inside text fields). All bindings user-remappable (Preferences > Shortcuts; also hover any
command in a menu and use "..." > Change Keyboard Shortcut, Fusion-style). Store as JSON.
Shortcut presets: FabriCAD (above), SolidWorks-like (F fit, Ctrl+8 normal-to, Space orientation
dialog), AutoCAD-like (command aliases only).

### 9.1 Command box (AutoCAD-style, optional, toggle `Ctrl+9` like AutoCAD)

```
| cmd> LINE                                                      |
| Specify first point: 0,0                                       |
| Specify next point or [Undo]: @50<30                           |
```

- Single-line `QLineEdit` in status bar, expandable history (F2 shows text window).
- Letters typed in the viewport are single-key shortcuts by default. Command box receives focus via `:` or `Ctrl+9`-toggled mode "type to command
  line" (pref "Keyboard entry goes to command box" like AutoCAD).
- Commands accept full names and aliases: `L`/`LINE`, `C`/`CIRCLE`, `REC`/`RECTANG`, `EXT`/`EXTRUDE`,
  `REV`/`REVOLVE`, `F`/`FILLET`, `CHA`/`CHAMFER`, `TR`/`TRIM`, `EX`/`EXTEND`, `O`/`OFFSET`,
  `M`/`MOVE`, `CO`/`COPY`, `MI`/`MIRROR`, `AR`/`ARRAY`, `DIM`, `U`/`UNDO`, `Z`/`ZOOM` (E extents,
  W window), `P`/`PAN`, `SKETCH`, `FINISH`, `JOINT`, `MEASURE`, `SAVE`, `EXPORT`, `SET name=value`
  (parameter), `UNITS`.
- Coordinate entry in sketch tools: `x,y` absolute in sketch coords, `@dx,dy` relative,
  `@len<ang` polar (AutoCAD). Bracketed options `[Undo/Close]` selectable by typed letter.
- Autocomplete popup with matching commands (AutoCAD AutoComplete).
- Enter on empty = repeat last command.
- Every command in the GUI echoes its equivalent to the history (good for scripting/tests).
Implementation benefit: the same command parser drives headless tests.

---

## 10. Assemblies

Model: Fusion multi-component single document (components can also be external references,
"Insert into current design"). SolidWorks-style separate part/assembly files are not the default.

| Concept | Spec |
|---|---|
| Component | Container with own Origin, Bodies, Sketches, Construction, Joints, child components. Occurrences `Name:1`, `Name:2` share definition (edit one = edit all). New Component (empty or from bodies: right-click bodies > Create Components from Bodies). Activate radio sets edit context; inactive components fade (ghosted, 30% opacity) during in-context edit. |
| Ground | Right-click > Ground: fixed in space, pin icon. First inserted component grounded automatically (Fusion: grounds first when inserted from external). |
| Free move | Ungrounded unjointed components can be dragged; prompt "Capture Position" / "Revert" appears in timeline toolbar after moving (Fusion "Position" group). |
| Joint (J) | Dialog: Component 1 snap -> Component 2 snap; Motion type; Offset X/Y/Z; Angle; Flip; Animate (preview motion); Joint limits (min/max/rest). |
| As-built Joint (Shift+J) | Components already in position: pick Component 1, Component 2, Type, then Position (snap point) for axis; no movement. |
| Joint Origin | Pre-defined snap frame on a component for reuse. |
| Rigid Group | Locks selected components together. |
| Drive Joints | Select joint, type rotation/distance value; model animates. |
| Motion Link | Couples two joints (ratio, e.g. gear). |
| Contact sets | Enable contact (later). |
| Interference (INSPECT) | Select components/bodies (or all); compute pairwise interference; results table (pair, volume) + red highlighted intersect solids; "Create bodies" option. OCCT `BRepAlgoAPI_Common` + bounding box prefilter. |

Joint types and DOF:

| Type | Free DOF | Glyph |
|---|---|---|
| Rigid | 0 | lock |
| Revolute | 1 rot (about Z of joint frame) | curved arrow |
| Slider | 1 trans (Z/X/Y selectable) | straight arrow |
| Cylindrical | 1 rot + 1 trans same axis | |
| Pin-slot | 1 rot + 1 trans different axes | |
| Planar | 2 trans + 1 rot (normal) | |
| Ball | 3 rot (pitch, yaw, roll) | sphere |

Snap points (joint origin picking): hovering a face/edge shows snap glyphs: face center, edge
midpoint, edge endpoints, circle/arc center, cylinder axis top/middle/bottom, vertex, plus
"Between two faces" mode (midplane). Holding Ctrl locks the hovered snap's face so its other snap
points can be chosen. The snap defines a frame (origin + Z = face normal / circle axis).
After both snaps chosen, Component 1 jumps to align its frame to Component 2's (Z axes opposed
for faces, collinear for axes), animates the motion preview, dialog fields offset/rotate/flip.

Solver: frames + DOF; for MVP a sequential/propagation solver (tree of joints from grounded
component) suffices; loops later.

---

## 11. Drawing workspace

Entry: workspace switcher "DRAWING" > "From Design" (or File > New Drawing > From Design):
dialog:

```
+-----------------------------------+
| CREATE DRAWING                    |
| Design        [ Full Assembly  v] |  (or selected components)
| Destination   [ New drawing     ] |
| Template      [ From scratch   v] |
| Standard      [ ISO | ASME      ] |  ISO default (first-angle), ASME = third-angle
| Units         [ mm             v] |
| Sheet Size    [ A3 (420x297)   v] |  A4..A0, ANSI A..E
|          [ OK ]  [ Cancel ]        |
+-----------------------------------+
```

Drawing layout:

```
+---------------------------------------------------------------------------------+
| [DRAWING v] CREATE | MODIFY | GEOMETRY | DIMENSIONS | TEXT | SYMBOLS | TABLES | EXPORT |
+---------------------------------------------------------------------------------+
| (browser: sheets) |  +--------------------------------------------------------+  |
| Sheet1            |  | border/zones (A..F, 1..8)                              |  |
| Sheet2            |  |    [Front]        [Right]                              |  |
|                   |  |    [Top]          [Iso]          (1) balloon           |  |
|                   |  |                              +------------------------+  |
|                   |  |  PARTS LIST                  | TITLE BLOCK            |  |
|                   |  +------------------------------+------------------------+  |
|                   |  [Sheet1][Sheet2][+]                      (sheet tabs bottom)|
+---------------------------------------------------------------------------------+
```

Toolbar groups (DRAWING workspace):

| Group | Commands (shortcut) |
|---|---|
| CREATE | Base View, Projected View (P), Section View, Detail View, Break View, Move (M), Rotate, Delete |
| MODIFY | Edit view (double-click), Edit Title Block, Edit Sheet |
| GEOMETRY | Center Mark, Center Mark Pattern, Centerline, Edge Extension |
| DIMENSIONS | Dimension (D, auto-type: linear/angular/radial/diameter), Ordinate, Baseline, Chain, Arc Length, Break Dimension, Arrange Dimensions |
| TEXT | Text, Leader (L) |
| SYMBOLS | Surface Texture, Feature Control Frame (GD&T), Datum Identifier, Taper/Slope, Weld |
| TABLES | Table, Parts List, Balloon, Renumber, Align Balloons, Hole Table, Revision Table |
| EXPORT | Output PDF, Output DXF, Output CSV (parts list) |

Base View dialog (appears after placing view on sheet, view follows cursor until click):
Orientation (Front, Top, Right, Back, Bottom, Left, Iso Top Right, Iso Top Left, Iso Bottom...,
Current/Named views), Style (Visible Edges / Visible and Hidden Edges / Shaded / Shaded with Hidden
Edges), Scale (1:1, 1:2, 2:1 ...; sheet default), Tangent Edges (Full / Shortened / Off),
Interference edges, Thread feature: on/off, Label.

Projected View: select parent view, move cursor horizontally/vertically/diagonally - preview of the
orthographic (or iso if diagonal) projection follows; click to place each; Enter/OK to finish.
Projected views inherit style/scale and stay aligned (moving parent moves children along axes).

Section View: select parent view, draw section line (click points; ortho snap, offset segments
allowed), place view; options: label "A-A", hatch, depth (full/slice), style. Section arrows on
line, hatch 45 deg, per-material pattern.
Detail View: select parent, click center, drag radius (circle; or rectangle), place; scale default
2x parent; label "B (2:1)"; boundary style full/jagged; connection line.
Break View: two parallel lines, gap.
Center marks: click circles/arcs -> cross with extended lines; Centerline: pick two edges ->
centerline between; automatic center marks option per view.
Dimensions: D -> click edge -> linear; two edges -> distance/angle; circle -> diameter (Fusion
puts Ø); arc -> radius. Dimension values are associative (driven, measured from HLR result).
Model parameters can be "retrieved" (later).
Title block: fields Title, Part number, Drawn by, Date, Checked, Approved, Scale, Sheet n/m,
Size, Rev, Material, Mass, Company; autofilled from document properties; double-click edits.
Border with zone letters/numbers.
Parts list: select view -> place table: columns Item, Qty, Part Number, Description, Material,
Mass; bound to assembly BOM (components grouped by definition). Balloons: auto-number matching
parts list item, leader to component edge, circle style.
Export: PDF (vector, sheet size, all/selected sheets, open after), DXF (layers: VISIBLE, HIDDEN,
CENTER, DIM, TEXT, TITLE, HATCH; line types). DWG export is out of scope.

Drawing visual standards:

| Element | Line | Width (mm) | Colour |
|---|---|---|---|
| Visible edges | continuous | 0.5 (0.35 for A4/A3 thin) | black |
| Hidden edges | dashed (ISO 128 type 02, dash 3 gap 1) | 0.25 | black |
| Center lines / center marks | chain (long dash dot) | 0.18 | black |
| Dimension/extension lines | continuous thin | 0.18 | black |
| Section line | chain thick ends + arrows | 0.35 | |
| Hatch | thin 45 deg, 3 mm spacing | 0.18 | |
| Tangent edges | thin continuous (or off) | 0.18 | grey |
| Text | ISO 3098 font (use "osifont" or DejaVu Sans), 3.5 mm dims, 5/7 mm titles | | |

OCCT: `HLRBRep_PolyAlgo` (fast, mesh-based) or `HLRBRep_Algo` (exact) + `HLRBRep_HLRToShape`
(VCompound visible sharp, HCompound hidden, Rg1Line smooth/tangent, OutLine silhouettes) per view
direction; run off GUI thread (TBB/QtConcurrent). Sheets rendered by `QGraphicsScene` (2D, no GL
required -> headless testable; `QPdfWriter`/`QPrinter` for PDF; custom DXF R12/AC1015 writer).

---

## 12. Visual style and theme (light theme defaults)

| Token | Value | Use |
|---|---|---|
| `--app-bg` | `#F2F3F5` | window, panels |
| `--panel-bg` | `#FFFFFF` | browser, dialogs |
| `--panel-border` | `#D3D6DB` | 1 px separators |
| `--toolbar-bg` | `#F7F7F8` | toolbar |
| `--text` | `#2B2E33` | primary text |
| `--text-dim` | `#6B7079` | labels, disabled |
| `--accent` | `#0696D7` | active tab underline, focused field, primary button (Autodesk-like cyan-blue; choose own shade) |
| `--accent-sketch` | `#3BA55C` | Finish Sketch check button, SKETCH contextual tab underline |
| `--ok-btn` | accent filled, white text | OK |
| `--cancel-btn` | white, border | Cancel |
| `--error` | `#D93025` | |
| `--warning` | `#F2A900` | |
| Viewport bg | gradient `#FFFFFF` -> `#C8D0D9` (top->bottom) | |
| Preselect | `#4FA3FF` | hover |
| Selected | `#0B6CFF` | selection |
| Font | system UI (Qt default) 9-10 pt; group labels uppercase 8 pt semibold | |
| Icons | 32 px toolbar, 16 px menu/tree, monochrome-with-accent SVG (own artwork, Qt SVG) | |

Dark theme: same token set, viewport gradient `#3A3F46` -> `#1E2126`, fully-constrained sketch
curves white. Theme in Preferences > General.

Viewport defaults: Fusion defaults to perspective; **FabriCAD default
orthographic** (engineering), toggle in ViewCube menu. Visual style "Shaded with visible edges
only". Ground grid on XY (or Y-up equivalent) adaptive, 10 mm minor at default zoom. Origin
triad bottom-left (small XYZ, coloured X red, Y green, Z blue). Anti-aliasing MSAA 4x if available.

Feedback: tooltips after 0.6 s with name, shortcut, one-line description (Fusion shows extended
tooltip with image after longer hover). Status bar prompt text always describes the next expected
input ("Select profile(s)", "Specify next point or [Undo]").

---

## 13. Preferences (minimum set)

General: units default (mm), default modeling orientation (Z up / Y up), pan-zoom-orbit preset,
reverse zoom, zoom to cursor, orbit around selection/pick, theme, language, autosave interval.
Design: auto-project edges on reference (off; Fusion has "Auto project geometry"), auto look at
sketch (on), sketch grid/snap defaults, show constraint glyphs, capture design history (on).
Drawing: standard ISO/ASME, projection angle (first/third), default sheet, line widths, fonts.
Keyboard: shortcut editor, command aliases, command box keyboard capture.

---

## 14. Implementation priorities (UX)

1. Window shell: app bar, workspace switcher, tab toolbar with group dropdowns, browser dock,
   viewport with ViewCube + nav bar, status bar + command box.
2. Command framework: `Command` objects with inputs (selection, value-with-expression, enum,
   bool), dialog auto-generated from inputs, preview hook, OK/Cancel, repeat-last, shortcut and
   alias registry, S-box search over registry, marking menu populated from registry.
3. Selection/highlight manager with filters and colours above.
4. Sketch mode: plane pick, look-at, palette, line/rect/circle/arc tools, inference, dynamic
   input with Tab, dimensions, constraint colouring from solver DOF.
5. Timeline + browser bound to the document model.
6. Features: Extrude, Revolve, Fillet, Chamfer, Shell, Hole, Patterns, Mirror, Combine, Press Pull,
   Construction geometry, Move/Copy.
7. Assembly: components, ground, joints with snap picking, drive, interference.
8. Drawing: sheet scene, base/projected/section/detail views via HLR, dims, center marks, title
   block, parts list/balloons, PDF/DXF.

Testability: every GUI action routes through the command registry; headless tests drive commands
via the command-box parser and assert on the document model (no GL).
