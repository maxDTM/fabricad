// Copyright (c) 2025 Kirill Gavrilov
//
// Origin: https://github.com/gkv311/occt-samples-qopenglwidget (occt-qt-tools/OcctQtTools.cpp),
// revision 72e32d3a8398, MIT license (full notice in OcctQtTools.h). Trimmed for FabriCAD.
// SPDX-License-Identifier: MIT

#include "viewer/occt/OcctQtTools.h"

#include <Aspect_ScrollDelta.hxx>
#include <Message.hxx>
#include <OpenGl_Caps.hxx>
#include <Standard_Version.hxx>
#include <V3d_View.hxx>

#include <Standard_WarningsDisable.hxx>
#include <QCoreApplication>
#include <QGuiApplication>
#include <Standard_WarningsRestore.hxx>

#include <cstdlib>

Quantity_Color OcctQtTools::qtColorToOcct(const QColor& theColor)
{
  return Quantity_Color(theColor.red() / 255.0, theColor.green() / 255.0, theColor.blue() / 255.0, Quantity_TOC_sRGB);
}

Quantity_ColorRGBA OcctQtTools::qtColorToOcctRgba(const QColor& theColor)
{
  return Quantity_ColorRGBA(OcctQtTools::qtColorToOcct(theColor), theColor.alpha() / 255.0f);
}

void OcctQtTools::qtGlPlatformSetup()
{
#if !defined(_WIN32) && !defined(__APPLE__) && !defined(HAVE_WAYLAND)
  // Qt6 tries to use Wayland platform by default, which is incompatible with OCCT depending on Xlib;
  // Force 'xcb' platform plugin (alternatively, could be passed QApplication as '-platform xcb' argument).
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    qputenv("QT_QPA_PLATFORM", "xcb");
#endif

  // request Qt to share resources between OpenGL contexts like QOpenGLWidget and QQuickWidget;
  // allows detaching OCCT 3D Viewer widget inside QDockWidget into new top-level window
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

  // global OpenGL setup managed by Qt
  QSurfaceFormat::setDefaultFormat(OcctQtTools::qtGlSurfaceFormat());
}

QSurfaceFormat OcctQtTools::qtGlSurfaceFormat(QSurfaceFormat::OpenGLContextProfile theProfile, bool theToDebug)
{
  QSurfaceFormat::OpenGLContextProfile aProfile = theProfile;
  if (theProfile == QSurfaceFormat::NoProfile)
  {
    aProfile = QSurfaceFormat::CompatibilityProfile;
#ifdef __APPLE__
    aProfile = QSurfaceFormat::CoreProfile;
#endif
  }

  QSurfaceFormat aGlFormat;
  aGlFormat.setDepthBufferSize(24);
  aGlFormat.setStencilBufferSize(8);
  aGlFormat.setProfile(aProfile);
  if (aProfile == QSurfaceFormat::CoreProfile)
    aGlFormat.setVersion(4, 5);
  if (theToDebug)
    aGlFormat.setOption(QSurfaceFormat::DebugContext, true);
  return aGlFormat;
}

void OcctQtTools::qtGlCapsFromSurfaceFormat(OpenGl_Caps& theCaps, const QSurfaceFormat& theFormat)
{
  theCaps.contextDebug = theFormat.testOption(QSurfaceFormat::DebugContext);
  theCaps.contextSyncDebug = theCaps.contextDebug;
  theCaps.contextCompatible = theFormat.profile() != QSurfaceFormat::CoreProfile;
#if (OCC_VERSION_HEX >= 0x070700)
  theCaps.buffersDeepColor = theFormat.redBufferSize() == 10
                          && theFormat.greenBufferSize() == 10
                          && theFormat.blueBufferSize() == 10;
#endif
}

bool OcctQtTools::qtHandleMouseEvent(Aspect_WindowInputListener& theListener,
                                     const Handle(V3d_View)& theView,
                                     const QMouseEvent* theEvent,
                                     Qt::MouseButtons theButtonMask)
{
  if (theView->Window().IsNull())
    return false;

  const Graphic3d_Vec2d  aPnt2d(theEvent->position().x(), theEvent->position().y());
  const Graphic3d_Vec2i  aPnt2i(theView->Window()->ConvertPointToBacking(aPnt2d) + Graphic3d_Vec2d(0.5));
  const Aspect_VKeyMouse aButtons = OcctQtTools::qtMouseButtons2VKeys(theEvent->buttons() & theButtonMask);
  const Aspect_VKeyFlags aFlags = OcctQtTools::qtMouseModifiers2VKeys(theEvent->modifiers());
  if (theEvent->type() == QEvent::MouseMove)
    return theListener.UpdateMousePosition(aPnt2i, aButtons, aFlags, false);

  return theListener.UpdateMouseButtons(aPnt2i, aButtons, aFlags, false);
}

bool OcctQtTools::qtHandleWheelEvent(Aspect_WindowInputListener& theListener,
                                     const Handle(V3d_View)& theView,
                                     const QWheelEvent* theEvent,
                                     double theScale)
{
  if (theView->Window().IsNull())
    return false;

  const Graphic3d_Vec2d aPnt2d(theEvent->position().x(), theEvent->position().y());
  const Graphic3d_Vec2i aPnt2i(theView->Window()->ConvertPointToBacking(aPnt2d) + Graphic3d_Vec2d(0.5));
  return theListener.UpdateMouseScroll(Aspect_ScrollDelta(aPnt2i, theScale * double(theEvent->angleDelta().y()) / 120.0));
}

Aspect_VKeyMouse OcctQtTools::qtMouseButtons2VKeys(Qt::MouseButtons theButtons)
{
  Aspect_VKeyMouse aButtons = Aspect_VKeyMouse_NONE;
  if ((theButtons & Qt::LeftButton) != 0)
    aButtons |= Aspect_VKeyMouse_LeftButton;
  if ((theButtons & Qt::MiddleButton) != 0)
    aButtons |= Aspect_VKeyMouse_MiddleButton;
  if ((theButtons & Qt::RightButton) != 0)
    aButtons |= Aspect_VKeyMouse_RightButton;
  return aButtons;
}

Aspect_VKeyFlags OcctQtTools::qtMouseModifiers2VKeys(Qt::KeyboardModifiers theModifiers)
{
  Aspect_VKeyFlags aFlags = Aspect_VKeyFlags_NONE;
  if ((theModifiers & Qt::ShiftModifier) != 0)
    aFlags |= Aspect_VKeyFlags_SHIFT;
  if ((theModifiers & Qt::ControlModifier) != 0)
    aFlags |= Aspect_VKeyFlags_CTRL;
  if ((theModifiers & Qt::AltModifier) != 0)
    aFlags |= Aspect_VKeyFlags_ALT;
  return aFlags;
}
