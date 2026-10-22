// FabriCAD drawing: DrawingCanvas implementation (QGraphicsView sheet canvas).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "drawing/DrawingCanvas.h"

#include "core/Document.h"
#include "core/Json.h"
#include "core/Selection.h"
#include "core/Tool.h"
#include "core/UndoStack.h"
#include "drawing/CalloutRenderer.h"
#include "drawing/DrawingModel.h"
#include "drawing/Sheet.h"
#include "theme/Tokens.h"

#include <QGraphicsScene>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QStyleOptionGraphicsItem>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <format>
#include <cmath>

namespace fabricad::drawing {

using core::ObjectId;

namespace {

constexpr double kSheetMargin = 25.0;  // mm of grey around the paper
// Colours (docs/plans/ui-redesign.md 4.5). Everything drawn ON the sheet sits on white paper in both
// themes, so it uses the light-theme value of its token; only the surround follows the theme. They are
// read at paint time (no cached QColor survives a theme switch).
using theme::Tok;
QColor onPaper(Tok t) { return theme::color(t, theme::Theme::Light); }
QColor selectColor() { return onPaper(Tok::DrawingSelect); }
QColor hoverColor() { return onPaper(Tok::PreEdge); }
QColor highlightColor() { return onPaper(Tok::LiveOutline); }
QColor busyColor() { return onPaper(Tok::TextTertiary); }
QColor errorColor() { return onPaper(Tok::StatusError); }
QColor previewColor() { return onPaper(Tok::PreviewNewBodyEdge); }

constexpr int kZSheet = 0, kZView = 10, kZEntity = 20, kZRubber = 90, kZPreview = 100;

core::MouseButton toButton(Qt::MouseButton b) {
  switch (b) {
    case Qt::LeftButton: return core::MouseButton::Left;
    case Qt::MiddleButton: return core::MouseButton::Middle;
    case Qt::RightButton: return core::MouseButton::Right;
    default: return core::MouseButton::None;
  }
}

int toButtons(Qt::MouseButtons b) {
  int m = 0;
  if (b & Qt::LeftButton) m |= 1;
  if (b & Qt::MiddleButton) m |= 2;
  if (b & Qt::RightButton) m |= 4;
  return m;
}

int toModifiers(Qt::KeyboardModifiers m) {
  int r = core::ModNone;
  if (m & Qt::ShiftModifier) r |= core::ModShift;
  if (m & Qt::ControlModifier) r |= core::ModCtrl;
  if (m & Qt::AltModifier) r |= core::ModAlt;
  return r;
}

/// Shortest distance from p to anything in the list (0 when inside a fill or a text box).
double distanceToList(const DisplayList& list, const Point2& p) {
  double best = 1e300;
  for (const auto& c : list.curves) best = std::min(best, c.distanceTo(p));
  for (const auto& f : list.fills)
    if (pointInPolygon(f.polygon, p)) return 0.0;
  for (const auto& t : list.texts) {
    double w = SheetRenderer::textWidth(t.text, t.height), h = t.height * 1.3;
    double x0 = t.pos.x, y0 = t.pos.y;
    if (t.halign == HAlign::Center) x0 -= w / 2;
    else if (t.halign == HAlign::Right) x0 -= w;
    if (t.valign == VAlign::Middle) y0 -= h / 2;
    else if (t.valign == VAlign::Bottom) y0 -= h;
    if (std::abs(t.angle) < 1e-9) {
      if (p.x >= x0 && p.x <= x0 + w && p.y >= y0 && p.y <= y0 + h) return 0.0;
    } else {
      // rotated text: test in the text frame
      Point2 d = p - t.pos;
      double a = -deg2rad(t.angle);
      Point2 r{d.x * std::cos(a) - d.y * std::sin(a), d.x * std::sin(a) + d.y * std::cos(a)};
      double lx = t.halign == HAlign::Center ? -w / 2 : (t.halign == HAlign::Right ? -w : 0);
      double ly = t.valign == VAlign::Middle ? -h / 2 : (t.valign == VAlign::Bottom ? -h : 0);
      if (r.x >= lx && r.x <= lx + w && r.y >= ly && r.y <= ly + h) return 0.0;
    }
  }
  return best;
}

QRectF toRect(const BBox2& b) {
  if (!b.isValid()) return QRectF();
  return QRectF(b.min.x, b.min.y, b.width(), b.height());
}

}  // namespace

const char* hitKindName(HitKind k) {
  switch (k) {
    case HitKind::None: return "None";
    case HitKind::View: return "View";
    case HitKind::Dimension: return "Dimension";
    case HitKind::Note: return "Note";
    case HitKind::CenterMark: return "CenterMark";
    case HitKind::CenterLine: return "CenterLine";
    case HitKind::Balloon: return "Balloon";
    case HitKind::PartsList: return "PartsList";
    case HitKind::TitleBlock: return "TitleBlock";
    case HitKind::Callout: return "Callout";
  }
  return "None";
}

// ---------------------------------------------------------------------------------------
// DisplayListItem
// ---------------------------------------------------------------------------------------

DisplayListItem::DisplayListItem(HitKind kind, ObjectId id, DisplayList list, const std::string& font)
    : kind_(kind), id_(id), list_(std::move(list)), font_(font), color_(onPaper(Tok::DrawingInk)) {
  setAcceptedMouseButtons(Qt::NoButton);  // the canvas routes input itself
  updateBounds();
}

void DisplayListItem::setList(DisplayList list) {
  prepareGeometryChange();
  list_ = std::move(list);
  updateBounds();
  update();
}

void DisplayListItem::updateBounds() {
  BBox2 b = list_.bbox();
  if (!b.isValid()) {
    bounds_ = QRectF();
    return;
  }
  // text widths are estimates; keep a generous margin so nothing is clipped
  double m = 2.0;
  for (const auto& t : list_.texts) m = std::max(m, t.height * 0.6);
  bounds_ = toRect(b.expanded(m));
}

void DisplayListItem::setState(bool selected, bool hovered, bool highlighted) {
  if (selected_ == selected && hovered_ == hovered && highlighted_ == highlighted) return;
  selected_ = selected;
  hovered_ = hovered;
  highlighted_ = highlighted;
  update();
}

void DisplayListItem::setDragOffset(const Point2& d) { setPos(d.x, d.y); }

void DisplayListItem::paint(QPainter* p, const QStyleOptionGraphicsItem* opt, QWidget* w) {
  (void)opt;
  (void)w;
  QColor c = color_;
  if (highlighted_) c = highlightColor();
  if (hovered_) c = hoverColor();
  if (selected_) c = selectColor();
  if (busy_ && list_.curves.empty()) c = busyColor();
  p->setRenderHint(QPainter::Antialiasing, true);
  SheetRenderer::paint(*p, list_, c, font_);
  const bool frame = (kind_ == HitKind::View || kind_ == HitKind::PartsList) && (selected_ || hovered_ || highlighted_ || busy_);
  if (frame || !error_.empty()) {
    QPen pen(c, 0.25, Qt::DashLine);
    pen.setCosmetic(false);
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    p->drawRect(bounds_.adjusted(0.5, 0.5, -0.5, -0.5));
  }
  if (busy_ || !error_.empty()) {
    QFont f(QString::fromStdString(font_));
    f.setPointSizeF(1.0);
    p->setFont(f);
    p->save();
    QPointF at(bounds_.left() + 1.0, bounds_.top() - 1.0);
    p->translate(at);
    p->scale(3.0, 3.0);  // 3 mm text
    p->setPen(busy_ ? busyColor() : errorColor());
    p->drawText(QPointF(0, 0), QString::fromStdString(busy_ ? std::string("updating...") : error_));
    p->restore();
  }
}

// ---------------------------------------------------------------------------------------
// DrawingCanvas
// ---------------------------------------------------------------------------------------

DrawingCanvas::DrawingCanvas(QWidget* parent) : QGraphicsView(parent) {
  scene_ = new QGraphicsScene(this);
  setScene(scene_);
  setRenderHint(QPainter::Antialiasing, true);
  setRenderHint(QPainter::TextAntialiasing, true);
  setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
  setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
  setResizeAnchor(QGraphicsView::AnchorViewCenter);
  setDragMode(QGraphicsView::NoDrag);
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  // the surround (drawing.canvas) is painted in drawBackground at paint time, so it follows the theme
  setFrameShape(QFrame::NoFrame);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  scene_->setSceneRect(-kSheetMargin, -kSheetMargin, 420 + 2 * kSheetMargin, 297 + 2 * kSheetMargin);
  // initial zoom ~ 2 px/mm so an empty canvas is usable before the first fit
  setTransform(QTransform::fromScale(2.0, 2.0));
}

DrawingCanvas::~DrawingCanvas() {
  for (auto& c : modelConnections_) QObject::disconnect(c);
}

const DrawingStyle& DrawingCanvas::style() const {
  static const DrawingStyle kDefault;
  return model_ ? model_->style() : kDefault;
}

void DrawingCanvas::setModel(DrawingModel* model) {
  if (model == model_) return;
  for (auto& c : modelConnections_) QObject::disconnect(c);
  modelConnections_.clear();
  model_ = model;
  sheetId_ = core::kInvalidId;
  selection_.clear();
  highlighted_.clear();
  hover_ = core::kInvalidId;
  if (model_) {
    undo_ = &model_->document().undoStack();
    connectModel();
    if (Sheet* s = model_->activeSheet()) sheetId_ = s->id;
  }
  rebuild();
  zoomToFit();
}

void DrawingCanvas::connectModel() {
  modelConnections_.push_back(connect(model_, &DrawingModel::sheetChanged, this, &DrawingCanvas::onSheetChanged));
  modelConnections_.push_back(connect(model_, &DrawingModel::viewGeometryChanged, this, [this](ObjectId viewId) {
    if (!model_) return;
    Sheet* s = model_->sheetOf(viewId);
    if (s && s->id == sheetId_) onSheetChanged(sheetId_);
  }));
  modelConnections_.push_back(connect(model_, &DrawingModel::sheetsChanged, this, &DrawingCanvas::onSheetsChanged));
  modelConnections_.push_back(connect(model_, &DrawingModel::busyChanged, this, &DrawingCanvas::onBusyChanged));
  modelConnections_.push_back(connect(model_, &DrawingModel::activeSheetChanged, this, [this](ObjectId id) {
    if (id != core::kInvalidId && id != sheetId_) setSheet(id);
  }));
  modelConnections_.push_back(connect(model_, &QObject::destroyed, this, [this]() {
    modelConnections_.clear();
    model_ = nullptr;
    items_.clear();
    sheetItem_ = previewItem_ = rubberItem_ = nullptr;
    scene_->clear();
  }));
}

void DrawingCanvas::onSheetsChanged() {
  if (!model_) return;
  if (!model_->sheet(sheetId_)) {
    Sheet* s = model_->activeSheet();
    sheetId_ = s ? s->id : core::kInvalidId;
    selection_.clear();
    zoomToFit();
  }
  rebuild();
}

void DrawingCanvas::onBusyChanged(bool busy) {
  emit statusMessage(busy ? tr("Updating views...") : QString());
}

void DrawingCanvas::onSheetChanged(ObjectId id) {
  if (id != sheetId_ || rebuildPending_) return;
  // coalesce bursts (several signals per model edit)
  rebuildPending_ = true;
  QTimer::singleShot(0, this, [this]() {
    rebuildPending_ = false;
    rebuild();
  });
}

void DrawingCanvas::setSheet(ObjectId sheetId) {
  if (sheetId == sheetId_) return;
  sheetId_ = sheetId;
  selection_.clear();
  hover_ = core::kInvalidId;
  if (model_ && model_->sheet(sheetId)) model_->setActiveSheet(sheetId);
  rebuild();
  zoomToFit();
  mirrorSelection();
  emit selectionChanged();
}

Sheet* DrawingCanvas::sheet() const { return model_ ? model_->sheet(sheetId_) : nullptr; }

// --- interaction --------------------------------------------------------------------------

void DrawingCanvas::setInteraction(ICanvasInteraction* i) {
  if (interaction_ == i) return;
  interaction_ = i;
  drag_ = DragState{};
  if (rubberItem_) rubberItem_->setList({});
  updatePreview();
  updateItemStates();
}

void DrawingCanvas::updatePreview() {
  if (!previewItem_) return;
  DisplayList list;
  if (interaction_) interaction_->canvasPreview(*this, list);
  previewItem_->setList(std::move(list));
  updateItemStates();
  viewport()->update();
}

void DrawingCanvas::setHighlighted(std::vector<ObjectId> ids) {
  highlighted_ = std::move(ids);
  updateItemStates();
}

// --- selection ----------------------------------------------------------------------------

void DrawingCanvas::setSelection(std::vector<ObjectId> ids) {
  // validate against the model (items may not be rebuilt yet after an edit)
  const Sheet* s = sheet();
  std::vector<ObjectId> clean;
  for (ObjectId id : ids)
    if (s && s->contains(id) && std::find(clean.begin(), clean.end(), id) == clean.end()) clean.push_back(id);
  if (clean == selection_) return;
  selection_ = std::move(clean);
  updateItemStates();
  mirrorSelection();
  emit selectionChanged();
}

void DrawingCanvas::select(ObjectId id, bool add) {
  const Sheet* sh = sheet();
  if (!sh || !sh->contains(id)) return;
  std::vector<ObjectId> sel = add ? selection_ : std::vector<ObjectId>{};
  auto it = std::find(sel.begin(), sel.end(), id);
  if (add && it != sel.end()) sel.erase(it);
  else sel.push_back(id);
  setSelection(std::move(sel));
}

void DrawingCanvas::clearSelection() { setSelection({}); }

bool DrawingCanvas::isSelected(ObjectId id) const { return std::find(selection_.begin(), selection_.end(), id) != selection_.end(); }

std::vector<ObjectId> DrawingCanvas::selectedViews() const {
  std::vector<ObjectId> out;
  for (ObjectId id : selection_) {
    auto it = items_.find(id);
    if (it != items_.end() && it->second->kind() == HitKind::View) out.push_back(id);
  }
  return out;
}

void DrawingCanvas::mirrorSelection() {
  if (!model_ || mirroring_) return;
  mirroring_ = true;
  std::vector<core::ShapeRef> refs;
  const Sheet* s = sheet();
  for (ObjectId id : selection_) {
    const bool isView = s && s->view(id) != nullptr;
    auto it = items_.find(id);
    std::string kindName = it != items_.end() ? hitKindName(it->second->kind()) : (isView ? "View" : "Entity");
    if (isView) {
      // status-bar text (UX-43): "VIEW1 (Front)", "Section A-A", "Detail B"
      const DrawingView& v = *s->view(id);
      const std::string label = v.label.empty() ? std::format("View {}", id) : v.label;
      switch (v.type) {
        case ViewType::Base: kindName = std::format("{} ({})", label, viewOrientationName(v.orientation)); break;
        case ViewType::Projected: kindName = std::format("{} (Projected)", label); break;
        case ViewType::Section: kindName = label.starts_with("Section") ? label : "Section " + label; break;
        case ViewType::Detail: kindName = label.starts_with("Detail") || label.starts_with("DETAIL") ? label : "Detail " + label; break;
      }
    }
    refs.push_back(core::ShapeRef::make(id, isView ? core::ShapeKind::DrawingView : core::ShapeKind::DrawingEntity, 0, kindName));
  }
  model_->document().selection().set(std::move(refs));
  mirroring_ = false;
}

void DrawingCanvas::updateItemStates() {
  std::vector<ObjectId> hl = highlighted_;
  if (interaction_)
    for (ObjectId id : interaction_->canvasHighlights()) hl.push_back(id);
  for (auto& [id, item] : items_) {
    bool sel = isSelected(id);
    bool hov = id == hover_;
    bool high = std::find(hl.begin(), hl.end(), id) != hl.end();
    item->setState(sel, hov, high);
  }
  if (sheetItem_) sheetItem_->setState(false, hover_ == sheetId_ && sheetItem_->kind() == HitKind::TitleBlock, false);
}

// --- hit testing -----------------------------------------------------------------------------

double DrawingCanvas::pickTolerance() const { return std::clamp(pickPixels_ * mmPerPixel(), 0.3, 4.0); }

BBox2 DrawingCanvas::viewRect(const DrawingView& v) const {
  BBox2 b = v.sheetBBox();
  if (v.showLabel || v.type == ViewType::Section || v.type == ViewType::Detail) {
    Point2 lp = v.labelPosition();
    double h = style().dimTextHeight * 1.2;
    double w = SheetRenderer::textWidth(viewLabelText(v), h);
    b.add({lp.x - w / 2, lp.y});
    b.add({lp.x + w / 2, lp.y + h * 1.3});
  }
  return b;
}

CanvasHit DrawingCanvas::hitViewGeometry(const DrawingView& view, const Point2& sheet) const {
  CanvasHit h;
  h.kind = HitKind::View;
  h.id = view.id;
  h.viewId = view.id;
  h.sheetPos = sheet;
  h.viewLocal = view.fromSheet(sheet);
  const double tolView = pickTolerance() / std::max(1e-9, view.scale);
  if (const Vertex2D* v = view.pickVertex(h.viewLocal, tolView * 1.2)) {
    h.onVertex = true;
    h.vertex = *v;
  }
  size_t idx = 0;
  if (const Curve2D* c = view.pickCurve(h.viewLocal, tolView, &idx)) {
    if (c->cls != LineClass::Hatch && c->cls != LineClass::Detail) {
      h.onCurve = true;
      h.curveIndex = idx;
      h.curve = *c;
    }
  }
  return h;
}

namespace {
bool isTableCallout(const Sheet& s, ObjectId id) {
  for (const auto& t : s.callouts.revisionTables)
    if (t.id == id) return true;
  return false;
}
}  // namespace

CanvasHit DrawingCanvas::hitTest(const Point2& at, bool wantGeometry) const {
  flushPendingRebuild();
  CanvasHit none;
  none.sheetPos = at;
  const Sheet* s = sheet();
  if (!s) return none;
  const double tol = pickTolerance();

  // 1. annotations (precise, by distance to their graphics)
  double bestDist = 1e300;
  const DisplayListItem* bestItem = nullptr;
  for (const auto& [id, item] : items_) {
    if (item->kind() == HitKind::View) continue;
    QRectF r = item->boundingRect().translated(item->pos()).adjusted(-tol, -tol, tol, tol);
    if (!r.contains(at.x, at.y)) continue;
    double d = distanceToList(item->list(), at - Point2{item->pos().x(), item->pos().y()});
    // tables are solid: a click anywhere in a cell hits them (DRW-ASM-OUTPUT-5: a double-click
    // inside the revision table opened Edit Sheet because it missed the table)
    if (item->kind() == HitKind::PartsList ||
        (item->kind() == HitKind::Callout && isTableCallout(*s, item->id())))
      if (item->boundingRect().translated(item->pos()).contains(at.x, at.y)) d = std::min(d, tol * 0.5);
    if (d <= tol && d < bestDist) {
      bestDist = d;
      bestItem = item;
    }
  }
  if (bestItem) {
    CanvasHit h;
    h.kind = bestItem->kind();
    h.id = bestItem->id();
    h.sheetPos = at;
    switch (h.kind) {
      case HitKind::Dimension: if (const Dimension* d = s->dimension(h.id)) h.viewId = d->viewId; break;
      case HitKind::CenterMark: for (const auto& m : s->centerMarks) if (m.id == h.id) h.viewId = m.viewId; break;
      case HitKind::CenterLine: for (const auto& l : s->centerLines) if (l.id == h.id) h.viewId = l.viewId; break;
      case HitKind::Balloon: for (const auto& b : s->balloons) if (b.id == h.id) h.viewId = b.viewId; break;
      case HitKind::PartsList: for (const auto& p : s->partsLists) if (p.id == h.id) h.viewId = p.viewId; break;
      case HitKind::Callout: h.viewId = s->callouts.viewOf(h.id); break;
      default: break;
    }
    return h;
  }

  // 2. view geometry (curves/vertices), smallest view first so details win over their parent
  std::vector<const DrawingView*> views;
  for (const auto& v : s->views) views.push_back(&v);
  std::sort(views.begin(), views.end(), [&](const DrawingView* a, const DrawingView* b) {
    BBox2 ra = viewRect(*a), rb = viewRect(*b);
    return ra.width() * ra.height() < rb.width() * rb.height();
  });
  if (wantGeometry) {
    for (const DrawingView* v : views) {
      if (!viewRect(*v).expanded(tol).contains(at)) continue;
      CanvasHit h = hitViewGeometry(*v, at);
      if (h.hasGeometry()) return h;
    }
  }
  // 3. inside a view's rectangle (or its label)
  for (const DrawingView* v : views) {
    if (viewRect(*v).expanded(tol).contains(at)) {
      CanvasHit h;
      h.kind = HitKind::View;
      h.id = v->id;
      h.viewId = v->id;
      h.sheetPos = at;
      h.viewLocal = v->fromSheet(at);
      return h;
    }
  }
  // 4. title block
  if (s->titleBlock.visible && s->titleBlockRect().contains(at)) {
    CanvasHit h;
    h.kind = HitKind::TitleBlock;
    h.id = s->id;
    h.sheetPos = at;
    return h;
  }
  return none;
}

bool DrawingCanvas::snapToVertex(const Point2& at, Point2& out, ObjectId* viewId) const {
  flushPendingRebuild();
  const Sheet* s = sheet();
  if (!s) return false;
  const double tol = pickTolerance() * 1.5;
  double best = tol;
  bool found = false;
  for (const auto& v : s->views) {
    if (!v.geometryValid) continue;
    if (!viewRect(v).expanded(tol).contains(at)) continue;
    for (const auto& vx : v.vertices) {
      Point2 sp = v.toSheet(vx.p);
      double d = sp.distance(at);
      if (d < best) {
        best = d;
        out = sp;
        found = true;
        if (viewId) *viewId = v.id;
      }
    }
  }
  return found;
}

// --- navigation ------------------------------------------------------------------------------

double DrawingCanvas::zoom() const { return transform().m11(); }

void DrawingCanvas::setZoom(double pixelsPerMm) {
  pixelsPerMm = std::clamp(pixelsPerMm, 0.2, 200.0);
  setTransform(QTransform::fromScale(pixelsPerMm, pixelsPerMm));
  emit zoomChanged(pixelsPerMm);
}

void DrawingCanvas::zoomBy(double factor, const std::optional<Point2>& aboutSheet) {
  double z = std::clamp(zoom() * factor, 0.2, 200.0);
  if (aboutSheet) {
    QPointF before = mapFromScene(aboutSheet->x, aboutSheet->y);
    setTransform(QTransform::fromScale(z, z));
    QPointF after = mapFromScene(aboutSheet->x, aboutSheet->y);
    QPointF d = after - before;
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + static_cast<int>(d.x()));
    verticalScrollBar()->setValue(verticalScrollBar()->value() + static_cast<int>(d.y()));
  } else {
    setTransform(QTransform::fromScale(z, z));
  }
  emit zoomChanged(z);
}

void DrawingCanvas::zoomToFit() {
  const Sheet* s = sheet();
  QRectF paper = s ? QRectF(0, 0, s->format.width, s->format.height) : QRectF(0, 0, 420, 297);
  scene_->setSceneRect(paper.adjusted(-kSheetMargin, -kSheetMargin, kSheetMargin, kSheetMargin));
  if (viewport()->width() < 50 || viewport()->height() < 50) {
    // not laid out yet: fit again on the first real resize
    fitPending_ = true;
    setZoom(2.0);
    return;
  }
  fitPending_ = false;
  fitInView(paper.adjusted(-5, -5, 5, 5), Qt::KeepAspectRatio);
  emit zoomChanged(zoom());
}

Point2 DrawingCanvas::sheetFromWidget(const QPoint& p) const {
  QPointF s = mapToScene(p);
  return {s.x(), s.y()};
}

QPointF DrawingCanvas::widgetFromSheet(const Point2& p) const { return mapFromScene(p.x, p.y); }

Point2 DrawingCanvas::defaultPlacement() const {
  const Sheet* s = sheet();
  if (!s) return {150, 100};
  BBox2 f = s->frameRect();
  double bottom = s->titleBlock.visible ? f.max.y - s->titleBlock.height : f.max.y;
  return {(f.min.x + f.max.x) / 2, (f.min.y + bottom) / 2};
}

// --- scene maintenance --------------------------------------------------------------------------

DisplayListItem* DrawingCanvas::item(ObjectId id) const {
  auto it = items_.find(id);
  return it == items_.end() ? nullptr : it->second;
}

DisplayList DrawingCanvas::displayList() const {
  flushPendingRebuild();
  DisplayList out;
  if (sheetItem_) out.append(sheetItem_->list());
  for (const auto& [id, item] : items_) out.append(item->list());
  return out;
}

void DrawingCanvas::flushPendingRebuild() const {
  if (rebuildPending_) {
    auto* self = const_cast<DrawingCanvas*>(this);
    self->rebuildPending_ = false;
    self->rebuild();
  }
}

void DrawingCanvas::rebuild() {
  // keep the preview/rubber items alive across rebuilds
  DisplayList preview, rubber;
  if (previewItem_) preview = previewItem_->list();
  if (rubberItem_) rubber = rubberItem_->list();
  scene_->clear();
  items_.clear();
  sheetItem_ = previewItem_ = rubberItem_ = nullptr;

  const std::string font = style().fontFamily;
  previewItem_ = new DisplayListItem(HitKind::None, core::kInvalidId, preview, font);
  previewItem_->setColor(previewColor());
  previewItem_->setZValue(kZPreview);
  scene_->addItem(previewItem_);
  rubberItem_ = new DisplayListItem(HitKind::None, core::kInvalidId, rubber, font);
  rubberItem_->setColor(selectColor());
  rubberItem_->setZValue(kZRubber);
  scene_->addItem(rubberItem_);

  const Sheet* s = sheet();
  if (!model_ || !s) {
    scene_->setSceneRect(QRectF(0, 0, 420, 297).adjusted(-kSheetMargin, -kSheetMargin, kSheetMargin, kSheetMargin));
    viewport()->update();
    return;
  }
  SheetRenderer r(model_->style(), model_->document().units());

  DisplayList frame;
  r.addFrame(frame, *s);
  if (s->titleBlock.visible) r.addTitleBlock(frame, *s, titleBlockValues(*model_, *s));
  sheetItem_ = new DisplayListItem(HitKind::TitleBlock, s->id, std::move(frame), font);
  sheetItem_->setZValue(kZSheet);
  scene_->addItem(sheetItem_);

  auto add = [&](HitKind kind, ObjectId id, DisplayList list, int z) {
    auto* it = new DisplayListItem(kind, id, std::move(list), font);
    it->setZValue(z);
    scene_->addItem(it);
    items_[id] = it;
    return it;
  };

  for (const auto& v : s->views) {
    DisplayList l;
    r.addView(l, *s, v);
    if (!v.geometryValid) {
      // placeholder frame so the view can be selected/moved while HLR runs
      BBox2 b = v.sheetBBox();
      l.curves.push_back(Curve2D::polyline({b.min, {b.max.x, b.min.y}, b.max, {b.min.x, b.max.y}, b.min}, LineClass::Detail));
    }
    DisplayListItem* it = add(HitKind::View, v.id, std::move(l), kZView);
    it->setBusy(!v.geometryValid && v.geometryPending);
    if (!v.error.empty() && !v.geometryValid) it->setErrorText(v.error);
  }
  for (const auto& d : s->dimensions) {
    const DrawingView* v = s->view(d.viewId);
    if (!v) continue;
    DisplayList l;
    r.addDimension(l, *v, d);
    add(HitKind::Dimension, d.id, std::move(l), kZEntity);
  }
  for (const auto& n : s->notes) {
    DisplayList l;
    r.addNote(l, n);
    add(HitKind::Note, n.id, std::move(l), kZEntity);
  }
  for (const auto& m : s->centerMarks) {
    const DrawingView* v = s->view(m.viewId);
    if (!v) continue;
    DisplayList l;
    r.addCenterMark(l, *v, m);
    add(HitKind::CenterMark, m.id, std::move(l), kZEntity);
  }
  for (const auto& cl : s->centerLines) {
    const DrawingView* v = s->view(cl.viewId);
    if (!v) continue;
    DisplayList l;
    r.addCenterLine(l, *v, cl);
    add(HitKind::CenterLine, cl.id, std::move(l), kZEntity);
  }
  for (const auto& b : s->balloons) {
    const DrawingView* v = s->view(b.viewId);
    if (!v) continue;
    DisplayList l;
    r.addBalloon(l, *v, b);
    add(HitKind::Balloon, b.id, std::move(l), kZEntity);
  }
  for (const auto& pl : s->partsLists) {
    DisplayList l;
    r.addPartsList(l, pl);
    add(HitKind::PartsList, pl.id, std::move(l), kZEntity);
  }

  for (auto& [id, l] : CalloutRenderer(model_->style(), model_->document().units()).entityLists(*s))
    add(HitKind::Callout, id, std::move(l), kZEntity);

  QRectF paper(0, 0, s->format.width, s->format.height);
  scene_->setSceneRect(paper.adjusted(-kSheetMargin, -kSheetMargin, kSheetMargin, kSheetMargin));

  // drop selection entries that vanished
  std::vector<ObjectId> keep;
  for (ObjectId id : selection_)
    if (items_.count(id) || s->contains(id)) keep.push_back(id);
  bool selChanged = keep.size() != selection_.size();
  selection_ = std::move(keep);
  if (hover_ != core::kInvalidId && !items_.count(hover_) && hover_ != s->id) hover_ = core::kInvalidId;
  updateItemStates();
  if (selChanged) {
    mirrorSelection();
    emit selectionChanged();
  }
  viewport()->update();
}

void DrawingCanvas::drawBackground(QPainter* painter, const QRectF& rect) {
  // Sheet chrome in theme colours: surround drawing.canvas, a drop shadow (shadow.card) and a
  // 1-device-pixel border.default hairline round the paper. Paper (drawing.paper) is white in both themes.
  painter->fillRect(rect, theme::color(Tok::DrawingCanvas));
  const Sheet* s = sheet();
  QRectF paper = s ? QRectF(0, 0, s->format.width, s->format.height) : QRectF(0, 0, 420, 297);
  painter->setPen(Qt::NoPen);
  QColor shadow = theme::color(Tok::ShadowCard);
  if (theme::current() == theme::Theme::Dark) shadow.setAlphaF(std::min(1.0, shadow.alphaF() * 1.2));
  painter->setBrush(shadow);
  painter->drawRect(paper.translated(1.5, 1.5));
  painter->setBrush(theme::color(Tok::DrawingPaper));
  painter->drawRect(paper);
  QPen edge(theme::color(Tok::BorderDefault), 0);  // cosmetic 1 px: one device pixel at any dpr
  edge.setCosmetic(true);
  if (const double dpr = devicePixelRatioF(); dpr > 1.0) edge.setWidthF(1.0 / dpr);
  painter->setPen(edge);
  painter->setBrush(Qt::NoBrush);
  painter->drawRect(paper);
}

void DrawingCanvas::changeEvent(QEvent* e) {
  // ThemeManager sets the application palette and the token QSS; both reach every widget, so the
  // canvas repaints with the new surround without depending on the app module.
  if (e->type() == QEvent::PaletteChange || e->type() == QEvent::StyleChange) viewport()->update();
  QGraphicsView::changeEvent(e);
}

// --- input ------------------------------------------------------------------------------------------

void DrawingCanvas::mousePressEvent(QMouseEvent* e) {
  setFocus();
  if (e->button() == Qt::MiddleButton) {
    panning_ = true;
    panLast_ = e->pos();
    setCursor(Qt::ClosedHandCursor);
    e->accept();
    return;
  }
  handlePress(sheetFromWidget(e->pos()), toButton(e->button()), toModifiers(e->modifiers()));
  e->accept();
}

void DrawingCanvas::mouseMoveEvent(QMouseEvent* e) {
  if (panning_) {
    QPoint d = e->pos() - panLast_;
    panLast_ = e->pos();
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
    verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
    e->accept();
    return;
  }
  handleMove(sheetFromWidget(e->pos()), toButtons(e->buttons()), toModifiers(e->modifiers()));
  e->accept();
}

void DrawingCanvas::mouseReleaseEvent(QMouseEvent* e) {
  if (e->button() == Qt::MiddleButton && panning_) {
    panning_ = false;
    unsetCursor();
    e->accept();
    return;
  }
  handleRelease(sheetFromWidget(e->pos()), toButton(e->button()), toModifiers(e->modifiers()));
  e->accept();
}

void DrawingCanvas::mouseDoubleClickEvent(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton) return;
  handleDoubleClick(sheetFromWidget(e->pos()), toModifiers(e->modifiers()));
  e->accept();
}

void DrawingCanvas::wheelEvent(QWheelEvent* e) {
  double steps = e->angleDelta().y() / 120.0;
  if (std::abs(steps) < 1e-9) return;
  double factor = std::pow(1.2, steps);
  zoomBy(factor, sheetFromWidget(e->position().toPoint()));
  e->accept();
}

void DrawingCanvas::keyPressEvent(QKeyEvent* e) {
  if (handleKey(e->key(), toModifiers(e->modifiers()))) {
    e->accept();
    return;
  }
  QGraphicsView::keyPressEvent(e);
}

void DrawingCanvas::resizeEvent(QResizeEvent* e) {
  QGraphicsView::resizeEvent(e);
  if (fitPending_ && viewport()->width() >= 50 && viewport()->height() >= 50) zoomToFit();
}

void DrawingCanvas::simulatePress(const Point2& sheet, core::MouseButton b, int modifiers) { handlePress(sheet, b, modifiers); }
void DrawingCanvas::simulateMove(const Point2& sheet, int buttons, int modifiers) { handleMove(sheet, buttons, modifiers); }
void DrawingCanvas::simulateRelease(const Point2& sheet, core::MouseButton b, int modifiers) { handleRelease(sheet, b, modifiers); }
void DrawingCanvas::simulateClick(const Point2& sheet, int modifiers) {
  handleMove(sheet, 0, modifiers);
  handlePress(sheet, core::MouseButton::Left, modifiers);
  handleRelease(sheet, core::MouseButton::Left, modifiers);
}
void DrawingCanvas::simulateDoubleClick(const Point2& sheet, int modifiers) {
  simulateClick(sheet, modifiers);
  handleDoubleClick(sheet, modifiers);
}
void DrawingCanvas::simulateKey(int key, int modifiers) { handleKey(key, modifiers); }
void DrawingCanvas::simulateDrag(const Point2& a, const Point2& b, int modifiers) {
  handleMove(a, 0, modifiers);
  handlePress(a, core::MouseButton::Left, modifiers);
  Point2 mid = (a + b) * 0.5;
  handleMove(mid, 1, modifiers);
  handleMove(b, 1, modifiers);
  handleRelease(b, core::MouseButton::Left, modifiers);
}

bool DrawingCanvas::isMovable(HitKind k) const {
  return k == HitKind::View || k == HitKind::Dimension || k == HitKind::Note || k == HitKind::Balloon ||
         k == HitKind::PartsList || k == HitKind::Callout;
}

void DrawingCanvas::updateHover(const Point2& sheet) {
  CanvasHit h = hitTest(sheet, false);
  ObjectId id = h.kind == HitKind::None ? core::kInvalidId : h.id;
  if (h.kind == HitKind::TitleBlock) id = core::kInvalidId;  // no hover frame for the title block
  if (id != hover_) {
    hover_ = id;
    updateItemStates();
    emit hoverChanged(hover_);
  }
}

void DrawingCanvas::handlePress(const Point2& sheet, core::MouseButton b, int modifiers) {
  flushPendingRebuild();
  lastCursor_ = sheet;
  if (interaction_) {
    interaction_->canvasPress(*this, sheet, b, modifiers);
    updatePreview();
    return;
  }
  if (b != core::MouseButton::Left) return;
  CanvasHit h = hitTest(sheet, false);
  const bool shift = (modifiers & core::ModShift) != 0;
  drag_ = DragState{};
  drag_.start = drag_.last = sheet;
  if (h.kind == HitKind::None || h.kind == HitKind::TitleBlock) {
    if (!shift) clearSelection();
    drag_.active = true;
    drag_.rubberBand = true;
    return;
  }
  if (shift) {
    select(h.id, true);
  } else if (!isSelected(h.id)) {
    select(h.id, false);
  }
  if (isMovable(h.kind) && isSelected(h.id)) {
    drag_.active = true;
    drag_.id = h.id;
    drag_.kind = h.kind;
    for (ObjectId id : selection_) {
      auto it = items_.find(id);
      if (it != items_.end() && isMovable(it->second->kind())) drag_.ids.push_back(id);
    }
  }
}

void DrawingCanvas::handleMove(const Point2& sheet, int buttons, int modifiers) {
  lastCursor_ = sheet;
  emit cursorMoved(sheet);
  if (interaction_) {
    interaction_->canvasMove(*this, sheet, buttons, modifiers);
    updatePreview();
    return;
  }
  if (drag_.active && (buttons & 1)) {
    Point2 delta = sheet - drag_.start;
    if (!drag_.moved && delta.length() < pickTolerance() * 0.5) return;
    drag_.moved = true;
    drag_.last = sheet;
    if (drag_.rubberBand) {
      DisplayList l;
      BBox2 r;
      r.add(drag_.start);
      r.add(sheet);
      l.curves.push_back(Curve2D::polyline({r.min, {r.max.x, r.min.y}, r.max, {r.min.x, r.max.y}, r.min}, LineClass::Detail));
      rubberItem_->setList(std::move(l));
    } else {
      applyDrag(delta, false);
    }
    viewport()->update();
    return;
  }
  updateHover(sheet);
}

void DrawingCanvas::handleRelease(const Point2& sheet, core::MouseButton b, int modifiers) {
  flushPendingRebuild();
  lastCursor_ = sheet;
  if (interaction_) {
    interaction_->canvasRelease(*this, sheet, b, modifiers);
    updatePreview();
    return;
  }
  if (b != core::MouseButton::Left || !drag_.active) return;
  DragState d = drag_;
  if (d.rubberBand) {
    drag_ = DragState{};
    rubberItem_->setList({});
    if (d.moved) {
      BBox2 r;
      r.add(d.start);
      r.add(sheet);
      std::vector<ObjectId> sel = (modifiers & core::ModShift) ? selection_ : std::vector<ObjectId>{};
      for (const auto& [id, item] : items_) {
        QRectF ir = item->boundingRect();
        BBox2 ib;
        ib.add({ir.left(), ir.top()});
        ib.add({ir.right(), ir.bottom()});
        if (ib.isValid() && ib.min.x >= r.min.x && ib.max.x <= r.max.x && ib.min.y >= r.min.y && ib.max.y <= r.max.y)
          if (std::find(sel.begin(), sel.end(), id) == sel.end()) sel.push_back(id);
      }
      setSelection(std::move(sel));
    }
    return;
  }
  if (d.moved) applyDrag(sheet - d.start, true);  // reads drag_.ids
  drag_ = DragState{};
}

void DrawingCanvas::applyDrag(const Point2& delta, bool commit) {
  if (!model_) return;
  Sheet* s = sheet();
  if (!s) return;
  if (!commit) {
    for (ObjectId id : drag_.ids) {
      DisplayListItem* it = item(id);
      if (!it) continue;
      Point2 d = delta;
      if (it->kind() == HitKind::View) {
        if (const DrawingView* v = s->view(id)) {
          Point2 target = model_->alignedPosition(*v, v->position + delta);
          d = target - v->position;
        }
      }
      it->setDragOffset(d);
    }
    return;
  }
  for (ObjectId id : drag_.ids)
    if (DisplayListItem* it = item(id)) it->setDragOffset({0, 0});
  std::vector<ObjectId> ids = drag_.ids;
  if (ids.empty()) return;
  const bool oneView = ids.size() == 1 && s->view(ids[0]) != nullptr;
  model_->edit(ids.size() == 1 ? (oneView ? "Move view" : "Move annotation") : "Move objects", undo_, [&]() {
    if (Sheet* sh = sheet()) model_->moveObjects(*sh, ids, delta);
  });
}

void DrawingCanvas::handleDoubleClick(const Point2& sheet, int modifiers) {
  flushPendingRebuild();
  if (interaction_) {
    interaction_->canvasDoubleClick(*this, sheet, modifiers);
    updatePreview();
    return;
  }
  CanvasHit h = hitTest(sheet, false);
  const std::string id = std::to_string(h.id);
  switch (h.kind) {
    case HitKind::View: requestTool("drawing.edit-view", {id}); break;
    case HitKind::TitleBlock: requestTool("drawing.edit-title-block"); break;
    case HitKind::Note: requestTool("drawing.text", {"--edit", id}); break;
    case HitKind::Dimension: requestTool("drawing.edit-dimension", {id}); break;
    case HitKind::PartsList: requestTool("drawing.parts-list", {"--edit", id}); break;
    case HitKind::Callout: requestTool("drawing.edit-callout", {id}); break;
    case HitKind::None: requestTool("drawing.edit-sheet"); break;
    default: break;
  }
}

bool DrawingCanvas::handleKey(int key, int modifiers) {
  if (interaction_ && interaction_->canvasKey(*this, key, modifiers)) {
    updatePreview();
    return true;
  }
  switch (key) {
    case Qt::Key_Escape:
      if (manager_ && manager_->isActive()) manager_->cancel();
      else clearSelection();
      return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
      if (manager_ && manager_->isActive()) manager_->accept();
      else if (manager_) manager_->repeatLast();
      return true;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
      if (!selection_.empty() && !(manager_ && manager_->isActive())) {
        requestTool("drawing.delete");
        return true;
      }
      return false;
    case Qt::Key_F6:
      zoomToFit();
      return true;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
      zoomBy(1.25);
      return true;
    case Qt::Key_Minus:
      zoomBy(0.8);
      return true;
    default:
      return false;
  }
}

void DrawingCanvas::requestTool(const std::string& id, std::vector<std::string> args) {
  if (manager_) {
    manager_->run(id, args);
    return;
  }
  emit toolRequested(id, std::move(args));
}

BBox2 DrawingCanvas::selectionRect() const {
  BBox2 b;
  for (ObjectId id : selection_)
    if (const DisplayListItem* it = item(id)) {
      QRectF r = it->boundingRect();
      b.add({r.left(), r.top()});
      b.add({r.right(), r.bottom()});
    }
  return b;
}

}  // namespace fabricad::drawing
