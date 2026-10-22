// FabriCAD viewer: the published overlay layout of one viewport (docs/plans/ui-redesign.md 6.3).
// The app computes an OverlayLayout for the active viewport (card, sketch palette, ViewCube, nav
// bar...) and publishes it here. Other overlays, toasts and the tutorial bubble read occupied()
// or findFree() to keep clear of them, and follow changed().
//   auto* host = viewer::OverlayHost::of(viewerWidget);   // found or created (a child QObject)
//   QRect spot = host->findFree(bubbleSize, anchorPoint);
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "viewer/OverlayLayout.h"

#include <QObject>

class QWidget;

namespace fabricad::viewer {

class OverlayHost : public QObject {
  Q_OBJECT
 public:
  /// The host of `viewport` (created on first use, owned by the viewport).
  static OverlayHost* of(QWidget* viewport);

  void publish(const OverlayLayout& layout);
  const OverlayLayout& layout() const { return layout_; }
  const std::vector<OverlayLayout::Region>& occupied() const { return layout_.occupied(); }
  QRect findFree(const QSize& size, const QPoint& near) const { return layout_.findFree(size, near); }

 signals:
  void changed();

 private:
  explicit OverlayHost(QWidget* viewport);
  OverlayLayout layout_;
};

}  // namespace fabricad::viewer
