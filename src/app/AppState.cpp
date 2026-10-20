// FabriCAD app: small widget-free helpers.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/AppState.h"

#include "core/Document.h"
#include "core/Json.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QSettings>

namespace fabricad::app {

using core::ShapeKind;

// --- RecentFiles -----------------------------------------------------------------------

void RecentFiles::add(const QString& path) {
  if (path.isEmpty()) return;
  const QString abs = QFileInfo(path).absoluteFilePath();
  list_.removeAll(abs);
  list_.prepend(abs);
  while (list_.size() > max_) list_.removeLast();
}

void RecentFiles::remove(const QString& path) { list_.removeAll(QFileInfo(path).absoluteFilePath()); }

void RecentFiles::setList(const QStringList& l) {
  list_.clear();
  for (auto it = l.crbegin(); it != l.crend(); ++it) add(*it);
}

void RecentFiles::load() {
  QSettings s;
  setList(s.value("recentFiles").toStringList());
}

void RecentFiles::save() const {
  QSettings s;
  s.setValue("recentFiles", list_);
}

// --- UiState -------------------------------------------------------------------------------

const char* displayModeName(core::DisplayMode m) {
  switch (m) {
    case core::DisplayMode::Shaded: return "shaded";
    case core::DisplayMode::ShadedEdges: return "shaded-edges";
    case core::DisplayMode::Wireframe: return "wireframe";
    case core::DisplayMode::HiddenLine: return "hidden-line";
  }
  return "shaded-edges";
}

core::DisplayMode displayModeFromName(const std::string& s) {
  for (auto m : {core::DisplayMode::Shaded, core::DisplayMode::ShadedEdges, core::DisplayMode::Wireframe,
                 core::DisplayMode::HiddenLine})
    if (s == displayModeName(m)) return m;
  return core::DisplayMode::ShadedEdges;
}

QJsonObject UiState::toJson() const {
  QJsonObject o;
  QJsonArray hidden;
  for (core::ObjectId id : hiddenFeatures) hidden.append(core::idToJson(id));
  o["hiddenFeatures"] = hidden;
  o["showOrigin"] = showOrigin;
  QJsonObject folders;
  folders["Sketches"] = showSketches;
  folders["Construction"] = showConstruction;
  folders["Joints"] = showJoints;
  o["folders"] = folders;
  o["displayMode"] = displayModeName(displayMode);
  o["projection"] = projection == core::ProjectionMode::Orthographic ? "orthographic" : "perspective";
  return o;
}

UiState UiState::fromJson(const QJsonObject& o) {
  UiState s;
  for (const QJsonValue& v : o["hiddenFeatures"].toArray())
    if (core::ObjectId id = core::idFromJson(v); id != core::kInvalidId) s.hiddenFeatures.insert(id);
  s.showOrigin = o["showOrigin"].toBool(false);
  const QJsonObject f = o["folders"].toObject();
  s.showSketches = f["Sketches"].toBool(true);
  s.showConstruction = f["Construction"].toBool(true);
  s.showJoints = f["Joints"].toBool(true);
  if (o.contains("displayMode")) s.displayMode = displayModeFromName(o["displayMode"].toString().toStdString());
  s.projection = o["projection"].toString() == "perspective" ? core::ProjectionMode::Perspective
                                                             : core::ProjectionMode::Orthographic;
  return s;
}

// --- selection text -------------------------------------------------------------------------

std::string kindLabel(ShapeKind k, bool plural) {
  std::string s;
  switch (k) {
    case ShapeKind::Vertex: s = plural ? "Vertices" : "Vertex"; return s;
    case ShapeKind::Edge: s = "Edge"; break;
    case ShapeKind::Face: s = "Face"; break;
    case ShapeKind::Body: s = plural ? "Bodies" : "Body"; return s;
    case ShapeKind::SketchPoint: s = "Sketch Point"; break;
    case ShapeKind::SketchCurve: s = "Sketch Curve"; break;
    case ShapeKind::Profile: s = "Profile"; break;
    case ShapeKind::Sketch: s = "Sketch"; break;
    case ShapeKind::ConstructionPlane: s = "Plane"; break;
    case ShapeKind::ConstructionAxis: s = plural ? "Axes" : "Axis"; return s;
    case ShapeKind::ConstructionPoint: s = "Point"; break;
    case ShapeKind::OriginPlane: s = "Origin Plane"; break;
    case ShapeKind::OriginAxis: s = plural ? "Origin Axes" : "Origin Axis"; return s;
    case ShapeKind::OriginPoint: s = "Origin"; break;
    case ShapeKind::Component: s = "Component"; break;
    case ShapeKind::Joint: s = "Joint"; break;
    case ShapeKind::JointOrigin: s = "Joint Origin"; break;
    case ShapeKind::Feature: s = "Feature"; break;
    case ShapeKind::DrawingView: s = "View"; break;
    case ShapeKind::DrawingEntity: s = "Entity"; break;
    case ShapeKind::None: s = "Item"; break;
  }
  if (plural) s += "s";
  return s;
}

std::string ownerName(const core::Document& doc, const core::ShapeRef& r) {
  switch (r.kind) {
    case ShapeKind::Vertex:
    case ShapeKind::Edge:
    case ShapeKind::Face:
    case ShapeKind::Body:
      if (const core::Body* b = doc.body(r.owner)) return b->name;
      return "Body";
    case ShapeKind::OriginPlane: return r.name + " Plane";
    case ShapeKind::OriginAxis: return r.name + " Axis";
    case ShapeKind::OriginPoint: return "Origin";
    case ShapeKind::Component:
      if (const core::Component* c = doc.component(r.owner)) return c->name;
      return "Component";
    default:
      if (const core::Feature* f = doc.feature(r.owner)) return f->name();
      return kindLabel(r.kind);
  }
}

std::string describeSelection(const core::Document& doc, const std::vector<core::ShapeRef>& items) {
  if (items.empty()) return {};
  if (items.size() == 1) {
    const core::ShapeRef& r = items.front();
    switch (r.kind) {
      case ShapeKind::Body:
      case ShapeKind::Sketch:
      case ShapeKind::Component:
      case ShapeKind::OriginPlane:
      case ShapeKind::OriginAxis:
      case ShapeKind::OriginPoint:
      case ShapeKind::ConstructionPlane:
      case ShapeKind::ConstructionAxis:
      case ShapeKind::ConstructionPoint:
      case ShapeKind::Joint:
      case ShapeKind::Feature: return ownerName(doc, r);
      default: return kindLabel(r.kind) + " of " + ownerName(doc, r);
    }
  }
  const ShapeKind k = items.front().kind;
  for (const core::ShapeRef& r : items)
    if (r.kind != k) return std::to_string(items.size()) + " items";
  return std::to_string(items.size()) + " " + kindLabel(k, true);
}

}  // namespace fabricad::app
