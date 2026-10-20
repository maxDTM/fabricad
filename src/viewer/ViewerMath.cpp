// FabriCAD viewer: GL-free math and interaction tables.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "viewer/ViewerMath.h"

#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace fabricad::viewer {

using core::ShapeKind;
using core::ShapeKindMask;

bool cameraScreenRay(const Graphic3d_Camera& cam, double x, double y, int width, int height, gp_Pnt& origin,
                     gp_Dir& dir) {
  if (width <= 0 || height <= 0) return false;
  const double nx = 2.0 * x / width - 1.0;
  const double ny = 1.0 - 2.0 * y / height;
  const gp_Pnt pNear = cam.UnProject(gp_Pnt(nx, ny, -1.0));
  const gp_Pnt pFar = cam.UnProject(gp_Pnt(nx, ny, 1.0));
  gp_Vec v(pNear, pFar);
  if (v.Magnitude() < 1e-12) {
    // degenerate Z range: fall back to the camera direction
    v = gp_Vec(cam.Direction());
  }
  origin = pNear;
  dir = gp_Dir(v);
  return true;
}

bool cameraWorldToScreen(const Graphic3d_Camera& cam, const gp_Pnt& p, int width, int height, double& x, double& y) {
  if (width <= 0 || height <= 0) return false;
  if (!cam.IsOrthographic()) {
    gp_Vec toP(cam.Eye(), p);
    if (toP.Dot(gp_Vec(cam.Direction())) <= 0) return false;
  }
  const gp_Pnt ndc = cam.Project(p);
  x = (ndc.X() + 1.0) * 0.5 * width;
  y = (1.0 - ndc.Y()) * 0.5 * height;
  return std::isfinite(x) && std::isfinite(y);
}

double cameraPixelSize(const Graphic3d_Camera& cam, const gp_Pnt& at, int width, int height) {
  if (width <= 0 || height <= 0) return 1.0;
  const gp_Pnt ndc = cam.Project(at);
  const gp_Pnt a = cam.UnProject(ndc);
  const gp_Pnt b = cam.UnProject(gp_Pnt(ndc.X() + 2.0 / width, ndc.Y(), ndc.Z()));
  const double d = a.Distance(b);
  return (std::isfinite(d) && d > 0) ? d : 1.0;
}

bool intersectLinePlane(const gp_Pnt& origin, const gp_Dir& dir, const gp_Pln& plane, gp_Pnt& out) {
  const gp_Dir n = plane.Axis().Direction();
  const double denom = gp_Vec(dir).Dot(gp_Vec(n));
  if (std::abs(denom) < 1e-12) return false;
  const double t = gp_Vec(origin, plane.Location()).Dot(gp_Vec(n)) / denom;
  out = origin.Translated(gp_Vec(dir) * t);
  return true;
}

gp_Pnt snapToGrid(const gp_Ax3& plane, const gp_Pnt& p, double step) {
  if (!(step > 0)) return p;
  const gp_Vec d(plane.Location(), p);
  const double u = d.Dot(gp_Vec(plane.XDirection()));
  const double v = d.Dot(gp_Vec(plane.YDirection()));
  const double w = d.Dot(gp_Vec(plane.Direction()));
  const double su = std::round(u / step) * step;
  const double sv = std::round(v / step) * step;
  (void)w;  // snapped point lies on the plane
  return plane.Location().Translated(gp_Vec(plane.XDirection()) * su + gp_Vec(plane.YDirection()) * sv);
}

double niceStep(double value) {
  if (!(value > 0) || !std::isfinite(value)) return 1.0;
  const double e = std::floor(std::log10(value));
  const double base = std::pow(10.0, e);
  for (double m : {1.0, 2.0, 5.0, 10.0}) {
    if (m * base >= value * (1.0 - 1e-9)) return m * base;
  }
  return 10.0 * base;
}

double adaptiveGridStep(double pixelSizeMm, double minPixels) { return niceStep(pixelSizeMm * minPixels); }

ViewOrientation standardViewOrientation(core::StandardView view) {
  using V = core::StandardView;
  switch (view) {
    case V::Front: return {gp_Dir(0, -1, 0), gp_Dir(0, 0, 1)};
    case V::Back: return {gp_Dir(0, 1, 0), gp_Dir(0, 0, 1)};
    case V::Left: return {gp_Dir(-1, 0, 0), gp_Dir(0, 0, 1)};
    case V::Right: return {gp_Dir(1, 0, 0), gp_Dir(0, 0, 1)};
    case V::Top: return {gp_Dir(0, 0, 1), gp_Dir(0, 1, 0)};
    case V::Bottom: return {gp_Dir(0, 0, -1), gp_Dir(0, -1, 0)};
    case V::Iso:
    case V::Home: {
      // front-right-top; up is Z orthogonalised against the view direction
      gp_Dir toEye(1, -1, 1);
      gp_Vec up(0, 0, 1);
      up -= gp_Vec(toEye) * up.Dot(gp_Vec(toEye));
      return {toEye, gp_Dir(up)};
    }
  }
  return {gp_Dir(0, 0, 1), gp_Dir(0, 1, 0)};
}

ViewOrientation planeViewOrientation(const gp_Ax3& plane) { return {plane.Direction(), plane.YDirection()}; }

ShapeKindMask priorityMask(SelectionPriority p) {
  const ShapeKindMask nonBrep = core::kAllKinds & ~core::kBrepKinds;
  switch (p) {
    case SelectionPriority::Default: return core::kAllKinds;
    case SelectionPriority::Body: return nonBrep | ShapeKind::Body;
    case SelectionPriority::Face: return nonBrep | ShapeKind::Face;
    case SelectionPriority::Edge: return nonBrep | ShapeKind::Edge;
    case SelectionPriority::Vertex: return nonBrep | ShapeKind::Vertex;
    case SelectionPriority::Component: return nonBrep | ShapeKind::Body;
  }
  return core::kAllKinds;
}

BrepModes brepModesFor(ShapeKindMask filter) {
  BrepModes m;
  m.face = core::hasKind(filter, ShapeKind::Face);
  m.edge = core::hasKind(filter, ShapeKind::Edge);
  m.vertex = core::hasKind(filter, ShapeKind::Vertex);
  // bodies are picked through their faces when sub-shapes are pickable; whole-body mode
  // only when the filter asks for bodies but no sub-shapes (Body priority, Combine tool...).
  m.body = (core::hasKind(filter, ShapeKind::Body) || core::hasKind(filter, ShapeKind::Component)) && !m.face &&
           !m.edge && !m.vertex;
  return m;
}

SelectOp selectOpForModifiers(int modifiers) {
  if (modifiers & core::ModCtrl) return SelectOp::Toggle;
  if (modifiers & core::ModShift) return SelectOp::Add;
  return SelectOp::Replace;
}

std::vector<NavBinding> navigationBindings(NavPreset preset) {
  constexpr int M = 2;
  using core::ModCtrl;
  using core::ModNone;
  using core::ModShift;
  switch (preset) {
    case NavPreset::Fusion:
    case NavPreset::AutoCAD:
    case NavPreset::Inventor:
      return {{M, ModNone, NavAction::Pan}, {M, ModShift, NavAction::Orbit}, {M, ModCtrl, NavAction::Zoom}};
    case NavPreset::SolidWorks:
      return {{M, ModNone, NavAction::Orbit}, {M, ModCtrl, NavAction::Pan}, {M, ModShift, NavAction::Zoom}};
  }
  return {};
}

NavAction navActionFor(NavPreset preset, int button, int modifiers) {
  modifiers &= (core::ModShift | core::ModCtrl);
  for (const NavBinding& b : navigationBindings(preset))
    if (b.button == button && b.modifiers == modifiers) return b.action;
  return NavAction::None;
}

const char* navPresetName(NavPreset p) {
  switch (p) {
    case NavPreset::Fusion: return "FabriCAD";
    case NavPreset::SolidWorks: return "SolidWorks";
    case NavPreset::AutoCAD: return "AutoCAD";
    case NavPreset::Inventor: return "Inventor";
  }
  return "FabriCAD";
}

NavPreset navPresetFromName(const std::string& name) {
  for (NavPreset p : {NavPreset::Fusion, NavPreset::SolidWorks, NavPreset::AutoCAD, NavPreset::Inventor})
    if (name == navPresetName(p)) return p;
  if (name == "Fusion") return NavPreset::Fusion;
  return NavPreset::Fusion;
}

Rgb hexColor(const char* hex) {
  if (!hex) return {};
  if (*hex == '#') ++hex;
  if (std::strlen(hex) < 6) return {};
  const unsigned long v = std::strtoul(hex, nullptr, 16);
  return {((v >> 16) & 0xFF) / 255.0, ((v >> 8) & 0xFF) / 255.0, (v & 0xFF) / 255.0};
}

}  // namespace fabricad::viewer
