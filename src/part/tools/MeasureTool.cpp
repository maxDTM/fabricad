// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/tools/MeasureTool.h"

#include "core/AppHost.h"
#include "core/Document.h"
#include "core/GeometryRefs.h"
#include "core/Selection.h"
#include "core/ShapeUtils.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Pln.hxx>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <functional>
#include <numbers>

namespace fabricad::part {

using core::Document;
using core::ObjectId;
using core::ShapeKind;
using core::ShapeRef;
using core::ToolContext;
using core::ToolInfo;
using core::Vec3;

namespace {

ToolInfo measureInfo() {
  return ToolInfo{.id = "inspect.measure",
                  .name = "Measure",
                  .tooltip = "Distance, angle, length, radius, area, volume and mass of one or two entities",
                  .icon = "inspect-measure",
                  .shortcut = "I",
                  .aliases = {"MEASURE", "MEA", "DIST", "DISTANCE", "INSPECT"},
                  .tab = "SOLID",
                  .group = "INSPECT",
                  .order = 1,
                  .promoted = true,
                  .repeatable = false};
}

std::string fmt(double v, int prec = 3) {
  std::string s = std::format("{:.{}f}", v, prec);
  if (s == "-0" || s.rfind("-0.", 0) == 0) {
    bool allZero = true;
    for (char c : s)
      if (c != '-' && c != '0' && c != '.') allZero = false;
    if (allZero) s.erase(0, 1);
  }
  return s;
}

std::string lengthText(double mm, const core::UnitSystem& u) {
  return core::formatQuantity(core::Quantity{mm, core::Dimension::Length()}, u, true);
}
std::string angleText(double deg, const core::UnitSystem& u) {
  return core::formatQuantity(core::Quantity{deg, core::Dimension::Angle()}, u, true);
}
std::string areaText(double mm2, const core::UnitSystem& u) {
  double f = core::toDisplayValue(core::Quantity{1.0, core::Dimension::Length()}, u);
  std::string unit = core::lengthUnitSymbol(u.length);
  return fmt(mm2 * f * f, u.precision) + " " + unit + "2";
}
std::string volumeText(double mm3, const core::UnitSystem& u) {
  double f = core::toDisplayValue(core::Quantity{1.0, core::Dimension::Length()}, u);
  std::string unit = core::lengthUnitSymbol(u.length);
  return fmt(mm3 * f * f * f, u.precision) + " " + unit + "3";
}
std::string pointText(const gp_Pnt& p, const core::UnitSystem& u) {
  double f = core::toDisplayValue(core::Quantity{1.0, core::Dimension::Length()}, u);
  return std::format("({}, {}, {}) {}", fmt(p.X() * f, u.precision), fmt(p.Y() * f, u.precision), fmt(p.Z() * f, u.precision),
                     core::lengthUnitSymbol(u.length));
}
std::string dirText(const gp_Dir& d) { return std::format("({}, {}, {})", fmt(d.X(), 4), fmt(d.Y(), 4), fmt(d.Z(), 4)); }

void addRow(MeasureResult& r, std::string label, std::string value, double number) {
  r.rows.push_back({std::move(label), std::move(value), number});
}

/// Owning component of a reference (for the world placement).
ObjectId componentOf(const Document& doc, const ShapeRef& ref) {
  if (ref.kind == ShapeKind::Component) return ref.owner;
  if (ref.isBrep()) {
    const core::Body* b = doc.body(ref.owner);
    return b ? b->component : core::kRootComponentId;
  }
  if (const core::Feature* f = doc.feature(ref.owner)) return f->component();
  return core::kRootComponentId;
}

TopoDS_Shape componentShape(const Document& doc, ObjectId compId) {
  std::vector<TopoDS_Shape> parts;
  std::function<void(ObjectId, const gp_Trsf&)> add = [&](ObjectId id, const gp_Trsf& world) {
    const core::Component* c = doc.component(id);
    if (!c) return;
    ObjectId def = c->isOccurrence() ? c->definition : id;
    for (ObjectId b : doc.bodiesOf(def)) {
      const TopoDS_Shape& s = doc.bodyShape(b);
      if (!s.IsNull()) parts.push_back(core::transformed(s, world));
    }
    if (const core::Component* d = doc.component(def))
      for (ObjectId child : d->children)
        if (const core::Component* cc = doc.component(child)) {
          gp_Trsf w = world;
          w.Multiply(cc->placement);
          add(child, w);
        }
  };
  add(compId, doc.worldPlacement(compId));
  if (parts.empty()) return {};
  return parts.size() == 1 ? parts.front() : core::makeCompound(parts);
}

struct EdgeInfo {
  double length = 0;
  bool isLine = false, isCircle = false;
  gp_Pnt start, end, center;
  gp_Dir direction{0, 0, 1};
  double radius = 0;
};

EdgeInfo edgeInfo(const TopoDS_Edge& e) {
  EdgeInfo i;
  BRepAdaptor_Curve c(e);
  GProp_GProps props;
  BRepGProp::LinearProperties(e, props);
  i.length = props.Mass();
  i.start = c.Value(c.FirstParameter());
  i.end = c.Value(c.LastParameter());
  if (c.GetType() == GeomAbs_Line) {
    i.isLine = true;
    gp_Vec v(i.start, i.end);
    if (v.Magnitude() > 1e-12) i.direction = gp_Dir(v);
  } else if (c.GetType() == GeomAbs_Circle) {
    i.isCircle = true;
    gp_Circ circ = c.Circle();
    i.center = circ.Location();
    i.radius = circ.Radius();
    i.direction = circ.Axis().Direction();
  }
  return i;
}

struct FaceInfo {
  double area = 0, perimeter = 0;
  gp_Pnt centroid;
  bool isPlane = false, isCylinder = false, isSphere = false;
  gp_Dir normal{0, 0, 1};
  gp_Pnt axisPoint;
  double radius = 0;
};

FaceInfo faceInfo(const TopoDS_Face& f) {
  FaceInfo i;
  GProp_GProps props;
  BRepGProp::SurfaceProperties(f, props);
  i.area = props.Mass();
  i.centroid = props.CentreOfMass();
  for (TopExp_Explorer ex(f, TopAbs_EDGE); ex.More(); ex.Next()) {
    GProp_GProps lp;
    BRepGProp::LinearProperties(ex.Current(), lp);
    i.perimeter += lp.Mass();
  }
  BRepAdaptor_Surface s(f);
  if (s.GetType() == GeomAbs_Plane) {
    i.isPlane = true;
    gp_Pln pln = s.Plane();
    gp_Dir n = pln.Axis().Direction();
    if (f.Orientation() == TopAbs_REVERSED) n.Reverse();
    i.normal = n;
  } else if (s.GetType() == GeomAbs_Cylinder) {
    i.isCylinder = true;
    gp_Cylinder cyl = s.Cylinder();
    i.normal = cyl.Axis().Direction();
    i.axisPoint = cyl.Location();
    i.radius = cyl.Radius();
  } else if (s.GetType() == GeomAbs_Sphere) {
    i.isSphere = true;
    i.radius = s.Sphere().Radius();
    i.axisPoint = s.Sphere().Location();
  }
  return i;
}

std::string refLabel(const Document& doc, const ShapeRef& ref) {
  switch (ref.kind) {
    case ShapeKind::Vertex: return "Vertex";
    case ShapeKind::Edge: return "Edge";
    case ShapeKind::Face: return "Face";
    case ShapeKind::Body: {
      const core::Body* b = doc.body(ref.owner);
      return b ? b->name : "Body";
    }
    case ShapeKind::Component: {
      const core::Component* c = doc.component(ref.owner);
      return c ? c->name : "Component";
    }
    default: return core::shapeKindName(ref.kind);
  }
}

void measureSingle(const Document& doc, const ShapeRef& ref, const TopoDS_Shape& shape, const core::UnitSystem& u,
                   MeasureResult& r) {
  addRow(r, "Entity", refLabel(doc, ref), 0);
  const double deg = 180.0 / std::numbers::pi;
  (void)deg;
  switch (shape.ShapeType()) {
    case TopAbs_VERTEX: {
      gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(shape));
      addRow(r, "Position", pointText(p, u), 0);
      addRow(r, "X", lengthText(p.X(), u), p.X());
      addRow(r, "Y", lengthText(p.Y(), u), p.Y());
      addRow(r, "Z", lengthText(p.Z(), u), p.Z());
      r.summary = "Point " + pointText(p, u);
      break;
    }
    case TopAbs_EDGE: {
      EdgeInfo e = edgeInfo(TopoDS::Edge(shape));
      addRow(r, "Length", lengthText(e.length, u), e.length);
      if (e.isCircle) {
        addRow(r, "Radius", lengthText(e.radius, u), e.radius);
        addRow(r, "Diameter", lengthText(2 * e.radius, u), 2 * e.radius);
        addRow(r, "Centre", pointText(e.center, u), 0);
        addRow(r, "Axis", dirText(e.direction), 0);
        r.summary = std::format("Length {}, R {}", lengthText(e.length, u), lengthText(e.radius, u));
      } else {
        addRow(r, "Start", pointText(e.start, u), 0);
        addRow(r, "End", pointText(e.end, u), 0);
        if (e.isLine) {
          addRow(r, "Direction", dirText(e.direction), 0);
          gp_Vec d(e.start, e.end);
          addRow(r, "Delta X", lengthText(std::abs(d.X()), u), std::abs(d.X()));
          addRow(r, "Delta Y", lengthText(std::abs(d.Y()), u), std::abs(d.Y()));
          addRow(r, "Delta Z", lengthText(std::abs(d.Z()), u), std::abs(d.Z()));
        }
        r.summary = "Length " + lengthText(e.length, u);
      }
      break;
    }
    case TopAbs_FACE: {
      FaceInfo f = faceInfo(TopoDS::Face(shape));
      addRow(r, "Area", areaText(f.area, u), f.area);
      addRow(r, "Perimeter", lengthText(f.perimeter, u), f.perimeter);
      addRow(r, "Centroid", pointText(f.centroid, u), 0);
      if (f.isPlane) {
        addRow(r, "Type", "Plane", 0);
        addRow(r, "Normal", dirText(f.normal), 0);
      } else if (f.isCylinder) {
        addRow(r, "Type", "Cylinder", 0);
        addRow(r, "Radius", lengthText(f.radius, u), f.radius);
        addRow(r, "Diameter", lengthText(2 * f.radius, u), 2 * f.radius);
        addRow(r, "Axis", dirText(f.normal), 0);
      } else if (f.isSphere) {
        addRow(r, "Type", "Sphere", 0);
        addRow(r, "Radius", lengthText(f.radius, u), f.radius);
      }
      r.summary = "Area " + areaText(f.area, u);
      break;
    }
    default: {
      // body / component / compound: mass properties
      GProp_GProps vp;
      BRepGProp::VolumeProperties(shape, vp);
      double vol = vp.Mass();
      double area = core::area(shape);
      addRow(r, "Volume", volumeText(vol, u), vol);
      addRow(r, "Surface area", areaText(area, u), area);
      addRow(r, "Centre of mass", pointText(vp.CentreOfMass(), u), 0);
      Vec3 mn, mx;
      if (core::bbox(shape, mn, mx)) {
        Vec3 size = mx - mn;
        addRow(r, "Bounding box", std::format("{} x {} x {}", lengthText(size.x, u), lengthText(size.y, u), lengthText(size.z, u)), 0);
      }
      double density = 0;
      std::string material;
      if (ref.kind == ShapeKind::Body) {
        if (const core::Body* b = doc.body(ref.owner)) {
          density = b->density;
          material = b->material;
        }
      } else if (ref.kind == ShapeKind::Component) {
        // mass of a component: sum over its bodies with their own densities
        double mass = 0;
        bool any = false;
        std::function<void(ObjectId)> acc = [&](ObjectId id) {
          const core::Component* c = doc.component(id);
          if (!c) return;
          ObjectId def = c->isOccurrence() ? c->definition : id;
          for (ObjectId bid : doc.bodiesOf(def))
            if (const core::Body* b = doc.body(bid); b && b->density > 0) {
              mass += core::volume(doc.bodyShape(bid)) * b->density / 1000.0;
              any = true;
            }
          if (const core::Component* d = doc.component(def))
            for (ObjectId child : d->children) acc(child);
        };
        acc(ref.owner);
        if (any) addRow(r, "Mass", fmt(mass, u.precision) + " g", mass);
      }
      if (!material.empty()) addRow(r, "Material", material, 0);
      if (density > 0) {
        addRow(r, "Density", fmt(density, 3) + " g/cm3", density);
        double mass = vol * density / 1000.0;  // mm3 * g/cm3 -> g
        addRow(r, "Mass", fmt(mass, u.precision) + " g", mass);
      }
      int faces = 0, edges = 0;
      for (TopExp_Explorer ex(shape, TopAbs_FACE); ex.More(); ex.Next()) ++faces;
      for (TopExp_Explorer ex(shape, TopAbs_EDGE); ex.More(); ex.Next()) ++edges;
      addRow(r, "Faces / edges", std::format("{} / {}", faces, edges), 0);
      r.summary = std::format("Volume {}, area {}", volumeText(vol, u), areaText(area, u));
      break;
    }
  }
}

void measurePair(const Document& doc, const ShapeRef& ra, const ShapeRef& rb, const TopoDS_Shape& a, const TopoDS_Shape& b,
                 const core::UnitSystem& u, MeasureResult& r) {
  addRow(r, "Entities", refLabel(doc, ra) + " / " + refLabel(doc, rb), 0);
  try {
    BRepExtrema_DistShapeShape dist(a, b);
    if (dist.IsDone() && dist.NbSolution() > 0) {
      gp_Pnt p1 = dist.PointOnShape1(1), p2 = dist.PointOnShape2(1);
      double d = dist.Value();
      r.hasDistance = true;
      r.distance = d;
      r.p1 = Vec3::from(p1);
      r.p2 = Vec3::from(p2);
      addRow(r, "Distance", lengthText(d, u), d);
      gp_Vec delta(p1, p2);
      addRow(r, "Delta X", lengthText(std::abs(delta.X()), u), std::abs(delta.X()));
      addRow(r, "Delta Y", lengthText(std::abs(delta.Y()), u), std::abs(delta.Y()));
      addRow(r, "Delta Z", lengthText(std::abs(delta.Z()), u), std::abs(delta.Z()));
      r.summary = "Distance " + lengthText(d, u);
    }
  } catch (const Standard_Failure&) {
  }
  // directions for angles and centre distances
  auto axisOf = [](const TopoDS_Shape& s, gp_Dir& dir, gp_Pnt& centre, bool& isPlane) -> bool {
    isPlane = false;
    if (s.ShapeType() == TopAbs_EDGE) {
      EdgeInfo e = edgeInfo(TopoDS::Edge(s));
      if (e.isLine) {
        dir = e.direction;
        centre = e.start;
        return true;
      }
      if (e.isCircle) {
        dir = e.direction;
        centre = e.center;
        return true;
      }
      return false;
    }
    if (s.ShapeType() == TopAbs_FACE) {
      FaceInfo f = faceInfo(TopoDS::Face(s));
      if (f.isPlane) {
        dir = f.normal;
        centre = f.centroid;
        isPlane = true;
        return true;
      }
      if (f.isCylinder) {
        dir = f.normal;
        centre = f.axisPoint;
        return true;
      }
      if (f.isSphere) {
        centre = f.axisPoint;
        dir = gp_Dir(0, 0, 1);
        return false;
      }
    }
    return false;
  };
  gp_Dir da, db;
  gp_Pnt ca, cb;
  bool pa = false, pb = false;
  bool ha = axisOf(a, da, ca, pa), hb = axisOf(b, db, cb, pb);
  if (ha && hb) {
    double ang = da.Angle(db) * 180.0 / std::numbers::pi;
    // a line against a plane: angle between the line and the plane
    if (pa != pb) ang = std::abs(90.0 - ang);
    else if (ang > 90.0) ang = 180.0 - ang;  // unsigned between lines / between planes
    addRow(r, "Angle", angleText(ang, u), ang);
    bool circA = (a.ShapeType() == TopAbs_EDGE && edgeInfo(TopoDS::Edge(a)).isCircle) ||
                 (a.ShapeType() == TopAbs_FACE && faceInfo(TopoDS::Face(a)).isCylinder);
    bool circB = (b.ShapeType() == TopAbs_EDGE && edgeInfo(TopoDS::Edge(b)).isCircle) ||
                 (b.ShapeType() == TopAbs_FACE && faceInfo(TopoDS::Face(b)).isCylinder);
    if (circA && circB) {
      double cd = ca.Distance(cb);
      addRow(r, "Centre distance", lengthText(cd, u), cd);
    }
    if (pa && pb && ang < 1e-6) {
      double off = std::abs(gp_Vec(ca, cb).Dot(da));
      addRow(r, "Plane offset", lengthText(off, u), off);
    }
  }
  if (r.summary.empty()) r.summary = "No distance between these entities";
}

}  // namespace

const MeasureRow* MeasureResult::row(const std::string& label) const {
  for (const auto& r : rows)
    if (r.label == label) return &r;
  return nullptr;
}

TopoDS_Shape measureShapeOf(const Document& doc, const ShapeRef& ref) {
  if (!ref.isValid()) return {};
  if (ref.kind == ShapeKind::Component) return componentShape(doc, ref.owner);
  TopoDS_Shape s;
  if (ref.kind == ShapeKind::Body) s = doc.bodyShape(ref.owner);
  else s = core::resolveShape(doc, ref);
  if (s.IsNull()) {
    // origin / construction / sketch points and axes become vertices or edges
    if (auto p = core::resolvePoint(doc, ref)) return BRepBuilderAPI_MakeVertex(*p).Shape();
    return {};
  }
  gp_Trsf world = doc.worldPlacement(componentOf(doc, ref));
  if (world.Form() == gp_Identity) return s;
  return core::transformed(s, world);
}

MeasureResult measureRefs(const Document& doc, const std::vector<ShapeRef>& refs, const core::UnitSystem& units) {
  MeasureResult r;
  std::vector<TopoDS_Shape> shapes;
  for (const ShapeRef& ref : refs) {
    TopoDS_Shape s = measureShapeOf(doc, ref);
    if (s.IsNull()) continue;
    shapes.push_back(s);
  }
  if (shapes.empty()) return r;
  try {
    if (shapes.size() == 1) measureSingle(doc, refs.front(), shapes.front(), units, r);
    else measurePair(doc, refs[0], refs[1], shapes[0], shapes[1], units, r);
  } catch (const Standard_Failure& e) {
    r.rows.clear();
    r.summary = std::format("OCCT: {}", e.GetMessageString() ? e.GetMessageString() : "measure failed");
  }
  return r;
}

std::string measureText(const MeasureResult& r) {
  std::string out;
  for (const auto& row : r.rows) out += row.label + "\t" + row.value + "\n";
  return out;
}

// ---------------------------------------------------------------------------------------
// MeasureTool
// ---------------------------------------------------------------------------------------

MeasureTool::MeasureTool() : ToolBase(measureInfo()) {}

void MeasureTool::activate(ToolContext& ctx) {
  ctx_ = &ctx;
  picks_.clear();
  result_ = {};
  previews_.clear();
  if (!ctx.document) {
    ctx.cancel();
    return;
  }
  core::ShapeKindMask filter = core::kBrepKinds | ShapeKind::Component | core::kPointKinds | core::kAxisKinds |
                               core::kPlaneKinds | ShapeKind::SketchCurve | ShapeKind::SketchPoint;
  if (ctx.selection) ctx.selection->setFilter(filter);
  if (ctx.viewer) ctx.viewer->setSelectionFilter(filter);
  for (const ShapeRef& r : ctx.preselection)
    if (picks_.size() < 2 && !measureShapeOf(*ctx.document, r).IsNull()) picks_.push_back(r);
  if (ctx.selection) ctx.selection->clear();
  if (ctx.host) ctx.host->setToolAcceptEnabled(true);
  recompute(ctx);
}

void MeasureTool::deactivate(ToolContext& ctx) {
  if (ctx.viewer) {
    for (core::PreviewHandle h : previews_) ctx.viewer->removePreview(h);
    ctx.viewer->setHighlighted({});
  }
  previews_.clear();
  ctx_ = nullptr;
}

void MeasureTool::accept(ToolContext& ctx) { ctx.finish(); }

void MeasureTool::pick(ToolContext& ctx, const ShapeRef& ref) {
  if (!ctx.document || measureShapeOf(*ctx.document, ref).IsNull()) return;
  auto it = std::find(picks_.begin(), picks_.end(), ref);
  if (it != picks_.end()) picks_.erase(it);  // picking a selected entity again removes it
  else {
    if (picks_.size() >= 2) picks_.clear();  // third pick starts a new measurement (Fusion)
    picks_.push_back(ref);
  }
  recompute(ctx);
}

void MeasureTool::clearPicks(ToolContext& ctx) {
  picks_.clear();
  recompute(ctx);
}

void MeasureTool::selectionChanged(ToolContext& ctx) {
  if (!ctx.selection) return;
  std::vector<ShapeRef> items = ctx.selection->items();
  if (items.empty()) return;
  for (const ShapeRef& r : items) pick(ctx, r);
  ctx.selection->clear();
}

bool MeasureTool::commandLineInput(ToolContext& ctx, const std::string& text) {
  std::string t;
  for (char c : text) t += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  while (!t.empty() && t.back() == ' ') t.pop_back();
  if (t == "CLEAR" || t == "RESTART") {
    clearPicks(ctx);
    return true;
  }
  if (t == "COPY") {
    if (QClipboard* cb = QApplication::clipboard()) cb->setText(QString::fromStdString(measureText(result_)));
    ctx.showMessage("Measurement copied", false);
    return true;
  }
  return false;
}

void MeasureTool::recompute(ToolContext& ctx) {
  result_ = picks_.empty() ? MeasureResult{} : measureRefs(*ctx.document, picks_, ctx.document->units());
  showResult(ctx);
}

void MeasureTool::showResult(ToolContext& ctx) {
  if (ctx.viewer) {
    for (core::PreviewHandle h : previews_) ctx.viewer->removePreview(h);
    previews_.clear();
    ctx.viewer->setHighlighted(picks_);
    if (result_.hasDistance) {
      core::PreviewStyle line = core::PreviewStyle::inference();
      line.color = {0.95f, 0.55f, 0.1f, 1.0f};
      line.dashed = false;
      line.lineWidth = 2.0f;
      previews_.push_back(ctx.viewer->showPreviewLines({result_.p1, result_.p2}, line));
      core::PreviewStyle pts = line;
      pts.pointSize = 7.0f;
      previews_.push_back(ctx.viewer->showPreviewPoints({result_.p1, result_.p2}, pts));
      Vec3 mid = (result_.p1 + result_.p2) * 0.5;
      previews_.push_back(ctx.viewer->showPreviewText(mid, result_.summary, line));
    }
    ctx.viewer->requestRedraw();
  }
  if (picks_.empty()) ctx.setPrompt("Measure: select one or two entities (vertex, edge, face, body, component)");
  else if (picks_.size() == 1) ctx.setPrompt("Measure: select a second entity for a distance / angle, or OK");
  else ctx.setPrompt(result_.summary.empty() ? "Measure: no result" : "Measure: " + result_.summary);
  if (pickLabel_) pickLabel_->setText(QString::fromStdString(std::format("{} selected", picks_.size())));
  if (summaryLabel_) summaryLabel_->setText(QString::fromStdString(result_.summary));
  if (table_) {
    table_->setRowCount(static_cast<int>(result_.rows.size()));
    for (int i = 0; i < static_cast<int>(result_.rows.size()); ++i) {
      const MeasureRow& row = result_.rows[static_cast<size_t>(i)];
      auto* label = new QTableWidgetItem(QString::fromStdString(row.label));
      label->setFlags(label->flags() & ~Qt::ItemIsEditable);
      auto* value = new QTableWidgetItem(QString::fromStdString(row.value));
      value->setFlags((value->flags() | Qt::ItemIsSelectable) & ~Qt::ItemIsEditable);
      table_->setItem(i, 0, label);
      table_->setItem(i, 1, value);
    }
    table_->resizeRowsToContents();
  }
}

QWidget* MeasureTool::createDialog(ToolContext& ctx, QWidget* parent) {
  auto* w = new QWidget(parent);
  auto* v = new QVBoxLayout(w);
  v->setContentsMargins(6, 6, 6, 6);
  auto* top = new QHBoxLayout();
  pickLabel_ = new QLabel(w);
  auto* clear = new QPushButton("Restart", w);
  clear->setToolTip("Clear the selection and measure something else");
  QObject::connect(clear, &QPushButton::clicked, this, [this] {
    if (ctx_) clearPicks(*ctx_);
  });
  top->addWidget(pickLabel_, 1);
  top->addWidget(clear);
  v->addLayout(top);
  table_ = new QTableWidget(0, 2, w);
  table_->setObjectName("measureTable");
  table_->setHorizontalHeaderLabels({"Property", "Value"});
  table_->horizontalHeader()->setStretchLastSection(true);
  table_->verticalHeader()->setVisible(false);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setMinimumHeight(180);
  table_->setToolTip("Double-click a row to copy its value");
  QObject::connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
    if (row < 0 || row >= static_cast<int>(result_.rows.size())) return;
    if (QClipboard* cb = QApplication::clipboard()) cb->setText(QString::fromStdString(result_.rows[static_cast<size_t>(row)].value));
    if (ctx_) ctx_->showMessage(std::format("Copied {}", result_.rows[static_cast<size_t>(row)].value), false);
  });
  v->addWidget(table_);
  auto* bottom = new QHBoxLayout();
  summaryLabel_ = new QLabel(w);
  summaryLabel_->setWordWrap(true);
  auto* copy = new QPushButton("Copy all", w);
  QObject::connect(copy, &QPushButton::clicked, this, [this] {
    if (QClipboard* cb = QApplication::clipboard()) cb->setText(QString::fromStdString(measureText(result_)));
    if (ctx_) ctx_->showMessage("Measurement copied", false);
  });
  bottom->addWidget(summaryLabel_, 1);
  bottom->addWidget(copy);
  v->addLayout(bottom);
  showResult(ctx);
  return w;
}

}  // namespace fabricad::part
