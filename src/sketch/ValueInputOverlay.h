// FabriCAD sketch: floating value boxes over the 3D view (the widgets behind ValueInputModel).
//
// One small frame (label + QLineEdit) per visible field, parented to the viewer widget and
// positioned with IViewerContext::worldToScreen() from the field's anchor, stacked below /
// right of the cursor like Fusion's dynamic input. The active box has the accent border, a
// locked box shows a lock mark. Keys are routed by the shell to the active tool (which feeds
// the model), so the widgets mostly mirror the model; typing directly into a box (after a
// click) is pushed back into the model through textEdited. Works without widgets too: when
// the viewer has no QWidget (headless), nothing is created and the tool falls back to a
// preview text label built from ValueInputModel::summary().
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "sketch/ValueInput.h"

#include <QObject>
#include <QPointer>
#include <QTimer>

#include <functional>
#include <vector>

class QFrame;
class QLabel;
class QLineEdit;
class QWidget;

namespace fabricad::core {
class IViewerContext;
}

namespace fabricad::sketch {

class ValueInputOverlay : public QObject {
  Q_OBJECT
 public:
  /// `parent` is the widget the boxes float over (viewer->widget()); nullptr creates nothing.
  ValueInputOverlay(ValueInputModel& model, core::IViewerContext* viewer, QWidget* parent);
  ~ValueInputOverlay() override;

  bool hasWidgets() const { return parent_ != nullptr; }
  /// Enter pressed inside a box / Tab inside a box: forwarded here (key, modifiers, text).
  void setKeyForwarder(std::function<void(int key, int modifiers, const std::string& text)> fn) { forward_ = std::move(fn); }

  /// Rebuild widgets from the model (called from the model's change callback).
  void sync();
  /// Recompute positions from the anchors (camera moved, widget resized).
  void reposition();
  void setVisible(bool visible);
  bool isVisible() const { return visible_; }
  /// Pixel offset of the first box from its anchor (Fusion: right/below the cursor).
  void setOffset(int dx, int dy) { dx_ = dx; dy_ = dy; }

  // test inspection
  int boxCount() const { return static_cast<int>(boxes_.size()); }
  QLineEdit* editAt(int fieldIndex) const;
  QFrame* frameAt(int fieldIndex) const;

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  struct Box {
    QPointer<QFrame> frame;
    QPointer<QLabel> label;
    QPointer<QLineEdit> edit;
    QPointer<QLabel> lock;
    int fieldIndex = -1;
  };
  void ensureBoxes();
  void applyStyle(Box& b, const ValueField& f, bool active);

  ValueInputModel& model_;
  core::IViewerContext* viewer_ = nullptr;
  QPointer<QWidget> parent_;
  std::vector<Box> boxes_;
  std::function<void(int, int, const std::string&)> forward_;
  QTimer repositionTimer_;
  bool visible_ = true;
  bool syncing_ = false;
  int dx_ = 18, dy_ = 14;
};

}  // namespace fabricad::sketch
