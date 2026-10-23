// FabriCAD tutorial engine: anchor -> global screen geometry (docs/plans/tutorial.md 3, "Locating").
// Read-only. Shared by the overlay (spotlight, bubble placement), the lint test (every anchor of every
// tour resolves) and the TestProbe (the GUI runner moves the real cursor to `point`).
//
// Matching follows the source strings, never the rendered caps: tool ids, objectNames, toolbar
// properties (toolbarTab / toolbarGroup), action text, feature and item names; all text compares are
// case-insensitive with '&' mnemonics stripped.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "app/tutorial/TutorialDefinition.h"
#include "core/ShapeRef.h"

#include <QPoint>
#include <functional>
#include <QPointer>
#include <QRect>
#include <QWidget>

#include <vector>

namespace fabricad::app {
class MainWindow;
}

namespace fabricad::app::tutorial {

struct ResolvedAnchor {
  bool found = false;     ///< the target exists (widget, item, model entity)
  bool visible = false;   ///< it can be pointed at now (on screen, not covered)
  QRect rect;             ///< global rect to spotlight (a small square around `point` for model anchors)
  QPoint point;           ///< global click point
  QPointer<QWidget> widget;  ///< the widget hit (null for model anchors)
  std::vector<core::ShapeRef> refs;  ///< model anchors: the entities (viewer highlight)
  /// How the target is reached when it is not directly visible: "menu" (open the section caption
  /// first), "expand" (a collapsed parent row), "offscreen" / "occluded" (orbit or press Home).
  QString via;
  QString why;            ///< reason when !found or !visible
  bool pickVerified = false;  ///< model anchors: the viewer's own pick at `point` returns the target
};

class AnchorResolver {
 public:
  explicit AnchorResolver(MainWindow& w) : w_(w) {}

  ResolvedAnchor resolve(const Anchor& a) const;

  /// Case-insensitive compare with '&' mnemonics and a trailing " ▾" / "..." stripped.
  static bool textMatches(const QString& shown, const QString& wanted);
  /// First visible widget (MainWindow first, then other top-level windows) matching `pred`.
  static QWidget* findWidget(QWidget* root, const std::function<bool(QWidget*)>& pred, bool visibleOnly = true);

 private:
  ResolvedAnchor widgetAnchor(const QString& path) const;
  ResolvedAnchor toolAnchor(const QString& id) const;
  ResolvedAnchor groupAnchor(const QString& spec) const;
  ResolvedAnchor menuAnchor(const QString& spec) const;
  ResolvedAnchor propAnchor(const QString& spec) const;
  ResolvedAnchor propWidget(const QString& name) const;
  ResolvedAnchor valueBoxAnchor(const QString& caption) const;
  ResolvedAnchor browserAnchor(const QString& path) const;
  ResolvedAnchor timelineAnchor(const QString& name) const;
  ResolvedAnchor tabAnchor(const QString& tab) const;
  ResolvedAnchor tabCloseAnchor(const QString& which) const;
  ResolvedAnchor modelAnchor(const QString& selector, bool sketchPoint) const;
  static ResolvedAnchor fromWidget(QWidget* w, const QString& why = {});

  MainWindow& w_;
};

}  // namespace fabricad::app::tutorial
