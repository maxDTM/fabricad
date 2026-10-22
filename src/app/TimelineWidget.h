// FabriCAD app: bottom timeline strip (ux.md section 4).
//
//   |<< < > >>|  [S1][Ex1][S2]|[Ex2][Fi1]                    (marker = rollback position)
//
// Painting and hit-testing use TimelineLayout (unit-tested). Interactions:
//   * drag the marker: rolls back live while dragging, one SetRollbackCommand on release;
//   * drag an icon: reorder; a red insertion line + "cannot move" tooltip when
//     Document::canMoveFeature rejects it; MoveFeatureCommand on drop;
//   * click selects the feature (Feature ref), double-click edits, right-click menu
//     (Edit, Roll History Marker Here, Suppress, Rename, Delete, Find in Browser);
//   * colours: error red, warning amber, rolled back greyed, suppressed struck through.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "app/TimelineLayout.h"
#include "core/Types.h"

#include <QWidget>

class QScrollArea;

namespace fabricad::core {
class Document;
}

namespace fabricad::app {

class TimelineActions;

class TimelineStrip : public QWidget {
  Q_OBJECT
 public:
  explicit TimelineStrip(core::Document& doc, QWidget* parent = nullptr);
  void refresh();
  const TimelineLayout& layout() const { return layout_; }
  QSize sizeHint() const override;
  /// Selected/highlighted entry (feature id) for painting.
  void setHighlightedFeature(core::ObjectId id);
  core::ObjectId highlightedFeature() const { return highlighted_; }
  /// The context menu and commands shared with the FEATURES list.
  TimelineActions* actions() const { return actions_; }
  /// Group brackets drawn under the icons (read-only, accent.indicator): entry ranges per group,
  /// outer groups first (depth 0 = top level).
  struct Bracket {
    core::ObjectId group = core::kInvalidId;
    int first = 0, last = 0;  ///< entry indices
    int depth = 0;
  };
  const std::vector<Bracket>& brackets() const { return brackets_; }

 signals:
  void editRequested(fabricad::core::ObjectId id);
  void renameRequested(fabricad::core::ObjectId id);
  void deleteRequested(fabricad::core::ObjectId id);
  void suppressRequested(fabricad::core::ObjectId id, bool suppress);
  void findInBrowserRequested(fabricad::core::ObjectId id);
  void selectRequested(fabricad::core::ObjectId id, bool add);

 protected:
  void paintEvent(QPaintEvent* e) override;
  void mousePressEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void mouseReleaseEvent(QMouseEvent* e) override;
  void mouseDoubleClickEvent(QMouseEvent* e) override;
  void contextMenuEvent(QContextMenuEvent* e) override;
  bool event(QEvent* e) override;

 private:
  enum class Drag { None, Marker, Item };
  core::Document& doc_;
  TimelineLayout layout_;
  Drag drag_ = Drag::None;
  int dragItem_ = -1;
  int dropGap_ = -1;
  bool dropValid_ = true;
  QString dropReason_;
  int markerStart_ = 0;
  QPoint pressPos_;
  bool dragStarted_ = false;
  core::ObjectId highlighted_ = core::kInvalidId;
  TimelineActions* actions_ = nullptr;
  std::vector<Bracket> brackets_;
};

class TimelineWidget : public QWidget {
  Q_OBJECT
 public:
  explicit TimelineWidget(core::Document& doc, QWidget* parent = nullptr);
  TimelineStrip* strip() const { return strip_; }
  /// Playback buttons.
  void stepToStart();
  void stepBack();
  void stepForward();
  void stepToEnd();
  void scrollToFeature(core::ObjectId id);

 private:
  void scheduleRefresh();
  core::Document& doc_;
  TimelineStrip* strip_ = nullptr;
  QScrollArea* scroll_ = nullptr;
  bool pending_ = false;
};

}  // namespace fabricad::app
