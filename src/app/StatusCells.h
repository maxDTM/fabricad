// FabriCAD app: cells of the redesigned status bar (docs/plans/ui-redesign.md 5.1).
//
//   | EXTRUDE | DRAG THE HANDLE OR TYPE A DISTANCE. | ENTER APPLY  ESC CANCEL | > extrude 25   HISTORY ^ |
//     OCCT 7.8 | mm | GRID 10 mm | SNAP | |||||... REBUILD 14 ms | 1 PROFILE
//
// - ElideLabel: a label that elides on the right (the prompt) and keeps its full text().
// - StatusCommandCell (#StatusCommand): 3 px gold bar + gold square + the active command name;
//   hidden while idle.
// - KeyHints (#StatusHints): keycap + label pairs ("ENTER APPLY").
// - StatusCell (#StatusCell): caption in caps + value as typed ("GRID" "10 mm"); optional
//   on/off state and click action.
// - RebuildMeter (#StatusRebuild): 8 bars + "REBUILD 14 ms" for the last recompute.
// Everything paints from tokens at paint time (theme switches repaint).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QLabel>
#include <QWidget>

#include <functional>
#include <utility>
#include <vector>

namespace fabricad::app {

class ElideLabel : public QLabel {
  Q_OBJECT
 public:
  explicit ElideLabel(QWidget* parent = nullptr);
  QSize minimumSizeHint() const override;
  QSize sizeHint() const override;

 protected:
  void paintEvent(QPaintEvent* e) override;
};

class StatusCommandCell : public QWidget {
  Q_OBJECT
 public:
  explicit StatusCommandCell(QWidget* parent = nullptr);
  /// Show the running command ("Extrude"); empty hides the cell.
  void setCommand(const QString& name);
  QString command() const { return name_; }
  QSize sizeHint() const override;

 protected:
  void paintEvent(QPaintEvent* e) override;

 private:
  QString name_;
};

class KeyHints : public QWidget {
  Q_OBJECT
 public:
  using Hint = std::pair<QString, QString>;  ///< (key sequence "Return", label "Apply")
  explicit KeyHints(QWidget* parent = nullptr);
  void setHints(std::vector<Hint> hints);
  const std::vector<Hint>& hints() const { return hints_; }
  /// "ENTER APPLY  ESC CANCEL" (tests; the font adds the caps).
  QString text() const;
  QSize sizeHint() const override;
  QSize minimumSizeHint() const override { return QSize(0, 20); }

 protected:
  void paintEvent(QPaintEvent* e) override;

 private:
  QFont keyFont() const;
  std::vector<Hint> hints_;
};

class StatusCell : public QWidget {
  Q_OBJECT
 public:
  StatusCell(const QString& caption, QWidget* parent = nullptr);
  void setCaption(const QString& c);
  void setValue(const QString& v);  ///< as typed ("10 mm")
  QString caption() const;
  QString value() const;
  /// Checkable cells (SNAP, GRID) show their state (accent when on).
  void setOn(bool on);
  bool isOn() const { return on_; }
  void setClickAction(std::function<void()> fn);

 protected:
  void mouseReleaseEvent(QMouseEvent* e) override;

 private:
  QLabel* caption_ = nullptr;
  QLabel* value_ = nullptr;
  bool on_ = false;
  std::function<void()> click_;
};

class RebuildMeter : public QWidget {
  Q_OBJECT
 public:
  explicit RebuildMeter(QWidget* parent = nullptr);
  /// Last recompute time; < 0 = none yet.
  void setMilliseconds(qint64 ms);
  qint64 milliseconds() const { return ms_; }
  /// Lit bars for a time (0..8, log scale: 1 ms -> 1 bar, 2 s and more -> 8).
  static int barsFor(qint64 ms);
  QSize sizeHint() const override;

 protected:
  void paintEvent(QPaintEvent* e) override;

 private:
  qint64 ms_ = -1;
};

}  // namespace fabricad::app
