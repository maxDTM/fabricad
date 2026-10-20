# SPDX-License-Identifier: LGPL-2.1-or-later
"""Check the FreeCAD chair exports against docs/comparison/CHAIR_SPEC.md.

Run: freecadcmd verify_freecad.py   (env CHAIR_OUT overrides the out/ directory)
"""
import os
import re
import json
import struct

import FreeCAD as App
import Part
import Mesh
import MeshPart
import Import

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.environ.get("CHAIR_OUT", os.path.join(HERE, "out"))

REF_VOL = {"Seat": 8565976.0, "LegLeft": 1718632.0, "LegRight": 1716808.0}
# exact analytic values (spec rounds Seat / LegRight to integers)
REF_BBOX = {
    "Seat": ((-210, 210), (-198.478, 235.734), (430.000, 840.619)),
    "LegLeft": ((-205, -15), (-180, 180), (0, 430)),
    "LegRight": ((15, 205), (-180, 180), (0, 430)),
}
VOL_TOL = 0.0005   # 0.05 %
BB_TOL = 0.01
problems = []
result = {"step": {}, "stl": {}, "fcstd": {}}


def classify(bb):
    if bb.ZMax > 435:
        return "Seat"
    return "LegLeft" if bb.Center.x < 0 else "LegRight"


def bbox_tuple(bb):
    return ((bb.XMin, bb.XMax), (bb.YMin, bb.YMax), (bb.ZMin, bb.ZMax))


def check(name, vol, bb, where):
    rv = REF_VOL[name]
    dv = (vol - rv) / rv
    ent = {"volume": vol, "ref_volume": rv, "vol_dev_pct": dv * 100,
           "bbox": bbox_tuple(bb), "ref_bbox": REF_BBOX[name]}
    if abs(dv) > VOL_TOL:
        problems.append(f"{where} {name}: volume {vol:.3f} deviates {dv*100:.4f}% from {rv}")
    worst = 0.0
    for (a, b), (ra, rb) in zip(bbox_tuple(bb), REF_BBOX[name]):
        worst = max(worst, abs(a - ra), abs(b - rb))
    ent["bbox_max_dev"] = worst
    if worst > BB_TOL:
        problems.append(f"{where} {name}: bbox deviates by {worst:.4f} mm")
    return ent


# ------------------------------------------------------------------ STEP (B-rep)
step = os.path.join(OUT, "chair.step")
shape = Part.read(step)
solids = shape.Solids
result["step"]["solid_count"] = len(solids)
for s in solids:
    n = classify(s.BoundBox)
    # Shape.BoundBox is OCCT's fast (non-optimal) box: on the rotated seat it is padded by the
    # fillet/torus control polygons (X +-211.648). The spec compares the true extent.
    obb = s.optimalBoundingBox(False, False)
    result["step"][n] = check(n, s.Volume, obb, "STEP")
    result["step"][n]["fast_BoundBox"] = bbox_tuple(s.BoundBox)
    result["step"][n]["valid"] = s.isValid()
txt = open(step, encoding="latin-1").read()
result["step"]["product_names"] = sorted(set(re.findall(r"PRODUCT\('([^']*)'", txt)))
result["step"]["header_app"] = re.findall(r"FILE_NAME\([^;]*;", txt, re.S)[0][:300]
# assembly structure as FreeCAD's own importer sees it
d = App.newDocument("stepcheck")
Import.insert(step, d.Name)
result["step"]["imported_objects"] = [(o.Label, o.TypeId) for o in d.Objects]
App.closeDocument(d.Name)


# ------------------------------------------------------------------ STL
def stl_info(path):
    raw = open(path, "rb").read()
    info = {"bytes": len(raw)}
    binary = False
    if len(raw) >= 84:
        n = struct.unpack("<I", raw[80:84])[0]
        binary = (84 + 50 * n == len(raw))
        info["header"] = raw[:80].rstrip(b"\0 ").decode("latin-1", "replace")
    info["binary"] = binary
    m = Mesh.Mesh(path)
    info["facets"] = m.CountFacets
    info["points"] = m.CountPoints
    bb = m.BoundBox
    info["bbox"] = bbox_tuple(bb)
    info["volume"] = m.Volume
    info["solid"] = m.isSolid()
    info["non_manifold"] = m.hasNonManifolds()
    info["self_intersections"] = m.hasSelfIntersections()
    return info, m


for f in ("chair.stl", "seat.stl", "leg_left.stl", "leg_right.stl", "chair_default.stl"):
    p = os.path.join(OUT, f)
    info, m = stl_info(p)
    if f != "chair_default.stl" and not info["binary"]:
        problems.append(f"{f} is not binary STL")
    single = {"seat.stl": "Seat", "leg_left.stl": "LegLeft", "leg_right.stl": "LegRight"}.get(f)
    if single:
        rv = REF_VOL[single]
        info["mesh_vol_dev_pct"] = (info["volume"] - rv) / rv * 100
        bbw = max(max(abs(a - ra), abs(b - rb)) for (a, b), (ra, rb) in zip(info["bbox"], REF_BBOX[single]))
        info["bbox_max_dev"] = bbw
    result["stl"][f] = info

# Infer FreeCAD's default (Mesh.export) tessellation: compare chair_default.stl against
# MeshPart.meshFromShape on the same shapes with candidate settings.
compound = Part.makeCompound(solids)
cands = {}
for lin, ang in ((0.1, 0.1), (0.1, 0.5), (0.1, 0.2), (0.05, 0.2)):
    mm = MeshPart.meshFromShape(Shape=compound.copy(), LinearDeflection=lin, AngularDeflection=ang, Relative=False)
    cands[f"lin={lin},ang={ang}"] = mm.CountFacets
result["stl"]["default_candidates_facets"] = cands
p = App.ParamGet("User parameter:BaseApp/Preferences/Mod/Mesh")
result["stl"]["pref_MaxDeviationExport"] = p.GetFloat("MaxDeviationExport", -1.0)

# ------------------------------------------------------------------ FCStd reopen
fc = os.path.join(OUT, "chair.FCStd")
d = App.openDocument(fc)
d.recompute()
errs = [o.Name for o in d.Objects if "Invalid" in o.State or "Error" in str(o.State)]
result["fcstd"]["objects"] = len(d.Objects)
result["fcstd"]["invalid_after_recompute"] = errs
for lbl in ("Seat", "LegLeft", "LegRight"):
    b = d.getObjectsByLabel(lbl)
    bodies = [o for o in b if o.TypeId == "PartDesign::Body"]
    if bodies:
        result["fcstd"][lbl] = bodies[0].Shape.Volume
if errs:
    problems.append(f"FCStd reopen: objects in error {errs}")
App.closeDocument(d.Name)

result["problems"] = problems
print("VERIFY_JSON " + json.dumps(result, default=str), flush=True)
print("VERIFY_PROBLEMS", problems if problems else "none", flush=True)
