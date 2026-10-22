// FabriCAD drawing: annotations (notes, centre marks/lines, balloons, parts list) and the
// local BOM adapter.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "drawing/Annotations.h"

#include "core/Document.h"
#include "core/Json.h"
#include "core/ShapeUtils.h"
#include "drawing/DrawingView.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>

namespace fabricad::drawing {

using core::idFromJson;
using core::idToJson;
using core::qs;
using core::ss;

namespace {
QJsonArray pt(const Point2& p) { return QJsonArray{p.x, p.y}; }
Point2 ptFrom(const QJsonValue& v) {
  auto a = v.toArray();
  return a.size() >= 2 ? Point2{a[0].toDouble(), a[1].toDouble()} : Point2{};
}
}  // namespace

// --- TextNote --------------------------------------------------------------------------

QJsonObject TextNote::toJson() const {
  QJsonObject o;
  o["id"] = idToJson(id);
  o["text"] = qs(text);
  o["pos"] = pt(position);
  o["height"] = height;
  o["angle"] = angle;
  o["leader"] = leader;
  if (leader) o["leaderTo"] = pt(leaderTo);
  return o;
}

TextNote TextNote::fromJson(const QJsonObject& o) {
  TextNote n;
  n.id = idFromJson(o["id"]);
  n.text = ss(o["text"].toString());
  n.position = ptFrom(o["pos"]);
  n.height = o["height"].toDouble(3.5);
  n.angle = o["angle"].toDouble(0);
  n.leader = o["leader"].toBool(false);
  n.leaderTo = ptFrom(o["leaderTo"]);
  return n;
}

// --- CenterMark / CenterLine --------------------------------------------------------------

QJsonObject CenterMark::toJson() const {
  QJsonObject o;
  o["id"] = idToJson(id);
  o["view"] = idToJson(viewId);
  o["ref"] = ref.toJson();
  o["ext"] = extension;
  return o;
}

CenterMark CenterMark::fromJson(const QJsonObject& o) {
  CenterMark m;
  m.id = idFromJson(o["id"]);
  m.viewId = idFromJson(o["view"]);
  m.ref = DimRef::fromJson(o["ref"].toObject());
  m.extension = o["ext"].toDouble(2.0);
  return m;
}

QJsonObject CenterLine::toJson() const {
  QJsonObject o;
  o["id"] = idToJson(id);
  o["view"] = idToJson(viewId);
  o["a"] = a.toJson();
  o["b"] = b.toJson();
  o["ext"] = extension;
  return o;
}

CenterLine CenterLine::fromJson(const QJsonObject& o) {
  CenterLine l;
  l.id = idFromJson(o["id"]);
  l.viewId = idFromJson(o["view"]);
  l.a = DimRef::fromJson(o["a"].toObject());
  l.b = DimRef::fromJson(o["b"].toObject());
  l.extension = o["ext"].toDouble(2.0);
  return l;
}

std::vector<Curve2D> layoutCenterMark(const CenterMark& m, const DrawingView& view) {
  std::vector<Curve2D> out;
  DimTarget t = resolveDimRef(m.ref, view);
  if (!t.ok || t.isVertex || t.curve.type != CurveType::Arc) return out;
  Point2 c = view.toSheet(t.curve.center);
  double r = t.curve.radius * view.scale + m.extension;
  out.push_back(Curve2D::segment({c.x - r, c.y}, {c.x + r, c.y}, LineClass::Center));
  out.push_back(Curve2D::segment({c.x, c.y - r}, {c.x, c.y + r}, LineClass::Center));
  return out;
}

std::vector<Curve2D> layoutCenterLine(const CenterLine& l, const DrawingView& view) {
  std::vector<Curve2D> out;
  DimTarget a = resolveDimRef(l.a, view), b = resolveDimRef(l.b, view);
  if (!a.ok || !b.ok) return out;
  Point2 p1, p2;
  if (!a.isVertex && !b.isVertex && a.curve.type == CurveType::Segment && b.curve.type == CurveType::Segment) {
    // midline between two (roughly parallel) edges
    Point2 da = a.curve.direction();
    Point2 db = b.curve.direction();
    if (da.dot(db) < 0) db = -db;
    Point2 dir = (da + db).normalized();
    Point2 ma = a.curve.midpoint(), mb = b.curve.midpoint();
    Point2 mid = (ma + mb) * 0.5;
    double half = std::max(a.curve.length(), b.curve.length()) * 0.5;
    p1 = mid - dir * half;
    p2 = mid + dir * half;
  } else {
    auto pointOf = [](const DimTarget& t) { return t.isVertex ? t.point : (t.curve.type == CurveType::Arc ? t.curve.center : t.curve.midpoint()); };
    p1 = pointOf(a);
    p2 = pointOf(b);
  }
  Point2 s1 = view.toSheet(p1), s2 = view.toSheet(p2);
  Point2 d = (s2 - s1).normalized();
  out.push_back(Curve2D::segment(s1 - d * l.extension, s2 + d * l.extension, LineClass::Center));
  return out;
}

// --- Balloon ----------------------------------------------------------------------------

QJsonObject Balloon::toJson() const {
  QJsonObject o;
  o["id"] = idToJson(id);
  o["view"] = idToJson(viewId);
  o["item"] = idToJson(itemKey);
  o["attach"] = pt(attach);
  o["pos"] = pt(position);
  return o;
}

Balloon Balloon::fromJson(const QJsonObject& o) {
  Balloon b;
  b.id = idFromJson(o["id"]);
  b.viewId = idFromJson(o["view"]);
  b.itemKey = idFromJson(o["item"]);
  b.attach = ptFrom(o["attach"]);
  b.position = ptFrom(o["pos"]);
  return b;
}

// --- PartsList ----------------------------------------------------------------------------

double PartsList::width() const {
  double w = 0;
  for (double c : columnWidths) w += c;
  return w;
}

double PartsList::height() const { return rowHeight * static_cast<double>(rows.size() + 1); }

QJsonObject PartsList::toJson() const {
  QJsonObject o;
  o["id"] = idToJson(id);
  o["view"] = idToJson(viewId);
  o["pos"] = pt(position);
  QJsonArray cols, widths;
  for (const auto& c : columns) cols.push_back(qs(c));
  for (double w : columnWidths) widths.push_back(w);
  o["columns"] = cols;
  o["widths"] = widths;
  o["rowHeight"] = rowHeight;
  o["textHeight"] = textHeight;
  return o;
}

PartsList PartsList::fromJson(const QJsonObject& o) {
  PartsList p;
  p.id = idFromJson(o["id"]);
  p.viewId = idFromJson(o["view"]);
  p.position = ptFrom(o["pos"]);
  if (o.contains("columns")) {
    p.columns.clear();
    for (const auto& c : o["columns"].toArray()) p.columns.push_back(ss(c.toString()));
  }
  if (o.contains("widths")) {
    p.columnWidths.clear();
    for (const auto& w : o["widths"].toArray()) p.columnWidths.push_back(w.toDouble(20));
  }
  while (p.columnWidths.size() < p.columns.size()) p.columnWidths.push_back(25.0);
  p.rowHeight = o["rowHeight"].toDouble(7.0);
  p.textHeight = o["textHeight"].toDouble(3.0);
  return p;
}

// --- BOM adapter -----------------------------------------------------------------------------

namespace {
bool restrictedTo(const std::vector<core::ShapeRef>& sources, const core::Document& doc, core::ObjectId componentId,
                  core::ObjectId bodyId) {
  if (sources.empty()) return true;
  for (const auto& r : sources) {
    if (r.kind == core::ShapeKind::Body && r.owner == bodyId) return true;
    if (r.kind == core::ShapeKind::Body && bodyId == core::kInvalidId) {
      // assembly rows: a body source selects the component chain that owns it
      const core::Body* b = doc.body(r.owner);
      for (core::ObjectId c = b ? b->component : core::kInvalidId; c != core::kInvalidId;) {
        if (c == componentId) return true;
        const core::Component* comp = doc.component(c);
        c = comp ? comp->parent : core::kInvalidId;
      }
    }
    if (r.kind == core::ShapeKind::Component) {
      // componentId or any ancestor equals r.owner
      core::ObjectId c = componentId;
      while (c != core::kInvalidId) {
        if (c == r.owner) return true;
        const core::Component* comp = doc.component(c);
        c = comp ? comp->parent : core::kInvalidId;
      }
    }
  }
  return false;
}

double bodyMassKg(const core::Document& doc, core::ObjectId bodyId) {
  const core::Body* b = doc.body(bodyId);
  if (!b || b->density <= 0) return 0.0;
  const TopoDS_Shape& s = doc.bodyShape(bodyId);
  if (s.IsNull()) return 0.0;
  // density g/cm^3, volume mm^3 -> kg: v[mm3] * 1e-3 [cm3/mm3] * rho [g/cm3] * 1e-3 [kg/g]
  return core::volume(s) * 1e-6 * b->density;
}
}  // namespace

std::vector<BomRow> buildBom(const core::Document& doc, const std::vector<core::ShapeRef>& sources) {
  std::vector<BomRow> rows;
  const core::Component& root = doc.rootComponent();
  const bool hasChildren = !root.children.empty();

  if (!hasChildren) {
    // Part document: one row per body with geometry.
    for (core::ObjectId bodyId : doc.liveBodies(false)) {
      const core::Body* b = doc.body(bodyId);
      if (!b) continue;
      if (!restrictedTo(sources, doc, b->component, bodyId)) continue;
      BomRow r;
      r.item = static_cast<int>(rows.size()) + 1;
      r.quantity = 1;
      r.partNumber = b->name;
      r.description = root.description.empty() ? root.name : root.description;
      r.material = b->material;
      r.mass = bodyMassKg(doc, bodyId);
      r.keys = {bodyId};
      rows.push_back(std::move(r));
    }
    return rows;
  }

  // Assembly: one row per leaf component definition; occurrences add quantity.
  std::map<core::ObjectId, size_t> rowByDefinition;
  std::function<void(const core::Component&)> visit = [&](const core::Component& comp) {
    if (comp.isRoot()) {
      for (core::ObjectId child : comp.children)
        if (const core::Component* c = doc.component(child)) visit(*c);
      return;
    }
    core::ObjectId def = comp.isOccurrence() ? comp.definition : comp.id;
    const core::Component* defComp = doc.component(def);
    if (!defComp) defComp = &comp;
    std::vector<core::ObjectId> bodies = doc.bodiesOf(def);
    bool leaf = defComp->children.empty();
    bool include = restrictedTo(sources, doc, comp.id, core::kInvalidId);
    if (leaf || !bodies.empty()) {
      if (include) {
        auto it = rowByDefinition.find(def);
        if (it == rowByDefinition.end()) {
          BomRow r;
          r.item = static_cast<int>(rows.size()) + 1;
          r.quantity = 1;
          r.partNumber = defComp->partNumber.empty() ? defComp->name : defComp->partNumber;
          r.description = defComp->description;
          r.material = defComp->material;
          if (r.material.empty())
            for (core::ObjectId b : bodies)
              if (const core::Body* body = doc.body(b); body && !body->material.empty()) {
                r.material = body->material;
                break;
              }
          for (core::ObjectId b : bodies) r.mass += bodyMassKg(doc, b);
          r.keys = {comp.id};
          if (comp.id != def) r.keys.push_back(def);
          rowByDefinition[def] = rows.size();
          rows.push_back(std::move(r));
        } else {
          BomRow& r = rows[it->second];
          r.quantity += 1;
          r.keys.push_back(comp.id);
        }
      }
    }
    if (!comp.isOccurrence())
      for (core::ObjectId child : comp.children)
        if (const core::Component* c = doc.component(child)) visit(*c);
  };
  visit(root);
  return rows;
}

int bomItemFor(const std::vector<BomRow>& rows, core::ObjectId key) {
  for (const auto& r : rows)
    if (std::find(r.keys.begin(), r.keys.end(), key) != r.keys.end()) return r.item;
  return 0;
}

core::ObjectId bomKeyForBody(const core::Document& doc, core::ObjectId bodyId, bool perBody) {
  if (perBody) return bodyId;
  const core::Body* b = doc.body(bodyId);
  if (!b) return core::kInvalidId;
  // nearest non-root ancestor component (the row key); root bodies key by body id
  core::ObjectId c = b->component;
  if (c == core::kRootComponentId) return bodyId;
  return c;
}

}  // namespace fabricad::drawing
