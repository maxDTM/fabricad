// FabriCAD tutorial engine: the state machine (docs/plans/tutorial.md 4, "Off-script and recovery").
// Current step, condition watch (polled; a step completes once `until` has held for stable_ms),
// skip_if, timeout hints, off-script hints, Back (undo to the previous step's start), Restore
// chapter (decision 9: the chapter's start file opens in a NEW tab, nothing is lost), chapter
// checkpoints (FABRICAD_TUTORIAL_CHECKPOINT_DIR: the GUI runner collects them), and the saved
// position in QSettings tutorial/<id>/step. The engine never performs a modelling action: the
// recovery buttons are explicit user choices and the only document changes it makes.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "app/tutorial/StateQuery.h"
#include "app/tutorial/TutorialDefinition.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>

#include <optional>
#include <vector>

class QTimer;

namespace fabricad::app {
class MainWindow;
}

namespace fabricad::app::tutorial {

class TutorialEngine : public QObject {
  Q_OBJECT
 public:
  static constexpr int kPollMs = 100;
  static constexpr int kOffScriptMs = 4000;  ///< a document change that did not finish the step

  explicit TutorialEngine(MainWindow& w, QObject* parent = nullptr);
  ~TutorialEngine() override;

  /// Start (or restart) a tour at a flat step index. False when the tour has no steps.
  bool start(const Tour& tour, int step = 0);
  /// Exit the tour (keeps the saved position so Help > Tutorials can resume it).
  void stop();
  bool running() const { return tour_.has_value(); }
  const Tour* tour() const { return tour_ ? &*tour_ : nullptr; }
  int stepIndex() const { return index_; }
  int chapterIndex() const { return chapter_; }
  const Step* step() const { return tour_ ? tour_->stepAt(index_) : nullptr; }

  /// NEXT / SKIP (Alt+Right). Completes a manual step; skips any other step.
  void next();
  /// BACK (Alt+Left): the previous step, undoing to its start.
  void back();
  /// Undo the changes made since the current step started. False when nothing to undo.
  bool undoToStepStart();
  /// Open the chapter's start state in a new tab and go to its first step.
  bool restoreChapter(QString* error = nullptr);

  /// Evaluate the current step now (normally every kPollMs).
  void poll();
  /// Why the current step has not completed yet (last evaluation).
  QString waitingFor() const { return why_; }
  QString hint() const { return hint_; }

  /// {timeline} / {orientation} expanded for the current layout (ui/timelineStyle, ui/orientation).
  static QString expand(const QString& text);

  StateQuery& query() { return query_; }
  /// Camera moves seen (the viewer's cameraChanged).
  quint64 cameraSeq() const { return facts_.cameraSeq; }

  /// QSettings tutorial/<id>/step: the saved step id ("" when none / finished).
  static QString savedStep(const QString& tourId);
  static bool finishedBefore(const QString& tourId);

 signals:
  void started();
  void stepChanged(int index);
  void hintChanged(const QString& hint);
  void chapterCompleted(int chapter);
  /// completed: the last step finished (false: exited).
  void finished(bool completed);

 private:
  void enter(int index, bool recordUndo = true);
  void complete();
  void setHint(const QString& h);
  void watchViewer();
  void saveCheckpoint(int chapter);

  MainWindow& w_;
  StateQuery query_;
  std::optional<Tour> tour_;
  int index_ = -1;
  int chapter_ = -1;
  StepBaseline base_;
  EngineFacts facts_;
  std::vector<int> undoAtStep_;  ///< undo index when each step started (-1 unknown)
  QTimer* poll_ = nullptr;
  QElapsedTimer stepClock_;
  QElapsedTimer trueSince_;
  bool wasTrue_ = false;
  bool timeoutHintShown_ = false;
  int lastUndo_ = -1;
  QElapsedTimer undoChangedAt_;
  QString why_;
  QString hint_;
  QPointer<QObject> watchedViewer_;
  QMetaObject::Connection cameraConn_;
};

}  // namespace fabricad::app::tutorial
