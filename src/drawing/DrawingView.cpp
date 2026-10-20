// FabriCAD drawing: DrawingView helpers and (de)serialisation.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "drawing/DrawingView.h"

#include "core/Json.h"
#include "drawing/Projection.h"

#include <QJsonArray>

#include <algorithm>

namespace fabricad::drawing {

using core::idFromJson;
using core::idToJson;
using core::qs;
using core::ss;

namespace {
QJsonArray pt(const Point2& p) { return QJsonArray{p.x, p.y}; }
Point2 ptFrom(const QJsonValue& v, const Point2& fb = {}) {
  auto a = v.toArray();
  return a.size() >= 2 ? Point2{a[0].toDouble(), a[1].toDouble()} : fb;
}
}  // namespace

QJsonObject SectionLine::toJson() const {
  QJsonObject o;
  o["p1"] = pt(p1);
  o["p2"] = pt(p2);
  o["arrow"] = pt(arrow);
  o["label"] = qs(label);
  return o;
}

SectionLine SectionLine::fromJson(const QJsonObject& o) {
  SectionLine s;
  s.p1 = ptFrom(o["p1"]);
  s.p2 = ptFrom(o["p2"]);
  s.arrow = ptFrom(o["arrow"], {0, 1});
  s.label = ss(o["label"].toString());
  return s;
}

QJsonObject DetailSpec::toJson() const {
  QJsonObject o;
  o["center"] = pt(center);
  o["radius"] = radius;
  o["label"] = qs(label);
  return o;
}

DetailSpec DetailSpec::fromJson(const QJsonObject& o) {
  DetailSpec d;
  d.center = ptFrom(o["center"]);
  d.radius = o["radius"].toDouble(10.0);
  d.label = ss(o["label"].toString());
  return d;
}

gp_Ax2 DrawingView::definitionFrame() const {
  if (usesStandardOrientation()) return frameForOrientation(orientation);
  return customFrame;
}

BBox2 DrawingView::sheetBBox() const {
  BBox2 b;
  if (bbox.isValid()) {
    b.add(toSheet(bbox.min));
    b.add(toSheet(bbox.max));
  } else {
    b.add(position - Point2{10, 10});
    b.add(position + Point2{10, 10});
  }
  return b;
}

Point2 DrawingView::labelPosition() const {
  BBox2 b = sheetBBox();
  return {b.center().x, b.max.y + 6.0};
}

const Curve2D* DrawingView::pickCurve(const Point2& p, double tol, size_t* index) const {
  const Curve2D* best = nullptr;
  double bestD = tol;
  for (size_t i = 0; i < curves.size(); ++i) {
    const Curve2D& c = curves[i];
    if (c.cls == LineClass::Hatch) continue;
    double d = c.distanceTo(p);
    if (d <= bestD) {
      bestD = d;
      best = &c;
      if (index) *index = i;
    }
  }
  return best;
}

const Vertex2D* DrawingView::pickVertex(const Point2& p, double tol) const {
  const Vertex2D* best = nullptr;
  double bestD = tol;
  for (const auto& v : vertices) {
    double d = v.p.distance(p);
    if (d <= bestD) {
      bestD = d;
      best = &v;
    }
  }
  return best;
}

const Curve2D* DrawingView::curveForSource(core::ObjectId body, int edge) const {
  if (body == core::kInvalidId || edge <= 0) return nullptr;
  for (const auto& c : curves)
    if (c.sourceBody == body && c.sourceEdge == edge) return &c;
  return nullptr;
}

const Curve2D* DrawingView::curveForName(core::ObjectId body, const std::string& name, const Point2& near) const {
  if (body == core::kInvalidId || name.empty()) return nullptr;
  const Curve2D* best = nullptr;
  double bestD = 1e300;
  for (const auto& c : curves) {
    if (c.sourceBody != body || c.sourceName != name) continue;
    double d = c.distanceTo(near);
    if (d < bestD) {
      bestD = d;
      best = &c;
    }
  }
  return best;
}

QJsonObject DrawingView::geometryKey() const {
  QJsonObject o;
  o["type"] = viewTypeName(type);
  o["parent"] = idToJson(parent);
  QJsonArray src;
  for (const auto& r : sources) src.push_back(r.toJson());
  o["sources"] = src;
  o["orientation"] = viewOrientationName(orientation);
  if (!usesStandardOrientation()) o["frame"] = frameToJson(customFrame);
  o["hidden"] = hiddenLines;
  o["tangent"] = tangentEdgeModeName(tangentEdges);
  o["hatch"] = hatch;
  o["exact"] = exact;
  if (type == ViewType::Section) o["section"] = section.toJson();
  if (type == ViewType::Detail) {
    o["detail"] = detail.toJson();
    o["scale"] = scale;
  }
  return o;
}

QJsonObject DrawingView::toJson() const {
  QJsonObject o = geometryKey();
  o["id"] = idToJson(id);
  o["label"] = qs(label);
  o["direction"] = projectionDirectionName(direction);
  o["alignment"] = viewAlignmentName(alignment);
  o["scale"] = scale;
  o["position"] = pt(position);
  o["showLabel"] = showLabel;
  o["frame"] = frameToJson(customFrame);
  return o;
}

DrawingView DrawingView::fromJson(const QJsonObject& o) {
  DrawingView v;
  v.id = idFromJson(o["id"]);
  v.label = ss(o["label"].toString());
  v.type = viewTypeFromName(ss(o["type"].toString()));
  v.parent = idFromJson(o["parent"]);
  for (const auto& r : o["sources"].toArray()) v.sources.push_back(core::ShapeRef::fromJson(r.toObject()));
  v.orientation = viewOrientationFromName(ss(o["orientation"].toString()));
  v.customFrame = frameFromJson(o["frame"].toObject(), frameForOrientation(v.orientation));
  v.direction = projectionDirectionFromName(ss(o["direction"].toString()));
  v.alignment = viewAlignmentFromName(ss(o["alignment"].toString()));
  v.scale = o["scale"].toDouble(1.0);
  if (v.scale <= 0) v.scale = 1.0;
  v.position = ptFrom(o["position"]);
  v.hiddenLines = o["hidden"].toBool(false);
  v.tangentEdges = tangentEdgeModeFromName(ss(o["tangent"].toString()));
  v.showLabel = o["showLabel"].toBool(false);
  v.hatch = o["hatch"].toBool(true);
  v.exact = o["exact"].toBool(true);
  if (o.contains("section")) v.section = SectionLine::fromJson(o["section"].toObject());
  if (o.contains("detail")) v.detail = DetailSpec::fromJson(o["detail"].toObject());
  return v;
}

}  // namespace fabricad::drawing
