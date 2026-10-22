// FabriCAD sketch tools: entering/leaving sketch mode, the idle select/drag tool and the
// Sketch Palette toggles.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sketch/tools/SketchToolBase.h"
#include "sketch/tools/SketchTools.h"

#include "core/AppHost.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/GeometryRefs.h"
#include "core/Selection.h"
#include "core/ShapeUtils.h"
#include "core/UndoStack.h"

#include <BRepBndLib.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <QtCore/Qt>

#include <algorithm>
#include <cctype>
#include <format>
#include <memory>

namespace fabricad::sketch {

using core::ShapeKind;
using core::ShapeRef;
using core::ToolContext;
using core::ToolInfo;
using core::ViewerKeyEvent;
using core::ViewerMouseEvent;

namespace {

std::string upper(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return s;
}

bool isPlaneRef(const ShapeRef& r) { return core::hasKind(core::kPlaneKinds, r.kind); }

/// The plane of a new sketch on a body face: the normal points out of the material (the face
/// as resolved may come with its surface's inward normal) and the axes are right-handed, so
/// the sketch is looked at from outside and never mirrored (SK-04).
gp_Ax3 outwardFacePlane(const core::Document& doc, const ShapeRef& ref, const gp_Ax3& pl) {
  gp_Dir n = pl.Direction();
  const TopoDS_Shape face = core::resolveShape(doc, ref);
  const TopoDS_Shape body = core::resolveShape(doc, ShapeRef::body(ref.owner));
  if (!face.IsNull() && !body.IsNull()) {
    GProp_GProps props;
    BRepGProp::SurfaceProperties(face, props);
    const gp_Pnt c = props.CentreOfMass();
    Bnd_Box box;
    BRepBndLib::Add(body, box);
    const double eps = box.IsVoid() ? 1e-3 : std::max(1e-3, 1e-4 * std::sqrt(box.SquareExtent()));
    auto state = [&](double side) {
      BRepClass3d_SolidClassifier cls(body, c.Translated(gp_Vec(n) * (side * eps)), 1e-7);
      return cls.State();
    };
    if (state(1.0) == TopAbs_IN && state(-1.0) == TopAbs_OUT) n.Reverse();
  }
  return gp_Ax3(pl.Location(), n, pl.XDirection());
}

/// Static prompt of the idle select tool (the DOF count is shown by the status bar label).
constexpr const char* kSelectHint = "Select sketch geometry; drag to move";

}  // namespace

// ---------------------------------------------------------------------------
// createSketch
// ---------------------------------------------------------------------------

core::ObjectId createSketch(ToolContext& ctx, const ShapeRef& plane) {
  if (!ctx.document) return core::kInvalidId;
  core::Document& doc = *ctx.document;
  auto resolved = core::resolvePlane(doc, plane);
  if (!resolved) {
    ctx.showMessage("Cannot create a sketch here: select a plane or a planar face", true);
    return core::kInvalidId;
  }
  auto feature = std::make_unique<SketchFeature>();
  feature->prop("plane").setRef(plane);
  // a face plane: outward and right-handed, kept on every later recompute (SK-04)
  const gp_Ax3 start = plane.kind == core::ShapeKind::Face ? outwardFacePlane(doc, plane, *resolved) : *resolved;
  feature->setPlane(start);
  if (plane.kind == core::ShapeKind::Face) feature->storePlaneAxes(start);
  core::ObjectId id = core::kInvalidId;
  if (ctx.undo) {
    auto cmd = std::make_unique<core::AddFeatureCommand>(doc, std::move(feature));
    core::AddFeatureCommand* raw = cmd.get();
    ctx.undo->push(std::move(cmd));
    id = raw->featureId();
  } else {
    id = doc.addFeature(std::move(feature))->id();
    doc.recompute();
  }
  if (id == core::kInvalidId) return id;
  if (ctx.selection) ctx.selection->clear();
  SketchSession::begin(ctx, id);
  return id;
}

namespace {

// ---------------------------------------------------------------------------
// sketch.create
// ---------------------------------------------------------------------------

class CreateSketchTool : public core::ToolBase {
 public:
  CreateSketchTool()
      : ToolBase(ToolInfo{.id = "sketch.create",
                          .name = "Create Sketch",
                          .tooltip = "Create a sketch on a plane or planar face",
                          .icon = "sketch-create",
                          .aliases = {"SKETCH", "SK"},
                          .tab = "SOLID",
                          .group = "CREATE",
                          .order = 2,
                          .promoted = true}) {}

  void activate(ToolContext& ctx) override {
    if (!ctx.document) {
      ctx.cancel();
      return;
    }
    if (!ctx.args.empty()) {
      if (tryPlaneName(ctx, ctx.args[0])) return;
      ctx.showMessage("Unknown plane '" + ctx.args[0] + "' (use XY, XZ or YZ)", true);
    }
    for (const ShapeRef& r : ctx.preselection) {
      if (isPlaneRef(r)) {
        start(ctx, r);
        return;
      }
    }
    ctx.setPrompt("Select a plane or planar face");
    if (ctx.selection) {
      ctx.selection->clear();
      ctx.selection->setFilter(core::kPlaneKinds);
    }
    if (ctx.viewer) ctx.viewer->setSelectionFilter(core::kPlaneKinds);
  }

  void selectionChanged(ToolContext& ctx) override {
    if (!ctx.selection) return;
    for (const ShapeRef& r : ctx.selection->items())
      if (isPlaneRef(r)) {
        start(ctx, r);
        return;
      }
  }

  bool mousePress(ToolContext& ctx, const ViewerMouseEvent& e) override {
    if (e.button == core::MouseButton::Left && e.hasPick && isPlaneRef(e.pick)) {
      start(ctx, e.pick);
      return true;
    }
    return false;
  }

  bool commandLineInput(ToolContext& ctx, const std::string& text) override { return tryPlaneName(ctx, text); }

 private:
  bool tryPlaneName(ToolContext& ctx, const std::string& text) {
    std::string n = upper(text);
    if (n == "XY" || n == "XZ" || n == "YZ") {
      start(ctx, ShapeRef::originPlane(n));
      return true;
    }
    return false;
  }
  void start(ToolContext& ctx, const ShapeRef& plane) {
    if (createSketch(ctx, plane) == core::kInvalidId) {
      ctx.cancel();
      return;
    }
    ctx.finish();
  }
};

// ---------------------------------------------------------------------------
// sketch.edit
// ---------------------------------------------------------------------------

class EditSketchTool : public core::ToolBase {
 public:
  EditSketchTool()
      : ToolBase(ToolInfo{.id = "sketch.edit",
                          .name = "Edit Sketch",
                          .tooltip = "Edit an existing sketch (argument: feature id, or select the sketch)",
                          .icon = "sketch-edit",
                          .aliases = {"EDITSKETCH"},
                          .tab = "SOLID",
                          .group = "CREATE",
                          .order = 3,
                          .repeatable = false,
                          .hidden = true}) {}

  void activate(ToolContext& ctx) override {
    if (!ctx.document) {
      ctx.cancel();
      return;
    }
    core::ObjectId id = core::kInvalidId;
    if (!ctx.args.empty()) {
      try {
        id = static_cast<core::ObjectId>(std::stoull(ctx.args[0]));
      } catch (...) {
        id = core::kInvalidId;
      }
    }
    if (id == core::kInvalidId) {
      for (const ShapeRef& r : ctx.preselection) {
        if (ctx.document->featureAs<SketchFeature>(r.owner)) {
          id = r.owner;
          break;
        }
      }
    }
    if (id == core::kInvalidId || !ctx.document->featureAs<SketchFeature>(id)) {
      ctx.showMessage("Select a sketch to edit", true);
      ctx.cancel();
      return;
    }
    if (ctx.selection) ctx.selection->clear();
    SketchSession::begin(ctx, id);
    ctx.finish();
  }
};

// ---------------------------------------------------------------------------
// sketch.finish
// ---------------------------------------------------------------------------

class FinishSketchTool : public core::ToolBase {
 public:
  FinishSketchTool()
      : ToolBase(ToolInfo{.id = "sketch.finish",
                          .name = "Finish Sketch",
                          .tooltip = "Leave sketch mode",
                          .icon = "sketch-finish",
                          .shortcut = "Ctrl+Return",
                          .aliases = {"FINISH", "FINISHSKETCH"},
                          .tab = "SKETCH",
                          .group = "FINISH SKETCH",
                          .order = 1,
                          .promoted = true,
                          .repeatable = false}) {}
  bool isInstant() const override { return true; }
  bool isEnabled(const ToolContext& ctx) const override { return ctx.document && SketchSession::current(); }
  void activate(ToolContext& ctx) override {
    SketchSession::end();
    if (ctx.host) ctx.host->setPrompt({});
  }
};

// ---------------------------------------------------------------------------
// sketch.select: idle tool (hover, click, window selection, drag with live solve)
// ---------------------------------------------------------------------------

class SelectTool : public SketchTool {
 public:
  SelectTool()
      : SketchTool(ToolInfo{.id = "sketch.select",
                            .name = "Select",
                            .tooltip = "Select sketch geometry; drag free geometry; double-click a dimension to edit it",
                            .icon = "select",
                            .tab = "SKETCH",
                            .group = "SELECT",
                            .order = 1,
                            .repeatable = false,
                            .hidden = true}) {}

  void activate(ToolContext& ctx) override {
    SketchTool::activate(ctx);
    if (viewer_) viewer_->setCursor(core::CursorShape::Arrow);
    pressed_ = false;
    dragging_ = false;
    ctx.setPrompt(kSelectHint);  // the DOF count lives in the status bar label only (SK-09)
  }

  // Esc / Enter must not end the idle tool: Esc clears the selection, Enter is a no-op.
  void accept(ToolContext&) override {}
  void cancel(ToolContext& ctx) override {
    if (!session() || session()->isEnding()) {
      ctx.cancel();
      return;
    }
    if (ctx.selection) ctx.selection->clear();
    session()->clearConstraintSelection();
    session()->render();
  }

  bool mousePress(ToolContext& ctx, const ViewerMouseEvent& e) override {
    if (e.button != core::MouseButton::Left) return false;
    auto c = cursor(e);
    if (!c || !session()) return false;
    pressed_ = true;
    moved_ = false;
    dragging_ = false;
    pressPos_ = *c;
    pressDim_ = session()->dimensionAt(*c, tol(*c));
    pressHit_ = pressDim_ ? std::nullopt : entityAt(*c);
    (void)ctx;
    return true;
  }

  bool mouseMove(ToolContext& ctx, const ViewerMouseEvent& e) override {
    auto c = cursor(e);
    if (!c || !session()) return false;
    Sketch* sk = sketch();
    if (pressed_ && (e.buttons & 1)) {
      if (!moved_ && c->distance(pressPos_) > tol(pressPos_, 3.0)) moved_ = true;
      if (!moved_) return true;
      if (pressHit_ && sk) {
        const Geometry* g = sk->geometry(pressHit_->ref.geo);
        if (g && g->isMovable()) {
          if (!dragging_) {
            session()->beginEdit();
            GeoRef ref = pressHit_->ref;
            if (g->type == GeomType::Point) ref.pos = PointPos::Center;
            if (ref.pos == PointPos::Mid) ref.pos = PointPos::None;  // drag the whole curve from its midpoint
            dragging_ = sk->beginDrag(ref, pressPos_);
            if (!dragging_) session()->cancelEdit();
          }
          if (dragging_) {
            sk->drag(*c);
            session()->refresh();
          }
        }
        return true;
      }
      // window selection rubber band
      clearTemp();
      core::PreviewStyle st = guideStyle();
      st.dashed = c->x < pressPos_.x;  // right-to-left = crossing
      previewPolyline({pressPos_, {c->x, pressPos_.y}, *c, {pressPos_.x, c->y}, pressPos_}, st);
      return true;
    }
    // hover: preselect
    if (ctx.selection) {
      auto hit = entityAt(*c);
      if (hit) ctx.selection->setPreselection(session()->toShapeRef(hit->ref));
      else ctx.selection->setPreselection(std::nullopt);
    }
    return true;
  }

  bool mouseRelease(ToolContext& ctx, const ViewerMouseEvent& e) override {
    if (e.button != core::MouseButton::Left || !pressed_) return false;
    pressed_ = false;
    clearTemp();
    auto c = cursor(e);
    if (!c || !session()) return true;
    Sketch* sk = sketch();
    const bool add = (e.modifiers & (core::ModCtrl | core::ModShift)) != 0;
    if (dragging_) {
      dragging_ = false;
      if (sk) sk->endDrag();
      session()->commitEdit("Move");
      ctx.setPrompt(kSelectHint);
      return true;
    }
    if (moved_) {
      if (!sk || !ctx.selection) return true;
      bool crossing = c->x < pressPos_.x;
      std::vector<ShapeRef> refs;
      for (int id : sk->curvesInRect(pressPos_, *c, crossing)) refs.push_back(session()->toShapeRef({id, PointPos::None}));
      if (add)
        for (const ShapeRef& r : refs) ctx.selection->add(r);
      else
        ctx.selection->set(refs);
      session()->render();
      return true;
    }
    // plain click
    if (pressDim_) {
      if (!add && ctx.selection) ctx.selection->clear();
      session()->selectConstraint(*pressDim_, add);
      if (sk)
        if (const Constraint* d = sk->constraint(*pressDim_)) ctx.setPrompt(std::format("{} = {}", d->name, session()->dimensionText(*d)));
      return true;
    }
    if (pressHit_ && ctx.selection) {
      ShapeRef r = session()->toShapeRef(pressHit_->ref);
      if (add) ctx.selection->toggle(r);
      else {
        ctx.selection->set(r);
        session()->clearConstraintSelection();
      }
      session()->render();
      return true;
    }
    if (!add && ctx.selection) {
      ctx.selection->clear();
      session()->clearConstraintSelection();
      session()->render();
    }
    return true;
  }

  bool mouseDoubleClick(ToolContext& ctx, const ViewerMouseEvent& e) override {
    auto c = cursor(e);
    if (!c || !session()) return false;
    pressed_ = false;
    if (auto dim = session()->dimensionAt(*c, tol(*c))) {
      if (ctx.manager) ctx.manager->run("sketch.dimension-edit", {std::to_string(*dim)});
      return true;
    }
    auto hit = entityAt(*c);
    Sketch* sk = sketch();
    if (hit && sk && hit->ref.pos == PointPos::None && ctx.selection) {
      std::vector<ShapeRef> refs;
      for (int id : sk->chain(hit->ref.geo)) refs.push_back(session()->toShapeRef({id, PointPos::None}));
      ctx.selection->set(refs);
      session()->render();
      return true;
    }
    return false;
  }

  bool keyPress(ToolContext& ctx, const ViewerKeyEvent& e) override {
    if (e.key == Qt::Key_Delete || e.key == Qt::Key_Backspace) {
      if (session() && session()->deleteSelection()) ctx.setPrompt(kSelectHint);
      return true;
    }
    if (e.key == Qt::Key_Escape) {
      cancel(ctx);
      return true;
    }
    // plain Enter is a no-op here; Ctrl+Enter (Finish Sketch) and other combos stay shortcuts (SK-01)
    if ((e.key == Qt::Key_Return || e.key == Qt::Key_Enter) && !(e.modifiers & (core::ModCtrl | core::ModAlt))) return true;
    return false;
  }

  /// With one dimension selected, typing a value edits it.
  bool commandLineInput(ToolContext& ctx, const std::string& text) override {
    if (!session() || session()->selectedConstraints().size() != 1) return false;
    int id = *session()->selectedConstraints().begin();
    Sketch* sk = sketch();
    const Constraint* c = sk ? sk->constraint(id) : nullptr;
    if (!c || !c->isDimensional()) return false;
    if (ctx.manager) return ctx.manager->run("sketch.dimension-edit", {std::to_string(id), text});
    return false;
  }

 private:
  /// Point first (end points and centres), otherwise the curve: a curve's midpoint selects the curve.
  std::optional<SketchHit> entityAt(Pt2 p) const {
    const Sketch* sk = sketch();
    if (!sk) return std::nullopt;
    auto hit = sk->hit(p, tol(p));
    if (hit && hit->ref.pos == PointPos::Mid) hit = sk->hitCurve(p, tol(p));
    return hit;
  }

  bool pressed_ = false;
  bool moved_ = false;
  bool dragging_ = false;
  Pt2 pressPos_;
  std::optional<SketchHit> pressHit_;
  std::optional<int> pressDim_;
};

// ---------------------------------------------------------------------------
// Sketch Palette toggles (instant)
// ---------------------------------------------------------------------------

/// `state`: the current on/off state for menus and toolbars (check marks in MORE, p1-check 7);
/// empty for plain actions (Look At).
std::unique_ptr<core::ITool> paletteToggle(const char* id, const char* name, const char* tooltip, int order,
                                           std::function<void(SketchSession&, ToolContext&)> fn,
                                           std::function<bool(const SketchSession&)> state = {}) {
  ToolInfo info{.id = id, .name = name, .tooltip = tooltip, .icon = id, .tab = "SKETCH", .group = "OPTIONS", .order = order,
                .repeatable = false};
  if (state)
    info.checked = [state] {
      const SketchSession* s = SketchSession::current();
      return s && state(*s);
    };
  return std::make_unique<core::LambdaTool>(
      std::move(info),
      [fn](ToolContext& ctx) {
        if (SketchSession* s = SketchSession::current()) fn(*s, ctx);
      },
      [](const ToolContext& ctx) { return ctx.document && SketchSession::current(); });
}

}  // namespace

void registerModeTools(core::ToolRegistry& reg) {
  reg.registerTool(std::make_unique<CreateSketchTool>());
  reg.registerTool(std::make_unique<EditSketchTool>());
  reg.registerTool(std::make_unique<FinishSketchTool>());
  reg.registerTool(std::make_unique<SelectTool>());
  // Ctrl+A: select every sketch entity (SK-10); runs beside the active tool
  reg.registerTool(std::make_unique<core::LambdaTool>(
      ToolInfo{.id = "sketch.select-all",
               .name = "Select All",
               .tooltip = "Select every curve and point of the sketch",
               .icon = "sketch-select-all",
               .shortcut = "Ctrl+A",
               .aliases = {"SELECTALL"},
               .tab = "SKETCH",
               .group = "SELECT",
               .order = 2,
               .repeatable = false,
               .hidden = true,
               .keepsActiveTool = true},
      [](ToolContext& ctx) {
        SketchSession* s = SketchSession::current();
        Sketch* sk = s ? s->sketch() : nullptr;
        if (!sk || !ctx.selection) return;
        std::vector<ShapeRef> refs;
        for (const Geometry& g : sk->geometries())
          if (!g.projected) refs.push_back(s->toShapeRef({g.id, PointPos::None}));
        ctx.selection->set(refs);
        s->render();
        ctx.showMessage(std::format("{} sketch entities selected", refs.size()));
      },
      [](const ToolContext& ctx) { return ctx.document && SketchSession::current(); }));
  reg.registerTool(paletteToggle("sketch.look-at", "Look At", "Look straight at the sketch plane", 1,
                                 [](SketchSession& s, ToolContext& ctx) {
                                   if (ctx.viewer) ctx.viewer->lookAt(s.feature() ? s.feature()->plane() : gp_Ax3(), true);
                                 }));
  reg.registerTool(paletteToggle("sketch.toggle-snap", "Snap", "Toggle snapping and inference", 2,
                                 [](SketchSession& s, ToolContext& ctx) {
                                   s.setSnapEnabled(!s.snapEnabled());
                                   ctx.showMessage(s.snapEnabled() ? "Snap on" : "Snap off");
                                 },
                                 [](const SketchSession& s) { return s.snapEnabled(); }));
  reg.registerTool(paletteToggle("sketch.toggle-grid", "Sketch Grid", "Show or hide the sketch grid", 3,
                                 [](SketchSession&, ToolContext& ctx) {
                                   if (ctx.viewer) ctx.viewer->setGridVisible(!ctx.viewer->isGridVisible());
                                 },
                                 [](const SketchSession& s) { return s.viewer() && s.viewer()->isGridVisible(); }));
  reg.registerTool(paletteToggle("sketch.toggle-profiles", "Show Profile", "Shade closed profiles", 4,
                                 [](SketchSession& s, ToolContext&) { s.setShowProfiles(!s.showProfiles()); },
                                 [](const SketchSession& s) { return s.showProfiles(); }));
  reg.registerTool(paletteToggle("sketch.toggle-points", "Show Points", "Show sketch points", 5,
                                 [](SketchSession& s, ToolContext&) { s.setShowPoints(!s.showPoints()); },
                                 [](const SketchSession& s) { return s.showPoints(); }));
  reg.registerTool(paletteToggle("sketch.toggle-dimensions", "Show Dimensions", "Show dimensions", 6,
                                 [](SketchSession& s, ToolContext&) { s.setShowDimensions(!s.showDimensions()); },
                                 [](const SketchSession& s) { return s.showDimensions(); }));
  reg.registerTool(paletteToggle("sketch.toggle-constraints", "Show Constraints", "Show constraint glyphs", 7,
                                 [](SketchSession& s, ToolContext&) { s.setShowConstraints(!s.showConstraints()); },
                                 [](const SketchSession& s) { return s.showConstraints(); }));
}

void registerSketchTools(core::ToolRegistry& reg) {
  registerModeTools(reg);
  registerCreateTools(reg);
  registerModifyTools(reg);
  registerConstraintTools(reg);
  registerCurveTools(reg);
  registerTransformTools(reg);
}

}  // namespace fabricad::sketch
