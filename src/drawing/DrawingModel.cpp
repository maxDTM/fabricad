// FabriCAD drawing: DrawingModel implementation.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "drawing/DrawingModel.h"

#include "core/Json.h"
#include "core/Log.h"
#include "core/ShapeRef.h"
#include "part/Naming.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

#include <BRepBuilderAPI_Transform.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>

#include <algorithm>
#include <set>

namespace fabricad::drawing {

using core::idFromJson;
using core::idToJson;
using core::ObjectId;

// ---------------------------------------------------------------------------------------
// Worker
// ---------------------------------------------------------------------------------------

ViewJobResult runViewJob(const ViewJob& job) {
  ViewJobResult r;
  r.viewId = job.viewId;
  r.generation = job.generation;
  try {
    ProjectionInput in = job.projection;
    if (job.doSection) {
      SectionOutput sec = computeSection(job.section);
      if (!sec.ok) {
        r.error = sec.error.empty() ? "section failed" : sec.error;
        return r;
      }
      in.sources = sec.cutSources;
      in.useFrameOrigin = true;
      in.frame = job.section.frame;
      in.extraCurves = sec.outlines;
      for (const auto& h : sec.hatch) in.extraCurves.push_back(h);
    }
    r.projection = projectShapes(in);
    if (!r.projection.ok) r.error = r.projection.error;
  } catch (const Standard_Failure& e) {
    r.error = std::string("OCCT: ") + (e.GetMessageString() ? e.GetMessageString() : "failure");
  } catch (const std::exception& e) {
    r.error = e.what();
  }
  return r;
}

// ---------------------------------------------------------------------------------------
// Construction / lookup
// ---------------------------------------------------------------------------------------

DrawingModel::DrawingModel(core::Document& doc) : QObject(&doc), doc_(doc) {
  setObjectName("fabricad.drawing.model");
  connect(&doc_, &core::Document::recomputeFinished, this, &DrawingModel::onRecomputeFinished);
  connect(&doc_, &core::Document::aboutToSave, this, &DrawingModel::saveToDocument);
  connect(&doc_, &core::Document::loaded, this, &DrawingModel::onDocumentLoaded);
  connect(&doc_, &core::Document::documentReset, this, &DrawingModel::onDocumentLoaded);
  // Pick up state that is already in the document (model created after load).
  fromJson(doc_.extensionData(kExtensionKey));
}

DrawingModel::~DrawingModel() {
  for (auto& j : jobs_) {
    if (j.watcher) {
      j.watcher->disconnect(this);
      j.watcher->waitForFinished();
      delete j.watcher;
    }
  }
  jobs_.clear();
}

DrawingModel* DrawingModel::forDocument(core::Document& doc) {
  if (DrawingModel* m = find(doc)) return m;
  return new DrawingModel(doc);
}

DrawingModel* DrawingModel::find(const core::Document& doc) {
  return doc.findChild<DrawingModel*>("fabricad.drawing.model", Qt::FindDirectChildrenOnly);
}

Sheet* DrawingModel::sheet(ObjectId id) {
  for (auto& s : sheets_)
    if (s.id == id) return &s;
  return nullptr;
}

const Sheet* DrawingModel::sheet(ObjectId id) const {
  for (const auto& s : sheets_)
    if (s.id == id) return &s;
  return nullptr;
}

Sheet* DrawingModel::activeSheet() {
  if (Sheet* s = sheet(activeSheet_)) return s;
  if (!sheets_.empty()) {
    activeSheet_ = sheets_.front().id;
    return &sheets_.front();
  }
  return nullptr;
}

void DrawingModel::setActiveSheet(ObjectId id) {
  if (id == activeSheet_ || !sheet(id)) return;
  activeSheet_ = id;
  emit activeSheetChanged(id);
}

std::string DrawingModel::nextSheetName() const {
  std::set<std::string> used;
  for (const auto& s : sheets_) used.insert(s.name);
  for (int i = 1;; ++i) {
    std::string n = "Sheet" + std::to_string(i);
    if (!used.count(n)) return n;
  }
}

Sheet& DrawingModel::addSheet(const SheetFormat& format, ProjectionStandard standard, const std::string& name) {
  Sheet s;
  s.id = allocateId();
  s.name = name.empty() ? nextSheetName() : name;
  s.format = format;
  s.standard = standard;
  sheets_.push_back(std::move(s));
  Sheet& ref = sheets_.back();
  if (activeSheet_ == core::kInvalidId) activeSheet_ = ref.id;
  doc_.setModified(true);
  emit sheetsChanged();
  return ref;
}

bool DrawingModel::removeSheet(ObjectId id) {
  auto it = std::find_if(sheets_.begin(), sheets_.end(), [&](const Sheet& s) { return s.id == id; });
  if (it == sheets_.end()) return false;
  for (const auto& v : it->views) {
    generation_.erase(v.id);
    cache_.erase(v.id);
  }
  sheets_.erase(it);
  if (activeSheet_ == id) activeSheet_ = sheets_.empty() ? core::kInvalidId : sheets_.front().id;
  doc_.setModified(true);
  emit sheetsChanged();
  if (activeSheet_ != id) emit activeSheetChanged(activeSheet_);
  return true;
}

Sheet* DrawingModel::sheetOf(ObjectId entityId) {
  for (auto& s : sheets_)
    if (s.contains(entityId)) return &s;
  return nullptr;
}

DrawingView* DrawingModel::view(ObjectId id, Sheet** sheetOut) {
  for (auto& s : sheets_)
    if (DrawingView* v = s.view(id)) {
      if (sheetOut) *sheetOut = &s;
      return v;
    }
  return nullptr;
}

const DrawingView* DrawingModel::view(ObjectId id) const {
  for (const auto& s : sheets_)
    if (const DrawingView* v = s.view(id)) return v;
  return nullptr;
}

// ---------------------------------------------------------------------------------------
// Views
// ---------------------------------------------------------------------------------------

DrawingView& DrawingModel::addBaseView(Sheet& sheet, std::vector<core::ShapeRef> sources, ViewOrientation orientation,
                                       double scale, const Point2& position) {
  DrawingView v;
  v.id = allocateId();
  v.label = sheet.nextViewLabel();
  v.type = ViewType::Base;
  v.sources = std::move(sources);
  v.orientation = orientation;
  v.customFrame = frameForOrientation(orientation == ViewOrientation::Custom ? ViewOrientation::Front : orientation);
  v.scale = scale > 0 ? scale : sheet.scale;
  v.position = position;
  sheet.views.push_back(std::move(v));
  DrawingView& ref = sheet.views.back();
  invalidateView(ref.id);
  notifyChanged(sheet.id);
  return ref;
}

gp_Ax2 DrawingModel::frameForProjection(const DrawingView& parent, const Sheet& sheet, ProjectionDirection dir) const {
  gp_Ax2 pf = parent.geometryValid ? parent.frame : parent.definitionFrame();
  return projectedFrame(pf, dir, sheet.standard);
}

DrawingView* DrawingModel::addProjectedView(Sheet& sheet, ObjectId parentId, ProjectionDirection direction,
                                            const Point2& position) {
  DrawingView* parent = sheet.view(parentId);
  if (!parent) return nullptr;
  DrawingView v;
  v.id = allocateId();
  v.label = sheet.nextViewLabel();
  v.type = ViewType::Projected;
  v.parent = parentId;
  v.sources = parent->sources;
  v.orientation = ViewOrientation::Custom;
  v.customFrame = frameForProjection(*parent, sheet, direction);
  v.direction = direction;
  v.alignment = isDiagonal(direction) ? ViewAlignment::None
                : (direction == ProjectionDirection::Left || direction == ProjectionDirection::Right)
                    ? ViewAlignment::Horizontal
                    : ViewAlignment::Vertical;
  v.scale = parent->scale;
  v.hiddenLines = parent->hiddenLines;
  v.tangentEdges = parent->tangentEdges;
  v.exact = parent->exact;
  v.position = position;
  if (v.alignment == ViewAlignment::Horizontal) v.position.y = parent->position.y;
  if (v.alignment == ViewAlignment::Vertical) v.position.x = parent->position.x;
  v.autoPlace = true;
  v.autoDir = projectionDirectionVector(direction);
  sheet.views.push_back(std::move(v));
  DrawingView& ref = sheet.views.back();
  invalidateView(ref.id);
  notifyChanged(sheet.id);
  return &ref;
}

DrawingView* DrawingModel::addSectionView(Sheet& sheet, ObjectId parentId, const SectionLine& line,
                                          const Point2& position) {
  DrawingView* parent = sheet.view(parentId);
  if (!parent) return nullptr;
  DrawingView v;
  v.id = allocateId();
  v.type = ViewType::Section;
  v.parent = parentId;
  v.sources = parent->sources;
  v.orientation = ViewOrientation::Custom;
  v.section = line;
  if (v.section.label.empty()) v.section.label = sheet.nextLetterLabel();
  v.label = v.section.label + "-" + v.section.label;
  gp_Ax2 pf = parent->geometryValid ? parent->frame : parent->definitionFrame();
  v.customFrame = sectionFrame(pf, line.p1, line.p2, line.arrow);
  // Alignment: a horizontal cut line produces a view placed above/below; vertical: left/right.
  Point2 d = (line.p2 - line.p1).normalized();
  v.alignment = std::abs(d.x) >= std::abs(d.y) ? ViewAlignment::Vertical : ViewAlignment::Horizontal;
  v.scale = parent->scale;
  v.hiddenLines = false;
  v.tangentEdges = parent->tangentEdges;
  v.exact = parent->exact;
  v.showLabel = true;
  v.position = position;
  // The placement click only chooses the side of the cut line; the model moves the view clear
  // of the parent once its size is known (autoArrange).
  Point2 s1 = parent->toSheet(line.p1), s2 = parent->toSheet(line.p2);
  Point2 n = (s2 - s1).normalized().perp();
  Point2 mid = (s1 + s2) * 0.5;
  v.autoDir = (position - mid).dot(n) >= 0 ? n : n * -1.0;
  v.autoPlace = true;
  sheet.views.push_back(std::move(v));
  DrawingView& ref = sheet.views.back();
  ref.position = alignedPosition(ref, position);
  invalidateView(ref.id);
  notifyChanged(sheet.id);
  return &ref;
}

DrawingView* DrawingModel::addDetailView(Sheet& sheet, ObjectId parentId, const DetailSpec& detail, double scale,
                                         const Point2& position) {
  DrawingView* parent = sheet.view(parentId);
  if (!parent) return nullptr;
  DrawingView v;
  v.id = allocateId();
  v.type = ViewType::Detail;
  v.parent = parentId;
  v.sources = parent->sources;
  v.orientation = ViewOrientation::Custom;
  v.customFrame = parent->geometryValid ? parent->frame : parent->definitionFrame();
  v.detail = detail;
  if (v.detail.label.empty()) v.detail.label = sheet.nextLetterLabel();
  v.label = "DETAIL " + v.detail.label;
  v.scale = scale > 0 ? scale : parent->scale * 2.0;
  v.hiddenLines = parent->hiddenLines;
  v.tangentEdges = parent->tangentEdges;
  v.showLabel = true;
  v.position = position;
  sheet.views.push_back(std::move(v));
  DrawingView& ref = sheet.views.back();
  parent = sheet.view(parentId);  // push_back may have reallocated
  if (parent && parent->geometryValid) {
    computeDetail(ref, *parent);
    emit viewGeometryChanged(ref.id);
  }
  notifyChanged(sheet.id);
  return &ref;
}

bool DrawingModel::removeView(ObjectId id) {
  Sheet* s = nullptr;
  DrawingView* v = view(id, &s);
  if (!v || !s) return false;
  for (ObjectId child : s->childrenOf(id)) removeView(child);
  for (ObjectId e : s->entitiesOf(id)) removeEntity(e);
  // parts lists bound to this view fall back to the whole document
  for (auto& pl : s->partsLists)
    if (pl.viewId == id) pl.viewId = core::kInvalidId;
  s->views.erase(std::remove_if(s->views.begin(), s->views.end(), [&](const DrawingView& x) { return x.id == id; }),
                 s->views.end());
  generation_.erase(id);
  cache_.erase(id);
  jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [&](const RunningJob& j) { return j.viewId == id && !j.watcher; }),
              jobs_.end());
  notifyChanged(s->id);
  return true;
}

Point2 DrawingModel::alignedPosition(const DrawingView& child, const Point2& desired) const {
  const DrawingView* parent = child.parent != core::kInvalidId ? view(child.parent) : nullptr;
  if (!parent) return desired;
  Point2 p = desired;
  if (child.alignment == ViewAlignment::Horizontal) p.y = parent->position.y;
  if (child.alignment == ViewAlignment::Vertical) p.x = parent->position.x;
  return p;
}

void DrawingModel::moveView(ObjectId id, const Point2& position) {
  Sheet* s = nullptr;
  DrawingView* v = view(id, &s);
  if (!v || !s) return;
  Point2 target = alignedPosition(*v, position);
  Point2 delta = target - v->position;
  v->autoPlace = false;  // the user decides where it goes from now on
  if (delta.length() < 1e-12) return;
  v->position = target;
  // entities attached to this view move with it (text anchors are sheet-space)
  for (auto& d : s->dimensions)
    if (d.viewId == id) d.textPos += delta;
  for (auto& b : s->balloons)
    if (b.viewId == id) b.position += delta;
  s->callouts.followView(id, delta);
  for (ObjectId child : s->childrenOf(id)) {
    DrawingView* c = s->view(child);
    if (!c) continue;
    // children keep their relative placement (section/detail views included)
    moveView(child, c->position + delta);
  }
  notifyChanged(s->id);
}

void DrawingModel::viewDefinitionChanged(ObjectId id) {
  Sheet* s = nullptr;
  DrawingView* v = view(id, &s);
  if (!v || !s) return;
  if (v->type == ViewType::Section) {
    v->label = v->section.label + "-" + v->section.label;
    if (const DrawingView* parent = s->view(v->parent)) {
      gp_Ax2 pf = parent->geometryValid ? parent->frame : parent->definitionFrame();
      v->customFrame = sectionFrame(pf, v->section.p1, v->section.p2, v->section.arrow);
    }
  }
  if (v->type == ViewType::Detail) v->label = "DETAIL " + v->detail.label;
  // children of a base view inherit style changes only when explicitly re-projected; scale
  // changes propagate to aligned children (Fusion keeps projected views at the parent scale)
  if (v->type == ViewType::Base || v->type == ViewType::Projected) {
    for (ObjectId child : s->childrenOf(id)) {
      DrawingView* c = s->view(child);
      if (c && c->type == ViewType::Projected && std::abs(c->scale - v->scale) > 1e-12) {
        c->scale = v->scale;
        invalidateView(c->id);
      }
    }
  }
  auto it = cache_.find(id);
  if (it != cache_.end() && it->second.key == v->geometryKey() && it->second.docGeneration == docGeneration_ &&
      v->geometryValid) {
    // geometry unaffected (e.g. scale/label): dependants still need re-measuring
    finishView(*s, *v);
  } else {
    invalidateView(id);
  }
  notifyChanged(s->id);
}

// ---------------------------------------------------------------------------------------
// Entities
// ---------------------------------------------------------------------------------------

Dimension& DrawingModel::addDimension(Sheet& sheet, Dimension dim) {
  if (dim.id == core::kInvalidId) dim.id = allocateId();
  sheet.dimensions.push_back(std::move(dim));
  Dimension& d = sheet.dimensions.back();
  if (const DrawingView* v = sheet.view(d.viewId)) updateDimension(d, *v);
  notifyChanged(sheet.id);
  return d;
}

TextNote& DrawingModel::addNote(Sheet& sheet, TextNote note) {
  if (note.id == core::kInvalidId) note.id = allocateId();
  sheet.notes.push_back(std::move(note));
  notifyChanged(sheet.id);
  return sheet.notes.back();
}

CenterMark& DrawingModel::addCenterMark(Sheet& sheet, CenterMark mark) {
  if (mark.id == core::kInvalidId) mark.id = allocateId();
  sheet.centerMarks.push_back(std::move(mark));
  notifyChanged(sheet.id);
  return sheet.centerMarks.back();
}

CenterLine& DrawingModel::addCenterLine(Sheet& sheet, CenterLine line) {
  if (line.id == core::kInvalidId) line.id = allocateId();
  sheet.centerLines.push_back(std::move(line));
  notifyChanged(sheet.id);
  return sheet.centerLines.back();
}

Balloon& DrawingModel::addBalloon(Sheet& sheet, Balloon balloon) {
  if (balloon.id == core::kInvalidId) balloon.id = allocateId();
  sheet.balloons.push_back(std::move(balloon));
  refreshAnnotations(sheet);
  notifyChanged(sheet.id);
  return sheet.balloons.back();
}

PartsList& DrawingModel::addPartsList(Sheet& sheet, PartsList list) {
  if (list.id == core::kInvalidId) list.id = allocateId();
  sheet.partsLists.push_back(std::move(list));
  refreshAnnotations(sheet);
  notifyChanged(sheet.id);
  return sheet.partsLists.back();
}

template <class T>
static bool eraseById(std::vector<T>& v, ObjectId id) {
  auto it = std::remove_if(v.begin(), v.end(), [&](const T& e) { return e.id == id; });
  if (it == v.end()) return false;
  v.erase(it, v.end());
  return true;
}

bool DrawingModel::removeEntity(ObjectId id) {
  for (auto& s : sheets_) {
    if (eraseById(s.dimensions, id) || eraseById(s.notes, id) || eraseById(s.centerMarks, id) ||
        eraseById(s.centerLines, id) || eraseById(s.balloons, id) || eraseById(s.partsLists, id) ||
        s.callouts.erase(id)) {
      notifyChanged(s.id);
      return true;
    }
  }
  return false;
}

bool DrawingModel::removeObject(ObjectId id) { return view(id) ? removeView(id) : removeEntity(id); }

std::vector<BomRow> DrawingModel::bom(const DrawingView* v) const {
  // A parts list lists the whole design (Fusion: the top-level assembly of the referenced
  // design). Only a view made of explicit components narrows it to those sub-assemblies; a
  // base view that took a body preselection does not (UX-37).
  std::vector<core::ShapeRef> comps;
  if (v)
    for (const core::ShapeRef& r : v->sources)
      if (r.kind == core::ShapeKind::Component && r.owner != core::kRootComponentId) comps.push_back(r);
  return buildBom(doc_, comps);
}

void fitPartsListInFrame(const Sheet& sheet, PartsList& pl) {
  const BBox2 f = sheet.frameRect();
  if (!f.isValid()) return;
  const double margin = 2.0;
  const double avail = f.width() - 2 * margin;
  double w = pl.width();
  if (w > avail && w > 0) {
    const double k = avail / w;
    for (double& c : pl.columnWidths) c *= k;
    pl.textHeight = std::max(1.8, pl.textHeight * k);
    w = pl.width();
  }
  const double h = pl.height();
  if (pl.position.x + w > f.max.x - margin) pl.position.x = f.max.x - margin - w;
  if (pl.position.x < f.min.x + margin) pl.position.x = f.min.x + margin;
  if (pl.position.y + h > f.max.y - margin) pl.position.y = f.max.y - margin - h;
  if (pl.position.y < f.min.y + margin) pl.position.y = f.min.y + margin;
}

void DrawingModel::refreshAnnotations(Sheet& sheet) {
  for (auto& d : sheet.dimensions) {
    const DrawingView* v = sheet.view(d.viewId);
    if (v) updateDimension(d, *v);
    else d.valid = false;
  }
  std::vector<BomRow> rows;
  bool haveRows = false;
  for (auto& pl : sheet.partsLists) {
    pl.rows = bom(sheet.view(pl.viewId));
    fitPartsListInFrame(sheet, pl);
    if (!haveRows) {
      rows = pl.rows;
      haveRows = true;
    }
  }
  if (!sheet.balloons.empty()) {
    if (!haveRows) rows = bom(nullptr);
    for (auto& b : sheet.balloons) b.itemNumber = bomItemFor(rows, b.itemKey);
  }
  refreshCallouts(doc_, sheet);
}

// ---------------------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------------------

std::vector<SourceShape> DrawingModel::collectSources(const DrawingView& v) const {
  std::vector<SourceShape> out;
  std::set<std::pair<ObjectId, const void*>> seen;
  auto addBody = [&](ObjectId bodyId, const gp_Trsf& trsf, bool checkVisible) {
    const core::Body* b = doc_.body(bodyId);
    if (!b || (checkVisible && !b->visible)) return;
    const TopoDS_Shape& shape = doc_.bodyShape(bodyId);
    if (shape.IsNull()) return;
    auto key = std::make_pair(bodyId, static_cast<const void*>(shape.TShape().get()));
    // the same body may legitimately appear several times (occurrences): key on location too
    (void)seen;
    try {
      BRepBuilderAPI_Transform tr(shape, trsf, true);  // deep copy: worker-safe
      SourceShape src{bodyId, tr.Shape(), {}};
      // Stable identities of the edges (history name + body-local signature) so dimensions
      // survive the renumbering that every upstream edit causes (see Curve2D::sourceName).
      TopTools_IndexedMapOfShape localEdges, worldEdges;
      TopExp::MapShapes(shape, TopAbs_EDGE, localEdges);
      TopExp::MapShapes(src.shape, TopAbs_EDGE, worldEdges);
      std::vector<std::string> localNames(static_cast<size_t>(localEdges.Extent()));
      for (const auto& [name, sub] : part::currentNames(doc_, bodyId)) {
        if (sub.ShapeType() != TopAbs_EDGE) continue;
        int i = localEdges.FindIndex(sub);
        if (i > 0 && localNames[static_cast<size_t>(i) - 1].empty()) localNames[static_cast<size_t>(i) - 1] = name;
      }
      src.edges.resize(static_cast<size_t>(worldEdges.Extent()));
      for (int i = 1; i <= localEdges.Extent(); ++i) {
        int wi = 0;
        try {
          wi = worldEdges.FindIndex(tr.ModifiedShape(localEdges(i)));
        } catch (const Standard_Failure&) {
          wi = i <= worldEdges.Extent() ? i : 0;  // same structure, same order
        }
        if (wi <= 0) continue;
        SourceEdgeId& id = src.edges[static_cast<size_t>(wi) - 1];
        id.name = localNames[static_cast<size_t>(i) - 1];
        id.signature = core::computeSignature(localEdges(i));
      }
      out.push_back(std::move(src));
    } catch (const Standard_Failure&) {
    }
    (void)key;
  };
  std::function<void(ObjectId, const gp_Trsf&, bool)> addComponent = [&](ObjectId compId, const gp_Trsf& world,
                                                                         bool checkVisible) {
    const core::Component* c = doc_.component(compId);
    if (!c || (checkVisible && !c->visible)) return;
    ObjectId def = c->isOccurrence() ? c->definition : compId;
    for (ObjectId b : doc_.bodiesOf(def)) addBody(b, world, checkVisible);
    const core::Component* d = doc_.component(def);
    if (!d) return;
    for (ObjectId child : d->children) {
      const core::Component* cc = doc_.component(child);
      if (!cc) continue;
      gp_Trsf w = world;
      w.Multiply(cc->placement);
      addComponent(child, w, checkVisible);
    }
  };
  if (v.sources.empty()) {
    addComponent(core::kRootComponentId, gp_Trsf(), true);
    return out;
  }
  for (const auto& r : v.sources) {
    if (r.kind == core::ShapeKind::Body) {
      const core::Body* b = doc_.body(r.owner);
      if (b) addBody(r.owner, doc_.worldPlacement(b->component), false);
    } else if (r.kind == core::ShapeKind::Component) {
      addComponent(r.owner, doc_.worldPlacement(r.owner), false);
    }
  }
  return out;
}

ViewJob DrawingModel::makeJob(const DrawingView& v, bool exact) const {
  ViewJob job;
  job.viewId = v.id;
  job.generation = 0;
  job.projection.sources = collectSources(v);
  job.projection.frame = v.definitionFrame();
  job.projection.hiddenLines = v.hiddenLines;
  job.projection.tangentEdges = v.tangentEdges != TangentEdgeMode::Off;
  job.projection.exact = exact;
  if (v.type == ViewType::Section) {
    job.doSection = true;
    job.section.sources = job.projection.sources;
    job.section.frame = v.customFrame;
    job.section.hatch = v.hatch;
    job.section.hatchSpacing = style_.hatchSpacing / std::max(1e-6, v.scale);
    job.section.hatchAngle = style_.hatchAngle;
    if (v.section.isComplex()) {
      // stepped / aligned cut: the path lives in the parent's projection frame
      const DrawingView* parent = view(v.parent);
      job.section.parentFrame = parent ? (parent->geometryValid ? parent->frame : parent->definitionFrame()) : v.customFrame;
      job.section.path = v.section.points;
      job.section.arrow = v.section.arrow;
      job.section.aligned = v.section.aligned;
    }
  }
  return job;
}

bool DrawingModel::prepareJob(const Sheet& sheet, DrawingView& v, ViewJob& job) const {
  (void)sheet;
  job = makeJob(v, v.exact);
  job.generation = ++const_cast<DrawingModel*>(this)->generation_[v.id];
  return !job.projection.sources.empty();
}

void DrawingModel::moveObjects(Sheet& sheet, const std::vector<ObjectId>& ids, const Point2& delta) {
  if (delta.length() < 1e-12 || ids.empty()) return;
  std::vector<ObjectId> viewIds;
  for (ObjectId id : ids)
    if (sheet.view(id)) viewIds.push_back(id);
  for (ObjectId id : ids) {
    if (DrawingView* v = sheet.view(id)) {
      // a view whose ancestor is also moved follows through moveView(); skip it
      bool ancestorMoved = false;
      for (ObjectId p = v->parent; p != core::kInvalidId;) {
        if (std::find(viewIds.begin(), viewIds.end(), p) != viewIds.end()) {
          ancestorMoved = true;
          break;
        }
        const DrawingView* pv = sheet.view(p);
        p = pv ? pv->parent : core::kInvalidId;
      }
      if (!ancestorMoved) moveView(id, v->position + delta);
    } else if (Dimension* d = sheet.dimension(id)) {
      d->textPos += delta;
    } else if (TextNote* n = sheet.note(id)) {
      n->position += delta;
      if (n->leader) n->leaderTo += delta;
    } else if (Balloon* b = sheet.balloon(id)) {
      b->position += delta;
    } else if (PartsList* p = sheet.partsList(id)) {
      p->position += delta;
    } else {
      sheet.callouts.move(id, delta, &sheet);
    }
  }
  notifyChanged(sheet.id);
}

void DrawingModel::invalidateView(ObjectId id) {
  Sheet* s = nullptr;
  DrawingView* v = view(id, &s);
  if (!v) return;
  v->geometryValid = false;
  v->geometryPending = true;
  ++generation_[id];  // drops results of running jobs
  if (v->type != ViewType::Detail) cache_.erase(id);
  for (ObjectId child : s->childrenOf(id)) {
    DrawingView* c = s->view(child);
    if (c && c->type == ViewType::Detail) {
      c->geometryValid = false;
      c->geometryPending = true;
    }
  }
}

void DrawingModel::invalidateAll() {
  for (auto& s : sheets_)
    for (auto& v : s.views) {
      v.geometryValid = false;
      v.geometryPending = true;
      ++generation_[v.id];
    }
  cache_.clear();
}

void DrawingModel::updateGeometry() {
  for (auto& s : sheets_) {
    for (auto& v : s.views) {
      if (v.geometryValid || !v.geometryPending) continue;
      if (v.type == ViewType::Detail) continue;  // derived from the parent on completion
      // skip if a job for the current generation is already running
      bool running = false;
      for (const auto& j : jobs_)
        if (j.viewId == v.id && j.generation == generation_[v.id]) running = true;
      if (running) continue;
      ViewJob job;
      if (!prepareJob(s, v, job)) {
        v.geometryPending = false;
        v.geometryValid = false;
        v.curves.clear();
        v.vertices.clear();
        v.bbox = BBox2{};
        v.error = "no geometry";
        finishView(s, v);
        emit viewGeometryChanged(v.id);
        continue;
      }
      startJob(job);
    }
    // detail views whose parent is already valid
    for (auto& v : s.views)
      if (v.type == ViewType::Detail && !v.geometryValid) {
        const DrawingView* parent = s.view(v.parent);
        if (parent && parent->geometryValid) {
          computeDetail(v, *parent);
          finishView(s, v);
          emit viewGeometryChanged(v.id);
        }
      }
  }
}

void DrawingModel::startJob(const ViewJob& job) {
  if (synchronous_) {
    applyResult(runViewJob(job));
    return;
  }
  bool wasBusy = isBusy();
  auto* watcher = new QFutureWatcher<ViewJobResult>(this);
  jobs_.push_back({job.viewId, job.generation, watcher});
  connect(watcher, &QFutureWatcher<ViewJobResult>::finished, this, [this, watcher]() {
    ViewJobResult r = watcher->result();
    jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [&](const RunningJob& j) { return j.watcher == watcher; }),
                jobs_.end());
    watcher->deleteLater();
    applyResult(r);
    if (!isBusy()) emit busyChanged(false);
  });
  watcher->setFuture(QtConcurrent::run(runViewJob, job));
  if (!wasBusy) emit busyChanged(true);
}

void DrawingModel::applyResult(const ViewJobResult& r) {
  Sheet* s = nullptr;
  DrawingView* v = view(r.viewId, &s);
  if (!v || !s) return;
  if (r.generation != generation_[r.viewId]) return;  // stale
  v->geometryPending = false;
  if (!r.error.empty()) {
    v->geometryValid = false;
    v->error = r.error;
    v->curves.clear();
    v->vertices.clear();
    v->bbox = BBox2{};
    FC_WARN("drawing: view {} failed: {}", v->label, r.error);
  } else {
    v->error.clear();
    v->curves = r.projection.curves;
    v->vertices = collectVertices(v->curves, 1e-3);
    v->bbox = r.projection.bbox;
    v->origin3d = r.projection.origin;
    v->frame = r.projection.frame;
    v->geometryValid = true;
    ++v->generation;
    if (v->autoPlace) autoArrange(*s, *v);
    cacheGeometry(*v);
    FC_DEBUG("drawing: view {} projected in {:.0f} ms ({} curves)", v->label, r.projection.millis, v->curves.size());
  }
  finishView(*s, *v);
  emit viewGeometryChanged(v->id);
}

namespace {
constexpr double kViewGap = 15.0;       ///< minimum clearance between views (mm)
constexpr double kFrameInset = 5.0;     ///< keep views this far inside the frame (mm)

void extentAlong(const BBox2& b, const Point2& ref, const Point2& dir, double& lo, double& hi) {
  lo = 1e300;
  hi = -1e300;
  for (const Point2& c : {b.min, b.max, Point2{b.min.x, b.max.y}, Point2{b.max.x, b.min.y}}) {
    double t = (c - ref).dot(dir);
    lo = std::min(lo, t);
    hi = std::max(hi, t);
  }
}

bool boxesOverlap(const BBox2& a, const BBox2& b, double gap) {
  return a.isValid() && b.isValid() && a.min.x < b.max.x + gap && b.min.x < a.max.x + gap && a.min.y < b.max.y + gap &&
         b.min.y < a.max.y + gap;
}
}  // namespace

void DrawingModel::autoArrange(Sheet& sheet, DrawingView& v) {
  v.autoPlace = false;
  if (!v.geometryValid || v.autoDir.length() < 0.5) return;
  const Point2 d = v.autoDir.normalized();
  const DrawingView* parent = sheet.view(v.parent);
  // 1. at least parent half-size + gap + own half-size from the parent along d (never closer)
  if (parent && parent != &v && parent->geometryValid) {
    double pLo, pHi, cLo, cHi;
    extentAlong(parent->sheetBBox(), parent->position, d, pLo, pHi);
    extentAlong(v.sheetBBox(), v.position, d, cLo, cHi);
    double cur = (v.position - parent->position).dot(d);
    double need = pHi + kViewGap - cLo;
    if (cur < need) v.position += d * (need - cur);
  }
  // 2. push clear of every other view, moving along one axis of d (keeps aligned views aligned)
  for (int iter = 0; iter < 16; ++iter) {
    BBox2 cb = v.sheetBBox();
    bool moved = false;
    for (const auto& o : sheet.views) {
      if (o.id == v.id || !o.geometryValid) continue;
      BBox2 ob = o.sheetBBox();
      if (!boxesOverlap(cb, ob, kViewGap * 0.5)) continue;
      double dx = 0, dy = 0;
      if (std::abs(d.x) > 1e-9) dx = d.x > 0 ? (ob.max.x + kViewGap - cb.min.x) : (ob.min.x - kViewGap - cb.max.x);
      if (std::abs(d.y) > 1e-9) dy = d.y > 0 ? (ob.max.y + kViewGap - cb.min.y) : (ob.min.y - kViewGap - cb.max.y);
      Point2 shift;
      if (dx != 0 && (dy == 0 || std::abs(dx) <= std::abs(dy))) shift = {dx, 0};
      else shift = {0, dy};
      if (shift.length() < 1e-9) continue;
      v.position += shift;
      moved = true;
      break;
    }
    if (!moved) break;
  }
  // 3. inside the frame (aligned views keep their alignment axis), above the title block
  BBox2 f = sheet.frameRect().expanded(-kFrameInset);
  if (sheet.titleBlock.visible) {
    BBox2 tb = sheet.titleBlockRect();
    BBox2 cb = v.sheetBBox();
    if (boxesOverlap(cb, tb, 0.0) && cb.height() < tb.min.y - f.min.y) f.max.y = std::min(f.max.y, tb.min.y - kFrameInset);
  }
  BBox2 cb = v.sheetBBox();
  if (v.alignment != ViewAlignment::Vertical && cb.width() <= f.width()) {
    if (cb.min.x < f.min.x) v.position.x += f.min.x - cb.min.x;
    else if (cb.max.x > f.max.x) v.position.x -= cb.max.x - f.max.x;
  }
  if (v.alignment != ViewAlignment::Horizontal && cb.height() <= f.height()) {
    if (cb.min.y < f.min.y) v.position.y += f.min.y - cb.min.y;
    else if (cb.max.y > f.max.y) v.position.y -= cb.max.y - f.max.y;
  }
}

void DrawingModel::finishView(Sheet& sheet, DrawingView& v) {
  updateDetailViews(sheet, v);
  refreshAnnotations(sheet);
  emit sheetChanged(sheet.id);
}

void DrawingModel::updateDetailViews(Sheet& sheet, const DrawingView& parent) {
  for (auto& v : sheet.views) {
    if (v.type != ViewType::Detail || v.parent != parent.id) continue;
    computeDetail(v, parent);
    emit viewGeometryChanged(v.id);
  }
}

void DrawingModel::computeDetail(DrawingView& detail, const DrawingView& parent) {
  detail.curves.clear();
  detail.geometryPending = false;
  if (!parent.geometryValid) {
    detail.geometryValid = false;
    detail.error = parent.error.empty() ? "parent not ready" : parent.error;
    return;
  }
  const Point2 c = detail.detail.center;
  const double r = detail.detail.radius;
  for (const auto& curve : parent.curves) {
    for (auto& piece : clipToCircle(curve, c, r)) {
      // re-centre on the detail circle so the view origin is the circle centre
      piece = piece.transformed(-c, 1.0, false);
      detail.curves.push_back(std::move(piece));
    }
  }
  detail.curves.push_back(Curve2D::circle({0, 0}, r, LineClass::Detail));
  detail.vertices = collectVertices(detail.curves, 1e-3);
  detail.bbox = BBox2{};
  for (const auto& k : detail.curves) detail.bbox.add(k.bbox());
  detail.frame = parent.frame;
  detail.origin3d = core::Vec3::from(viewToWorld(parent.frame, c));
  detail.geometryValid = true;
  detail.error.clear();
  ++detail.generation;
}

void DrawingModel::cacheGeometry(const DrawingView& v) {
  CachedGeometry g;
  g.key = v.geometryKey();
  g.docGeneration = docGeneration_;
  g.curves = v.curves;
  g.vertices = v.vertices;
  g.bbox = v.bbox;
  g.origin3d = v.origin3d;
  g.frame = v.frame;
  cache_[v.id] = std::move(g);
}

void DrawingModel::restoreCachedGeometry() {
  for (auto& s : sheets_) {
    for (auto& v : s.views) {
      if (v.type == ViewType::Detail) continue;
      auto it = cache_.find(v.id);
      if (it == cache_.end() || it->second.docGeneration != docGeneration_ || it->second.key != v.geometryKey()) {
        v.geometryValid = false;
        v.geometryPending = true;
        continue;
      }
      const CachedGeometry& g = it->second;
      v.curves = g.curves;
      v.vertices = g.vertices;
      v.bbox = g.bbox;
      v.origin3d = g.origin3d;
      v.frame = g.frame;
      v.geometryValid = true;
      v.geometryPending = false;
      v.error.clear();
    }
    for (auto& v : s.views) {
      if (v.type != ViewType::Detail) continue;
      const DrawingView* parent = s.view(v.parent);
      if (parent && parent->geometryValid) computeDetail(v, *parent);
      else {
        v.geometryValid = false;
        v.geometryPending = true;
      }
    }
    refreshAnnotations(s);
  }
}

bool DrawingModel::waitForIdle(int timeoutMs) {
  QElapsedTimer t;
  t.start();
  while (isBusy()) {
    if (t.elapsed() > timeoutMs) return false;
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }
  return true;
}

void DrawingModel::onRecomputeFinished() {
  ++docGeneration_;
  if (sheets_.empty()) return;
  invalidateAll();
  updateGeometry();
}

void DrawingModel::onDocumentLoaded() {
  if (loadPending_) return;
  loadPending_ = true;
  QTimer::singleShot(0, this, [this]() {
    loadPending_ = false;
    loadFromDocument();
  });
}

// ---------------------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------------------

QJsonObject DrawingModel::toJson() const {
  QJsonObject o;
  o["version"] = kFormatVersion;
  o["activeSheet"] = idToJson(activeSheet_);
  QJsonArray arr;
  for (const auto& s : sheets_) arr.push_back(s.toJson());
  o["sheets"] = arr;
  return o;
}

void DrawingModel::fromJson(const QJsonObject& o) {
  std::vector<Sheet> loaded;
  for (const auto& v : o["sheets"].toArray()) loaded.push_back(Sheet::fromJson(v.toObject()));
  sheets_ = std::move(loaded);
  ObjectId active = idFromJson(o["activeSheet"]);
  activeSheet_ = sheet(active) ? active : (sheets_.empty() ? core::kInvalidId : sheets_.front().id);
  // drop cache entries for views that no longer exist
  std::set<ObjectId> ids;
  for (const auto& s : sheets_)
    for (const auto& v : s.views) ids.insert(v.id);
  for (auto it = cache_.begin(); it != cache_.end();)
    it = ids.count(it->first) ? std::next(it) : cache_.erase(it);
  restoreCachedGeometry();
  emit sheetsChanged();
  emit activeSheetChanged(activeSheet_);
  for (const auto& s : sheets_) emit sheetChanged(s.id);
  updateGeometry();
}

void DrawingModel::saveToDocument() {
  if (sheets_.empty() && doc_.extensionData(kExtensionKey).isEmpty()) return;
  doc_.setExtensionData(kExtensionKey, toJson());
}

void DrawingModel::loadFromDocument() {
  ++docGeneration_;
  cache_.clear();
  fromJson(doc_.extensionData(kExtensionKey));
}

// ---------------------------------------------------------------------------------------
// Undo
// ---------------------------------------------------------------------------------------

void DrawingModel::edit(const std::string& text, core::UndoStack* undo, const std::function<void()>& fn, int mergeKey) {
  QJsonObject before = toJson();
  fn();
  QJsonObject after = toJson();
  if (undo) undo->push(std::make_unique<DrawingSnapshotCommand>(*this, text, before, after, mergeKey));
  updateGeometry();
}

void DrawingModel::commitSnapshot(const std::string& text, core::UndoStack* undo, const QJsonObject& before) {
  QJsonObject after = toJson();
  if (after == before) return;
  if (undo) undo->push(std::make_unique<DrawingSnapshotCommand>(*this, text, before, after, -1));
  doc_.setModified(true);
  updateGeometry();
}

void DrawingModel::revertTo(const QJsonObject& before) {
  if (toJson() == before) return;
  fromJson(before);
}

void DrawingModel::notifyChanged(ObjectId sheetId) {
  doc_.setModified(true);
  emit sheetChanged(sheetId);
}

DrawingSnapshotCommand::DrawingSnapshotCommand(DrawingModel& model, std::string text, QJsonObject before,
                                               QJsonObject after, int mergeKey)
    : core::UndoCommand(std::move(text)), model_(model), before_(std::move(before)), after_(std::move(after)),
      mergeKey_(mergeKey) {}

void DrawingSnapshotCommand::redo() {
  if (first_) {
    first_ = false;  // already applied by DrawingModel::edit
    return;
  }
  model_.fromJson(after_);
  model_.document().setModified(true);
}

void DrawingSnapshotCommand::undo() {
  model_.fromJson(before_);
  model_.document().setModified(true);
}

bool DrawingSnapshotCommand::mergeWith(const core::UndoCommand& other) {
  const auto* o = dynamic_cast<const DrawingSnapshotCommand*>(&other);
  if (!o || o->mergeKey_ != mergeKey_ || mergeKey_ < 0) return false;
  after_ = o->after_;
  return true;
}

}  // namespace fabricad::drawing
