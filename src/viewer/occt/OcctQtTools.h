// Copyright (c) 2025 Kirill Gavrilov
//
// Origin: https://github.com/gkv311/occt-samples-qopenglwidget (occt-qt-tools/OcctQtTools.h),
// revision 72e32d3a8398, MIT license (see the notice below). Trimmed for FabriCAD: kept the
// platform setup, surface format and input translation helpers; dropped touch/3D-mouse/
// native event handling and message redirection.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
// SPDX-License-Identifier: MIT

#ifndef _OcctQtTools_HeaderFile
#define _OcctQtTools_HeaderFile

#include <Aspect_VKey.hxx>
#include <Aspect_VKeyFlags.hxx>
#include <Aspect_WindowInputListener.hxx>
#include <Quantity_ColorRGBA.hxx>

#include <Standard_WarningsDisable.hxx>
#include <QColor>
#include <QMouseEvent>
#include <QSurfaceFormat>
#include <QWheelEvent>
#include <Standard_WarningsRestore.hxx>

class OpenGl_Caps;
class V3d_View;

//! Auxiliary tools between Qt and OCCT definitions.
class OcctQtTools
{
public: //! @name Qt application pre-setup for OCCT 3D Viewer integration

  //! Perform global Qt platform setup - to be called before QApplication creation.
  //! Defines platform plugin to load (xcb on Linux, OCCT is a GLX build here)
  //! and the default surface format.
  static void qtGlPlatformSetup();

  //! Define default Qt surface format for GL context.
  static QSurfaceFormat qtGlSurfaceFormat(QSurfaceFormat::OpenGLContextProfile theProfile = QSurfaceFormat::NoProfile,
                                          bool theToDebug = false);

  //! Fill in OCCT GL caps from Qt surface format.
  static void qtGlCapsFromSurfaceFormat(OpenGl_Caps& theCaps, const QSurfaceFormat& theFormat);

public: //! @name common conversion tools

  //! Map QColor into Quantity_Color.
  static Quantity_Color qtColorToOcct(const QColor& theColor);

  //! Map QColor into Quantity_ColorRGBA.
  static Quantity_ColorRGBA qtColorToOcctRgba(const QColor& theColor);

public: //! @name methods for wrapping Qt input events into Aspect_WindowInputListener events

  //! Queue Qt mouse event to OCCT listener; `theButtonMask` filters the buttons passed on
  //! (FabriCAD keeps LMB/RMB for its own selection and tools).
  static bool qtHandleMouseEvent(Aspect_WindowInputListener& theListener,
                                 const Handle(V3d_View)& theView,
                                 const QMouseEvent* theEvent,
                                 Qt::MouseButtons theButtonMask = Qt::AllButtons);

  //! Queue Qt mouse wheel event to OCCT listener (`theScale` -1 reverses the direction).
  static bool qtHandleWheelEvent(Aspect_WindowInputListener& theListener,
                                 const Handle(V3d_View)& theView,
                                 const QWheelEvent* theEvent,
                                 double theScale = 1.0);

  //! Map Qt buttons bitmask to virtual keys.
  static Aspect_VKeyMouse qtMouseButtons2VKeys(Qt::MouseButtons theButtons);

  //! Map Qt mouse modifiers bitmask to virtual keys.
  static Aspect_VKeyFlags qtMouseModifiers2VKeys(Qt::KeyboardModifiers theModifiers);
};

#endif // _OcctQtTools_HeaderFile
