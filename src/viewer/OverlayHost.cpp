// FabriCAD viewer: published overlay layout.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "viewer/OverlayHost.h"

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
  emit changed();
}

}  // namespace fabricad::viewer
