// FabriCAD viewer: published overlay layout.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "viewer/OverlayHost.h"

#include <QVariantList>
#include <QWidget>

namespace fabricad::viewer {

OverlayHost::OverlayHost(QWidget* viewport) : QObject(viewport) { setObjectName("OverlayHost"); }

OverlayHost* OverlayHost::of(QWidget* viewport) {
  if (!viewport) return nullptr;
  if (auto* h = viewport->findChild<OverlayHost*>("OverlayHost", Qt::FindDirectChildrenOnly)) return h;
  return new OverlayHost(viewport);
}

void OverlayHost::publish(const OverlayLayout& layout) {
  layout_ = layout;
  // the same regions as a plain property, for widgets outside the viewer library (the sketch
  // value boxes keep clear of them: p4-check M3)
  if (auto* viewport = qobject_cast<QWidget*>(parent())) {
    QVariantList rects;
    for (const OverlayLayout::Region& r : layout_.occupied())
      if (r.rect.isValid()) rects.push_back(r.rect);
    if (viewport->property(kKeepOutProperty).toList() != rects) viewport->setProperty(kKeepOutProperty, rects);
  }
  emit changed();
}

}  // namespace fabricad::viewer
