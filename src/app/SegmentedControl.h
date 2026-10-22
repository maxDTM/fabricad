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
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QPointer>
#include <QStringList>
#include <QWidget>

class QComboBox;

namespace fabricad::app {

class SegmentedControl : public QWidget {
  Q_OBJECT
 public:
  static constexpr int kMaxSegments = 4;
  static constexpr int kHeight = 28;

  explicit SegmentedControl(QWidget* parent = nullptr);

  void setOptions(const QStringList& labels);
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
  QStringList labels_;
  int index_ = -1;  ///< own index when no combo is bound
  int hover_ = -1;
  int painted_ = -1;
  QPointer<QComboBox> combo_;
};

}  // namespace fabricad::app
