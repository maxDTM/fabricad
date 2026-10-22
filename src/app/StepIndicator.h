// FabriCAD app: the command card's step indicator (docs/plans/ui-redesign.md 5.4).
//
//   ━━━━━━━━━━━━━  ━━━━━━━━━━━━━  ─────────────     2 px bars + caption labels
//   01 PROFILE     02 DISTANCE    03 CONFIRM
//   done           current        to do
//
// Done = accent.indicator bar; current = live.base bar + live.text label; to do = border.default
// bar + text.tertiary label. Clicking a done step emits stepClicked (the card focuses its input).
// objectName "StepIndicator" (section 8.2).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QString>
#include <QWidget>

#include <vector>

namespace fabricad::app {

class StepIndicator : public QWidget {
  Q_OBJECT
 public:
  enum class State { Done, Current, Todo };
  struct Step {
    QString label;  ///< "Profile" (shown as "01 PROFILE")
    State state = State::Todo;
    bool operator==(const Step&) const = default;
  };
  static constexpr int kHeight = 22;

  explicit StepIndicator(QWidget* parent = nullptr);
  void setSteps(const std::vector<Step>& steps);
  const std::vector<Step>& steps() const { return steps_; }
  int currentStep() const;  ///< index of the current step, -1 when none
  QRect stepRect(int i) const;
  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

 signals:
  void stepClicked(int index);

 protected:
  void paintEvent(QPaintEvent* e) override;
  void mousePressEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;

 private:
  std::vector<Step> steps_;
};

}  // namespace fabricad::app
