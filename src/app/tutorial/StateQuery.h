// FabriCAD tutorial engine: conditions read from app state (docs/plans/tutorial.md 3, "Single evaluator").
// Read-only: StateQuery looks at the active tab (tool, selection, sketch, card, prompt, settings) and
// hands `check` leaves to the script runner's evaluateCheck(), so a tour condition and a script CHECK
// mean the same thing. The overlay, the lint test and the TestProbe all evaluate through this class.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "app/tutorial/TutorialDefinition.h"

#include <QString>

namespace fabricad::app {
class MainWindow;
}

namespace fabricad::app::tutorial {

/// What a step started from: conditions such as {"camera": "changed"} compare against it.
struct StepBaseline {
  const void* document = nullptr;  ///< the active Document when the step started
  int undoIndex = -1;
  quint64 cameraSeq = 0;
  QString stepId;
};

/// Live facts the engine tracks itself (the shell has no getter for them).
struct EngineFacts {
  quint64 cameraSeq = 0;   ///< cameraChanged() count of the active viewer
  bool nextPressed = false;
  QString stepId;
};

class StateQuery {
 public:
  explicit StateQuery(MainWindow& w) : w_(w) {}

  bool eval(const Condition& c, const StepBaseline& base, const EngineFacts& facts, QString* why = nullptr) const;
  /// One CHECK expression ("body:Body1 volume ~ 32000 0.01%") against the active document.
  bool check(const QString& expr, QString* message = nullptr) const;

  // single facts (the probe's `state` uses them too)
  QString activeTool() const;   ///< "" when idle
  int sketchDof() const;        ///< -1 when no sketch is being edited
  bool inSketch() const;
  int undoIndex() const;
  QString focusedValueBox() const;  ///< caption of the focused sketch value box, "" when none

 private:
  bool leaf(const Condition& c, const StepBaseline& base, const EngineFacts& facts, QString* why) const;
  MainWindow& w_;
};

}  // namespace fabricad::app::tutorial
