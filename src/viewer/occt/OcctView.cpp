// Copyright (c) 2023 Kirill Gavrilov
// Copyright (c) 2026 FabriCAD contributors
//
// Origin: https://github.com/gkv311/occt-samples-qopenglwidget
// (occt-qopenglwidget/OcctQOpenGLWidgetViewer.cpp), revision 72e32d3a8398, MIT license (full
// notice in OcctQtTools.h). Adapted for FabriCAD (see OcctView.h).
// SPDX-License-Identifier: MIT

#include <OpenGl_Context.hxx>

#include "viewer/occt/OcctView.h"

#include "viewer/occt/OcctGlTools.h"
#include "viewer/occt/OcctQtTools.h"

#include <Standard_WarningsDisable.hxx>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <Standard_WarningsRestore.hxx>

#include <algorithm>

#include <AIS_AnimationCamera.hxx>
#include <AIS_ViewCube.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Message.hxx>
#include <OpenGl_GraphicDriver.hxx>

namespace fabricad::viewer {

namespace {
/// AIS_ViewCube whose post-click fit is delegated to the viewer (margin, nav-bar inset).
class FabViewCube : public AIS_ViewCube
{
public:
  OcctView::CubeFitter fitter;

protected:
  void viewFitAll(const Handle(V3d_View)& theView, const Handle(Graphic3d_Camera)& theCamera) override
  {
    if (fitter)
      fitter(theView, theCamera);
    else
      AIS_ViewCube::viewFitAll(theView, theCamera);
  }
};
} // namespace

void OcctView::setCubeFitter(CubeFitter f)
{
  if (auto* aCube = dynamic_cast<FabViewCube*>(myViewCube.get()))
    aCube->fitter = std::move(f);
}

OcctView::OcctView(OcctViewListener* listener, QWidget* parent) : QOpenGLWidget(parent), myListener(listener)
{
  Handle(Aspect_DisplayConnection) aDisp;
#if !defined(__APPLE__) && !defined(_WIN32) && !defined(HAVE_WAYLAND)
  aDisp = new Xw_DisplayConnection();
#endif
  Handle(OpenGl_GraphicDriver) aDriver = new OpenGl_GraphicDriver(aDisp, false);
  // lets QOpenGLWidget to manage buffer swap
  aDriver->ChangeOptions().buffersNoSwap = true;
  // don't write into alpha channel
  aDriver->ChangeOptions().buffersOpaqueAlpha = true;
  // offscreen FBOs should be always used
  aDriver->ChangeOptions().useSystemBuffer = false;
  // FabriCAD: OCCT would set its own swap interval (1) on the top-level window's drawable, which Qt
  // composes and swaps: keep Qt's (setupGlPlatform: 0, P5-KEYBOARD-1 stale frames under Xwayland)
  aDriver->ChangeOptions().swapInterval = std::max(0, QSurfaceFormat::defaultFormat().swapInterval());

  // create viewer
  myViewer = new V3d_Viewer(aDriver);
  myViewer->SetDefaultLights();
  myViewer->SetLightOn();

  // create AIS context
  myContext = new AIS_InteractiveContext(myViewer);

  myViewCube = new FabViewCube();
  myViewCube->SetViewAnimation(myViewAnimation);
  myViewCube->SetFixedAnimationLoop(false);
  myViewCube->SetAutoStartAnimation(true);

  // note - window will be created later within initializeGL() callback!
  myView = myViewer->CreateView();
  myView->SetImmediateUpdate(false);

  // Qt widget setup
  setMouseTracking(true);
  setBackgroundRole(QPalette::NoRole);
  setFocusPolicy(Qt::StrongFocus);
  setUpdatesEnabled(true);
  setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
}

OcctView::~OcctView()
{
  // hold on X11 display connection till making another connection active by glXMakeCurrent()
  // to workaround sudden crash in QOpenGLWidget destructor
  Handle(Aspect_DisplayConnection) aDisp = myViewer->Driver()->GetDisplayConnection();

  // release OCCT viewer
  myContext->RemoveAll(false);
  myContext.Nullify();
  myView->Remove();
  myView.Nullify();
  myViewer.Nullify();

  // make active OpenGL context created by Qt
  makeCurrent();
  aDisp.Nullify();
}

void OcctView::animateTo(const Handle(Graphic3d_Camera)& target, double seconds)
{
  if (myView.IsNull() || target.IsNull())
    return;
  if (seconds <= 0.0 || !myIsReady)
  {
    myView->Camera()->Copy(target);
    myView->Invalidate();
    update();
    return;
  }
  Handle(Graphic3d_Camera) aStart = new Graphic3d_Camera();
  aStart->Copy(myView->Camera());
  Handle(Graphic3d_Camera) anEnd = new Graphic3d_Camera();
  anEnd->Copy(target);
  myViewAnimation->Stop();
  myViewAnimation->SetView(myView);
  myViewAnimation->SetCameraStart(aStart);
  myViewAnimation->SetCameraEnd(anEnd);
  myViewAnimation->SetOwnDuration(seconds);
  myViewAnimation->StartTimer(0.0, 1.0, true, false);
  update();
}

bool OcctView::isAnimating() const { return !myViewAnimation.IsNull() && !myViewAnimation->IsStopped(); }

bool OcctView::finishAnimation()
{
  if (myViewAnimation.IsNull() || myViewAnimation->IsStopped() || myView.IsNull())
    return false;
  Handle(Graphic3d_Camera) anEnd = myViewAnimation->CameraEnd();
  myViewAnimation->Stop();
  if (!anEnd.IsNull())
    myView->Camera()->Copy(anEnd);
  myView->Invalidate();
  update();
  return true;
}

void OcctView::stopAnimation()
{
  if (!myViewAnimation.IsNull() && !myViewAnimation->IsStopped())
    myViewAnimation->Stop();
}

void OcctView::mousePressEvent(QMouseEvent* theEvent)
{
  QOpenGLWidget::mousePressEvent(theEvent);
  theEvent->accept();
  if (myView.IsNull() || !myIsReady)
    return;
  const Qt::MouseButtons aNav = myListener->occtMousePress(theEvent);
  if (OcctQtTools::qtHandleMouseEvent(*this, myView, theEvent, aNav))
    updateView();
}

void OcctView::mouseReleaseEvent(QMouseEvent* theEvent)
{
  QOpenGLWidget::mouseReleaseEvent(theEvent);
  theEvent->accept();
  if (myView.IsNull() || !myIsReady)
    return;
  const Qt::MouseButtons aNav = myListener->occtMouseRelease(theEvent);
  if (OcctQtTools::qtHandleMouseEvent(*this, myView, theEvent, aNav))
    updateView();
}

void OcctView::mouseMoveEvent(QMouseEvent* theEvent)
{
  QOpenGLWidget::mouseMoveEvent(theEvent);
  theEvent->accept();
  if (myView.IsNull() || !myIsReady)
    return;
  const Qt::MouseButtons aNav = myListener->occtMouseMove(theEvent);
  if (OcctQtTools::qtHandleMouseEvent(*this, myView, theEvent, aNav))
    updateView();
}

void OcctView::mouseDoubleClickEvent(QMouseEvent* theEvent)
{
  theEvent->accept();
  if (myView.IsNull() || !myIsReady)
    return;
  myListener->occtMouseDoubleClick(theEvent);
}

void OcctView::wheelEvent(QWheelEvent* theEvent)
{
  QOpenGLWidget::wheelEvent(theEvent);
  theEvent->accept();
  if (myView.IsNull() || !myIsReady)
    return;
  const double aScale = myListener->occtWheel(theEvent);
  if (aScale != 0.0 && OcctQtTools::qtHandleWheelEvent(*this, myView, theEvent, aScale))
    updateView();
}

void OcctView::leaveEvent(QEvent* theEvent)
{
  QOpenGLWidget::leaveEvent(theEvent);
  if (myIsReady)
    myListener->occtLeave();
}

void OcctView::handleViewRedraw(const Handle(AIS_InteractiveContext)& theCtx, const Handle(V3d_View)& theView)
{
  // a running camera animation must redraw the whole frame each step (else the immediate
  // layer is drawn over the previous frame and the cube/scene tear)
  if (!myViewAnimation.IsNull() && !myViewAnimation->IsStopped())
    theView->Invalidate();
  AIS_ViewController::handleViewRedraw(theCtx, theView);
  if (myToAskNextFrame)
  {
    theView->Invalidate();
    updateView(); // ask more frames for animation
  }
}

void OcctView::dumpGlInfo()
{
  TColStd_IndexedDataMapOfStringString aGlCapsDict;
  myView->DiagnosticInformation(aGlCapsDict, Graphic3d_DiagnosticInfo_Basic);
  TCollection_AsciiString anInfo;
  for (TColStd_IndexedDataMapOfStringString::Iterator aValueIter(aGlCapsDict); aValueIter.More(); aValueIter.Next())
  {
    if (!aValueIter.Value().IsEmpty())
    {
      if (!anInfo.IsEmpty())
        anInfo += "\n";
      anInfo += aValueIter.Key() + ": " + aValueIter.Value();
    }
  }
  myGlInfo = QString::fromUtf8(anInfo.ToCString());
}

void OcctView::initializeGL()
{
  if (myInitFailed)
    return;
  Handle(OpenGl_GraphicDriver) aDriver = Handle(OpenGl_GraphicDriver)::DownCast(myViewer->Driver());
  OcctQtTools::qtGlCapsFromSurfaceFormat(aDriver->ChangeOptions(), format());

  const Aspect_Drawable aNativeWin = (Aspect_Drawable)effectiveWinId();
  const Graphic3d_Vec2i aViewSize(rect().right() - rect().left(), rect().bottom() - rect().top());

  const bool isFirstInit = myView->Window().IsNull();
  if (!OcctGlTools::InitializeGlWindow(myView, aNativeWin, aViewSize, devicePixelRatioF()))
  {
    myInitFailed = true;
    myListener->occtInitFailed("OpenGl_Context is unable to wrap the OpenGL context created by Qt");
    return;
  }

  makeCurrent(); // restore Qt framebuffer
  dumpGlInfo();
  if (isFirstInit)
  {
    myIsReady = true;
    myListener->occtInitialized();
  }
}

void OcctView::paintGL()
{
  if (myView.IsNull() || myView->Window().IsNull() || myInitFailed)
    return;
  // every repaint requested so far is in this frame (a request made while painting stays pending)
  myPaintedSeq = myRequestSeq;
  ++myFrameCount;

  const double aDevPixelRatioOld = myView->Window()->DevicePixelRatio();
  if (myView->Window()->NativeHandle() != OcctGlTools::GetGlNativeWindow((Aspect_Drawable)effectiveWinId()))
  {
    // workaround window recreation done by Qt on monitor (QScreen) disconnection
    Message::SendWarning() << "Native window handle has changed by QOpenGLWidget!";
    initializeGL();
  }
  else if (devicePixelRatioF() != aDevPixelRatioOld)
  {
    initializeGL();
  }

  // wrap FBO created by QOpenGLFramebufferObject
  if (!OcctGlTools::InitializeGlFbo(myView))
  {
    myInitFailed = true;
    myListener->occtInitFailed("Default FBO wrapper creation failed");
    return;
  }

  myListener->occtBeforeRedraw();

  // reset global GL state from Qt before redrawing OCCT
  OcctGlTools::ResetGlStateBeforeOcct(myView);

  // flush pending input events and redraw the viewer
  myView->InvalidateImmediate();
  AIS_ViewController::FlushViewEvents(myContext, myView, true);

  // reset global GL state after OCCT before redrawing Qt
  OcctGlTools::ResetGlStateAfterOcct(myView);

  QPainter aPainter(this);
  myListener->occtPaintOverlay(aPainter);
}

}  // namespace fabricad::viewer
