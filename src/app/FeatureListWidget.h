// FabriCAD app: the FEATURES panel body, an Onshape-style feature list (objectName FeatureList).
//
// docs/plans/feature-list-timeline.md (widget part), docs/plans/ui-redesign.md 5.2 and 4.6.
// Rows come from FeatureListModel (Document::timelineItems); everything is painted from
// theme::Tokens at paint time (both themes, repaint on ThemeManager::themeChanged).
//
// Mouse: click selects (synced with the viewer / MODEL / SELECTION through the SelectionModel;
// Ctrl toggles, Shift extends), double-click edits a feature (renames a group), the chevron
// folds a group, the group's tri-state box suppresses / includes all its features (one undo
// step), drag a row to reorder (Document::canMoveFeatureTo / canMoveGroup decide; a refused drop
// shows the reason in an inline tip and the status bar), drag the ROLLBACK bar (one
// SetRollbackCommand on release), the dashed button opens the command search at the rollback.
// Keys: Up/Down move, Shift extends, Left/Right fold, Home/End, F2 renames, Space suppresses,
// Enter edits, Delete deletes. On the ROLLBACK bar Up/Down move the bar and Enter leaves it.
// Right-click: TimelineActions (the same menu as the bottom strip, plus the group actions).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "app/FeatureListModel.h"
#include "core/Types.h"

#include <QAbstractScrollArea>
#include <QPointer>

#include <optional>
#include <set>
#include <vector>

#include <QLineEdit>

namespace fabricad::core {
class Document;
}

namespace fabricad::app {

class TimelineActions;

class FeatureListWidget : public QAbstractScrollArea {
  Q_OBJECT
 public:
  static constexpr int kRowH = 28;
  static constexpr int kBarH = 16;
  static constexpr int kInsertH = 44;
  static constexpr int kTop = 6;        ///< space between the panel header and the first row
  static constexpr int kIndent = 18;    ///< per nesting level
  static constexpr int kBox = 14;       ///< group checkbox

  explicit FeatureListWidget(core::Document& doc, QWidget* parent = nullptr);
  ~FeatureListWidget() override;

  core::Document& document() const { return doc_; }
  const FeatureListModel& model() const { return model_; }
  TimelineActions* actions() const { return actions_; }

  /// Rebuild the rows now (normally coalesced to the next event-loop turn).
  void refresh();

  // --- live row (the command being run) --------------------------------------------------------
  /// The feature a command edits: gold live row (4.6). kInvalidId clears.
  void setLiveFeature(core::ObjectId id);
  /// A command creating a new feature: a live row at the rollback position.
  void setPendingLive(std::optional<FeatureListModel::PendingLive> live);
  bool hasLiveRow() const { return model_.liveRow() >= 0; }

  /// Compact mode (command card docked in the sidebar, redesign decision 6): only the live row.
  void setCompact(bool on);
  bool isCompact() const { return compact_; }
  /// Height the compact list needs (0 when there is no live row).
  int compactHeight() const;
  /// Height of all rows (for size hints).
  int contentHeight() const;

  // --- search and filter (header icons) --------------------------------------------------------
  void setSearch(const QString& text);
  void setHideSuppressed(bool on);
  void setProblemsOnly(bool on);

  // --- rows, selection and focus ---------------------------------------------------------------
  /// Selected features and groups, in row order.
  std::vector<core::ObjectId> selectedItems() const;
  int currentRow() const { return current_; }
  void setCurrentRow(int row);
  /// Make a feature's row current and visible (expands its groups).
  void scrollToFeature(core::ObjectId id);
  /// Inline rename of a feature or group row (F2).
  void startRename(core::ObjectId id);
  QLineEdit* renameEditor() const { return editor_; }
  /// Row rectangle in viewport coordinates (null when not laid out, e.g. compact mode).
  QRect rowRect(int row) const;
  int rowAt(const QPoint& viewportPos) const;
  /// Parts of a row for hit tests (viewport coordinates).
  QRect checkBoxRect(int row) const;
  QRect chevronRect(int row) const;
  /// Origin row: the XY / XZ / YZ labels.
  QRect originPlaneRect(int index) const;

  /// Keys the list handles itself while it has the focus (MainWindow lets them through).
  static bool handlesKey(int key, int modifiers);

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

 signals:
  void editRequested(fabricad::core::ObjectId id);
  void deleteRequested(const std::vector<fabricad::core::ObjectId>& features);
  void findInBrowserRequested(fabricad::core::ObjectId id);
  void insertRequested();
  void messageRequested(const QString& text, int level);
  /// Rows were rebuilt (count or live row may have changed): the sidebar re-sizes compact mode.
  void rowsChanged();
  /// A feature or group row was clicked (not dragged): a running command takes the focus back
  /// so its Enter applies it (p2p3-check 1).
  void rowClicked();

 protected:
  void paintEvent(QPaintEvent* e) override;
  void resizeEvent(QResizeEvent* e) override;
  void mousePressEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void mouseReleaseEvent(QMouseEvent* e) override;
  void mouseDoubleClickEvent(QMouseEvent* e) override;
  void keyPressEvent(QKeyEvent* e) override;
  void contextMenuEvent(QContextMenuEvent* e) override;
  void leaveEvent(QEvent* e) override;
  void focusInEvent(QFocusEvent* e) override;
  void focusOutEvent(QFocusEvent* e) override;
  bool viewportEvent(QEvent* e) override;
  bool eventFilter(QObject* o, QEvent* e) override;

 private:
  enum class Drag { None, Bar, Row };
  void scheduleRefresh();
  void relayout();
  int rowHeight(const FeatureRow& r) const;
  bool rowShown(int row) const;
  int nextFocusable(int from, int step) const;
  bool isSelected(const FeatureRow& r) const;
  void selectRow(int row, Qt::KeyboardModifiers mods);
  void syncFromSelection();
  void ensureVisible(int row);
  void moveBar(int step);
  void commitRename();
  void cancelRename();
  void hideTip();
  void updateHoverTip(int row);
  void paintRow(QPainter& p, int row, const QRect& r);
  void paintBar(QPainter& p, const QRect& r, bool hot);
  void paintInsert(QPainter& p, const QRect& r, bool hot);
  int depthAtX(int x) const;
  QRect contentRect(int row) const;  ///< rowRect without scrolling (content coordinates)

  core::Document& doc_;
  FeatureListModel model_;
  TimelineActions* actions_ = nullptr;
  std::vector<int> y_;               ///< content y of each row (-1 = not shown)
  int contentHeight_ = 0;
  bool pending_ = false;
  bool compact_ = false;
  int current_ = -1;
  int anchor_ = -1;
  int hover_ = -1;
  std::set<core::ObjectId> selectedGroups_;
  bool selecting_ = false;           ///< we are writing the SelectionModel
  Drag drag_ = Drag::None;
  int dragRow_ = -1;
  QPoint pressPos_;
  bool dragStarted_ = false;
  int barGap_ = -1;
  FeatureListModel::Drop drop_;
  QPointer<QLineEdit> editor_;
  core::ObjectId editing_ = core::kInvalidId;
  bool tipShown_ = false;
};

}  // namespace fabricad::app
