# SPDX-License-Identifier: LGPL-2.1-or-later
"""Chair benchmark (docs/comparison/CHAIR_SPEC.md) built in FreeCAD 1.1.x.

Run headless:
    FreeCAD.AppImage freecadcmd chair_freecad.py          (AppRun dispatches to usr/bin/freecadcmd)
Optional env: CHAIR_OUT=<dir> (default: ./out next to this script).

Every block marked `op(...)` corresponds to one GUI action (a command + its task panel, or
one property edit) that a FreeCAD user would perform. Sketch geometry and constraints are
added the way the Sketcher tools add them (rectangle tool -> 4 lines + coincident + H/V
constraints; fillet tool -> trimmed arc + tangency; then dimension constraints).
"""
import os
import sys
import time
import math

T0 = time.perf_counter()

import FreeCAD as App
import Part
import Sketcher
import Mesh
import MeshPart
import Import
import BOPTools.SplitFeatures
import CompoundTools.Explode
import UtilsAssembly
import JointObject

T_IMPORTS = time.perf_counter() - T0
V = App.Vector
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.environ.get("CHAIR_OUT", os.path.join(HERE, "out"))
os.makedirs(OUT, exist_ok=True)

OPS = []          # (stage, description)
STAGE_T = {}      # stage -> seconds
_stage = ["setup"]
_stage_t0 = [time.perf_counter()]


def stage(name):
    now = time.perf_counter()
    STAGE_T[_stage[0]] = now - _stage_t0[0]
    _stage[0] = name
    _stage_t0[0] = now


def op(desc):
    OPS.append((_stage[0], desc))


def log(msg):
    print(msg, flush=True)


# --------------------------------------------------------------------------- helpers
def origin_feature(body, role):
    for f in body.Origin.OriginFeatures:
        if f.Role == role:
            return f
    raise RuntimeError(role)


def new_sketch(body, name, support, offset=App.Placement()):
    """Sketcher_NewSketch: pick plane/face in the task dialog (+ optional attachment offset)."""
    sk = body.newObject("Sketcher::SketchObject", name)
    sk.AttachmentSupport = support
    sk.MapMode = "FlatFace"
    sk.AttachmentOffset = offset
    return sk


def add_rectangle(sk, x0, y0, x1, y1):
    """Sketcher_CreateRectangle: 4 lines, 4 coincident, 2 horizontal, 2 vertical (as the tool)."""
    n = sk.GeometryCount
    pts = [V(x0, y0, 0), V(x1, y0, 0), V(x1, y1, 0), V(x0, y1, 0)]
    for i in range(4):
        sk.addGeometry(Part.LineSegment(pts[i], pts[(i + 1) % 4]), False)
    cons = []
    for i in range(4):
        cons.append(Sketcher.Constraint("Coincident", n + i, 2, n + (i + 1) % 4, 1))
    cons += [
        Sketcher.Constraint("Horizontal", n + 0),
        Sketcher.Constraint("Horizontal", n + 2),
        Sketcher.Constraint("Vertical", n + 1),
        Sketcher.Constraint("Vertical", n + 3),
    ]
    sk.addConstraint(cons)
    return n  # geo ids n..n+3: bottom, right, top, left (local coords)


def dim_rectangle(sk, n, x0, y0, x1, y1):
    """Dimension tool x4: width, height, and corner position from sketch origin."""
    sk.addConstraint(Sketcher.Constraint("DistanceX", n + 0, 1, n + 0, 2, x1 - x0))
    sk.addConstraint(Sketcher.Constraint("DistanceY", n + 1, 1, n + 1, 2, y1 - y0))
    sk.addConstraint(Sketcher.Constraint("DistanceX", -1, 1, n + 0, 1, x0))
    sk.addConstraint(Sketcher.Constraint("DistanceY", -1, 1, n + 0, 1, y0))


def rect_sketch_ops(sk, x0, y0, x1, y1):
    op(f"  Sketcher: rectangle tool ({x0},{y0})-({x1},{y1})")
    n = add_rectangle(sk, x0, y0, x1, y1)
    op("  Sketcher: dimension width")
    op("  Sketcher: dimension height")
    op("  Sketcher: dimension corner X from origin")
    op("  Sketcher: dimension corner Y from origin")
    dim_rectangle(sk, n, x0, y0, x1, y1)
    return n


def check_sketch(sk):
    App.ActiveDocument.recompute()
    dof = sk.solve()
    fc = getattr(sk, "FullyConstrained", None)
    log(f"    {sk.Name}: solve={dof} FullyConstrained={fc} constraints={sk.ConstraintCount} "
        f"malformed={sk.MalformedConstraints} redundant={sk.RedundantConstraints} "
        f"conflicting={sk.ConflictingConstraints}")
    if not fc:
        raise RuntimeError(f"sketch {sk.Name} not fully constrained")


def find_edges(shape, pred):
    res = []
    for i, e in enumerate(shape.Edges, 1):
        if pred(e):
            res.append(f"Edge{i}")
    return res


def find_faces(shape, pred):
    res = []
    for i, f in enumerate(shape.Faces, 1):
        if pred(f):
            res.append(f"Face{i}")
    return res


def close(a, b, tol=1e-6):
    return abs(a - b) <= tol


def local_point(sk, world):
    """Where a world point lands in the sketch's own 2D coordinates (what the user reads off
    the grid / snaps to when placing geometry on a face-attached sketch)."""
    p = sk.Placement.inverse().multVec(world)
    return round(p.x, 9), round(p.y, 9)


# =========================================================================== start
stage("A_seat")
doc = App.newDocument("Chair")
op("File > New")

# ---------------------------------------------------------------- Seat body
op("Workbench: PartDesign")
op("PartDesign_Body (Seat)")
seat = doc.addObject("PartDesign::Body", "Seat")
op("Rename body to 'Seat' (F2)")
seat.Label = "Seat"

# S1: sketch on XY offset 430; rectangle, fillet front corners R20, pad 30
op("Sketcher_NewSketch on XY_Plane, attachment offset z=430")
s1 = new_sketch(seat, "SeatSketch", [(origin_feature(seat, "XY_Plane"), "")],
                App.Placement(V(0, 0, 430), App.Rotation()))
op("  Sketcher: rectangle tool (-210,-200)-(210,200)")
n = add_rectangle(s1, -210, -200, 210, 200)        # 0 bottom(front), 1 right, 2 top(rear), 3 left
op("  Sketcher: dimension rear-left corner -> origin X 210")
s1.addConstraint(Sketcher.Constraint("DistanceX", 2, 2, -1, 1, 210))
op("  Sketcher: dimension rear width 420")
s1.addConstraint(Sketcher.Constraint("DistanceX", 2, 2, 2, 1, 420))
op("  Sketcher: dimension origin->rear 200")
s1.addConstraint(Sketcher.Constraint("DistanceY", -1, 1, 2, 1, 200))
# fillet tool on the two front corners (click both edges near the corner)
op("  Sketcher: fillet tool front-right corner")
s1.fillet(0, 1, V(205, -200, 0), V(210, -195, 0), 20, True, False)
op("  Sketcher: fillet tool front-left corner")
s1.fillet(3, 0, V(-210, -195, 0), V(-205, -200, 0), 20, True, False)
arcs = [i for i, g in enumerate(s1.Geometry) if isinstance(g, Part.ArcOfCircle)]
# (dimensioning the front line before filleting fails: the fillet tool trims the line and
#  deletes constraints on the trimmed end point -> dimension it afterwards)
op("  Sketcher: dimension front->origin 200")
s1.addConstraint(Sketcher.Constraint("DistanceY", 0, 1, -1, 1, 200))
op("  Sketcher: radius dimension R20 (arc 1)")
s1.addConstraint(Sketcher.Constraint("Radius", arcs[0], 20))
op("  Sketcher: radius dimension R20 (arc 2)")
s1.addConstraint(Sketcher.Constraint("Radius", arcs[1], 20))
op("  Close sketch")
check_sketch(s1)

op("PartDesign_Pad SeatSketch, length 30")
pad1 = seat.newObject("PartDesign::Pad", "SeatPad")
pad1.Profile = s1
pad1.Length = 30
doc.recompute()

# S2: fillet R6 on the top front edge (tangent chain propagates)
top_front = find_edges(pad1.Shape, lambda e: isinstance(e.Curve, Part.Line)
                       and close(e.BoundBox.YMin, -200) and close(e.BoundBox.YMax, -200)
                       and close(e.BoundBox.ZMin, 460) and close(e.BoundBox.ZMax, 460))
assert len(top_front) == 1, top_front
op(f"PartDesign_Fillet R6, select top front edge {top_front[0]}")
fil1 = seat.newObject("PartDesign::Fillet", "SeatEdgeFillet")
fil1.Base = (pad1, top_front)
fil1.Radius = 6
doc.recompute()

# S3: backrest sketch on XZ offset to y=175 (XZ normal is -Y in FreeCAD 1.x -> offset -175),
#     pad reversed (towards +Y) 25
op("Sketcher_NewSketch on XZ_Plane, attachment offset z=-175 (plane y=175)")
s3 = new_sketch(seat, "BackrestSketch", [(origin_feature(seat, "XZ_Plane"), "")],
                App.Placement(V(0, 0, -175), App.Rotation()))
rect_sketch_ops(s3, -210, 430, 210, 840)
op("  Close sketch")
check_sketch(s3)
op("PartDesign_Pad BackrestSketch, length 25, Reversed")
pad2 = seat.newObject("PartDesign::Pad", "BackrestPad")
pad2.Profile = s3
pad2.Length = 25
pad2.Reversed = True
doc.recompute()

# S4: lumbar slot sketch on the backrest front face, pocket through all
front_faces = find_faces(pad2.Shape, lambda f: isinstance(f.Surface, Part.Plane)
                         and close(f.BoundBox.YMin, 175) and close(f.BoundBox.YMax, 175)
                         and f.BoundBox.ZMax > 800)
assert len(front_faces) == 1, front_faces
op(f"Select backrest front face ({front_faces[0]}) + Sketcher_NewSketch")
s4 = new_sketch(seat, "LumbarSketch", [(pad2, front_faces[0])])
doc.recompute()
xa, ya = local_point(s4, V(-150, 175, 680))
xb, yb = local_point(s4, V(150, 175, 740))
rect_sketch_ops(s4, min(xa, xb), min(ya, yb), max(xa, xb), max(ya, yb))
op("  Close sketch")
check_sketch(s4)
op("PartDesign_Pocket LumbarSketch, type Through all")
pocket = seat.newObject("PartDesign::Pocket", "LumbarSlot")
pocket.Profile = s4
pocket.Type = "ThroughAll"
doc.recompute()

# ---------------------------------------------------------------- LegFrame body
stage("A_legframe")
op("PartDesign_Body (LegFrame)")
leg = doc.addObject("PartDesign::Body", "LegFrame")
op("Rename body to 'LegFrame' (F2)")
leg.Label = "LegFrame"

op("Sketcher_NewSketch on XY_Plane")
l1 = new_sketch(leg, "LegsSketch", [(origin_feature(leg, "XY_Plane"), "")])
first = None
for cx in (-170, 170):
    for cy in (-160, 160):
        n = rect_sketch_ops(l1, cx - 20, cy - 20, cx + 20, cy + 20)
op("  Close sketch")
check_sketch(l1)
op("PartDesign_Pad LegsSketch, length 430")
lpad = leg.newObject("PartDesign::Pad", "LegsPad")
lpad.Profile = l1
lpad.Length = 430
doc.recompute()

# L2: side rails, one sketch per side on YZ plane offset x=+-170, pad symmetric 20
rails = []
for side, xoff in (("Left", -170), ("Right", 170)):
    op(f"Sketcher_NewSketch on YZ_Plane, attachment offset z={xoff}")
    sk = new_sketch(leg, f"Rail{side}Sketch", [(origin_feature(leg, "YZ_Plane"), "")],
                    App.Placement(V(0, 0, xoff), App.Rotation()))
    rect_sketch_ops(sk, -140, 130, 140, 170)      # YZ sketch: local x = world Y, local y = world Z
    op("  Close sketch")
    check_sketch(sk)
    op(f"PartDesign_Pad Rail{side}Sketch, length 20, Symmetric to plane")
    p = leg.newObject("PartDesign::Pad", f"Rail{side}Pad")
    p.Profile = sk
    p.Length = 20
    p.SideType = "Symmetric"
    doc.recompute()
    rails.append(p)

# L3: front cross rail on XZ plane offset y=-160 (offset +160 because XZ normal is -Y)
op("Sketcher_NewSketch on XZ_Plane, attachment offset z=160 (plane y=-160)")
l3 = new_sketch(leg, "CrossRailSketch", [(origin_feature(leg, "XZ_Plane"), "")],
                App.Placement(V(0, 0, 160), App.Rotation()))
rect_sketch_ops(l3, -150, 130, 150, 170)
op("  Close sketch")
check_sketch(l3)
op("PartDesign_Pad CrossRailSketch, length 20, Symmetric to plane")
xpad = leg.newObject("PartDesign::Pad", "CrossRailPad")
xpad.Profile = l3
xpad.Length = 20
xpad.SideType = "Symmetric"
doc.recompute()

# ---------------------------------------------------------------- Assembly (stage A)
stage("A_assembly")
op("Workbench: Assembly")
op("Assembly_CreateAssembly")
asm = doc.addObject("Assembly::AssemblyObject", "Assembly")
jg = asm.newObject("Assembly::JointGroup", "Joints")


def insert_component(body):
    """Assembly_InsertLink: tick the body in the Insert Component task panel."""
    lnk = asm.newObject("App::Link", body.Name)
    lnk.LinkedObject = body
    lnk.Label = body.Label
    return lnk


op("Assembly_InsertLink: Seat, LegFrame (one task panel, two items)")
seat_l = insert_component(seat)
leg_l = insert_component(leg)
doc.recompute()

op("Select Seat + Assembly_ToggleGrounded")
g_seat = jg.newObject("App::FeaturePython", "GroundedJoint")
JointObject.GroundedJoint(g_seat, seat_l)
doc.recompute()   # Assembly_ToggleGrounded recomputes; without it isPartConnected() misses the ground

# Rigid ("Fixed") joint: LegFrame leg top face, JCS at the front-right leg's outer corner vertex
# (190,-180,430); Seat bottom face, JCS at the centre of its front-right R20 arc (190,-180,430).
# Both JCS coincide as built, so with zero offsets nothing moves.
leg_shape = leg.Shape
leg_top = find_faces(leg_shape, lambda f: isinstance(f.Surface, Part.Plane)
                     and close(f.BoundBox.ZMin, 430) and close(f.BoundBox.ZMax, 430)
                     and close(f.BoundBox.XMax, 190) and close(f.BoundBox.YMin, -180))[0]
leg_vtx = [f"Vertex{i}" for i, v in enumerate(leg_shape.Vertexes, 1)
           if (v.Point - V(190, -180, 430)).Length < 1e-6][0]
seat_shape = seat.Shape


def seat_bottom_arc(center):
    return [f"Edge{i}" for i, e in enumerate(seat_shape.Edges, 1)
            if isinstance(e.Curve, Part.Circle) and (e.Curve.Center - center).Length < 1e-6
            and close(e.BoundBox.ZMax, 430)][0]


# The Seat side uses the bottom face's R20 arc edge (JCS snaps to the arc centre, identity
# rotation). Picking the planar bottom face itself gives a JCS whose X axis is reversed
# (face normal -Z), and the Fixed joint then spins LegFrame 180 deg about Z.
arc_r = seat_bottom_arc(V(190, -180, 430))
log(f"    joint refs: Seat {arc_r} (bottom R20 arc centre) / LegFrame {leg_top}+{leg_vtx}")

op("Assembly_CreateJointFixed: pick Seat bottom R20 arc edge (centre), then LegFrame leg top face (corner vertex), OK")
fixed = jg.newObject("App::FeaturePython", "FixedJoint")
JointObject.Joint(fixed, 0)   # 0 = Fixed
plc_before = (App.Placement(leg_l.Placement), App.Placement(seat_l.Placement))
fixed.Proxy.setJointConnectors(fixed, [
    [seat_l, [arc_r, arc_r]],
    [leg_l, [leg_top, leg_vtx]],
])
doc.recompute()
moved = not (leg_l.Placement.isSame(plc_before[0], 1e-9) and seat_l.Placement.isSame(plc_before[1], 1e-9))
log(f"    after Fixed joint solve: LegFrame plc={leg_l.Placement}  Seat plc={seat_l.Placement}  moved={moved}")
if moved:
    raise RuntimeError("rigid joint moved a part (not as-built)")

vol_A = {"Seat": seat.Shape.Volume, "LegFrame": leg.Shape.Volume}
log(f"    stage A volumes: {vol_A}")
doc.recompute()
op("File > Save (stage A)")
doc.saveAs(os.path.join(OUT, "chair_stageA.FCStd"))

# =========================================================================== stage B
stage("B_split")
op("Workbench: Part")
op("Select LegFrame body + YZ_Plane (LegFrame origin), Part_SliceApart")
yz = origin_feature(leg, "YZ_Plane")
slc = BOPTools.SplitFeatures.makeSlice(name="Slice")
slc.Base = leg
slc.Tools = [yz]
slc.Mode = "Split"
slc.Proxy.execute(slc)
slc.purgeTouched()
pieces = CompoundTools.Explode.explodeCompound(slc)
doc.recompute()
pieces = list(pieces[1]) if isinstance(pieces, tuple) else list(pieces)
pieces.sort(key=lambda o: o.Shape.BoundBox.Center.x)
log(f"    slice pieces: {[(p.Name, round(p.Shape.Volume, 3), p.Shape.BoundBox) for p in pieces]}")
assert len(pieces) == 2

op("Workbench: PartDesign")
op("Select left slice piece + PartDesign_Body (base feature) -> rename 'LegLeft'")
legL = doc.addObject("PartDesign::Body", "LegLeft")
legL.BaseFeature = pieces[0]
legL.Label = "LegLeft"
op("Select right slice piece + PartDesign_Body (base feature) -> rename 'LegRight'")
legR = doc.addObject("PartDesign::Body", "LegRight")
legR.BaseFeature = pieces[1]
legR.Label = "LegRight"
doc.recompute()
baseL = [o for o in legL.Group if o.isDerivedFrom("PartDesign::FeatureBase")][0]
baseR = [o for o in legR.Group if o.isDerivedFrom("PartDesign::FeatureBase")][0]
log(f"    LegLeft group: {[(o.Name, o.TypeId) for o in legL.Group]}  LegRight group: {[(o.Name, o.TypeId) for o in legR.Group]}")
vol_B = {"LegLeft": legL.Shape.Volume, "LegRight": legR.Shape.Volume}
log(f"    stage B volumes: {vol_B}")

op("Workbench: Assembly")
op("Delete LegFrame component (link) from the assembly; confirm joint-reference warning")
op("Assembly_InsertLink: LegLeft, LegRight (one task panel, two items)")
legL_l = insert_component(legL)
legR_l = insert_component(legR)
op("Edit FixedJoint: re-pick Seat left bottom arc (centre) + LegLeft leg top face (corner vertex)")
ll_shape = legL.Shape
ll_top = find_faces(ll_shape, lambda f: isinstance(f.Surface, Part.Plane)
                    and close(f.BoundBox.ZMin, 430) and close(f.BoundBox.ZMax, 430)
                    and close(f.BoundBox.XMin, -190) and close(f.BoundBox.YMin, -180))[0]
ll_vtx = [f"Vertex{i}" for i, v in enumerate(ll_shape.Vertexes, 1)
          if (v.Point - V(-190, -180, 430)).Length < 1e-6][0]
arc_l = seat_bottom_arc(V(-190, -180, 430))
doc.removeObject(leg_l.Name)
fixed.Proxy.setJointConnectors(fixed, [
    [seat_l, [arc_l, arc_l]],
    [legL_l, [ll_top, ll_vtx]],
])
doc.recompute()
log(f"    after re-pointed joint: LegLeft plc={legL_l.Placement} Seat plc={seat_l.Placement}")
if not (legL_l.Placement.isIdentity() and seat_l.Placement.isIdentity()):
    raise RuntimeError("re-pointed joint moved a part")

# =========================================================================== stage C
stage("C_features")
op("Workbench: PartDesign")
op("Activate LegLeft body (double-click)")
botL = find_edges(legL.Shape, lambda e: close(e.BoundBox.ZMin, 0) and close(e.BoundBox.ZMax, 0))
assert len(botL) == 8, botL
op("Select 8 bottom leg edges (8 clicks, view from below) + PartDesign_Chamfer, size 3")
cham = legL.newObject("PartDesign::Chamfer", "LegChamfer")
cham.Base = (baseL, botL)
cham.ChamferType = "Equal distance"
cham.Size = 3
doc.recompute()

op("Activate LegRight body (double-click)")
botR = find_edges(legR.Shape, lambda e: close(e.BoundBox.ZMin, 0) and close(e.BoundBox.ZMax, 0))
assert len(botR) == 8, botR
op("Select 8 bottom leg edges + PartDesign_Fillet, radius 5")
filR = legR.newObject("PartDesign::Fillet", "LegFillet")
filR.Base = (baseR, botR)
filR.Radius = 5
doc.recompute()

rail_out = find_faces(filR.Shape, lambda f: isinstance(f.Surface, Part.Plane)
                      and close(f.BoundBox.XMin, 180) and close(f.BoundBox.XMax, 180)
                      and close(f.BoundBox.YMin, -140) and close(f.BoundBox.YMax, 140))
assert len(rail_out) == 1, rail_out
op(f"Select right rail outer face ({rail_out[0]}) + Sketcher_NewSketch")
hs = new_sketch(legR, "HoleSketch", [(filR, rail_out[0])])
doc.recompute()
cx, cy = local_point(hs, V(180, 0, 150))
op("  Sketcher: circle tool")
c = hs.addGeometry(Part.Circle(V(cx, cy, 0), V(0, 0, 1), 5), False)
op("  Sketcher: diameter dimension 10")
hs.addConstraint(Sketcher.Constraint("Diameter", c, 10))
op("  Sketcher: centre X dimension from sketch origin")
hs.addConstraint(Sketcher.Constraint("DistanceX", -1, 1, c, 3, cx))
op("  Sketcher: centre Y dimension from sketch origin")
hs.addConstraint(Sketcher.Constraint("DistanceY", -1, 1, c, 3, cy))
op("  Close sketch")
check_sketch(hs)
op("PartDesign_Hole: diameter 10, no thread, depth Through all")
hole = legR.newObject("PartDesign::Hole", "RailHole")
hole.Profile = hs
hole.Threaded = False
hole.Diameter = 10
hole.DepthType = "ThroughAll"
doc.recompute()

vol_C = {"Seat": seat.Shape.Volume, "LegLeft": legL.Shape.Volume, "LegRight": legR.Shape.Volume}
log(f"    stage C volumes: {vol_C}")

# =========================================================================== stage D
stage("D_transforms")
op("Workbench: Assembly")
op("Select FixedJoint, Delete")
doc.removeObject(fixed.Name)
op("Select LegLeft + Assembly_ToggleGrounded")
g_l = jg.newObject("App::FeaturePython", "GroundedJoint")
JointObject.GroundedJoint(g_l, legL_l)
op("Select LegRight + Assembly_ToggleGrounded")
g_r = jg.newObject("App::FeaturePython", "GroundedJoint")
JointObject.GroundedJoint(g_r, legR_l)
op("LegLeft link: Placement dialog, translation x=-15")
legL_l.Placement = App.Placement(V(-15, 0, 0), App.Rotation())
op("LegRight link: Placement dialog, translation x=+15")
legR_l.Placement = App.Placement(V(15, 0, 0), App.Rotation())
op("Seat link: Placement dialog, axis X, angle -5, centre (0,200,430)")
seat_l.Placement = App.Placement(V(0, 0, 0), App.Rotation(V(1, 0, 0), -5), V(0, 200, 430))
doc.recompute()
op("Assembly_SolveAssembly")
asm.solve()
doc.recompute()

parts = {"Seat": seat_l, "LegLeft": legL_l, "LegRight": legR_l}
for k, l in parts.items():
    log(f"    {k} link placement: {l.Placement}")

# =========================================================================== save + export
stage("export")
op("File > Save As chair.FCStd")
doc.saveAs(os.path.join(OUT, "chair.FCStd"))

op("Select Assembly, File > Export > STEP (chair.step)")
Import.export([asm], os.path.join(OUT, "chair.step"))


def world_shape(link):
    s = link.LinkedObject.Shape.copy()
    s.Placement = link.Placement.multiply(s.Placement)
    return s


LIN, ANG = 0.05, 0.2
STL_FILES = {}


def write_stl(shapes, fname):
    comp = Part.makeCompound(shapes)
    m = MeshPart.meshFromShape(Shape=comp, LinearDeflection=LIN, AngularDeflection=ANG, Relative=False)
    path = os.path.join(OUT, fname)
    m.write(path)
    STL_FILES[fname] = m.CountFacets
    return m


op("Mesh_FromPartShape (Standard: surface dev 0.05, angular dev 0.2 rad = 11.459 deg, relative off) "
   "for the 3 parts + Mesh_Export x4 (chair, seat, leg_left, leg_right)")
write_stl([world_shape(l) for l in parts.values()], "chair.stl")
write_stl([world_shape(seat_l)], "seat.stl")
write_stl([world_shape(legL_l)], "leg_left.stl")
write_stl([world_shape(legR_l)], "leg_right.stl")

op("Select Assembly, File > Export > STL mesh (chair_default.stl), default settings")
Mesh.export([asm], os.path.join(OUT, "chair_default.stl"))
stage("done")

# =========================================================================== report
TOTAL = time.perf_counter() - T0
log("OPS_BEGIN")
for i, (st, d) in enumerate(OPS, 1):
    log(f"{i:3d} [{st}] {d}")
log("OPS_END")
log(f"OP_COUNT {len(OPS)} (top-level GUI commands: {sum(1 for _, d in OPS if not d.startswith('  '))}, "
    f"in-sketch actions: {sum(1 for _, d in OPS if d.startswith('  '))})")
log(f"STAGE_TIMES {dict((k, round(v, 4)) for k, v in STAGE_T.items())}")
log(f"STL_FACETS {STL_FILES}")
log(f"VOLUMES_A {vol_A}")
log(f"VOLUMES_B {vol_B}")
log(f"VOLUMES_C {vol_C}")
log(f"IMPORTS {T_IMPORTS:.4f}")
log(f"SCRIPT_WALL {TOTAL:.4f}")
