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
//
// Keep-out (phase 5, p4-check M3): the boxes stay clear of the viewport overlays the app
// publishes on the parent widget as the dynamic property `fcOverlayKeepOut` (a QVariantList of
// QRect: command card, orientation widget, tool column, breadcrumb row, readouts...). A box whose
// anchor sits behind the card moves beside the card instead of covering it, and a 1 px gold
// leader line (`#fcValueLeader`, colours from the theme QSS) ties the active box to its anchor.
// Without a screen position (no 3D view, e.g. offscreen) the boxes centre in the viewport.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "sketch/ValueInput.h"

#include <QObject>
#include <QColor>
#include <QPointer>
#include <QRect>
#include <QTimer>
#include <QVector>
#include <QWidget>

#include <functional>
#include <vector>

class QFrame;
class QLabel;
class QLineEdit;
namespace fabricad::core {
class IViewerContext;
}

namespace fabricad::sketch {

/// The gold leader from a value box to its anchor (handle): a mouse-transparent child of the
/// viewport that paints one line with a 1 px halo on each side (the halo rule, 4.5).
class ValueLeader : public QWidget {
  Q_OBJECT
  Q_PROPERTY(QColor lineColor READ lineColor WRITE setLineColor)
  Q_PROPERTY(QColor haloColor READ haloColor WRITE setHaloColor)
 public:
  explicit ValueLeader(QWidget* parent);
  /// Line from `from` (the anchor) to `to` (the box edge), in parent coordinates.
  void setLine(QPoint from, QPoint to);
  QPoint from() const { return from_; }
  QPoint to() const { return to_; }
  QColor lineColor() const { return line_; }
  void setLineColor(const QColor& c) { line_ = c; update(); }
  QColor haloColor() const { return halo_; }
  void setHaloColor(const QColor& c) { halo_ = c; update(); }

 protected:
  void paintEvent(QPaintEvent*) override;

 private:
  QPoint from_, to_;
  QColor line_{Qt::darkYellow}, halo_{Qt::transparent};  // the theme QSS sets both
};

class ValueInputOverlay : public QObject {
  Q_OBJECT
 public:
  /// `parent` is the widget the boxes float over (viewer->widget()); nullptr creates nothing.
  ValueInputOverlay(ValueInputModel& model, core::IViewerContext* viewer, QWidget* parent);
  ~ValueInputOverlay() override;

  bool hasWidgets() const { return parent_ != nullptr; }
  /// Enter pressed inside a box / Tab inside a box: forwarded here (key, modifiers, text).
  void setKeyForwarder(std::function<void(int key, int modifiers, const std::string& text)> fn) { forward_ = std::move(fn); }

  /// The ▴▾ stepper of a box was clicked: (field index, +1 / -1, x10 with Shift).
  void setStepper(std::function<void(int fieldIndex, int steps)> fn) { stepper_ = std::move(fn); }

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
  ValueLeader* leader() const { return leader_; }

  /// Place a box of `size` near the screen point `anchor`: right/below by `offset`, else the
  /// other three corners, else beside whichever keep-out rect it hits; always inside `bounds`.
  /// Pure geometry (tested).
  static QRect placeClear(QPoint anchor, QSize size, QPoint offset, const QVector<QRect>& keepOut, const QRect& bounds);

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  struct Box {
    QPointer<QFrame> frame;
    QPointer<QLabel> label;
    QPointer<QLineEdit> edit;
    QPointer<QLabel> lock;
    QPointer<QLabel> unit;
    QPointer<QWidget> steps;
    int fieldIndex = -1;
  };
  void ensureBoxes();
  void applyStyle(Box& b, const ValueField& f, bool active);
  QVector<QRect> keepOut() const;

  ValueInputModel& model_;
  core::IViewerContext* viewer_ = nullptr;
  QPointer<QWidget> parent_;
  std::vector<Box> boxes_;
  QPointer<ValueLeader> leader_;
  std::function<void(int, int, const std::string&)> forward_;
  std::function<void(int, int)> stepper_;
  QTimer repositionTimer_;
  bool visible_ = true;
  bool syncing_ = false;
  int dx_ = 18, dy_ = 14;
};

}  // namespace fabricad::sketch
