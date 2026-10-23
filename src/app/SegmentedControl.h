// FabriCAD app: segmented control for small enums in the command card (docs/plans/ui-redesign.md 5.4).
//
//   +------------+------------+------------+
//   |  ONE SIDE  |  SYMMETRIC | TWO SIDES  |   equal cells, 28 px (label only)
//   +------------+------------+------------+
//
// Checked cell: accent.selFill + 1 px accent.indicator + text.primary; the others text.secondary
// with 1 px border.control dividers. Left/Right/Home/End move the selection, a focus ring shows
// keyboard focus. Painted from the tokens at paint time (theme switch: update()).
//
// bindCombo() mirrors an existing QComboBox (the part PropertyPanel's enum rows): the combo stays
// the model and keeps its objectName, so tools, scripts and tests that drive the combo keep
// working; the control takes the same objectName (prop_<name>, section 8.2) and replaces the combo
// on screen. Enums with more than kMaxSegments values stay combos.
//
// Hints from the bound combo's dynamic properties (part PropertyPanel): "segmentIcons" (logical
// icon paths per option, themed through app/Icons) puts an 18 px icon above each label (52 px tall);
// "segmentOrder" (option indices in display order) reorders the cells without changing the model
// indices. Indices in this API are always option (combo) indices; cellRect() finds the cell.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QPointer>
#include <QStringList>
#include <QWidget>

#include <QComboBox>
#include <QIcon>
#include <QList>

namespace fabricad::app {

class SegmentedControl : public QWidget {
  Q_OBJECT
 public:
  static constexpr int kMaxSegments = 4;
  static constexpr int kHeight = 28;
  static constexpr int kIconHeight = 52;  ///< with segment icons (icon 18 + label, plan 5.4)

  explicit SegmentedControl(QWidget* parent = nullptr);

  void setOptions(const QStringList& labels);
  /// Display order (option indices); empty = model order. Invalid lists are ignored.
  void setOrder(const QList<int>& order);
  /// One icon per option (or empty for labels only).
  void setIcons(const QList<QIcon>& icons);
  bool hasIcons() const { return !icons_.isEmpty(); }
  /// Option index shown at display position `pos`.
  int optionAt(int pos) const;
  QStringList options() const { return labels_; }
  int count() const { return static_cast<int>(labels_.size()); }
  int currentIndex() const;
  /// Select a cell (also sets the bound combo, which then emits its own signal).
  void setCurrentIndex(int index);
  /// Mirror `combo` (options and index, both ways). The combo is hidden by the caller.
  void bindCombo(QComboBox* combo);
  QComboBox* boundCombo() const { return combo_; }
  /// Re-read the bound combo (its index may change with its signals blocked).
  void sync();
  QRect cellRect(int index) const;

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

 signals:
  void currentIndexChanged(int index);
  /// Emitted before the bound combo changes (its handlers may rebuild the panel around us).
  void aboutToChange(int index);

 protected:
  void paintEvent(QPaintEvent* e) override;
  void mousePressEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void leaveEvent(QEvent* e) override;
  void keyPressEvent(QKeyEvent* e) override;
  void focusInEvent(QFocusEvent* e) override;
  void focusOutEvent(QFocusEvent* e) override;

 private:
  int cellAt(const QPoint& p) const;
  int positionOf(int option) const;
  QStringList labels_;
  QList<int> order_;
  QList<QIcon> icons_;
  int index_ = -1;  ///< own index when no combo is bound
  int hover_ = -1;
  int painted_ = -1;
  QPointer<QComboBox> combo_;
};

}  // namespace fabricad::app
