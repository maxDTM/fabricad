// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/tutorial/TutorialEngine.h"

#include "app/MainWindow.h"
#include "app/UiSettings.h"
#include "core/Document.h"
#include "core/Json.h"
#include "core/UndoStack.h"
#include "viewer/ViewerWidget.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSettings>
#include <QTimer>

namespace fabricad::app::tutorial {

namespace {
QString key(const QString& tour, const char* what) { return QString("tutorial/%1/%2").arg(tour, what); }
}  // namespace

TutorialEngine::TutorialEngine(MainWindow& w, QObject* parent) : QObject(parent), w_(w), query_(w) {
  poll_ = new QTimer(this);
  poll_->setInterval(kPollMs);
  connect(poll_, &QTimer::timeout, this, &TutorialEngine::poll);
  connect(&w_, &MainWindow::activeTabChanged, this, [this] { watchViewer(); });
}

TutorialEngine::~TutorialEngine() = default;

QString TutorialEngine::savedStep(const QString& tourId) { return QSettings().value(key(tourId, "step")).toString(); }
bool TutorialEngine::finishedBefore(const QString& tourId) { return QSettings().value(key(tourId, "done")).toBool(); }

QString TutorialEngine::expand(const QString& text) {
  QString s = text;
  if (s.contains('{')) {
    const bool strip = UiSettings::instance().string("ui/timelineStyle") == "strip";
    s.replace("{timeline}", strip ? "timeline" : "FEATURES list");
    const bool cube = UiSettings::instance().string("ui/orientation") == "viewCube";
    s.replace("{orientation}", cube ? "ViewCube" : "orientation dial");
  }
  return s;
}

void TutorialEngine::watchViewer() {
  viewer::ViewerWidget* v = w_.viewer();
  if (watchedViewer_ == v) return;
  disconnect(cameraConn_);
  watchedViewer_ = v;
  if (v) cameraConn_ = connect(v, &viewer::ViewerWidget::cameraChanged, this, [this] { ++facts_.cameraSeq; });
}

bool TutorialEngine::start(const Tour& tour, int step) {
  if (tour.stepCount() == 0) return false;
  tour_ = tour;
  undoAtStep_.assign(tour.stepCount(), -1);
  watchViewer();
  emit started();
  enter(std::clamp(step, 0, tour.stepCount() - 1));
  poll_->start();
  return true;
}

void TutorialEngine::stop() {
  if (!tour_) return;
  poll_->stop();
  tour_.reset();
  index_ = chapter_ = -1;
  setHint({});
  emit finished(false);
}

void TutorialEngine::enter(int index, bool recordUndo) {
  if (!tour_) return;
  const Step* s = tour_->stepAt(index, &chapter_);
  if (!s) return;
  index_ = index;
  base_ = {};
  base_.document = w_.hasDocument() ? static_cast<const void*>(&w_.document()) : nullptr;
  base_.undoIndex = query_.undoIndex();
  base_.cameraSeq = facts_.cameraSeq;
  base_.stepId = s->id;
  facts_.nextPressed = false;
  facts_.stepId = s->id;
  if (recordUndo) undoAtStep_[index] = base_.undoIndex;
  lastUndo_ = base_.undoIndex;
  wasTrue_ = false;
  timeoutHintShown_ = false;
  stepClock_.start();
  why_.clear();
  hint_.clear();
  QSettings().setValue(key(tour_->id, "step"), s->id);
  // skip_if is checked once, at step start (step 0.3: the workspace is already Part)
  if (s->skipIf && query_.eval(*s->skipIf, base_, facts_)) {
    QTimer::singleShot(0, this, [this, index] {
      if (tour_ && index_ == index) complete();
    });
  }
  emit stepChanged(index_);
  hintWarning_ = false;
  emit hintChanged({}, false);
}

void TutorialEngine::setHint(const QString& h, bool warning) {
  if (hint_ == h && hintWarning_ == warning) return;
  hint_ = h;
  hintWarning_ = warning && !h.isEmpty();
  emit hintChanged(h, hintWarning_);
}

void TutorialEngine::poll() {
  const Step* s = step();
  if (!s) return;
  watchViewer();
  QString why;
  const bool ok = query_.eval(s->until, base_, facts_, &why);
  why_ = ok ? QString() : why;
  if (ok) {
    if (!wasTrue_) {
      wasTrue_ = true;
      trueSince_.start();
    }
    if (trueSince_.elapsed() >= s->stableMs || s->until.needsNext()) complete();
    return;
  }
  wasTrue_ = false;
  // off-script: the document changed and the step still waits a while later. Not while a tool
  // runs: a tool that commits early (Sketch Dimension commits the measured value, then its card
  // waits for the typed one) is the user doing the step, not a detour.
  const bool toolBusy = w_.toolManager().isActive();
  const int undo = query_.undoIndex();
  if (undo != lastUndo_) {
    lastUndo_ = undo;
    undoChangedAt_.start();
  }
  if (undo > base_.undoIndex && undoChangedAt_.isValid() && undoChangedAt_.elapsed() > kOffScriptMs && hint_.isEmpty() && !toolBusy && w_.hasDocument()) {
    const QString what = core::qs(w_.document().undoStack().undoText());
    setHint((what.isEmpty() ? QString("That changed the design.") : "That did **" + what + "**.") +
            " Press [[Ctrl+Z]], or use ⋯ > Undo to step start.",
            true);
  }
  if (!timeoutHintShown_ && s->timeoutMs > 0 && stepClock_.elapsed() > s->timeoutMs && hint_.isEmpty()) {
    timeoutHintShown_ = true;
    QString h = s->hint.isEmpty() ? QString() : expand(s->hint);
    if (s->anchor.isModel()) h += QString(h.isEmpty() ? "" : " ") + "Can't see it? Orbit, or press [[Home]].";
    if (!h.isEmpty()) setHint(h);
  }
}

void TutorialEngine::complete() {
  if (!tour_) return;
  int chapter = -1;
  tour_->stepAt(index_, &chapter);
  const int nextIndex = index_ + 1;
  int nextChapter = -1;
  const bool more = tour_->stepAt(nextIndex, &nextChapter) != nullptr;
  if (!more || nextChapter != chapter) {
    saveCheckpoint(chapter);
    emit chapterCompleted(chapter);
  }
  if (!more) {
    QSettings st;
    st.remove(key(tour_->id, "step"));
    st.setValue(key(tour_->id, "done"), true);
    poll_->stop();
    tour_.reset();
    index_ = chapter_ = -1;
    emit finished(true);
    return;
  }
  enter(nextIndex);
}

void TutorialEngine::next() {
  const Step* s = step();
  if (!s) return;
  facts_.nextPressed = true;  // NEXT of a manual step, SKIP of any other: both move on
  complete();
}

bool TutorialEngine::undoToStepStart() {
  if (!step() || !w_.hasDocument()) return false;
  core::UndoStack& u = w_.document().undoStack();
  const int target = undoAtStep_[index_] >= 0 ? undoAtStep_[index_] : base_.undoIndex;
  if (u.index() <= target) return false;
  if (w_.toolManager().isActive()) w_.toolManager().cancel();
  while (u.index() > target && u.canUndo()) u.undo();
  lastUndo_ = u.index();
  setHint({});
  return true;
}

void TutorialEngine::back() {
  if (!tour_ || index_ <= 0) return;
  int prev = index_ - 1;
  // a step that skips itself (skip_if, e.g. "the workspace is already Part") would bounce straight
  // forward again: go back past it
  while (prev > 0) {
    const Step* p = tour_->stepAt(prev);
    if (!p || !p->skipIf || !query_.eval(*p->skipIf, base_, facts_)) break;
    --prev;
  }
  if (w_.hasDocument() && undoAtStep_[prev] >= 0) {
    core::UndoStack& u = w_.document().undoStack();
    if (u.index() > undoAtStep_[prev]) {
      if (w_.toolManager().isActive()) w_.toolManager().cancel();
      while (u.index() > undoAtStep_[prev] && u.canUndo()) u.undo();
    }
  }
  enter(prev, false);
}

bool TutorialEngine::restoreChapter(QString* error) {
  if (!tour_ || chapter_ < 0) return false;
  const QString file = tour_->restoreFile(chapter_);
  auto doc = std::make_shared<core::Document>();
  if (!file.isEmpty()) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
      if (error) *error = "cannot open " + file;
      return false;
    }
    const QJsonDocument j = QJsonDocument::fromJson(f.readAll());
    std::string err;
    if (!j.isObject() || !doc->fromJson(j.object(), &err)) {
      if (error) *error = "cannot read " + file + (err.empty() ? QString() : ": " + core::qs(err));
      return false;
    }
    doc->undoStack().clear();
  }
  doc->setName(QString("%1 %2").arg(tour_->id, tour_->chapters[chapter_].id).toStdString());
  doc->setModified(false);
  w_.addDesignTab(doc);  // a new tab: the user's own work stays in its tab (decision 9)
  const int first = tour_->firstStepOfChapter(chapter_);
  for (int i = first; i < static_cast<int>(undoAtStep_.size()); ++i) undoAtStep_[i] = -1;
  enter(first);
  return true;
}

void TutorialEngine::saveCheckpoint(int chapter) {
  const QString dir = qEnvironmentVariable("FABRICAD_TUTORIAL_CHECKPOINT_DIR");
  if (dir.isEmpty() || !tour_ || chapter < 0 || !w_.hasDocument()) return;
  const QString name = tour_->chapters[chapter].checkpoint;
  if (name.isEmpty()) return;
  QDir().mkpath(dir);
  QFile f(QDir(dir).filePath(name));
  if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(w_.document().toJson()).toJson(QJsonDocument::Indented));
}

}  // namespace fabricad::app::tutorial
