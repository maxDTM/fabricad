// Copyright (c) 2023 Kirill Gavrilov
// Copyright (c) 2026 FabriCAD contributors
//
// Origin: https://github.com/gkv311/occt-samples-qopenglwidget
// (occt-qopenglwidget/OcctQOpenGLWidgetViewer.h), revision 72e32d3a8398, MIT license (full
// notice in OcctQtTools.h). Adapted for FabriCAD: the widget no longer owns application
// logic; it creates the OCCT viewer, wraps Qt's GL context/FBO, drives AIS_ViewController
// for navigation only (MMB/wheel) and forwards all other input to its OcctViewListener
// (viewer::ViewerWidget).
// SPDX-License-Identifier: MIT

#pragma once

#include <Standard_WarningsDisable.hxx>
#include <QOpenGLWidget>
#include <Standard_WarningsRestore.hxx>

#include <AIS_InteractiveContext.hxx>
#include <AIS_ViewController.hxx>
#include <Graphic3d_Camera.hxx>
#include <V3d_View.hxx>

#include <functional>

class AIS_ViewCube;
class QPainter;

namespace fabricad::viewer {

/// Receives the input/paint callbacks of an OcctView.
class OcctViewListener {
 public:
  virtual ~OcctViewListener() = default;
  virtual void occtInitialized() = 0;
  virtual void occtInitFailed(const QString& message) = 0;
  /// Return the mouse buttons that should drive navigation (passed to AIS_ViewController).
  virtual Qt::MouseButtons occtMousePress(QMouseEvent* e) = 0;
  virtual Qt::MouseButtons occtMouseMove(QMouseEvent* e) = 0;
  virtual Qt::MouseButtons occtMouseRelease(QMouseEvent* e) = 0;
  virtual void occtMouseDoubleClick(QMouseEvent* e) = 0;
  /// Return the wheel scale (0 = ignore, 1 normal, -1 reversed).
  virtual double occtWheel(QWheelEvent* e) = 0;
  /// Called inside paintGL before OCCT redraws (update grid spacing etc.).
  virtual void occtBeforeRedraw() = 0;
  /// QPainter overlay drawn after OCCT (rubber band, hints).
  virtual void occtPaintOverlay(QPainter& p) = 0;
  virtual void occtLeave() = 0;
};

class OcctView : public QOpenGLWidget, public AIS_ViewController {
  Q_OBJECT
 public:
  OcctView(OcctViewListener* listener, QWidget* parent = nullptr);
  ~OcctView() override;

  const Handle(V3d_Viewer)& viewer() const { return myViewer; }
  const Handle(V3d_View)& view() const { return myView; }
  const Handle(AIS_InteractiveContext)& context() const { return myContext; }
  const Handle(AIS_ViewCube)& viewCube() const { return myViewCube; }
  bool isReady() const { return myIsReady; }
  const QString& glInfo() const { return myGlInfo; }

  /// Animate the camera from its current state to `target` (copy) over `seconds`.
  void animateTo(const Handle(Graphic3d_Camera)& target, double seconds = 0.25);
  /// Stop a running camera animation.
  void stopAnimation();
  /// Request a repaint of the immediate layer and Qt overlay only (the last full frame is
  /// reused): rubber band, overlay controls.
  void updateView() { update(); }
  /// Request a full repaint: the scene changed (Display/Remove/SetLocation/Redisplay with
  /// update=false never invalidate the view, so updateView() alone would show a stale frame).
  void redrawAll()
  {
    if (!myView.IsNull())
      myView->Invalidate();
    update();
  }
  /// Fit hook used by the ViewCube animation (null = OCCT default fit).
  using CubeFitter = std::function<void(const Handle(V3d_View)&, const Handle(Graphic3d_Camera)&)>;
  void setCubeFitter(CubeFitter f);

  QSize minimumSizeHint() const override { return QSize(200, 200); }
  QSize sizeHint() const override { return QSize(720, 480); }

 protected:
  void initializeGL() override;
  void paintGL() override;
  void mousePressEvent(QMouseEvent* e) override;
  void mouseReleaseEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void mouseDoubleClickEvent(QMouseEvent* e) override;
  void wheelEvent(QWheelEvent* e) override;
  void leaveEvent(QEvent* e) override;

 private:
  void handleViewRedraw(const Handle(AIS_InteractiveContext)& theCtx, const Handle(V3d_View)& theView) override;
  void dumpGlInfo();

  OcctViewListener* myListener = nullptr;
  Handle(V3d_Viewer) myViewer;
  Handle(V3d_View) myView;
  Handle(AIS_InteractiveContext) myContext;
  Handle(AIS_ViewCube) myViewCube;
  QString myGlInfo;
  bool myIsReady = false;
  bool myInitFailed = false;
};

}  // namespace fabricad::viewer
