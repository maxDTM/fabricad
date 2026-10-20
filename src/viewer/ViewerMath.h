// FabriCAD viewer: GL-free math and interaction tables used by ViewerWidget.
// Everything here works on plain OCCT value types (Graphic3d_Camera is pure math), so it is
// unit-tested headless (src/viewer/tests/test_viewer_math.cpp).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Tool.h"
#include "core/Types.h"
#include "core/ViewerContext.h"

#include <Graphic3d_Camera.hxx>
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>

#include <string>
#include <vector>

namespace fabricad::viewer {

// --- camera mapping ---------------------------------------------------------------
// x, y are logical widget pixels (origin top-left); width/height the logical widget size.

/// World ray through a pixel. `dir` points from the eye into the scene.
bool cameraScreenRay(const Graphic3d_Camera& cam, double x, double y, int width, int height, gp_Pnt& origin,
                     gp_Dir& dir);
/// Project a world point to pixels. False when the point is behind the camera (perspective).
bool cameraWorldToScreen(const Graphic3d_Camera& cam, const gp_Pnt& p, int width, int height, double& x, double& y);
/// World length of one pixel at depth of `at`.
double cameraPixelSize(const Graphic3d_Camera& cam, const gp_Pnt& at, int width, int height);
/// Intersect a line (origin, dir) with a plane. False when parallel.
bool intersectLinePlane(const gp_Pnt& origin, const gp_Dir& dir, const gp_Pln& plane, gp_Pnt& out);

// --- grid ------------------------------------------------------------------------------
/// Round `p` (assumed on/near the plane) to the nearest grid node of `plane` with `step`.
gp_Pnt snapToGrid(const gp_Ax3& plane, const gp_Pnt& p, double step);
/// Smallest value of the 1-2-5 series (x 10^n) that is >= `value` (value > 0).
double niceStep(double value);
/// Adaptive grid spacing: minor lines at least `minPixels` apart.
double adaptiveGridStep(double pixelSizeMm, double minPixels = 12.0);

// --- standard views (Z up, FabriCAD default) --------------------------------------------
struct ViewOrientation {
  gp_Dir toEye;  ///< direction from the scene centre towards the eye
  gp_Dir up;
};
ViewOrientation standardViewOrientation(core::StandardView view);
/// "Look at" a plane: eye on the +normal side, up = plane Y direction.
ViewOrientation planeViewOrientation(const gp_Ax3& plane);

// --- selection -------------------------------------------------------------------------
/// User "selection priority" from the SELECT menu (combined with the tool filter).
enum class SelectionPriority { Default, Body, Face, Edge, Vertex, Component };
/// Kinds allowed by a priority (Default = all).
core::ShapeKindMask priorityMask(SelectionPriority p);
/// B-rep selection modes to activate on body presentations for an effective filter.
struct BrepModes {
  bool body = false;    ///< AIS mode 0 (whole shape)
  bool face = false;    ///< TopAbs_FACE
  bool edge = false;
  bool vertex = false;
  bool operator==(const BrepModes&) const = default;
};
/// Default rule: faces/edges/vertices when allowed; whole body only when sub-shapes are not
/// allowed (Fusion picks faces first; bodies come from the browser or Body priority).
BrepModes brepModesFor(core::ShapeKindMask filter);

/// Window (left-to-right, fully inside) vs crossing (right-to-left, touching) selection.
inline bool isCrossingSelection(int startX, int endX) { return endX < startX; }
inline bool exceedsDragThreshold(int dx, int dy, int threshold = 4) { return dx * dx + dy * dy > threshold * threshold; }

/// How a click combines with the existing selection.
enum class SelectOp { Replace, Add, Toggle };
SelectOp selectOpForModifiers(int modifiers);  ///< Ctrl toggles, Shift adds, none replaces

// --- navigation presets ----------------------------------------------------------------
enum class NavAction { None, Orbit, Pan, Zoom };
enum class NavPreset { Fusion, SolidWorks, AutoCAD, Inventor };
struct NavBinding {
  int button = 0;     ///< 1 L, 2 M, 4 R (ViewerMouseEvent::buttons bits)
  int modifiers = 0;  ///< core::KeyModifier bits
  NavAction action = NavAction::None;
};
std::vector<NavBinding> navigationBindings(NavPreset preset);
NavAction navActionFor(NavPreset preset, int button, int modifiers);
const char* navPresetName(NavPreset p);
NavPreset navPresetFromName(const std::string& name);

// --- misc --------------------------------------------------------------------------------
/// Colour helpers: "#RRGGBB" -> RGB 0..1 (sRGB).
struct Rgb {
  double r = 0, g = 0, b = 0;
};
Rgb hexColor(const char* hex);

}  // namespace fabricad::viewer
