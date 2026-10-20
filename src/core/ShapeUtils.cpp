// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/ShapeUtils.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax1.hxx>

#include <functional>

namespace fabricad::core {

bool isValidShape(const TopoDS_Shape& s) {
  if (s.IsNull()) return false;
  try {
    BRepCheck_Analyzer an(s);
    return an.IsValid();
  } catch (const Standard_Failure&) {
    return false;
  }
}

bool bbox(const TopoDS_Shape& s, Vec3& min, Vec3& max) {
  if (s.IsNull()) return false;
  Bnd_Box box;
  BRepBndLib::Add(s, box, false);
  if (box.IsVoid()) return false;
  double x0, y0, z0, x1, y1, z1;
  box.Get(x0, y0, z0, x1, y1, z1);
  min = {x0, y0, z0};
  max = {x1, y1, z1};
  return true;
}

double bboxDiagonal(const TopoDS_Shape& s) {
  Vec3 a, b;
  if (!bbox(s, a, b)) return 0.0;
  return (b - a).length();
}

TopoDS_Shape unifySameDomain(const TopoDS_Shape& s, bool unifyEdges, bool unifyFaces) {
  if (s.IsNull()) return s;
  try {
    ShapeUpgrade_UnifySameDomain u(s, unifyEdges, unifyFaces, false);
    u.Build();
    TopoDS_Shape r = u.Shape();
    return r.IsNull() ? s : r;
  } catch (const Standard_Failure&) {
    return s;
  }
}

TopoDS_Shape transformed(const TopoDS_Shape& s, const gp_Trsf& t) {
  if (s.IsNull()) return s;
  BRepBuilderAPI_Transform tr(s, t, false);
  return tr.Shape();
}

double volume(const TopoDS_Shape& s) {
  if (s.IsNull()) return 0.0;
  GProp_GProps p;
  BRepGProp::VolumeProperties(s, p);
  return p.Mass();
}

double area(const TopoDS_Shape& s) {
  if (s.IsNull()) return 0.0;
  GProp_GProps p;
  BRepGProp::SurfaceProperties(s, p);
  return p.Mass();
}

Vec3 centroid(const TopoDS_Shape& s) {
  if (s.IsNull()) return {};
  GProp_GProps p;
  switch (s.ShapeType()) {
    case TopAbs_SOLID:
    case TopAbs_COMPSOLID:
    case TopAbs_COMPOUND: BRepGProp::VolumeProperties(s, p); break;
    case TopAbs_FACE:
    case TopAbs_SHELL: BRepGProp::SurfaceProperties(s, p); break;
    default: BRepGProp::LinearProperties(s, p); break;
  }
  if (p.Mass() <= 0) {
    Vec3 a, b;
    if (bbox(s, a, b)) return (a + b) * 0.5;
    return {};
  }
  return Vec3::from(p.CentreOfMass());
}

std::vector<TopoDS_Shape> subShapes(const TopoDS_Shape& s, TopAbs_ShapeEnum type) {
  std::vector<TopoDS_Shape> out;
  if (s.IsNull()) return out;
  TopTools_IndexedMapOfShape map;
  TopExp::MapShapes(s, type, map);
  out.reserve(map.Extent());
  for (int i = 1; i <= map.Extent(); ++i) out.push_back(map(i));
  return out;
}

TopoDS_Face planeFace(const gp_Ax3& plane, double halfSize) {
  gp_Pln pln(plane);
  return BRepBuilderAPI_MakeFace(pln, -halfSize, halfSize, -halfSize, halfSize).Face();
}

std::optional<gp_Ax3> facePlane(const TopoDS_Shape& face) {
  if (face.IsNull() || face.ShapeType() != TopAbs_FACE) return std::nullopt;
  BRepAdaptor_Surface surf(TopoDS::Face(face), false);
  if (surf.GetType() != GeomAbs_Plane) return std::nullopt;
  gp_Pln pln = surf.Plane();
  gp_Ax3 ax = pln.Position();
  if (face.Orientation() == TopAbs_REVERSED) {
    gp_Dir n = ax.Direction().Reversed();
    ax = gp_Ax3(ax.Location(), n, ax.XDirection());
  }
  // Origin: the world origin projected onto the plane. Unlike the face centroid this does not
  // move when the face changes shape (a sketch on the face keeps its coordinates after an
  // upstream edit), and the X direction comes from the plane itself (stable for prism faces).
  gp_Pnt o(0, 0, 0);
  gp_Vec off(ax.Location(), o);
  double h = off.Dot(gp_Vec(ax.Direction()));
  gp_Pnt onPlane = o.Translated(gp_Vec(ax.Direction()) * -h);
  ax.SetLocation(onPlane);
  return ax;
}

TopoDS_Shape makeCompound(const std::vector<TopoDS_Shape>& shapes) {
  TopoDS_Compound comp;
  BRep_Builder b;
  b.MakeCompound(comp);
  int n = 0;
  for (const auto& s : shapes) {
    if (s.IsNull()) continue;
    b.Add(comp, s);
    ++n;
  }
  if (n == 0) return {};
  return comp;
}

std::size_t shapeHash(const TopoDS_Shape& s) {
  if (s.IsNull()) return 0;
  std::size_t h = std::hash<const void*>()(s.TShape().get());
  h ^= std::hash<int>()(static_cast<int>(s.Orientation())) + 0x9e3779b9;
  const gp_Trsf& t = s.Location().Transformation();
  Vec3 tr = Vec3::from(t.TranslationPart());
  h ^= std::hash<double>()(tr.x) * 31 + std::hash<double>()(tr.y) * 37 + std::hash<double>()(tr.z) * 41;
  return h;
}

std::string shapeTypeName(const TopoDS_Shape& s) {
  if (s.IsNull()) return "Null";
  switch (s.ShapeType()) {
    case TopAbs_COMPOUND: return "Compound";
    case TopAbs_COMPSOLID: return "CompSolid";
    case TopAbs_SOLID: return "Solid";
    case TopAbs_SHELL: return "Shell";
    case TopAbs_FACE: return "Face";
    case TopAbs_WIRE: return "Wire";
    case TopAbs_EDGE: return "Edge";
    case TopAbs_VERTEX: return "Vertex";
    default: return "Shape";
  }
}

gp_Ax3 originPlane(const std::string& name) {
  if (name == "XZ") return gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, -1, 0), gp_Dir(1, 0, 0));
  if (name == "YZ") return gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0));
  return gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
}

gp_Ax1 originAxis(const std::string& name) {
  if (name == "X") return gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0));
  if (name == "Y") return gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0));
  return gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1));
}

}  // namespace fabricad::core
