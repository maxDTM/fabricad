// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/ViewerContext.h"

#include <gp_Lin.hxx>

namespace fabricad::core {

PreviewHandle NullViewerContext::showPreviewShape(const TopoDS_Shape& shape, const PreviewStyle& style) {
  PreviewHandle h = nextHandle_++;
  previews_[h] = PreviewItem{"shape", shape, {}, {}, style};
  return h;
}

PreviewHandle NullViewerContext::showPreviewLines(const std::vector<Vec3>& pts, const PreviewStyle& style) {
  PreviewHandle h = nextHandle_++;
  previews_[h] = PreviewItem{"lines", {}, pts, {}, style};
  return h;
}

PreviewHandle NullViewerContext::showPreviewPolyline(const std::vector<Vec3>& pts, const PreviewStyle& style) {
  PreviewHandle h = nextHandle_++;
  previews_[h] = PreviewItem{"polyline", {}, pts, {}, style};
  return h;
}

PreviewHandle NullViewerContext::showPreviewPoints(const std::vector<Vec3>& pts, const PreviewStyle& style) {
  PreviewHandle h = nextHandle_++;
  previews_[h] = PreviewItem{"points", {}, pts, {}, style};
  return h;
}

PreviewHandle NullViewerContext::showPreviewText(const Vec3& at, const std::string& text, const PreviewStyle& style) {
  PreviewHandle h = nextHandle_++;
  previews_[h] = PreviewItem{"text", {}, {at}, text, style};
  return h;
}

void NullViewerContext::updatePreviewShape(PreviewHandle h, const TopoDS_Shape& shape) {
  auto it = previews_.find(h);
  if (it != previews_.end()) it->second.shape = shape;
}

void NullViewerContext::updatePreviewLines(PreviewHandle h, const std::vector<Vec3>& pts) {
  auto it = previews_.find(h);
  if (it != previews_.end()) it->second.points = pts;
}

void NullViewerContext::removePreview(PreviewHandle h) { previews_.erase(h); }
void NullViewerContext::clearPreviews() { previews_.clear(); }

void NullViewerContext::enterSketchMode(const gp_Ax3& plane, bool lookAt) {
  sketchMode_ = true;
  sketchPlane_ = plane;
  grid_ = true;
  if (lookAt) viewPlane_ = plane;
}

void NullViewerContext::setStandardView(StandardView v, bool) {
  gp_Pnt o(0, 0, 0);
  switch (v) {
    case StandardView::Front: viewPlane_ = gp_Ax3(o, gp_Dir(0, -1, 0), gp_Dir(1, 0, 0)); break;
    case StandardView::Back: viewPlane_ = gp_Ax3(o, gp_Dir(0, 1, 0), gp_Dir(-1, 0, 0)); break;
    case StandardView::Left: viewPlane_ = gp_Ax3(o, gp_Dir(-1, 0, 0), gp_Dir(0, -1, 0)); break;
    case StandardView::Right: viewPlane_ = gp_Ax3(o, gp_Dir(1, 0, 0), gp_Dir(0, 1, 0)); break;
    case StandardView::Top: viewPlane_ = gp_Ax3(o, gp_Dir(0, 0, 1), gp_Dir(1, 0, 0)); break;
    case StandardView::Bottom: viewPlane_ = gp_Ax3(o, gp_Dir(0, 0, -1), gp_Dir(1, 0, 0)); break;
    case StandardView::Iso:
    case StandardView::Home: viewPlane_ = gp_Ax3(o, gp_Dir(1, -1, 1), gp_Dir(1, 1, 0)); break;
  }
}

bool NullViewerContext::screenToPlane(int x, int y, const gp_Pln& plane, gp_Pnt& out) const {
  gp_Pnt origin;
  gp_Dir dir;
  if (!screenRay(x, y, origin, dir)) return false;
  double denom = gp_Vec(dir).Dot(gp_Vec(plane.Axis().Direction()));
  if (std::abs(denom) < 1e-12) return false;
  double t = gp_Vec(origin, plane.Location()).Dot(gp_Vec(plane.Axis().Direction())) / denom;
  out = origin.Translated(gp_Vec(dir) * t);
  return true;
}

bool NullViewerContext::screenRay(int x, int y, gp_Pnt& origin, gp_Dir& dir) const {
  const gp_Ax3& vp = sketchMode_ ? sketchPlane_ : viewPlane_;
  // x right, y down in pixels -> plane X right, plane Y up; 1 pixel = pixelSize_ mm.
  gp_Vec off = gp_Vec(vp.XDirection()) * (x * pixelSize_) + gp_Vec(vp.YDirection()) * (-y * pixelSize_);
  origin = vp.Location().Translated(off + gp_Vec(vp.Direction()) * 1000.0);
  dir = vp.Direction().Reversed();
  return true;
}

bool NullViewerContext::worldToScreen(const gp_Pnt& p, double& x, double& y) const {
  const gp_Ax3& vp = sketchMode_ ? sketchPlane_ : viewPlane_;
  gp_Vec d(vp.Location(), p);
  x = d.Dot(gp_Vec(vp.XDirection())) / pixelSize_;
  y = -d.Dot(gp_Vec(vp.YDirection())) / pixelSize_;
  return true;
}

void NullViewerContext::setFeatureVisibilityOverride(ObjectId id, std::optional<bool> v) {
  if (v) overrides_[id] = v;
  else overrides_.erase(id);
}

}  // namespace fabricad::core
