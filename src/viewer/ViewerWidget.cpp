// FabriCAD viewer: 3D viewport implementation (OCCT AIS/V3d in a QOpenGLWidget).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "viewer/ViewerWidget.h"

#include "core/Document.h"
#include "core/GeometryRefs.h"
#include "core/Log.h"
#include "core/Selection.h"
#include "core/ShapeUtils.h"
#include "core/Tool.h"
#include "core/Units.h"
#include "viewer/ManipulatorPrs.h"
#include "viewer/PrimitivePrs.h"
#include "viewer/TopoHelpers.h"
#include "viewer/occt/OcctQtTools.h"
#include "viewer/occt/OcctView.h"

#include <AIS_Shape.hxx>
#include <AIS_TextLabel.hxx>
#include <AIS_ViewCube.hxx>
#include <Aspect_Grid.hxx>
#include <BRepBndLib.hxx>
#include <BRepLib.hxx>
#include <Bnd_Box.hxx>
#include <Font_FontMgr.hxx>
#include <Font_SystemFont.hxx>
#include <Graphic3d_MaterialAspect.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_IsoAspect.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_PointAspect.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <StdSelect_ViewerSelector3d.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <V3d_AmbientLight.hxx>
#include <V3d_DirectionalLight.hxx>
#include <V3d_Viewer.hxx>

#include <QGuiApplication>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPainter>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <map>

namespace fabricad::viewer {

using core::Document;
using core::ObjectId;
using core::PreviewHandle;
using core::PreviewStyle;
using core::ShapeKind;
using core::ShapeKindMask;
using core::ShapeRef;

namespace {

constexpr PreviewHandle kFirstGlPreview = 1000000;

Quantity_Color qcol(const std::array<float, 4>& c) { return Quantity_Color(c[0], c[1], c[2], Quantity_TOC_sRGB); }
Quantity_Color hexq(const char* hex) {
  Rgb c = hexColor(hex);
  return Quantity_Color(c.r, c.g, c.b, Quantity_TOC_sRGB);
}

bool sameTrsf(const gp_Trsf& a, const gp_Trsf& b) {
  for (int r = 1; r <= 3; ++r)
    for (int c = 1; c <= 4; ++c)
      if (std::abs(a.Value(r, c) - b.Value(r, c)) > 1e-12) return false;
  return true;
}

bool isIdentity(const gp_Trsf& t) { return sameTrsf(t, gp_Trsf()); }

bool isSketchStyle(SceneStyle s) {
  switch (s) {
    case SceneStyle::SketchFree:
    case SceneStyle::SketchDefined:
    case SceneStyle::SketchConstruction:
    case SceneStyle::SketchFixed:
    case SceneStyle::SketchProjected:
    case SceneStyle::SketchConflict:
    case SceneStyle::SketchPoints: return true;
    default: return false;
  }
}

int modifiersOf(Qt::KeyboardModifiers m) {
  int r = core::ModNone;
  if (m & Qt::ShiftModifier) r |= core::ModShift;
  if (m & Qt::ControlModifier) r |= core::ModCtrl;
  if (m & Qt::AltModifier) r |= core::ModAlt;
  return r;
}

int buttonsOf(Qt::MouseButtons b) {
  int r = 0;
  if (b & Qt::LeftButton) r |= 1;
  if (b & Qt::MiddleButton) r |= 2;
  if (b & Qt::RightButton) r |= 4;
  return r;
}

void setLineStyle(const Handle(Prs3d_Drawer)& dr, bool dashed) {
  if (!dashed) return;
  for (const Handle(Prs3d_LineAspect)& la :
       {dr->WireAspect(), dr->FreeBoundaryAspect(), dr->UnFreeBoundaryAspect(), dr->LineAspect()})
    if (!la.IsNull()) la->SetTypeOfLine(Aspect_TOL_DASH);
}

/// Dominant topological content of a shape (for overlay styling).
TopAbs_ShapeEnum contentType(const TopoDS_Shape& s) {
  if (s.IsNull()) return TopAbs_SHAPE;
  for (TopAbs_ShapeEnum t : {TopAbs_FACE, TopAbs_EDGE, TopAbs_VERTEX}) {
    TopExp_Explorer ex(s, t);
    if (ex.More()) return t;
  }
  return TopAbs_SHAPE;
}

/// Sans-serif UI font for all viewer text (labels, ViewCube): the first installed of Inter
/// and DejaVu Sans (which also has the constraint symbol glyphs), else OCCT's sans alias.
const char* viewerFont() {
  static const std::string name = [] {
    Handle(Font_FontMgr) mgr = Font_FontMgr::GetInstance();
    for (const char* f : {"Inter", "DejaVu Sans"}) {
      Font_FontAspect asp = Font_FontAspect_Regular;
      Handle(Font_SystemFont) sf = mgr->FindFont(TCollection_AsciiString(f), Font_StrictLevel_Strict, asp, false);
      if (!sf.IsNull()) return std::string(sf->FontName().ToCString());
    }
    return std::string(Font_NOF_SANS_SERIF);
  }();
  return name.c_str();
}

// ViewCube placement (TriedronPers offset of its centre from the top-right corner, px) and the
// radius of the ring the overlay controls sit on
constexpr double kCubeOffset = 115.0;
constexpr double kCubeCtlRadius = 84.0;

}  // namespace

void setupGlPlatform() { OcctQtTools::qtGlPlatformSetup(); }

// ---------------------------------------------------------------------------------------
// Listener bridging OcctView callbacks to ViewerWidget's private handlers.
// ---------------------------------------------------------------------------------------
class ViewerListener : public OcctViewListener {
 public:
  explicit ViewerListener(ViewerWidget* w) : w_(w) {}
  void occtInitialized() override { w_->onGlInitialized(); }
  void occtInitFailed(const QString& m) override { w_->onGlFailed(m); }
  Qt::MouseButtons occtMousePress(QMouseEvent* e) override { return w_->onMousePress(e); }
  Qt::MouseButtons occtMouseMove(QMouseEvent* e) override { return w_->onMouseMove(e); }
  Qt::MouseButtons occtMouseRelease(QMouseEvent* e) override { return w_->onMouseRelease(e); }
  void occtMouseDoubleClick(QMouseEvent* e) override { w_->onMouseDoubleClick(e); }
  double occtWheel(QWheelEvent* e) override { return w_->onWheel(e); }
  void occtBeforeRedraw() override { w_->beforeRedraw(); }
  void occtPaintOverlay(QPainter& p) override { w_->paintOverlay(p); }
  void occtLeave() override { w_->onLeave(); }

 private:
  ViewerWidget* w_;
};

struct ViewerWidget::Impl {
  std::unique_ptr<ViewerListener> listener;
  OcctView* view = nullptr;
  QString info;
  bool failed = false;

  struct DisplayObj {
    Handle(AIS_InteractiveObject) obj;
    SceneItem item;
    std::vector<int> modes;
    bool modesValid = false;
  };
  std::map<std::string, DisplayObj> objects;
  std::map<const SelectMgr_SelectableObject*, std::string> objectKeys;

  struct Preview {
    Handle(AIS_InteractiveObject) obj;
    PreviewStyle style;
  };
  std::map<PreviewHandle, Preview> previews;
  PreviewHandle nextPreview = kFirstGlPreview;
  std::vector<Handle(AIS_InteractiveObject)> overlays;

  struct ManipObj {
    Handle(ManipulatorPrs) prs;
    Handle(AIS_TextLabel) label;
    double pixelSize = 0.0;  ///< pixel size the glyphs were built with (auto-sized gizmos)
  };
  std::map<core::ManipulatorId, ManipObj> manips;

  Handle(Graphic3d_Camera) home;
  std::vector<Handle(AIS_TextLabel)> originLabels;
  double originLabelSize = 0.0;

  bool ready() const { return view && view->isReady(); }
  const Handle(AIS_InteractiveContext)& ctx() const { return view->context(); }
  const Handle(V3d_View)& v3d() const { return view->view(); }

  Handle(Graphic3d_Camera) cameraCopy() const {
    Handle(Graphic3d_Camera) c = new Graphic3d_Camera();
    c->Copy(v3d()->Camera());
    return c;
  }
  QMargins insets;
  /// Bounding box of the displayed document geometry (world placement applied).
  Bnd_Box sceneBox() const {
    Bnd_Box box;
    for (const auto& [k, o] : objects) {
      if (o.item.shape.IsNull()) continue;
      BRepBndLib::Add(isIdentity(o.item.placement) ? o.item.shape : o.item.shape.Moved(TopLoc_Location(o.item.placement)), box);
    }
    return box;
  }
  /// Fit `cam` in place to `box` with a comfortable margin, keeping the content clear of the
  /// overlay insets (nav bar).
  void fitCamera(const Handle(Graphic3d_Camera)& cam, const Bnd_Box& box) const {
    if (box.IsVoid()) return;
    v3d()->FitMinMax(cam, box, 0.15, 10.0 * Precision::Confusion());
    applyViewportInset(*cam, view->width(), view->height(), insets.top(), insets.bottom());
  }
  /// `cam` fitted to `box` (default: the whole scene).
  Handle(Graphic3d_Camera) fitted(const Handle(Graphic3d_Camera)& cam, const Bnd_Box* box = nullptr) const {
    Handle(Graphic3d_Camera) out = new Graphic3d_Camera();
    out->Copy(cam);
    fitCamera(out, box && !box->IsVoid() ? *box : sceneBox());
    return out;
  }
  /// Scene has displayed document geometry (so FitAll is meaningful).
  bool hasGeometry() const { return !objects.empty(); }
};

// ---------------------------------------------------------------------------------------
// construction / document wiring
// ---------------------------------------------------------------------------------------

ViewerWidget::ViewerWidget(QWidget* parent) : QWidget(parent), d_(std::make_unique<Impl>()) {
  setMinimumSize(320, 240);
  setAttribute(Qt::WA_OpaquePaintEvent);
  setFocusPolicy(Qt::StrongFocus);
  auto* lay = new QVBoxLayout(this);
  lay->setContentsMargins(0, 0, 0, 0);
  d_->listener = std::make_unique<ViewerListener>(this);
  d_->insets = insets_;
  manipulators_.setChangeListener([this] { syncManipulatorPrs(); });
}

ViewerWidget::~ViewerWidget() {
  for (auto& c : docConnections_) QObject::disconnect(c);
  if (d_->view) {
    delete d_->view;  // releases OCCT resources while the GL context still exists
    d_->view = nullptr;
  }
}

void ViewerWidget::setDocument(Document* doc) {
  if (document_ == doc) return;
  document_ = doc;
  connectDocument();
  scene_.clear();
  hoverRef_.reset();
  syncGhostingToActiveComponent();
  onFilterInputsChanged();
  scheduleSync();
}

// Ghosting follows Document::activeComponent in one place (ASM-01): every path that changes the
// active component (tools, browser "Activate Component", undo, removeComponent) reaches here.
// The active component and its descendants stay solid; the root clears ghosting.
void ViewerWidget::syncGhostingToActiveComponent() {
  std::vector<ObjectId> keep;
  if (document_) {
    const ObjectId active = document_->activeComponent();
    const core::Component* c = document_->component(active);
    if (c && !c->isRoot()) {
      std::vector<ObjectId> stack{active};
      while (!stack.empty()) {
        const ObjectId id = stack.back();
        stack.pop_back();
        keep.push_back(id);
        if (const core::Component* cc = document_->component(id))
          for (ObjectId ch : cc->children) stack.push_back(ch);
      }
    }
  }
  if (keep == sceneOptions_.activeComponents) return;
  setGhostedComponents(keep);
}

bool ViewerWidget::ghostedPickable() const {
  ShapeKindMask toolMask = toolFilter_;
  if (document_) toolMask &= document_->selection().filter();
  return toolMask != core::kAllKinds && core::hasKind(toolMask, ShapeKind::Component);
}

void ViewerWidget::setViewportInsets(QMargins m) {
  insets_ = m;
  d_->insets = m;
}

void ViewerWidget::setToolManager(core::ToolManager* manager) { manager_ = manager; }

void ViewerWidget::connectDocument() {
  for (auto& c : docConnections_) QObject::disconnect(c);
  docConnections_.clear();
  if (!document_) return;
  auto sync = [this] { scheduleSync(); };
  auto syncId = [this](ObjectId) { scheduleSync(); };
  auto& cs = docConnections_;
  cs.push_back(connect(document_, &Document::bodyAdded, this, syncId));
  cs.push_back(connect(document_, &Document::bodyRemoved, this, syncId));
  cs.push_back(connect(document_, &Document::bodyChanged, this, syncId));
  cs.push_back(connect(document_, &Document::featureAdded, this, [this](ObjectId, int) { scheduleSync(); }));
  cs.push_back(connect(document_, &Document::featureRemoved, this, [this](ObjectId id, int) {
    sceneOptions_.hiddenFeatures.erase(id);
    scheduleSync();
  }));
  cs.push_back(connect(document_, &Document::featureMoved, this, [this](ObjectId, int, int) { scheduleSync(); }));
  cs.push_back(connect(document_, &Document::featureChanged, this, syncId));
  cs.push_back(connect(document_, &Document::featureStatusChanged, this, syncId));
  cs.push_back(connect(document_, &Document::rollbackChanged, this, [this](int) { scheduleSync(); }));
  cs.push_back(connect(document_, &Document::recomputeFinished, this, sync));
  auto compSync = [this](ObjectId) {
    // a component added/removed below the active one changes the solid (non-ghosted) set
    if (document_ && document_->activeComponent() != core::kRootComponentId) syncGhostingToActiveComponent();
    scheduleSync();
  };
  cs.push_back(connect(document_, &Document::componentAdded, this, compSync));
  cs.push_back(connect(document_, &Document::componentRemoved, this, compSync));
  cs.push_back(connect(document_, &Document::componentChanged, this, syncId));
  cs.push_back(connect(document_, &Document::activeComponentChanged, this,
                       [this](ObjectId) { syncGhostingToActiveComponent(); }));
  cs.push_back(connect(document_, &Document::documentReset, this, [this] {
    syncGhostingToActiveComponent();
    scene_.clear();
    hoverRef_.reset();
    if (d_->ready()) {
      for (auto& [k, o] : d_->objects) d_->ctx()->Remove(o.obj, false);
      d_->objects.clear();
      d_->objectKeys.clear();
    }
    scheduleSync();
  }));
  auto* sel = &document_->selection();
  cs.push_back(connect(sel, &core::SelectionModel::changed, this, [this] { scheduleOverlaySync(); }));
  cs.push_back(connect(sel, &core::SelectionModel::filterChanged, this, [this](ShapeKindMask) { onFilterInputsChanged(); }));
  cs.push_back(connect(sel, &core::SelectionModel::preselectionChanged, this, [this] {
    const auto& pre = document_->selection().preselection();
    if (pre != hoverRef_) scheduleOverlaySync();
  }));
}

void ViewerWidget::scheduleSync() {
  if (syncPending_) return;
  syncPending_ = true;
  QTimer::singleShot(0, this, [this] {
    if (syncPending_) doSync();
  });
}

void ViewerWidget::scheduleOverlaySync() {
  if (overlayPending_) return;
  overlayPending_ = true;
  QTimer::singleShot(0, this, [this] {
    if (overlayPending_) syncOverlays();
  });
}

void ViewerWidget::syncNow() {
  if (syncPending_) doSync();
  if (overlayPending_) syncOverlays();
}

bool ViewerWidget::initializeView() {
  if (d_->view) return true;
  if (d_->failed) return false;
  const QString plat = QGuiApplication::platformName();
  if (plat != "xcb") {
    onGlFailed(QString("The OpenCASCADE 3D view needs the X11 (xcb) platform plugin; Qt is running on '%1'.").arg(plat));
    return false;
  }
  try {
    d_->view = new OcctView(d_->listener.get(), this);
  } catch (const Standard_Failure& f) {
    onGlFailed(QString("Cannot create the OpenCASCADE viewer: %1").arg(f.GetMessageString()));
    return false;
  }
  layout()->addWidget(d_->view);
  applyGestureMap();
  d_->view->show();
  // keep overlay children (nav bar, command dialog) above the GL widget
  for (QObject* c : children())
    if (auto* w = qobject_cast<QWidget*>(c); w && w != d_->view && w->isVisible()) w->raise();
  return true;
}

bool ViewerWidget::hasGlView() const { return d_->ready(); }
QString ViewerWidget::glInfo() const { return d_->view ? d_->view->glInfo() : d_->info; }

void ViewerWidget::onGlFailed(const QString& why) {
  d_->failed = true;
  d_->info = why;
  FC_WARN("viewer: {}", why.toStdString());
  if (d_->view) d_->view->hide();
  emit viewInitFailed(why);
  update();
}

void ViewerWidget::onGlInitialized() {
  const Handle(V3d_View)& view = d_->v3d();
  const Handle(AIS_InteractiveContext)& ctx = d_->ctx();

  // background, anti-aliasing, origin triad
  view->SetBgGradientColors(hexq("#FFFFFF"), hexq("#C8D0D9"), Aspect_GradientFillMethod_Vertical, false);
  int msaa = 4;
  if (qEnvironmentVariableIsSet("FABRICAD_MSAA")) msaa = qEnvironmentVariableIntValue("FABRICAD_MSAA");
  view->ChangeRenderingParams().NbMsaaSamples = std::clamp(msaa, 0, 8);
  view->ChangeRenderingParams().IsAntialiasingEnabled = msaa > 0;
  view->TriedronDisplay(Aspect_TOTP_LEFT_LOWER, Quantity_NOC_GRAY40, 0.08, V3d_ZBUFFER);

  // lighting: camera headlight + soft ambient + a world key light from above, so every face
  // facing the user reads as the light steel grey of ux.md 6.1 (OCCT's default rig leaves
  // faces turned away from its single world light nearly black)
  {
    Handle(V3d_Viewer) v = view->Viewer();
    std::vector<Handle(V3d_Light)> old;
    for (V3d_ListOfLightIterator it(v->DefinedLightIterator()); it.More(); it.Next()) old.push_back(it.Value());
    for (const Handle(V3d_Light)& l : old) v->DelLight(l);
    Handle(V3d_AmbientLight) amb = new V3d_AmbientLight(Quantity_NOC_WHITE);
    amb->SetIntensity(1.0f);
    Handle(V3d_DirectionalLight) head = new V3d_DirectionalLight(gp_Dir(0.25, 0.35, -1.0), Quantity_NOC_WHITE, true);
    head->SetIntensity(0.8f);
    Handle(V3d_DirectionalLight) key = new V3d_DirectionalLight(gp_Dir(-0.35, -0.55, -1.0), Quantity_NOC_WHITE, false);
    key->SetIntensity(0.3f);
    for (const Handle(V3d_Light)& l : {Handle(V3d_Light)(amb), Handle(V3d_Light)(head), Handle(V3d_Light)(key)}) {
      v->AddLight(l);
      v->SetLightOn(l);
      view->SetLightOn(l);
    }
  }

  // default drawer: Fusion-like steel grey with dark visible edges, no iso lines
  Handle(Prs3d_Drawer) dr = ctx->DefaultDrawer();
  dr->SetTypeOfDeflection(Aspect_TOD_RELATIVE);
  dr->SetDeviationCoefficient(0.0015);
  dr->SetDeviationAngle(0.35);
  dr->UIsoAspect()->SetNumber(0);
  dr->VIsoAspect()->SetNumber(0);
  dr->SetFaceBoundaryDraw(true);
  dr->SetFaceBoundaryUpperContinuity(GeomAbs_C1);  // PART-11: no seam edges (see makeDisplayObject)
  dr->SetFaceBoundaryAspect(new Prs3d_LineAspect(hexq("#1A1A1A"), Aspect_TOL_SOLID, 1.0));
  ctx->SetPixelTolerance(4);
  ctx->SetAutomaticHilight(true);

  // highlight styles: preselection light blue
  for (auto type : {Prs3d_TypeOfHighlight_Dynamic, Prs3d_TypeOfHighlight_LocalDynamic}) {
    Handle(Prs3d_Drawer) hs = ctx->HighlightStyle(type);
    hs->SetColor(hexq("#4FA3FF"));
    hs->SetDisplayMode(AIS_Shaded);
    hs->SetTransparency(0.4f);
  }
  for (auto type : {Prs3d_TypeOfHighlight_Selected, Prs3d_TypeOfHighlight_LocalSelected}) {
    Handle(Prs3d_Drawer) hs = ctx->HighlightStyle(type);
    hs->SetColor(hexq("#0B6CFF"));
  }

  // ViewCube top-right, upper-case labels, light theme with an outline; generous edge and
  // corner zones (facet extension + minimum sizes) so they are easy to hit
  const Handle(AIS_ViewCube)& cube = d_->view->viewCube();
  cube->SetSize(80.0);
  cube->SetBoxFacetExtension(14.0);
  cube->SetRoundRadius(0.1);
  cube->SetBoxEdgeMinSize(5.0);
  cube->SetBoxCornerMinSize(5.0);
  cube->SetBoxEdgeGap(0.0);
  cube->SetFontHeight(13.0);
  cube->SetFont(viewerFont());
  cube->SetBoxColor(hexq("#F2F4F7"));
  cube->SetTextColor(hexq("#2B2E33"));
  cube->SetInnerColor(hexq("#9AA4B1"));
  // soft blue hover tint instead of OCCT's cyan
  for (Handle(Prs3d_Drawer) hl : {cube->DynamicHilightAttributes(), cube->HilightAttributes()}) {
    if (hl.IsNull()) continue;
    hl->SetColor(hexq("#9CC6FF"));
    hl->SetTransparency(0.0f);
    if (hl->HasOwnShadingAspect()) hl->ShadingAspect()->SetColor(hexq("#9CC6FF"));
  }
  cube->SetDrawAxes(false);
  cube->SetBoxSideLabel(V3d_Xpos, "RIGHT");
  cube->SetBoxSideLabel(V3d_Xneg, "LEFT");
  cube->SetBoxSideLabel(V3d_Ypos, "BACK");
  cube->SetBoxSideLabel(V3d_Yneg, "FRONT");
  cube->SetBoxSideLabel(V3d_Zpos, "TOP");
  cube->SetBoxSideLabel(V3d_Zneg, "BOTTOM");
  cube->SetDuration(0.25);
  cube->SetTransformPersistence(new Graphic3d_TransformPers(Graphic3d_TMF_TriedronPers, Aspect_TOTP_RIGHT_UPPER,
                                                            Graphic3d_Vec2i(int(kCubeOffset), int(kCubeOffset))));
  ctx->Display(cube, 0, 0, false);
  // after a cube click: rotate, then fit with our margin/nav-bar inset (OCCT's own fit leaves
  // almost no margin), keeping the chosen projection
  d_->view->setCubeFitter([this](const Handle(V3d_View)&, const Handle(Graphic3d_Camera)& cam) {
    if (d_->hasGeometry()) d_->fitCamera(cam, d_->sceneBox());
    assertProjection(cam);
  });

  // camera: orthographic iso, Z up
  view->Camera()->SetProjectionType(projection_ == core::ProjectionMode::Orthographic ? Graphic3d_Camera::Projection_Orthographic
                                                                                     : Graphic3d_Camera::Projection_Perspective);
  view->SetProj(V3d_XposYnegZpos);
  view->SetUp(0, 0, 1);
  view->SetSize(250.0);

  applyGestureMap();
  updateGrid(true);
  doSync();
  if (d_->hasGeometry()) fitAll(false);
  emit viewInitialized();
}

void ViewerWidget::applyGestureMap() {
  if (!d_->view) return;
  AIS_MouseGestureMap& g = d_->view->ChangeMouseGestureMap();
  g.Clear();
  for (const NavBinding& b : navigationBindings(navPreset_)) {
    unsigned key = 0;
    if (b.button & 1) key |= Aspect_VKeyMouse_LeftButton;
    if (b.button & 2) key |= Aspect_VKeyMouse_MiddleButton;
    if (b.button & 4) key |= Aspect_VKeyMouse_RightButton;
    if (b.modifiers & core::ModShift) key |= Aspect_VKeyFlags_SHIFT;
    if (b.modifiers & core::ModCtrl) key |= Aspect_VKeyFlags_CTRL;
    AIS_MouseGesture gesture = AIS_MouseGesture_NONE;
    switch (b.action) {
      case NavAction::Orbit: gesture = AIS_MouseGesture_RotateOrbit; break;
      case NavAction::Pan: gesture = AIS_MouseGesture_Pan; break;
      case NavAction::Zoom: gesture = AIS_MouseGesture_Zoom; break;
      case NavAction::None: break;
    }
    if (gesture != AIS_MouseGesture_NONE) g.Bind(key, gesture);
  }
  if (lmbNav_ != NavAction::None) {
    const AIS_MouseGesture lg = lmbNav_ == NavAction::Orbit ? AIS_MouseGesture_RotateOrbit
                                : lmbNav_ == NavAction::Pan ? AIS_MouseGesture_Pan
                                                            : AIS_MouseGesture_Zoom;
    g.Bind(Aspect_VKeyMouse_LeftButton, lg);
  }
  d_->view->SetRotationMode(AIS_RotationMode_PickCenter);
  d_->view->SetNavigationMode(AIS_NavigationMode_Orbit);
  d_->view->SetAllowHighlight(false);  // the viewer does its own MoveTo (filtered)
  d_->view->SetAllowDragging(false);
  d_->view->SetStickToRayOnZoom(true);
}

void ViewerWidget::setNavigationPreset(NavPreset p) {
  navPreset_ = p;
  applyGestureMap();
}

void ViewerWidget::setLeftButtonNavigation(NavAction a) {
  lmbNav_ = a;
  applyGestureMap();
  setCursor(a == NavAction::None ? core::CursorShape::Arrow : core::CursorShape::Hand);
  emit displaySettingsChanged();
}

void ViewerWidget::orbitBy(double yawDeg, double pitchDeg, bool animate) {
  if (!d_->ready()) return;
  Handle(Graphic3d_Camera) cam = d_->cameraCopy();
  const gp_Pnt c = cam->Center();
  gp_Vec toEye(c, cam->Eye());
  gp_Dir up = cam->Up();
  const gp_Dir right = gp_Dir(gp_Vec(cam->Direction()).Crossed(gp_Vec(up)));
  gp_Trsf r1, r2;
  r1.SetRotation(gp_Ax1(c, up), yawDeg * M_PI / 180.0);
  r2.SetRotation(gp_Ax1(c, right), pitchDeg * M_PI / 180.0);
  const gp_Trsf r = r2 * r1;
  toEye.Transform(r);
  up.Transform(r);
  cam->SetEyeAndCenter(c.Translated(toEye), c);
  cam->SetUp(up);
  animateCamera(cam, animate ? 0.15 : 0.0);
}

void ViewerWidget::rollBy(double deg, bool animate) {
  if (!d_->ready()) return;
  Handle(Graphic3d_Camera) cam = d_->cameraCopy();
  gp_Dir up = cam->Up();
  gp_Trsf r;
  r.SetRotation(gp_Ax1(cam->Center(), cam->Direction()), deg * M_PI / 180.0);
  up.Transform(r);
  cam->SetUp(up);
  animateCamera(cam, animate ? 0.2 : 0.0);
}

void ViewerWidget::animateCamera(const Handle(Graphic3d_Camera)& target, double seconds) {
  Handle(Graphic3d_Camera) cam = new Graphic3d_Camera();
  cam->Copy(target);  // never animate to (or alias) a stored camera such as Home
  assertProjection(cam);
  d_->view->animateTo(cam, seconds);
}

void ViewerWidget::assertProjection(const Handle(Graphic3d_Camera)& cam) {
  const auto want = projection_ == core::ProjectionMode::Orthographic ? Graphic3d_Camera::Projection_Orthographic
                                                                      : Graphic3d_Camera::Projection_Perspective;
  if (!cam.IsNull() && cam->ProjectionType() != want) cam->SetProjectionType(want);
  if (!d_->ready()) return;
  const Handle(Graphic3d_Camera)& vc = d_->v3d()->Camera();
  if (vc->ProjectionType() != want) {
    FC_WARN("viewer: camera projection drifted, re-applying");
    vc->SetProjectionType(want);
    d_->v3d()->Invalidate();
  }
}

// ---------------------------------------------------------------------------------------
// filters, visibility options
// ---------------------------------------------------------------------------------------

ShapeKindMask ViewerWidget::effectiveFilter() const {
  ShapeKindMask m = toolFilter_ & priorityMask(priority_);
  if (document_) m &= const_cast<Document*>(document_)->selection().filter();
  return m;
}

void ViewerWidget::setSelectionPriority(SelectionPriority p) {
  priority_ = p;
  onFilterInputsChanged();
}

void ViewerWidget::setSelectionFilter(ShapeKindMask kinds) {
  toolFilter_ = kinds;
  null_.setSelectionFilter(kinds);
  onFilterInputsChanged();
}

void ViewerWidget::onFilterInputsChanged() {
  ShapeKindMask toolMask = toolFilter_;
  if (document_) toolMask &= document_->selection().filter();
  const bool restricted = toolMask != core::kAllKinds;
  // G3 PART-09: origin planes are forced visible for picking only while a tool asks for a
  // plane (its own filter, Selection::filter(), is restricted and has OriginPlane). Idle, the
  // filter is the user's (Faces, ...), and the planes follow the browser's Origin eye.
  const bool toolRestricted = document_ && document_->selection().filter() != core::kAllKinds;
  const bool planes = restricted && toolRestricted && core::hasKind(toolMask, ShapeKind::OriginPlane);
  const bool profiles = restricted && core::hasKind(toolMask, ShapeKind::Profile);
  if (planes != sceneOptions_.showOriginPlanes || profiles != sceneOptions_.showProfiles) {
    sceneOptions_.showOriginPlanes = planes;
    sceneOptions_.showProfiles = profiles;
    scheduleSync();
  }
  applySelectionModes();
  requestRedraw();
}

void ViewerWidget::setFeatureHidden(ObjectId id, bool hidden) {
  bool changed = hidden ? sceneOptions_.hiddenFeatures.insert(id).second : sceneOptions_.hiddenFeatures.erase(id) > 0;
  if (!changed) return;
  scheduleSync();
  emit featureVisibilityChanged();
}

void ViewerWidget::setHiddenFeatures(std::set<ObjectId> ids) {
  sceneOptions_.hiddenFeatures = std::move(ids);
  scheduleSync();
  emit featureVisibilityChanged();
}

void ViewerWidget::setOriginVisible(bool v) {
  if (sceneOptions_.showOrigin == v) return;
  sceneOptions_.showOrigin = v;
  scheduleSync();
  emit featureVisibilityChanged();
}

void ViewerWidget::setFolderVisible(const std::string& folder, bool v) {
  bool* flag = folder == "Sketches"       ? &sceneOptions_.showSketches
               : folder == "Construction" ? &sceneOptions_.showConstruction
               : folder == "Joints"       ? &sceneOptions_.showJoints
               : folder == "Origin"       ? &sceneOptions_.showOrigin
                                          : nullptr;
  if (!flag || *flag == v) return;
  *flag = v;
  scheduleSync();
  emit featureVisibilityChanged();
}

bool ViewerWidget::isFolderVisible(const std::string& folder) const {
  if (folder == "Sketches") return sceneOptions_.showSketches;
  if (folder == "Construction") return sceneOptions_.showConstruction;
  if (folder == "Joints") return sceneOptions_.showJoints;
  if (folder == "Origin") return sceneOptions_.showOrigin;
  return true;
}

bool ViewerWidget::isFeatureDisplayed(ObjectId id) const {
  if (!document_) return false;
  const core::Feature* f = document_->feature(id);
  return f && SceneModel::featureVisible(*document_, *f, sceneOptions_);
}

void ViewerWidget::setGhostedComponents(const std::vector<ObjectId>& ids) {
  null_.setGhostedComponents(ids);
  sceneOptions_.activeComponents = ids;
  scheduleSync();
}

void ViewerWidget::setFeatureVisibilityOverride(ObjectId id, std::optional<bool> visible) {
  null_.setFeatureVisibilityOverride(id, visible);
  if (visible) sceneOptions_.visibilityOverrides[id] = visible;
  else sceneOptions_.visibilityOverrides.erase(id);
  scheduleSync();
}

// ---------------------------------------------------------------------------------------
// display sync
// ---------------------------------------------------------------------------------------

namespace {

Handle(AIS_InteractiveObject) makeDisplayObject(const SceneItem& it, core::DisplayMode mode, int& dispMode,
                                                Graphic3d_ZLayerId& layer) {
  // G3 PART-11: tag edge regularity once so the face-boundary pass (upper continuity C1 on
  // the default drawer) skips seam edges of closed surfaces (CN); real and tangent (G1)
  // edges stay. Metadata only: the TShapes stay the same, so picking maps are unaffected.
  if (it.style == SceneStyle::Body && !it.shape.IsNull()) {
    try {
      BRepLib::EncodeRegularity(it.shape, 1.0e-6);
    } catch (const Standard_Failure&) {
    }
  }
  Handle(AIS_Shape) s = new AIS_Shape(it.shape);
  if (!isIdentity(it.placement)) s->SetLocalTransformation(it.placement);
  Handle(Prs3d_Drawer) dr = s->Attributes();
  const Quantity_Color col = qcol(it.color);
  layer = Graphic3d_ZLayerId_Default;
  dispMode = AIS_WireFrame;
  switch (it.style) {
    case SceneStyle::Body: {
      // ASPECT material: ambient/diffuse act as multipliers of the body colour. Plastified's
      // defaults render the steel grey far too dark; these give Fusion-like light parts.
      Graphic3d_MaterialAspect mat(Graphic3d_NameOfMaterial_Plastified);
      mat.SetAmbientColor(Quantity_Color(0.32, 0.32, 0.32, Quantity_TOC_RGB));
      mat.SetDiffuseColor(Quantity_Color(0.62, 0.62, 0.62, Quantity_TOC_RGB));
      mat.SetSpecularColor(Quantity_Color(0.18, 0.18, 0.18, Quantity_TOC_RGB));
      mat.SetShininess(0.35f);
      s->SetMaterial(mat);
      if (mode == core::DisplayMode::HiddenLine) {
        s->SetColor(Quantity_NOC_WHITE);
        dr->ShadingAspect()->Aspect()->SetShadingModel(Graphic3d_TypeOfShadingModel_Unlit);
      } else {
        s->SetColor(col);
      }
      dr->SetFaceBoundaryDraw(mode == core::DisplayMode::ShadedEdges || mode == core::DisplayMode::HiddenLine);
      dr->SetFaceBoundaryAspect(new Prs3d_LineAspect(hexq("#1A1A1A"), Aspect_TOL_SOLID, 1.0));
      if (mode == core::DisplayMode::Wireframe) {
        dispMode = AIS_WireFrame;
        s->SetColor(hexq("#2B2E33"));
      } else {
        dispMode = AIS_Shaded;
      }
      if (it.ghosted) s->SetTransparency(0.7);
      else if (it.color[3] < 0.999f) s->SetTransparency(1.0 - it.color[3]);
      break;
    }
    case SceneStyle::SketchFree:
    case SceneStyle::SketchDefined:
    case SceneStyle::SketchConstruction:
    case SceneStyle::SketchFixed:
    case SceneStyle::SketchProjected:
    case SceneStyle::SketchConflict:
    case SceneStyle::SketchPoints: {
      s->SetColor(col);
      s->SetWidth(it.style == SceneStyle::SketchConstruction ? 1.5 : 2.0);
      setLineStyle(dr, it.style == SceneStyle::SketchConstruction);
      dr->SetVertexDrawMode(Prs3d_VDM_All);
      dr->SetPointAspect(new Prs3d_PointAspect(Aspect_TOM_BALL, col, 0.5));
      layer = Graphic3d_ZLayerId_Top;
      break;
    }
    case SceneStyle::Profile: {
      s->SetColor(col);
      s->SetTransparency(1.0 - it.color[3]);
      dr->SetFaceBoundaryDraw(false);
      dispMode = AIS_Shaded;
      layer = Graphic3d_ZLayerId_Top;
      break;
    }
    case SceneStyle::ConstructionPlane:
    case SceneStyle::OriginPlane: {
      s->SetColor(col);
      s->SetTransparency(1.0 - it.color[3]);
      dr->SetFaceBoundaryDraw(true);
      dr->SetFaceBoundaryAspect(new Prs3d_LineAspect(hexq("#E58A00"), Aspect_TOL_SOLID, 1.0));
      dispMode = AIS_Shaded;
      break;
    }
    case SceneStyle::ConstructionAxis:
    case SceneStyle::OriginAxis:
    case SceneStyle::Joint: {
      s->SetColor(col);
      s->SetWidth(2.0);
      setLineStyle(dr, it.style == SceneStyle::ConstructionAxis);
      break;
    }
    case SceneStyle::ConstructionPoint:
    case SceneStyle::OriginPoint: {
      s->SetColor(col);
      dr->SetPointAspect(new Prs3d_PointAspect(Aspect_TOM_BALL, col, 0.8));
      break;
    }
  }
  return s;
}

std::vector<int> modesFor(const SceneItem& it, ShapeKindMask eff, bool pickGhosted) {
  std::vector<int> m;
  // ghosted (inactive-component) items are pickable only in assembly tools (ASM-01)
  if (it.ghosted && !(pickGhosted && it.style == SceneStyle::Body)) return m;
  using core::hasKind;
  if (it.style == SceneStyle::Body) {
    BrepModes b = brepModesFor(eff);
    if (b.body) m.push_back(0);
    if (b.face) m.push_back(AIS_Shape::SelectionMode(TopAbs_FACE));
    if (b.edge) m.push_back(AIS_Shape::SelectionMode(TopAbs_EDGE));
    if (b.vertex) m.push_back(AIS_Shape::SelectionMode(TopAbs_VERTEX));
    return m;
  }
  if (isSketchStyle(it.style)) {
    if (hasKind(eff, ShapeKind::SketchCurve)) m.push_back(AIS_Shape::SelectionMode(TopAbs_EDGE));
    // G3 PART-03: an input taking profiles (Extrude/Revolve "Profile|Face|Sketch") must not
    // turn a click on or near a curve into the whole sketch (= every profile). The profile
    // presentations cover the click; a whole sketch is still picked from the browser.
    else if (hasKind(eff, ShapeKind::Sketch) && !hasKind(eff, ShapeKind::Profile)) m.push_back(0);
    return m;
  }
  if (hasKind(eff, it.ref.kind) || (it.style == SceneStyle::Profile && hasKind(eff, ShapeKind::Profile))) m.push_back(0);
  return m;
}

}  // namespace

void ViewerWidget::doSync() {
  if (syncing_) return;
  struct Guard {
    bool& b;
    explicit Guard(bool& f) : b(f) { b = true; }
    ~Guard() { b = false; }
  } guard(syncing_);
  syncPending_ = false;
  ++syncCount_;
  if (!document_) {
    sceneItems_.clear();
  } else {
    sceneItems_ = scene_.collect(*document_, sceneOptions_);
    if (sceneOptions_.showOrigin || sceneOptions_.showOriginPlanes) {
      // origin planes follow the model size (1-2-5 steps, so edits rarely rebuild them)
      Bnd_Box box;
      for (const SceneItem& it : sceneItems_)
        if (it.style == SceneStyle::Body && !it.shape.IsNull() && !it.ghosted)
          BRepBndLib::Add(isIdentity(it.placement) ? it.shape : it.shape.Moved(TopLoc_Location(it.placement)), box);
      const double size = originPlaneHalfSize(box.IsVoid() ? 0.0 : std::sqrt(box.SquareExtent()));
      if (std::abs(size - sceneOptions_.originSize) > 1e-9) {
        sceneOptions_.originSize = size;
        sceneItems_ = scene_.collect(*document_, sceneOptions_);
      }
    }
  }
  if (!d_->ready()) return;

  const Handle(AIS_InteractiveContext)& ctx = d_->ctx();
  std::set<std::string> seen;
  for (const SceneItem& item : sceneItems_) {
    seen.insert(item.key);
    auto it = d_->objects.find(item.key);
    const bool recreate = it == d_->objects.end() || it->second.item.geomHash != item.geomHash ||
                          it->second.item.style != item.style || it->second.item.color != item.color ||
                          it->second.item.ghosted != item.ghosted;
    if (!recreate) {
      if (!sameTrsf(it->second.item.placement, item.placement)) {
        ctx->SetLocation(it->second.obj, TopLoc_Location(item.placement));
        it->second.item.placement = item.placement;
      }
      continue;
    }
    if (it != d_->objects.end()) {
      d_->objectKeys.erase(it->second.obj.get());
      ctx->Remove(it->second.obj, false);
      d_->objects.erase(it);
    }
    int dispMode = 0;
    Graphic3d_ZLayerId layer = Graphic3d_ZLayerId_Default;
    Handle(AIS_InteractiveObject) obj = makeDisplayObject(item, displayMode_, dispMode, layer);
    // selection mode 0 (not -1): only then is the object loaded into the selection manager;
    // applySelectionModes() below replaces it with the modes the filter wants
    ctx->Display(obj, dispMode, 0, false);
    if (layer != Graphic3d_ZLayerId_Default) ctx->SetZLayer(obj, layer);
    Impl::DisplayObj dobj;
    dobj.obj = obj;
    dobj.item = item;
    d_->objectKeys[obj.get()] = item.key;
    d_->objects.emplace(item.key, std::move(dobj));
  }
  for (auto it = d_->objects.begin(); it != d_->objects.end();) {
    if (seen.count(it->first)) {
      ++it;
      continue;
    }
    d_->objectKeys.erase(it->second.obj.get());
    ctx->Remove(it->second.obj, false);
    it = d_->objects.erase(it);
  }
  syncOriginLabels();
  applySelectionModes();
  syncOverlays();
  requestRedraw();
}

void ViewerWidget::syncOriginLabels() {
  const Handle(AIS_InteractiveContext)& ctx = d_->ctx();
  const bool show = sceneOptions_.showOrigin || sceneOptions_.showOriginPlanes;
  const double size = sceneOptions_.originSize;
  if (show && !d_->originLabels.empty() && std::abs(d_->originLabelSize - size) < 1e-9) return;
  for (auto& l : d_->originLabels) ctx->Remove(l, false);
  d_->originLabels.clear();
  if (!show) return;
  d_->originLabelSize = size;
  for (const char* name : {"XY", "XZ", "YZ"}) {
    // label near the outer corner of the plane (planes sit in the positive quadrant)
    const gp_Ax3 ax = core::originPlane(name);
    const gp_Pnt at = ax.Location().Translated((gp_Vec(ax.XDirection()) + gp_Vec(ax.YDirection())) * (size * 1.75));
    Handle(AIS_TextLabel) t = new AIS_TextLabel();
    t->SetText(TCollection_ExtendedString(name));
    t->SetPosition(at);
    t->SetHeight(12.0);
    t->SetFont(viewerFont());
    t->SetColor(hexq("#8A5A00"));
    t->SetZoomable(false);
    ctx->Display(t, 0, -1, false);  // never pickable
    d_->originLabels.push_back(t);
  }
}

void ViewerWidget::applySelectionModes() {
  if (!d_->ready()) return;
  const ShapeKindMask eff = selectionEnabled_ ? effectiveFilter() : core::kNoKinds;
  const Handle(AIS_InteractiveContext)& ctx = d_->ctx();
  const bool pickGhosted = ghostedPickable();
  for (auto& [key, o] : d_->objects) {
    std::vector<int> modes = modesFor(o.item, eff, pickGhosted);
    if (o.modesValid && modes == o.modes) continue;
    ctx->Deactivate(o.obj);
    for (int m : modes) ctx->Activate(o.obj, m, false);
    o.modes = std::move(modes);
    o.modesValid = true;
  }
}

void ViewerWidget::syncOverlays() {
  overlayPending_ = false;
  if (!d_->ready() || !document_) return;
  const Handle(AIS_InteractiveContext)& ctx = d_->ctx();
  for (auto& o : d_->overlays) ctx->Remove(o, false);
  d_->overlays.clear();

  // the scene item that displays a ref's owner (body for B-rep refs, sketch for sketch refs);
  // null when that owner is hidden: no overlay then (a hidden selected body must not ghost)
  const bool pickGhosted = ghostedPickable();
  auto ownerItem = [&](const ShapeRef& r) -> const SceneItem* {
    for (const SceneItem& it : sceneItems_) {
      if (it.ghosted && !pickGhosted) continue;
      if (r.isBrep() && it.style == SceneStyle::Body && it.ref.owner == r.owner) return &it;
      if ((r.kind == ShapeKind::SketchCurve || r.kind == ShapeKind::SketchPoint) && isSketchStyle(it.style) &&
          it.ref.owner == r.owner)
        return &it;
      if (!r.isBrep() && !isSketchStyle(it.style) && it.style != SceneStyle::Body && it.ref.owner == r.owner) return &it;
    }
    return nullptr;
  };
  auto add = [&](const ShapeRef& r, const Quantity_Color& col, double transparency) {
    TopoDS_Shape shape;
    gp_Trsf placement;
    for (const SceneItem& it : sceneItems_)
      if (it.ref == r) {
        shape = it.shape;
        placement = it.placement;
        break;
      }
    if (shape.IsNull()) {
      const SceneItem* owner = ownerItem(r);
      if (!owner) return;
      shape = core::resolveShape(*document_, r);
      placement = owner->placement;
    }
    if (shape.IsNull()) return;
    const bool wholeBody = r.kind == ShapeKind::Body || r.kind == ShapeKind::Component;
    Handle(AIS_Shape) o = new AIS_Shape(shape);
    if (!isIdentity(placement)) o->SetLocalTransformation(placement);
    Handle(Prs3d_Drawer) dr = o->Attributes();
    o->SetColor(col);
    const TopAbs_ShapeEnum type = contentType(shape);
    int mode = AIS_WireFrame;
    if (type == TopAbs_FACE) {
      mode = AIS_Shaded;
      if (transparency > 0.0) o->SetTransparency(transparency);
      // whole bodies get a dark-blue outline so the selection reads against the model
      dr->SetFaceBoundaryDraw(wholeBody);
      if (wholeBody) dr->SetFaceBoundaryAspect(new Prs3d_LineAspect(hexq("#0B4FC0"), Aspect_TOL_SOLID, 2.0));
      dr->ShadingAspect()->Aspect()->SetPolygonOffsets(Aspect_POM_Fill, 0.0f, -2.0f);
    } else if (type == TopAbs_EDGE) {
      o->SetWidth(3.0);
    } else {
      dr->SetPointAspect(new Prs3d_PointAspect(Aspect_TOM_BALL, col, 1.0));
    }
    ctx->Display(o, mode, -1, false);
    ctx->SetZLayer(o, Graphic3d_ZLayerId_Top);
    d_->overlays.push_back(o);
  };

  const Quantity_Color selCol = hexq("#0B6CFF");
  // a component highlights each of its displayed bodies (with that body's placement)
  auto addRef = [&](const ShapeRef& r, const Quantity_Color& col, double transparency) {
    if (r.kind != ShapeKind::Component) return add(r, col, transparency);
    for (ObjectId b : document_->bodiesOf(r.owner)) add(ShapeRef::body(b), col, transparency);
  };
  for (const ShapeRef& r : document_->selection().items()) addRef(r, selCol, 0.15);
  for (const ShapeRef& r : highlighted_)
    if (!document_->selection().contains(r)) addRef(r, selCol, 0.3);
  const auto& pre = document_->selection().preselection();
  if (pre && pre != hoverRef_) addRef(*pre, hexq("#4FA3FF"), 0.6);
  requestRedraw();
}

// ---------------------------------------------------------------------------------------
// picking
// ---------------------------------------------------------------------------------------

namespace {

/// Map an AIS owner to a ShapeRef using the display item it belongs to.
bool ownerToRef(const Document& doc, SceneModel& scene, const SceneItem& item, const Handle(SelectMgr_EntityOwner)& owner,
                const gp_Pnt& hit, SelectionPriority prio, ShapeRef& out) {
  Handle(StdSelect_BRepOwner) brep = Handle(StdSelect_BRepOwner)::DownCast(owner);
  if (item.style == SceneStyle::Body) {
    const ObjectId bodyId = item.ref.owner;
    if (!brep.IsNull() && brep->HasShape()) {
      const TopoDS_Shape& sub = brep->Shape();
      const TopAbs_ShapeEnum t = sub.ShapeType();
      if (t == TopAbs_FACE || t == TopAbs_EDGE || t == TopAbs_VERTEX) {
        out = core::makeBrepRef(doc, bodyId, sub);
        if (out.index <= 0 && !isIdentity(item.placement)) {
          TopoDS_Shape local = sub.Moved(TopLoc_Location(item.placement.Inverted()));
          ShapeRef r2 = core::makeBrepRef(doc, bodyId, local);
          if (r2.index > 0) out = r2;
        }
        out.point = core::Vec3::from(hit);
        out.hasPoint = true;
        return true;
      }
    }
    out = ShapeRef::body(bodyId);
    if (prio == SelectionPriority::Component)
      if (const core::Body* b = doc.body(bodyId)) out = ShapeRef::component(b->component);
    out.point = core::Vec3::from(hit);
    out.hasPoint = true;
    return true;
  }
  if (isSketchStyle(item.style)) {
    const ObjectId sid = item.ref.owner;
    if (!brep.IsNull() && brep->HasShape() && brep->Shape().ShapeType() == TopAbs_EDGE) {
      const int cid = scene.sketchCurveForEdge(doc, sid, brep->Shape());
      out = cid >= 0 ? ShapeRef::sketchCurve(sid, cid) : ShapeRef::sketch(sid);
    } else {
      out = ShapeRef::sketch(sid);
    }
    out.point = core::Vec3::from(hit);
    out.hasPoint = true;
    return true;
  }
  out = item.ref;
  if (item.style != SceneStyle::Profile) {  // profile refs keep their seed point
    out.point = core::Vec3::from(hit);
    out.hasPoint = true;
  }
  return true;
}

}  // namespace

bool ViewerWidget::detect(int x, int y, ShapeKindMask kinds, ShapeRef& out, gp_Pnt* hit, bool* onCube) {
  if (onCube) *onCube = false;
  if (!d_->ready() || !document_) return false;
  // G3 PART-12: a tool that just started (plane input: origin planes / profiles shown) has a
  // scene rebuild pending; pick on the current scene and selection modes, not the stale ones.
  if (syncPending_ && !syncing_) doSync();
  const double dpr = d_->view->devicePixelRatioF();
  const int px = static_cast<int>(std::lround(x * dpr)), py = static_cast<int>(std::lround(y * dpr));
  const Handle(AIS_InteractiveContext)& ctx = d_->ctx();
  ctx->MoveTo(px, py, d_->v3d(), false);
  const Handle(StdSelect_ViewerSelector3d)& sel = ctx->MainSelector();
  bool found = false;
  gp_Pnt bestHit;
  for (int i = 1; i <= sel->NbPicked(); ++i) {
    const Handle(SelectMgr_EntityOwner)& owner = sel->Picked(i);
    if (owner.IsNull()) continue;
    const SelectMgr_SelectableObject* so = owner->Selectable().get();
    if (so == d_->view->viewCube().get()) {
      if (i == 1) {
        if (onCube) *onCube = true;
        return false;
      }
      continue;
    }
    auto k = d_->objectKeys.find(so);
    if (k == d_->objectKeys.end()) continue;
    auto o = d_->objects.find(k->second);
    if (o == d_->objects.end()) continue;
    ShapeRef r;
    const gp_Pnt p = sel->PickedPoint(i);
    if (!ownerToRef(*document_, scene_, o->second.item, owner, p, priority_, r)) continue;
    if (!core::hasKind(kinds, r.kind)) continue;
    out = r;
    bestHit = p;
    found = true;
    break;
  }

  // sketch points are picked in screen space (8 px), and win over curves/faces
  if (core::hasKind(kinds, ShapeKind::SketchPoint)) {
    std::set<ObjectId> sketches;
    for (const SceneItem& it : sceneItems_)
      if (isSketchStyle(it.style)) sketches.insert(it.ref.owner);
    double bestD = 8.0;
    for (ObjectId sid : sketches) {
      for (const SceneModel::PointCandidate& c : scene_.sketchPoints(*document_, sid)) {
        double sx, sy;
        if (!worldToScreen(c.position, sx, sy)) continue;
        const double dd = std::hypot(sx - x, sy - y);
        if (dd < bestD) {
          bestD = dd;
          out = c.ref;
          out.point = core::Vec3::from(c.position);
          out.hasPoint = true;
          bestHit = c.position;
          found = true;
        }
      }
    }
  }
  if (found && hit) *hit = bestHit;
  return found;
}

bool ViewerWidget::pick(int x, int y, ShapeKindMask kinds, ShapeRef& out, gp_Pnt* hitPoint) {
  if (!d_->ready()) return null_.pick(x, y, kinds, out, hitPoint);
  return detect(x, y, kinds, out, hitPoint);
}

std::vector<ShapeRef> ViewerWidget::pickAll(int x, int y, ShapeKindMask kinds) {
  std::vector<ShapeRef> out;
  if (!d_->ready() || !document_) return out;
  if (syncPending_ && !syncing_) doSync();
  const double dpr = d_->view->devicePixelRatioF();
  const Handle(AIS_InteractiveContext)& ctx = d_->ctx();
  ctx->MoveTo(static_cast<int>(std::lround(x * dpr)), static_cast<int>(std::lround(y * dpr)), d_->v3d(), false);
  const Handle(StdSelect_ViewerSelector3d)& sel = ctx->MainSelector();
  for (int i = 1; i <= sel->NbPicked(); ++i) {
    const Handle(SelectMgr_EntityOwner)& owner = sel->Picked(i);
    auto k = d_->objectKeys.find(owner->Selectable().get());
    if (k == d_->objectKeys.end()) continue;
    ShapeRef r;
    if (!ownerToRef(*document_, scene_, d_->objects[k->second].item, owner, sel->PickedPoint(i), priority_, r)) continue;
    if (!core::hasKind(kinds, r.kind)) continue;
    if (std::find(out.begin(), out.end(), r) == out.end()) out.push_back(r);
  }
  return out;
}

// ---------------------------------------------------------------------------------------
// input
// ---------------------------------------------------------------------------------------

core::ViewerMouseEvent ViewerWidget::buildEvent(QMouseEvent* e, core::MouseButton button, bool withPick) {
  // a click during a camera animation (Look At on entering a sketch, Fit, Home) is projected
  // with the camera the animation goes to, not with a frame in between (SK-03)
  if (button != core::MouseButton::None && d_->ready()) d_->view->finishAnimation();
  core::ViewerMouseEvent ev;
  ev.button = button;
  ev.buttons = buttonsOf(e->buttons());
  ev.modifiers = modifiersOf(e->modifiers());
  ev.x = static_cast<int>(std::lround(e->position().x()));
  ev.y = static_cast<int>(std::lround(e->position().y()));
  gp_Pnt o;
  gp_Dir d;
  if (screenRay(ev.x, ev.y, o, d)) {
    ev.rayOrigin = core::Vec3::from(o);
    ev.rayDir = core::Vec3::from(d);
    if (sketchMode_) {
      gp_Pnt p;
      if (intersectLinePlane(o, d, gp_Pln(sketchPlane_), p)) {
        if (gridSnap_ && !(ev.modifiers & core::ModCtrl)) p = snapToGrid(sketchPlane_, p, gridSpacing());
        ev.hasPlanePoint = true;
        ev.planePoint = core::Vec3::from(p);
      }
    }
  }
  if (withPick && selectionEnabled_) {
    gp_Pnt hit;
    ShapeRef r;
    if (detect(ev.x, ev.y, effectiveFilter(), r, &hit)) {
      ev.hasPick = true;
      ev.pick = r;
      ev.pickPoint = core::Vec3::from(hit);
    }
  }
  return ev;
}

void ViewerWidget::updateHover(const core::ViewerMouseEvent& ev) {
  if (!document_) return;
  std::optional<ShapeRef> now;
  if (ev.hasPick) now = ev.pick;
  if (now == hoverRef_) return;
  hoverRef_ = now;
  document_->selection().setPreselection(now);
  if (!now && d_->ready()) d_->ctx()->ClearDetected(false);
  requestImmediateRedraw();
}

Qt::MouseButtons ViewerWidget::onMousePress(QMouseEvent* e) {
  if (e->button() == Qt::MiddleButton) {
    navigating_ = true;
    return Qt::MiddleButton;
  }
  if (e->button() == Qt::LeftButton && lmbNav_ != NavAction::None) {
    navigating_ = true;
    return Qt::MiddleButton | Qt::LeftButton;
  }
  if (e->button() == Qt::LeftButton) {
    // ViewCube overlay controls and the cube itself take the press ahead of tools: a click
    // rotates to the clicked part, a drag orbits; neither selects nor reaches a tool
    if (const CubeCtl ctl = cubeControlAt(e->position()); ctl != CubeCtl::None) {
      ctlPress_ = true;
      triggerCubeControl(ctl);
      return Qt::MiddleButton;
    }
    {
      bool onCube = false;
      ShapeRef dummy;
      const QPoint p = e->position().toPoint();
      detect(p.x(), p.y(), effectiveFilter(), dummy, nullptr, &onCube);
      if (onCube) {
        cubePress_ = true;
        cubeDrag_ = false;
        pressPos_ = cubeLast_ = p;
        return Qt::MiddleButton;
      }
    }
    leftDown_ = true;
    rubber_ = false;
    suppressRelease_ = false;
    pressPos_ = e->position().toPoint();
    rubberEnd_ = pressPos_;
    // manipulators grab the pointer ahead of tools and selection
    if (gizmoPress(pressPos_.x(), pressPos_.y(), modifiersOf(e->modifiers()))) {
      leftConsumed_ = true;
      if (hoverRef_ && document_) {
        hoverRef_.reset();
        document_->selection().setPreselection(std::nullopt);
      }
      return Qt::MiddleButton;
    }
    core::ViewerMouseEvent ev = buildEvent(e, core::MouseButton::Left, true);
    leftConsumed_ = manager_ && manager_->mousePress(ev);
  } else if (e->button() == Qt::RightButton) {
    core::ViewerMouseEvent ev = buildEvent(e, core::MouseButton::Right, true);
    rightDown_ = true;
    rightDragged_ = false;
    rightPressPos_ = e->position().toPoint();
    ShapeRef dummy;
    detect(ev.x, ev.y, effectiveFilter(), dummy, nullptr, &rightOnCube_);
    rightConsumed_ = manager_ && manager_->mousePress(ev);
  }
  return Qt::MiddleButton;
}

Qt::MouseButtons ViewerWidget::onMouseMove(QMouseEvent* e) {
  if (lmbNav_ != NavAction::None && (e->buttons() & Qt::LeftButton)) return Qt::MiddleButton | Qt::LeftButton;
  if (e->buttons() & Qt::MiddleButton) {
    navigating_ = true;
    return Qt::MiddleButton;
  }
  navigating_ = false;
  const QPoint pos = e->position().toPoint();
  if (ctlPress_) return Qt::MiddleButton;
  if (cubePress_) {
    if (!cubeDrag_ && exceedsDragThreshold(pos.x() - pressPos_.x(), pos.y() - pressPos_.y())) cubeDrag_ = true;
    if (cubeDrag_) {
      const QPoint dl = pos - cubeLast_;
      cubeLast_ = pos;
      constexpr double kDegPerPx = 0.5;
      if (!dl.isNull()) orbitBy(-dl.x() * kDegPerPx, -dl.y() * kDegPerPx, false);
    }
    return Qt::MiddleButton;
  }
  if (!leftDown_) {
    const CubeCtl hc = cubeControlAt(e->position());
    if (hc != hoverCtl_) {
      hoverCtl_ = hc;
      requestImmediateRedraw();
    }
  }
  if (manipulators_.isDragging()) {
    gizmoMove(pos.x(), pos.y(), modifiersOf(e->modifiers()));
    return Qt::MiddleButton;
  }
  const bool onGizmo = !rubber_ && !leftDown_ && gizmoMove(pos.x(), pos.y(), modifiersOf(e->modifiers()));
  if (onGizmo != gizmoCursor_) {
    gizmoCursor_ = onGizmo;
    setCursor(onGizmo ? core::CursorShape::Hand : core::CursorShape::Arrow);
  }
  core::ViewerMouseEvent ev = buildEvent(e, core::MouseButton::None, !rubber_ && !onGizmo);
  if (manager_) manager_->mouseMove(ev);
  if (!rubber_) updateHover(ev);
  if (d_->ready()) {  // ViewCube hover tint: repaint when the hovered cube part changes
    const Handle(AIS_InteractiveContext)& ctx = d_->ctx();
    const void* co = nullptr;
    if (ctx->HasDetected() && ctx->DetectedOwner()->Selectable().get() == d_->view->viewCube().get())
      co = ctx->DetectedOwner().get();
    if (co != cubeHoverOwner_) {
      cubeHoverOwner_ = co;
      requestImmediateRedraw();
    }
  }
  if (leftDown_ && !leftConsumed_) {
    const QPoint dlt = pos - pressPos_;
    if (!rubber_ && exceedsDragThreshold(dlt.x(), dlt.y())) rubber_ = true;
    if (rubber_) {
      rubberEnd_ = pos;
      requestImmediateRedraw();
    }
  }
  if (rightDown_ && !rightConsumed_) {
    const QPoint dlt = pos - rightPressPos_;
    if (exceedsDragThreshold(dlt.x(), dlt.y(), 15)) rightDragged_ = true;
  }
  return Qt::MiddleButton;
}

Qt::MouseButtons ViewerWidget::onMouseRelease(QMouseEvent* e) {
  if (lmbNav_ != NavAction::None && e->button() == Qt::LeftButton) {
    navigating_ = false;
    return Qt::MiddleButton | Qt::LeftButton;
  }
  if (e->button() == Qt::MiddleButton) {
    navigating_ = false;
    return Qt::MiddleButton;
  }
  const QPoint pos = e->position().toPoint();
  if (e->button() == Qt::LeftButton && ctlPress_) {
    ctlPress_ = false;
    return Qt::MiddleButton;
  }
  if (e->button() == Qt::LeftButton && cubePress_) {
    const bool click = !cubeDrag_;
    cubePress_ = cubeDrag_ = false;
    if (click && d_->ready()) {
      bool onCube = false;
      ShapeRef dummy;
      detect(pos.x(), pos.y(), effectiveFilter(), dummy, nullptr, &onCube);
      if (onCube && d_->ctx()->HasDetected()) {
        const double dpr = d_->view->devicePixelRatioF();
        d_->ctx()->DetectedOwner()->HandleMouseClick(
            Graphic3d_Vec2i(static_cast<int>(pos.x() * dpr), static_cast<int>(pos.y() * dpr)),
            Aspect_VKeyMouse_LeftButton, Aspect_VKeyFlags_NONE, false);
      }
    }
    // drop the detected part so the hover tint does not stick after the click
    if (d_->ready()) d_->ctx()->ClearDetected(false);
    cubeHoverOwner_ = nullptr;
    requestRedraw();
    return Qt::MiddleButton;
  }
  if (e->button() == Qt::LeftButton && manipulators_.isDragging()) {
    gizmoRelease(pos.x(), pos.y(), modifiersOf(e->modifiers()));
    leftDown_ = false;
    leftConsumed_ = false;
    rubber_ = false;
    suppressRelease_ = false;
    requestRedraw();
    return Qt::MiddleButton;
  }
  if (e->button() == Qt::LeftButton) {
    core::ViewerMouseEvent ev = buildEvent(e, core::MouseButton::Left, !rubber_);
    const bool consumed = (manager_ && manager_->mouseRelease(ev)) || leftConsumed_;
    if (!consumed && !suppressRelease_ && leftDown_) {
      if (rubber_) {
        rectangleSelect(pressPos_, pos, ev.modifiers);
      } else {
        bool onCube = false;
        ShapeRef dummy;
        detect(ev.x, ev.y, effectiveFilter(), dummy, nullptr, &onCube);
        if (onCube && d_->ready() && d_->ctx()->HasDetected()) {
          const double dpr = d_->view->devicePixelRatioF();
          d_->ctx()->DetectedOwner()->HandleMouseClick(
              Graphic3d_Vec2i(static_cast<int>(pos.x() * dpr), static_cast<int>(pos.y() * dpr)),
              Aspect_VKeyMouse_LeftButton, Aspect_VKeyFlags_NONE, false);
          requestRedraw();
        } else {
          defaultClickSelect(ev);
        }
      }
    }
    leftDown_ = false;
    leftConsumed_ = false;
    rubber_ = false;
    suppressRelease_ = false;
    requestRedraw();
  } else if (e->button() == Qt::RightButton) {
    core::ViewerMouseEvent ev = buildEvent(e, core::MouseButton::Right, false);
    const bool consumed = (manager_ && manager_->mouseRelease(ev)) || rightConsumed_;
    if (!consumed && rightDown_) {
      const QPoint g = mapToGlobal(pos);
      if (rightOnCube_ && !rightDragged_) emit viewCubeMenuRequested(g);
      else emit markingMenuRequested(mapToGlobal(rightPressPos_), g, rightDragged_);
    }
    rightDown_ = false;
    rightConsumed_ = false;
  }
  return Qt::MiddleButton;
}

void ViewerWidget::onMouseDoubleClick(QMouseEvent* e) {
  if (e->button() == Qt::MiddleButton) {
    fitAll(true);
    return;
  }
  if (e->button() != Qt::LeftButton) return;
  // fast clicks on the ViewCube / its controls are two clicks, not a double-click
  if (const CubeCtl ctl = cubeControlAt(e->position()); ctl != CubeCtl::None) {
    ctlPress_ = true;
    triggerCubeControl(ctl);
    return;
  }
  {
    bool onCube = false;
    ShapeRef dummy;
    const QPoint p = e->position().toPoint();
    detect(p.x(), p.y(), effectiveFilter(), dummy, nullptr, &onCube);
    if (onCube) {
      cubePress_ = true;
      cubeDrag_ = false;
      pressPos_ = cubeLast_ = p;
      return;
    }
  }
  core::ViewerMouseEvent ev = buildEvent(e, core::MouseButton::Left, true);
  ev.clickCount = 2;
  leftDown_ = true;
  suppressRelease_ = true;
  const bool consumed = manager_ && manager_->mouseDoubleClick(ev);
  leftConsumed_ = consumed;
  if (consumed || !ev.hasPick || !document_) return;
  if (ev.pick.kind == ShapeKind::Edge) {
    const TopoDS_Shape& body = document_->bodyShape(ev.pick.owner);
    TopoDS_Shape edge = core::subShapeAt(body, TopAbs_EDGE, ev.pick.index);
    if (!edge.IsNull()) {
      std::vector<ShapeRef> refs;
      for (const TopoDS_Edge& ce : tangentChain(body, TopoDS::Edge(edge)))
        refs.push_back(core::makeBrepRef(*document_, ev.pick.owner, ce));
      core::SelectionModel& sel = document_->selection();
      if (selectOpForModifiers(ev.modifiers) == SelectOp::Replace) sel.set(refs);
      else
        for (const ShapeRef& r : refs) sel.add(r);
    }
  }
  emit entityDoubleClicked(ev.pick);
}

double ViewerWidget::onWheel(QWheelEvent*) { return reverseZoom_ ? -1.0 : 1.0; }

void ViewerWidget::onLeave() {
  manipulators_.leave();
  if (gizmoCursor_) {
    gizmoCursor_ = false;
    setCursor(core::CursorShape::Arrow);
  }
  if (hoverRef_ && document_) {
    hoverRef_.reset();
    document_->selection().setPreselection(std::nullopt);
  }
  if (d_->ready()) d_->ctx()->ClearDetected(false);
  cubeHoverOwner_ = nullptr;
  hoverCtl_ = CubeCtl::None;
  requestRedraw();
}

void ViewerWidget::defaultClickSelect(const core::ViewerMouseEvent& ev) {
  if (!document_) return;
  core::SelectionModel& sel = document_->selection();
  const SelectOp op = selectOpForModifiers(ev.modifiers);
  if (!ev.hasPick) {
    if (op == SelectOp::Replace) sel.clear();
    return;
  }
  switch (op) {
    case SelectOp::Replace: sel.set(ev.pick); break;
    case SelectOp::Add: sel.add(ev.pick); break;
    case SelectOp::Toggle: sel.toggle(ev.pick); break;
  }
}

void ViewerWidget::rectangleSelect(QPoint a, QPoint b, int modifiers) {
  if (!d_->ready() || !document_) return;
  const double dpr = d_->view->devicePixelRatioF();
  const int x0 = static_cast<int>(std::min(a.x(), b.x()) * dpr), x1 = static_cast<int>(std::max(a.x(), b.x()) * dpr);
  const int y0 = static_cast<int>(std::min(a.y(), b.y()) * dpr), y1 = static_cast<int>(std::max(a.y(), b.y()) * dpr);
  const Handle(StdSelect_ViewerSelector3d)& sel = d_->ctx()->MainSelector();
  sel->AllowOverlapDetection(isCrossingSelection(a.x(), b.x()));
  sel->Pick(x0, y0, x1, y1, d_->v3d());
  const ShapeKindMask eff = effectiveFilter();
  std::vector<ShapeRef> refs;
  for (int i = 1; i <= sel->NbPicked(); ++i) {
    const Handle(SelectMgr_EntityOwner)& owner = sel->Picked(i);
    auto k = d_->objectKeys.find(owner->Selectable().get());
    if (k == d_->objectKeys.end()) continue;
    ShapeRef r;
    if (!ownerToRef(*document_, scene_, d_->objects[k->second].item, owner, sel->PickedPoint(i), priority_, r)) continue;
    if (!core::hasKind(eff, r.kind)) continue;
    if (std::find(refs.begin(), refs.end(), r) == refs.end()) refs.push_back(r);
  }
  sel->AllowOverlapDetection(false);
  core::SelectionModel& model = document_->selection();
  switch (selectOpForModifiers(modifiers)) {
    case SelectOp::Replace: model.set(refs); break;
    case SelectOp::Add:
      for (const ShapeRef& r : refs) model.add(r);
      break;
    case SelectOp::Toggle:
      for (const ShapeRef& r : refs) model.toggle(r);
      break;
  }
}

// ---------------------------------------------------------------------------------------
// painting
// ---------------------------------------------------------------------------------------

void ViewerWidget::paintEvent(QPaintEvent*) {
  if (d_->view && d_->view->isVisible()) return;
  QPainter p(this);
  QLinearGradient g(0, 0, 0, height());
  g.setColorAt(0.0, QColor("#FFFFFF"));
  g.setColorAt(1.0, QColor("#C8D0D9"));
  p.fillRect(rect(), g);
  if (d_->failed) {
    p.setPen(QColor("#6B7079"));
    p.drawText(rect().adjusted(24, 24, -24, -24), Qt::AlignCenter | Qt::TextWordWrap, d_->info);
  }
}

void ViewerWidget::beforeRedraw() {
  assertProjection();
  updateGrid(false);
  refreshManipulatorSizes();
}

QPointF ViewerWidget::cubeCenter() const {
  const double dpr = d_->view ? d_->view->devicePixelRatioF() : 1.0;
  const double w = d_->view ? d_->view->width() : width();
  return {w - kCubeOffset / dpr, kCubeOffset / dpr};
}

std::vector<std::pair<ViewerWidget::CubeCtl, QRectF>> ViewerWidget::cubeControls() const {
  std::vector<std::pair<CubeCtl, QRectF>> out;
  if (!d_->ready()) return out;
  const QPointF c = cubeCenter();
  // the cube's size and offset are in device pixels (OCCT), the controls in logical ones
  const double dpr = d_->view->devicePixelRatioF();
  const double r = kCubeCtlRadius / dpr, s = 22.0, rs = 26.0 / dpr;
  auto box = [s](QPointF at) { return QRectF(at.x() - s / 2, at.y() - s / 2, s, s); };
  out.push_back({CubeCtl::Home, box(c + QPointF(-r, -r))});
  out.push_back({CubeCtl::RollCcw, box(c + QPointF(r - rs, -r))});
  out.push_back({CubeCtl::RollCw, box(c + QPointF(r, -r))});
  if (isFaceOnView(viewDirection(), upDirection())) {
    out.push_back({CubeCtl::StepLeft, box(c + QPointF(-r, 0))});
    out.push_back({CubeCtl::StepRight, box(c + QPointF(r, 0))});
    out.push_back({CubeCtl::StepUp, box(c + QPointF(0, -r))});
    out.push_back({CubeCtl::StepDown, box(c + QPointF(0, r))});
  }
  return out;
}

ViewerWidget::CubeCtl ViewerWidget::cubeControlAt(QPointF p) const {
  for (const auto& [ctl, r] : cubeControls())
    if (r.contains(p)) return ctl;
  return CubeCtl::None;
}

void ViewerWidget::triggerCubeControl(CubeCtl c) {
  switch (c) {
    case CubeCtl::Home: setStandardView(core::StandardView::Home, true); break;
    case CubeCtl::RollCcw: rollBy(90.0); break;
    case CubeCtl::RollCw: rollBy(-90.0); break;
    // step to the adjacent face: the face beside the arrow comes to the front
    case CubeCtl::StepLeft: orbitBy(-90.0, 0.0); break;
    case CubeCtl::StepRight: orbitBy(90.0, 0.0); break;
    case CubeCtl::StepUp: orbitBy(0.0, -90.0); break;
    case CubeCtl::StepDown: orbitBy(0.0, 90.0); break;
    case CubeCtl::None: break;
  }
}

void ViewerWidget::paintOverlay(QPainter& p) {
  // ViewCube controls (ux.md 5.1): home, roll arrows, face-step arrows (face-on views only)
  p.setRenderHint(QPainter::Antialiasing, true);
  for (const auto& [ctl, r] : cubeControls()) {
    const bool hot = ctl == hoverCtl_;
    const QColor fg = hot ? QColor("#0B6CFF") : QColor("#5F6B7A");
    if (hot) {
      p.setPen(Qt::NoPen);
      p.setBrush(QColor(255, 255, 255, 200));
      p.drawEllipse(r);
    }
    const QPointF c = r.center();
    QPen pen(fg, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    switch (ctl) {
      case CubeCtl::Home: {
        QPainterPath roof;
        roof.moveTo(c + QPointF(-7, -1));
        roof.lineTo(c + QPointF(0, -7));
        roof.lineTo(c + QPointF(7, -1));
        p.drawPath(roof);
        p.drawRect(QRectF(c.x() - 5, c.y() - 1, 10, 7));
        break;
      }
      case CubeCtl::RollCcw:
      case CubeCtl::RollCw: {
        const bool ccw = ctl == CubeCtl::RollCcw;
        const QRectF arc(c.x() - 6.5, c.y() - 6.5, 13, 13);
        // mirror-image arcs (gap at the lower right / lower left), head at the bottom
        p.drawArc(arc, (ccw ? 30 : 270) * 16, 240 * 16);
        const double a = 270.0 * M_PI / 180.0;
        const QPointF tip(c.x() + 6.5 * std::cos(a), c.y() - 6.5 * std::sin(a));
        const double t = a + (ccw ? M_PI / 2 : -M_PI / 2);  // tangent direction of travel
        const QPointF dir(std::cos(t), -std::sin(t)), nrm(-dir.y(), dir.x());
        QPolygonF head{tip + dir * 3.5, tip - dir * 2.0 + nrm * 3.5, tip - dir * 2.0 - nrm * 3.5};
        p.setBrush(fg);
        p.setPen(Qt::NoPen);
        p.drawPolygon(head);
        break;
      }
      default: {
        const QPointF d = ctl == CubeCtl::StepLeft    ? QPointF(-1, 0)
                          : ctl == CubeCtl::StepRight ? QPointF(1, 0)
                          : ctl == CubeCtl::StepUp    ? QPointF(0, -1)
                                                      : QPointF(0, 1);
        const QPointF n(-d.y(), d.x());
        QPolygonF tri{c + d * 6.0, c - d * 4.0 + n * 6.0, c - d * 4.0 - n * 6.0};
        p.setBrush(fg);
        p.setPen(Qt::NoPen);
        p.drawPolygon(tri);
        break;
      }
    }
  }
  if (!rubber_) return;
  const QRect r = QRect(pressPos_, rubberEnd_).normalized();
  const bool crossing = isCrossingSelection(pressPos_.x(), rubberEnd_.x());
  QPen pen(QColor("#0B6CFF"));
  pen.setWidth(1);
  pen.setStyle(crossing ? Qt::DashLine : Qt::SolidLine);
  p.setPen(pen);
  p.setBrush(QColor(11, 108, 255, crossing ? 30 : 45));
  p.drawRect(r);
}

void ViewerWidget::requestRedraw() {
  null_.requestRedraw();
  if (d_->view) d_->view->redrawAll();  // scene changes need a full frame, not just the immediate layer
  else update();
}

void ViewerWidget::requestImmediateRedraw() {
  if (d_->view) d_->view->updateView();
  else update();
}

void ViewerWidget::setCursor(core::CursorShape shape) {
  null_.setCursor(shape);
  Qt::CursorShape c = Qt::ArrowCursor;
  switch (shape) {
    case core::CursorShape::Arrow: c = Qt::ArrowCursor; break;
    case core::CursorShape::Cross: c = Qt::CrossCursor; break;
    case core::CursorShape::Pick: c = Qt::PointingHandCursor; break;
    case core::CursorShape::Hand: c = Qt::OpenHandCursor; break;
    case core::CursorShape::Busy: c = Qt::WaitCursor; break;
    case core::CursorShape::Forbidden: c = Qt::ForbiddenCursor; break;
  }
  QWidget::setCursor(c);
  if (d_->view) d_->view->setCursor(c);
}

// ---------------------------------------------------------------------------------------
// previews
// ---------------------------------------------------------------------------------------

namespace {

Quantity_ColorRGBA rgba(const PreviewStyle& s) {
  return Quantity_ColorRGBA(Quantity_Color(s.color[0], s.color[1], s.color[2], Quantity_TOC_sRGB), s.color[3]);
}

std::vector<gp_Pnt> toPnts(const std::vector<core::Vec3>& v) {
  std::vector<gp_Pnt> out;
  out.reserve(v.size());
  for (const core::Vec3& p : v) out.emplace_back(p.x, p.y, p.z);
  return out;
}

}  // namespace

PreviewHandle ViewerWidget::showPreviewShape(const TopoDS_Shape& shape, const PreviewStyle& style) {
  if (!d_->ready()) return null_.showPreviewShape(shape, style);
  Handle(AIS_Shape) s = new AIS_Shape(shape);
  const Quantity_Color col(style.color[0], style.color[1], style.color[2], Quantity_TOC_sRGB);
  s->SetColor(col);
  s->SetWidth(style.lineWidth);
  Handle(Prs3d_Drawer) dr = s->Attributes();
  setLineStyle(dr, style.dashed);
  int mode = AIS_WireFrame;
  if (style.shaded) {
    mode = AIS_Shaded;
    if (style.color[3] < 0.999f) s->SetTransparency(1.0 - style.color[3]);
    dr->SetFaceBoundaryDraw(true);
    dr->SetFaceBoundaryAspect(new Prs3d_LineAspect(col, Aspect_TOL_SOLID, 1.0));
  }
  d_->ctx()->Display(s, mode, -1, false);
  d_->ctx()->SetZLayer(s, style.onTop ? Graphic3d_ZLayerId_Topmost : Graphic3d_ZLayerId_Top);
  const PreviewHandle h = d_->nextPreview++;
  d_->previews[h] = {s, style};
  requestRedraw();
  return h;
}

namespace {
Handle(PrimitivePrs) makePrimitive(PrimitivePrs::Kind kind, const std::vector<core::Vec3>& pts, const PreviewStyle& style) {
  Handle(PrimitivePrs) p = new PrimitivePrs(kind, rgba(style), style.lineWidth, style.dashed, style.pointSize);
  p->setPoints(toPnts(pts));
  return p;
}
}  // namespace

PreviewHandle ViewerWidget::showPreviewLines(const std::vector<core::Vec3>& pts, const PreviewStyle& style) {
  if (!d_->ready()) return null_.showPreviewLines(pts, style);
  Handle(PrimitivePrs) p = makePrimitive(PrimitivePrs::Kind::Segments, pts, style);
  d_->ctx()->Display(p, 0, -1, false);
  d_->ctx()->SetZLayer(p, style.onTop ? Graphic3d_ZLayerId_Topmost : Graphic3d_ZLayerId_Top);
  const PreviewHandle h = d_->nextPreview++;
  d_->previews[h] = {p, style};
  requestRedraw();
  return h;
}

PreviewHandle ViewerWidget::showPreviewPolyline(const std::vector<core::Vec3>& pts, const PreviewStyle& style) {
  if (!d_->ready()) return null_.showPreviewPolyline(pts, style);
  Handle(PrimitivePrs) p = makePrimitive(PrimitivePrs::Kind::Polyline, pts, style);
  d_->ctx()->Display(p, 0, -1, false);
  d_->ctx()->SetZLayer(p, style.onTop ? Graphic3d_ZLayerId_Topmost : Graphic3d_ZLayerId_Top);
  const PreviewHandle h = d_->nextPreview++;
  d_->previews[h] = {p, style};
  requestRedraw();
  return h;
}

PreviewHandle ViewerWidget::showPreviewPoints(const std::vector<core::Vec3>& pts, const PreviewStyle& style) {
  if (!d_->ready()) return null_.showPreviewPoints(pts, style);
  Handle(PrimitivePrs) p = makePrimitive(PrimitivePrs::Kind::Points, pts, style);
  d_->ctx()->Display(p, 0, -1, false);
  d_->ctx()->SetZLayer(p, style.onTop ? Graphic3d_ZLayerId_Topmost : Graphic3d_ZLayerId_Top);
  const PreviewHandle h = d_->nextPreview++;
  d_->previews[h] = {p, style};
  requestRedraw();
  return h;
}

PreviewHandle ViewerWidget::showPreviewText(const core::Vec3& at, const std::string& text, const PreviewStyle& style) {
  if (!d_->ready()) return null_.showPreviewText(at, text, style);
  Handle(AIS_TextLabel) t = new AIS_TextLabel();
  t->SetText(TCollection_ExtendedString(text.c_str(), true));
  t->SetPosition(at.toPnt());
  t->SetHeight(style.textHeight);
  t->SetColor(Quantity_Color(style.color[0], style.color[1], style.color[2], Quantity_TOC_sRGB));
  t->SetZoomable(false);
  t->SetFont(viewerFont());
  d_->ctx()->Display(t, 0, -1, false);
  d_->ctx()->SetZLayer(t, Graphic3d_ZLayerId_Topmost);
  const PreviewHandle h = d_->nextPreview++;
  d_->previews[h] = {t, style};
  requestRedraw();
  return h;
}

void ViewerWidget::updatePreviewShape(PreviewHandle h, const TopoDS_Shape& shape) {
  auto it = d_->previews.find(h);
  if (it == d_->previews.end()) {
    null_.updatePreviewShape(h, shape);
    return;
  }
  Handle(AIS_Shape) s = Handle(AIS_Shape)::DownCast(it->second.obj);
  if (s.IsNull()) return;
  s->SetShape(shape);
  d_->ctx()->Redisplay(s, false);
  requestRedraw();
}

void ViewerWidget::updatePreviewLines(PreviewHandle h, const std::vector<core::Vec3>& pts) {
  auto it = d_->previews.find(h);
  if (it == d_->previews.end()) {
    null_.updatePreviewLines(h, pts);
    return;
  }
  Handle(PrimitivePrs) p = Handle(PrimitivePrs)::DownCast(it->second.obj);
  if (p.IsNull()) return;
  p->setPoints(toPnts(pts));
  d_->ctx()->Redisplay(p, false);
  requestRedraw();
}

void ViewerWidget::removePreview(PreviewHandle h) {
  auto it = d_->previews.find(h);
  if (it == d_->previews.end()) {
    null_.removePreview(h);
    return;
  }
  // a removed preview that is the detected (hovered) object would stay drawn in the immediate
  // layer until the next mouse move (SK-02)
  const Handle(AIS_InteractiveContext)& ctx = d_->ctx();
  if (ctx->HasDetected() && ctx->DetectedInteractive() == it->second.obj) ctx->ClearDetected(false);
  ctx->Remove(it->second.obj, false);
  d_->previews.erase(it);
  requestRedraw();
}

void ViewerWidget::clearPreviews() {
  null_.clearPreviews();
  if (d_->ready()) {
    const Handle(AIS_InteractiveContext)& ctx = d_->ctx();
    for (auto& [h, p] : d_->previews) {
      if (ctx->HasDetected() && ctx->DetectedInteractive() == p.obj) ctx->ClearDetected(false);
      ctx->Remove(p.obj, false);
    }
  }
  d_->previews.clear();
  requestRedraw();
}

void ViewerWidget::setHighlighted(const std::vector<ShapeRef>& refs) {
  null_.setHighlighted(refs);
  highlighted_ = refs;
  scheduleOverlaySync();
}

// ---------------------------------------------------------------------------------------
// manipulators
// ---------------------------------------------------------------------------------------

core::ManipulatorId ViewerWidget::showManipulator(const core::ManipulatorSpec& spec, core::ManipulatorCallback callback) {
  return manipulators_.add(spec, std::move(callback));  // the change listener builds the presentation
}

void ViewerWidget::updateManipulator(core::ManipulatorId id, const core::ManipulatorSpec& spec) { manipulators_.update(id, spec); }
void ViewerWidget::setManipulatorValue(core::ManipulatorId id, double value) { manipulators_.setValue(id, value); }
void ViewerWidget::removeManipulator(core::ManipulatorId id) { manipulators_.remove(id); }
void ViewerWidget::clearManipulators() { manipulators_.clear(); }

std::string ViewerWidget::manipulatorValueText(const core::ManipulatorModel& m, core::ManipulatorHandle highlight) const {
  using core::ManipulatorKind;
  const core::ManipulatorSpec& s = m.spec();
  if (!s.showValue || highlight == core::ManipulatorHandle::None) return {};
  bool angular = s.kind == ManipulatorKind::Angular;
  double v = s.value;
  if (s.kind == ManipulatorKind::Triad) {
    if (!m.isDragging()) return {};
    angular = core::isRotationHandle(highlight);
    v = m.dragAmount();
  } else if (s.kind == ManipulatorKind::Point) {
    return s.label;
  }
  std::string text;
  if (angular) {
    text = std::format("{:.4g}°", std::round(v * 1000.0) / 1000.0);
  } else if (document_) {
    text = core::formatQuantity(core::Quantity{v, core::Dimension::Length()}, document_->units());
  } else {
    text = std::format("{:.4g} mm", std::round(v * 1000.0) / 1000.0);
  }
  return s.label.empty() ? text : s.label + ": " + text;
}

// Gizmos are drawn and hit-tested with a scaled pixel size so auto-sized ones (the Move
// triad) appear kGizmoScale times larger than core's kAutoSizePixels; drawing and picking
// must use the same value.
namespace {
constexpr double kGizmoScale = 1.6;
}

double ViewerWidget::gizmoPixelSize(const gp_Pnt& at) const { return pixelSize(at) * kGizmoScale; }

bool ViewerWidget::gizmoRay(int x, int y, gp_Pnt& o, gp_Dir& d) const { return screenRay(x, y, o, d); }

bool ViewerWidget::gizmoPress(int x, int y, int modifiers) {
  if (manipulators_.count() == 0) return false;
  gp_Pnt o;
  gp_Dir d;
  if (!gizmoRay(x, y, o, d)) return false;
  return manipulators_.press(o, d, modifiers, [this](const gp_Pnt& at) { return gizmoPixelSize(at); });
}

bool ViewerWidget::gizmoMove(int x, int y, int modifiers) {
  if (manipulators_.count() == 0 && !manipulators_.isDragging()) return false;
  gp_Pnt o;
  gp_Dir d;
  if (!gizmoRay(x, y, o, d)) return false;
  return manipulators_.move(o, d, modifiers, [this](const gp_Pnt& at) { return gizmoPixelSize(at); });
}

bool ViewerWidget::gizmoRelease(int x, int y, int modifiers) {
  if (!manipulators_.isDragging()) return false;
  gp_Pnt o;
  gp_Dir d;
  if (!gizmoRay(x, y, o, d)) return false;
  return manipulators_.release(o, d, modifiers, [this](const gp_Pnt& at) { return gizmoPixelSize(at); });
}

void ViewerWidget::syncManipulatorPrs() {
  if (!d_->ready()) return;
  const Handle(AIS_InteractiveContext)& ctx = d_->ctx();
  for (auto it = d_->manips.begin(); it != d_->manips.end();) {
    if (manipulators_.has(it->first)) {
      ++it;
      continue;
    }
    if (!it->second.prs.IsNull()) ctx->Remove(it->second.prs, false);
    if (!it->second.label.IsNull()) ctx->Remove(it->second.label, false);
    it = d_->manips.erase(it);
  }
  for (core::ManipulatorId id : manipulators_.ids()) {
    const core::ManipulatorModel* m = manipulators_.model(id);
    if (!m) continue;
    Impl::ManipObj& obj = d_->manips[id];
    const double ps = gizmoPixelSize(m->spec().frame.Location());
    obj.pixelSize = ps;
    const core::ManipulatorHandle hl = manipulators_.highlightFor(id);
    std::vector<core::ManipulatorGlyph> glyphs = m->glyphs(ps, hl);
    if (obj.prs.IsNull()) {
      obj.prs = new ManipulatorPrs();
      obj.prs->setGlyphs(std::move(glyphs));
      ctx->Display(obj.prs, 0, -1, false);
      ctx->SetZLayer(obj.prs, Graphic3d_ZLayerId_Topmost);
    } else {
      obj.prs->setGlyphs(std::move(glyphs));
      ctx->Redisplay(obj.prs, false);
    }
    const std::string text = manipulatorValueText(*m, hl);
    if (text.empty()) {
      if (!obj.label.IsNull()) {
        ctx->Remove(obj.label, false);
        obj.label.Nullify();
      }
      continue;
    }
    const gp_Pnt at = m->handlePoint(hl, ps).Translated(gp_Vec(upDirection()) * (14.0 * ps));
    if (obj.label.IsNull()) {
      obj.label = new AIS_TextLabel();
      obj.label->SetHeight(13.0);
      obj.label->SetColor(hexq("#1A1A1A"));
      obj.label->SetZoomable(false);
      obj.label->SetFont(viewerFont());
      obj.label->SetDisplayType(Aspect_TODT_SUBTITLE);
      obj.label->SetColorSubTitle(hexq("#FFF4C2"));
      obj.label->SetText(TCollection_ExtendedString(text.c_str(), true));
      obj.label->SetPosition(at);
      ctx->Display(obj.label, 0, -1, false);
      ctx->SetZLayer(obj.label, Graphic3d_ZLayerId_Topmost);
    } else {
      obj.label->SetText(TCollection_ExtendedString(text.c_str(), true));
      obj.label->SetPosition(at);
      ctx->Redisplay(obj.label, false);
    }
  }
  requestRedraw();
}

void ViewerWidget::refreshManipulatorSizes() {
  if (!d_->ready() || d_->manips.empty()) return;
  for (const auto& [id, obj] : d_->manips) {
    const core::ManipulatorModel* m = manipulators_.model(id);
    if (!m || m->spec().size > 0) continue;
    const double ps = gizmoPixelSize(m->spec().frame.Location());
    if (obj.pixelSize <= 0 || std::abs(ps - obj.pixelSize) > 0.02 * obj.pixelSize) {
      syncManipulatorPrs();
      return;
    }
  }
}

// ---------------------------------------------------------------------------------------
// sketch mode and grid
// ---------------------------------------------------------------------------------------

void ViewerWidget::enterSketchMode(const gp_Ax3& plane, bool look) {
  null_.enterSketchMode(plane, look);
  sketchMode_ = true;
  sketchPlane_ = plane;
  if (look) lookAt(plane, true);
  updateGrid(true);
  requestRedraw();
  emit displaySettingsChanged();
}

void ViewerWidget::exitSketchMode() {
  null_.exitSketchMode();
  if (!sketchMode_) return;
  sketchMode_ = false;
  updateGrid(true);
  requestRedraw();
  emit displaySettingsChanged();
}

void ViewerWidget::setGridVisible(bool visible) {
  null_.setGridVisible(visible);
  gridVisible_ = visible;
  updateGrid(true);
  requestRedraw();
  emit displaySettingsChanged();
}

void ViewerWidget::setGridSnap(bool enabled, double spacingMm) {
  null_.setGridSnap(enabled, spacingMm);
  gridSnap_ = enabled;
  gridSnapSpacing_ = spacingMm > 0 ? spacingMm : 0.0;
  updateGrid(true);
  emit displaySettingsChanged();
}

double ViewerWidget::gridSpacing() const {
  if (gridSnapSpacing_ > 0) return gridSnapSpacing_;
  return gridStep_;
}

void ViewerWidget::updateGrid(bool force) {
  if (!d_->ready()) return;
  const Handle(V3d_Viewer)& viewer = d_->view->viewer();
  if (!gridVisible_) {
    if (viewer->IsGridActive()) viewer->DeactivateGrid();
    return;
  }
  const gp_Ax3 plane = sketchMode_ ? sketchPlane_ : gp_Ax3();
  const double pix = pixelSize(plane.Location());
  const double step = gridSnapSpacing_ > 0 ? gridSnapSpacing_ : adaptiveGridStep(pix);
  if (!force && viewer->IsGridActive() && std::abs(step - gridStep_) < 1e-12) return;
  gridStep_ = step;
  viewer->SetPrivilegedPlane(plane);
  viewer->SetRectangularGridValues(0.0, 0.0, step, step, 0.0);
  viewer->SetRectangularGridGraphicValues(step * 60.0, step * 60.0, 0.0);
  viewer->ActivateGrid(Aspect_GT_Rectangular, Aspect_GDM_Lines);
  if (!viewer->Grid().IsNull()) viewer->Grid()->SetColors(hexq("#D8DDE3"), hexq("#B0B8C2"));
}

// ---------------------------------------------------------------------------------------
// camera
// ---------------------------------------------------------------------------------------

void ViewerWidget::lookAt(const gp_Ax3& plane, bool animate) {
  null_.lookAt(plane, animate);
  if (!d_->ready()) return;
  Handle(Graphic3d_Camera) cam = d_->cameraCopy();
  const ViewOrientation o = planeViewOrientation(plane);
  // centre: current centre projected onto the plane
  const gp_Pln pln(plane);
  gp_Pnt c = cam->Center();
  const gp_Vec n(plane.Direction());
  c.Translate(-n * gp_Vec(plane.Location(), c).Dot(n));
  const double dist = std::max(cam->Distance(), 1.0);
  cam->SetEyeAndCenter(c.Translated(gp_Vec(o.toEye) * dist), c);
  cam->SetUp(o.up);
  animateCamera(cam, animate ? 0.3 : 0.0);
}

void ViewerWidget::setStandardView(core::StandardView v, bool animate) {
  null_.setStandardView(v, animate);
  if (!d_->ready()) return;
  if (v == core::StandardView::Home && !d_->home.IsNull()) {
    animateCamera(d_->home, animate ? 0.3 : 0.0);
    return;
  }
  Handle(Graphic3d_Camera) cam = d_->cameraCopy();
  const ViewOrientation o = standardViewOrientation(v);
  const gp_Pnt c = cam->Center();
  const double dist = std::max(cam->Distance(), 1.0);
  cam->SetEyeAndCenter(c.Translated(gp_Vec(o.toEye) * dist), c);
  cam->SetUp(o.up);
  if ((v == core::StandardView::Home || v == core::StandardView::Iso) && d_->hasGeometry()) cam = d_->fitted(cam);
  animateCamera(cam, animate ? 0.3 : 0.0);
}

void ViewerWidget::fitAll(bool animate) {
  null_.fitAll(animate);
  if (!d_->ready()) return;
  // the bodies plus, in sketch mode, the sketch: it is hidden from the scene and drawn as
  // previews (SK-11). Text labels are sized in pixels and do not count.
  Bnd_Box box = d_->sceneBox();
  if (inSketchMode()) {
    for (const auto& [h, p] : d_->previews) {
      if (p.obj.IsNull()) continue;
      if (Handle(AIS_TextLabel)::DownCast(p.obj)) continue;
      if (Handle(AIS_Shape) s = Handle(AIS_Shape)::DownCast(p.obj)) {
        if (!s->Shape().IsNull()) BRepBndLib::Add(s->Shape(), box);
        continue;
      }
      Bnd_Box b;
      p.obj->BoundingBox(b);
      if (!b.IsVoid()) box.Add(b);
    }
  }
  if (box.IsVoid()) {
    Handle(Graphic3d_Camera) cam = d_->cameraCopy();
    cam->SetScale(250.0);
    cam->SetCenter(gp_Pnt(0, 0, 0));
    animateCamera(cam, animate ? 0.25 : 0.0);
    return;
  }
  animateCamera(d_->fitted(d_->cameraCopy(), &box), animate ? 0.25 : 0.0);
}

void ViewerWidget::fitShapes(const std::vector<ShapeRef>& refs, bool animate) {
  null_.fitShapes(refs, animate);
  if (!d_->ready() || !document_) return;
  Bnd_Box box;
  for (const ShapeRef& r : refs) {
    TopoDS_Shape s = core::resolveShape(*document_, r);
    if (s.IsNull()) continue;
    if (r.isBrep())
      if (const core::Body* b = document_->body(r.owner)) {
        gp_Trsf t = document_->worldPlacement(b->component);
        if (!isIdentity(t)) s = s.Moved(TopLoc_Location(t));
      }
    BRepBndLib::Add(s, box);
  }
  if (box.IsVoid()) {
    fitAll(animate);
    return;
  }
  animateCamera(d_->fitted(d_->cameraCopy(), &box), animate ? 0.25 : 0.0);
}

void ViewerWidget::setProjection(core::ProjectionMode mode) {
  null_.setProjection(mode);
  projection_ = mode;
  if (d_->ready()) {
    const Handle(Graphic3d_Camera)& cam = d_->v3d()->Camera();
    cam->SetProjectionType(mode == core::ProjectionMode::Orthographic ? Graphic3d_Camera::Projection_Orthographic
                                                                      : Graphic3d_Camera::Projection_Perspective);
    if (mode == core::ProjectionMode::Perspective) cam->SetFOVy(45.0);
    d_->v3d()->Invalidate();
  }
  requestRedraw();
  emit displaySettingsChanged();
}

void ViewerWidget::setDisplayMode(core::DisplayMode mode) {
  null_.setDisplayMode(mode);
  if (displayMode_ == mode) return;
  displayMode_ = mode;
  if (d_->ready()) {
    // restyle bodies: drop their presentations and let sync rebuild them
    for (auto it = d_->objects.begin(); it != d_->objects.end();) {
      if (it->second.item.style != SceneStyle::Body) {
        ++it;
        continue;
      }
      d_->objectKeys.erase(it->second.obj.get());
      d_->ctx()->Remove(it->second.obj, false);
      it = d_->objects.erase(it);
    }
    doSync();
  }
  emit displaySettingsChanged();
}

void ViewerWidget::setCurrentViewAsHome() {
  if (d_->ready()) d_->home = d_->cameraCopy();
}

void ViewerWidget::resetHome() { d_->home.Nullify(); }

gp_Dir ViewerWidget::viewDirection() const {
  if (!d_->ready()) return null_.viewDirection();
  return d_->v3d()->Camera()->Direction().Reversed();
}

gp_Dir ViewerWidget::upDirection() const {
  if (!d_->ready()) return null_.upDirection();
  return d_->v3d()->Camera()->Up();
}

bool ViewerWidget::screenRay(int x, int y, gp_Pnt& origin, gp_Dir& dir) const {
  if (!d_->ready()) return null_.screenRay(x, y, origin, dir);
  return cameraScreenRay(*d_->v3d()->Camera(), x, y, d_->view->width(), d_->view->height(), origin, dir);
}

bool ViewerWidget::screenToPlane(int x, int y, const gp_Pln& plane, gp_Pnt& out) const {
  if (!d_->ready()) return null_.screenToPlane(x, y, plane, out);
  gp_Pnt o;
  gp_Dir d;
  if (!screenRay(x, y, o, d)) return false;
  return intersectLinePlane(o, d, plane, out);
}

bool ViewerWidget::worldToScreen(const gp_Pnt& p, double& x, double& y) const {
  if (!d_->ready()) return null_.worldToScreen(p, x, y);
  return cameraWorldToScreen(*d_->v3d()->Camera(), p, d_->view->width(), d_->view->height(), x, y);
}

double ViewerWidget::pixelSize(const gp_Pnt& at) const {
  if (!d_->ready()) return null_.pixelSize(at);
  return cameraPixelSize(*d_->v3d()->Camera(), at, d_->view->width(), d_->view->height());
}

}  // namespace fabricad::viewer
