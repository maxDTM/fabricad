// FabriCAD app: radial right-click marking menu widget (ux.md 6.3).
// Frameless translucent popup centred on the cursor: eight labelled slots around a centre
// dot plus the overflow list drawn below the ring. Hover by angle, click to run; the gesture
// path (RMB press-drag-release in the viewport) never shows the widget and runs the slot
// in the drag direction directly (MainWindow does that through slotForOffset()).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "app/MarkingMenuModel.h"

#include <QWidget>

#include <array>
#include <string>

namespace fabricad::app {

class MarkingMenu : public QWidget {
  Q_OBJECT
 public:
  explicit MarkingMenu(QWidget* parent = nullptr);
  void popup(const MarkingMenuModel& model, const QPoint& globalCentre);
  int hoveredSlot() const { return hover_; }

 signals:
  void commandChosen(const std::string& command);

 protected:
  void paintEvent(QPaintEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void mouseReleaseEvent(QMouseEvent* e) override;
  void keyPressEvent(QKeyEvent* e) override;

 private:
  QRect slotRect(int slot) const;
  int slotAt(const QPoint& p) const;
  QRect rowRect(int row) const;
  int rowAt(const QPoint& p) const;
  void choose(const std::string& cmd);

  MarkingMenuModel model_;
  int hover_ = -1;
  int hoverRow_ = -1;
  bool opening_ = false;
};

}  // namespace fabricad::app
