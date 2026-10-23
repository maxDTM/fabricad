# Validate the outputs of tests/scripts/export_formats.fcs with independent readers (P3 interop log).
# Usage: python -m venv v && v/bin/pip install ezdxf lib3mf trimesh networkx lxml pygltflib
#        v/bin/python -I docs/tests/interop/check_exports.py <script-out dir>
import sys, os
d = sys.argv[1]
import ezdxf
from ezdxf import recover
from ezdxf import bbox
for f in ["plate_top.dxf", "plate_top_r12.dxf", "plate_sketch.dxf"]:
    doc, auditor = recover.readfile(os.path.join(d, f))
    a = doc.audit()
    msp = doc.modelspace()
    ext = bbox.extents(msp)
    print(f, doc.dxfversion, "insunits", doc.header.get("$INSUNITS"), "entities", [e.dxftype()+"@"+e.dxf.layer for e in msp],
          "size %.4f x %.4f" % (ext.size.x, ext.size.y), "audit errors", len(a.errors), "fixes", len(a.fixes), "recover errors", len(auditor.errors))
import lib3mf
w = lib3mf.get_wrapper()
m = w.CreateModel()
r = m.QueryReader("3mf")
r.ReadFromFile(os.path.join(d, "plate.3mf"))
it = m.GetMeshObjects()
while it.MoveNext():
    o = it.GetCurrentMeshObject()
    print("3mf object", o.GetName(), "verts", o.GetVertexCount(), "tris", o.GetTriangleCount(), "manifold", o.IsManifoldAndOriented())
print("3mf warnings", r.GetWarningCount(), "unit", m.GetUnit())
import trimesh
for f in ["plate.3mf", "plate.obj", "plate.glb", "plate.stl"]:
    s = trimesh.load(os.path.join(d, f), force="mesh")
    print(f, "faces", len(s.faces), "watertight", s.is_watertight, "extents", [round(x, 4) for x in s.extents])
from pygltflib import GLTF2
g = GLTF2().load(os.path.join(d, "plate.gltf"))
print("gltf nodes", [n.name for n in g.nodes], "materials", [m.pbrMetallicRoughness.baseColorFactor for m in g.materials], "asset", g.asset.version)
