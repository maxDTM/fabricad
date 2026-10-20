// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/Naming.h"

#include "core/Document.h"
#include "core/ShapeUtils.h"

#include <BRepBuilderAPI_MakeShape.hxx>
#include <BRepTools_History.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <cmath>
#include <format>

namespace fabricad::part {

namespace {

bool present(const TopTools_IndexedMapOfShape& faces, const TopTools_IndexedMapOfShape& edges,
             const TopTools_IndexedMapOfShape& vertices, const TopoDS_Shape& s) {
  switch (s.ShapeType()) {
    case TopAbs_FACE: return faces.Contains(s);
    case TopAbs_EDGE: return edges.Contains(s);
    case TopAbs_VERTEX: return vertices.Contains(s);
    default: return false;
  }
}

void mapAll(const TopoDS_Shape& result, TopTools_IndexedMapOfShape& faces, TopTools_IndexedMapOfShape& edges,
            TopTools_IndexedMapOfShape& vertices) {
  if (result.IsNull()) return;
  TopExp::MapShapes(result, TopAbs_FACE, faces);
  TopExp::MapShapes(result, TopAbs_EDGE, edges);
  TopExp::MapShapes(result, TopAbs_VERTEX, vertices);
}

}  // namespace

bool shapeLess(const TopoDS_Shape& a, const TopoDS_Shape& b) {
  core::Vec3 ca = core::centroid(a), cb = core::centroid(b);
  const double eps = 1e-6;
  if (std::abs(ca.x - cb.x) > eps) return ca.x < cb.x;
  if (std::abs(ca.y - cb.y) > eps) return ca.y < cb.y;
  if (std::abs(ca.z - cb.z) > eps) return ca.z < cb.z;
  double sa = a.ShapeType() == TopAbs_FACE ? core::area(a) : core::bboxDiagonal(a);
  double sb = b.ShapeType() == TopAbs_FACE ? core::area(b) : core::bboxDiagonal(b);
  return sa < sb;
}

std::vector<TopoDS_Shape> sortedShapes(const TopTools_ListOfShape& list) {
  std::vector<TopoDS_Shape> out;
  for (const auto& s : list) out.push_back(s);
  std::sort(out.begin(), out.end(), shapeLess);
  return out;
}

History History::of(BRepBuilderAPI_MakeShape& maker) {
  History h;
  BRepBuilderAPI_MakeShape* m = &maker;
  h.modified = [m](const TopoDS_Shape& s) {
    try {
      return m->Modified(s);
    } catch (...) {
      return TopTools_ListOfShape();
    }
  };
  h.generated = [m](const TopoDS_Shape& s) {
    try {
      return m->Generated(s);
    } catch (...) {
      return TopTools_ListOfShape();
    }
  };
  h.isDeleted = [m](const TopoDS_Shape& s) {
    try {
      return m->IsDeleted(s) == Standard_True;
    } catch (...) {
      return false;
    }
  };
  return h;
}

History History::of(const Handle(BRepTools_History)& history) {
  History h;
  if (history.IsNull()) return none();
  h.modified = [history](const TopoDS_Shape& s) {
    return BRepTools_History::IsSupportedType(s) ? history->Modified(s) : TopTools_ListOfShape();
  };
  h.generated = [history](const TopoDS_Shape& s) {
    return BRepTools_History::IsSupportedType(s) ? history->Generated(s) : TopTools_ListOfShape();
  };
  h.isDeleted = [history](const TopoDS_Shape& s) {
    return BRepTools_History::IsSupportedType(s) && history->IsRemoved(s);
  };
  return h;
}

History History::none() {
  History h;
  h.modified = [](const TopoDS_Shape&) { return TopTools_ListOfShape(); };
  h.generated = [](const TopoDS_Shape&) { return TopTools_ListOfShape(); };
  h.isDeleted = [](const TopoDS_Shape&) { return false; };
  return h;
}

NameMap mapNames(const NameMap& names, const History& history, const TopoDS_Shape& result) {
  NameMap out;
  if (result.IsNull()) return out;
  TopTools_IndexedMapOfShape faces, edges, vertices;
  mapAll(result, faces, edges, vertices);
  for (const auto& [name, shape] : names) {
    if (shape.IsNull()) continue;
    if (history.isDeleted && history.isDeleted(shape)) continue;
    TopTools_ListOfShape mod = history.modified ? history.modified(shape) : TopTools_ListOfShape();
    if (mod.IsEmpty()) {
      if (present(faces, edges, vertices, shape)) out[name] = shape;
      continue;
    }
    std::vector<TopoDS_Shape> list = sortedShapes(mod);
    std::vector<TopoDS_Shape> kept;
    for (const auto& s : list)
      if (present(faces, edges, vertices, s)) kept.push_back(s);
    if (kept.empty()) continue;
    if (kept.size() == 1) {
      out[name] = kept.front();
    } else {
      for (size_t k = 0; k < kept.size(); ++k) out[std::format("{}#{}", name, k + 1)] = kept[k];
    }
  }
  return out;
}

void addEdgeNames(NameMap& names, const TopoDS_Shape& result) {
  if (result.IsNull()) return;
  // face -> name (faces only), hashed: patterns produce thousands of edges
  TopTools_IndexedMapOfShape faceMap;
  std::vector<std::string> faceNames;
  for (const auto& [n, s] : names) {
    if (s.ShapeType() != TopAbs_FACE) continue;
    int before = faceMap.Extent();
    int idx = faceMap.Add(s);
    if (idx > before) faceNames.push_back(n);  // first name wins (map order is deterministic)
  }
  if (faceNames.empty()) return;
  auto faceName = [&](const TopoDS_Shape& f) -> std::string {
    int idx = faceMap.FindIndex(f);
    return idx > 0 ? faceNames[static_cast<size_t>(idx) - 1] : std::string();
  };
  // already named edges
  TopTools_IndexedMapOfShape namedEdges;
  for (const auto& [n, s] : names)
    if (s.ShapeType() == TopAbs_EDGE) namedEdges.Add(s);

  TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
  TopExp::MapShapesAndAncestors(result, TopAbs_EDGE, TopAbs_FACE, edgeFaces);
  std::map<std::string, std::vector<TopoDS_Shape>> candidates;
  for (int i = 1; i <= edgeFaces.Extent(); ++i) {
    const TopoDS_Shape& edge = edgeFaces.FindKey(i);
    if (namedEdges.Contains(edge)) continue;
    const TopTools_ListOfShape& fl = edgeFaces(i);
    std::vector<std::string> fn;
    for (const auto& f : fl) {
      std::string n = faceName(f);
      if (n.empty()) {
        fn.clear();
        break;
      }
      fn.push_back(n);
    }
    if (fn.size() != 2) continue;
    std::sort(fn.begin(), fn.end());
    candidates[std::format("{{{}|{}}}", fn[0], fn[1])].push_back(edge);
  }
  for (auto& [key, list] : candidates) {
    if (list.size() == 1) {
      names[key] = list.front();
    } else {
      std::sort(list.begin(), list.end(), shapeLess);
      for (size_t k = 0; k < list.size(); ++k) names[std::format("{}#{}", key, k + 1)] = list[k];
    }
  }
}

void pruneNames(NameMap& names, const TopoDS_Shape& shape) {
  TopTools_IndexedMapOfShape faces, edges, vertices;
  mapAll(shape, faces, edges, vertices);
  for (auto it = names.begin(); it != names.end();) {
    if (it->second.IsNull() || !present(faces, edges, vertices, it->second)) it = names.erase(it);
    else ++it;
  }
}

std::string nameOf(const NameMap& names, const TopoDS_Shape& sub) {
  if (sub.IsNull()) return {};
  for (const auto& [n, s] : names)
    if (s.IsSame(sub)) return n;
  return {};
}

namespace {

/// Latest feature before `beforeIndex` whose touched/created bodies include bodyId.
const core::Feature* lastToucher(const core::Document& doc, core::ObjectId bodyId, int beforeIndex) {
  const auto& feats = doc.features();
  int n = static_cast<int>(feats.size());
  if (beforeIndex < 0 || beforeIndex > n) beforeIndex = n;
  for (int i = beforeIndex - 1; i >= 0; --i) {
    const core::Feature* f = feats[static_cast<size_t>(i)].get();
    const auto& t = f->touchedBodies();
    const auto& c = f->createdBodies();
    if (std::find(t.begin(), t.end(), bodyId) != t.end() || std::find(c.begin(), c.end(), bodyId) != c.end()) return f;
  }
  return nullptr;
}

}  // namespace

NameMap upstreamNames(const core::RecomputeContext& ctx, core::ObjectId bodyId) {
  const core::Document& doc = ctx.document();
  int idx = doc.indexOf(const_cast<core::RecomputeContext&>(ctx).self().id());
  const core::Feature* f = lastToucher(doc, bodyId, idx < 0 ? doc.rollbackIndex() : idx);
  if (!f) return {};
  NameMap names = f->namedShapes();
  pruneNames(names, ctx.bodyShape(bodyId));
  return names;
}

NameMap currentNames(const core::Document& doc, core::ObjectId bodyId) {
  const core::Feature* f = lastToucher(doc, bodyId, doc.rollbackIndex());
  if (!f) return {};
  NameMap names = f->namedShapes();
  pruneNames(names, doc.bodyShape(bodyId));
  return names;
}

TopoDS_Shape resolveRef(core::RecomputeContext& ctx, core::ShapeRef& ref) {
  if (!ref.isBrep() || ref.kind == core::ShapeKind::Body) return ctx.resolve(ref);
  NameMap names = upstreamNames(ctx, ref.owner);
  const TopoDS_Shape& body = ctx.bodyShape(ref.owner);
  if (!ref.name.empty() && !body.IsNull()) {
    auto it = names.find(ref.name);
    if (it != names.end() && it->second.ShapeType() == core::toTopAbs(ref.kind)) {
      int idx = core::indexOfSubShape(body, it->second, it->second.ShapeType());
      if (idx > 0) {
        ref.index = idx;
        ref.signature = core::computeSignature(it->second);
        return it->second;
      }
    }
  }
  TopoDS_Shape s = ctx.resolve(ref);
  if (!s.IsNull()) {
    std::string n = nameOf(names, s);
    if (!n.empty()) ref.name = n;
  }
  return s;
}

TopoDS_Shape resolveRefProperty(core::RecomputeContext& ctx, core::Property& p) {
  if (p.type() != core::PropertyType::Ref) return {};
  core::ShapeRef r = p.ref();
  if (!r.isValid()) return {};
  TopoDS_Shape s = resolveRef(ctx, r);
  p.setRef(r);
  return s;
}

void annotateRef(const core::Document& doc, core::ShapeRef& ref) {
  if (!ref.isBrep() || ref.kind == core::ShapeKind::Body) return;
  const TopoDS_Shape& body = doc.bodyShape(ref.owner);
  if (body.IsNull()) return;
  TopoDS_Shape sub = core::subShapeAt(body, core::toTopAbs(ref.kind), ref.index);
  if (sub.IsNull()) return;
  if (!ref.signature.isValid()) {
    ref.signature = core::computeSignature(sub);
    if (!ref.hasPoint) {
      ref.point = ref.signature.center;
      ref.hasPoint = true;
    }
  }
  std::string n = nameOf(currentNames(doc, ref.owner), sub);
  if (!n.empty()) ref.name = n;
}

}  // namespace fabricad::part
