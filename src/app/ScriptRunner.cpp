// FabriCAD app: script runner.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/ScriptRunner.h"

#include "app/CommandLine.h"
#include "app/MainWindow.h"
#include "core/AppHost.h"
#include "viewer/ViewerWidget.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <algorithm>
#include <cstdio>

namespace fabricad::app {

namespace {

void settle() {
  QCoreApplication::sendPostedEvents();
  for (int i = 0; i < 3; ++i) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

const char* levelName(int level) {
  switch (static_cast<core::MessageLevel>(level)) {
    case core::MessageLevel::Error: return "error";
    case core::MessageLevel::Warning: return "warn";
    default: return "info";
  }
}

}  // namespace

ScriptRunner::ScriptRunner(MainWindow& window, QObject* parent) : QObject(parent), window_(window) {
  connect(&window_, &MainWindow::messageShown, this, &ScriptRunner::onMessage);
}

ScriptRunner::~ScriptRunner() = default;

QString ScriptRunner::stripComment(const QString& line) {
  QString t = line;
  // full-line comments
  const QString lead = t.trimmed();
  if (lead.isEmpty() || lead.startsWith('#') || lead.startsWith("//") || lead.startsWith(';')) return {};
  // trailing comments: marker preceded by white space (so "body:#18" and paths survive)
  for (const char* marker : {" #", "\t#", " //", "\t//", " ;", "\t;"}) {
    const int at = t.indexOf(marker);
    if (at >= 0) t = t.left(at);
  }
  return t.trimmed();
}

bool ScriptRunner::loadFile(const QString& path, QString* error) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    if (error) *error = "cannot read " + path + ": " + f.errorString();
    return false;
  }
  lines_ = QString::fromUtf8(f.readAll()).split('\n');
  name_ = QFileInfo(path).fileName();
  return true;
}

void ScriptRunner::setLines(const QStringList& lines, const QString& name) {
  lines_ = lines;
  name_ = name;
}

void ScriptRunner::log(const QString& line) {
  log_.push_back(line);
  if (logStream_) {
    *logStream_ << line << '\n';
    logStream_->flush();
    if (options_.quiet) return;
  }
  std::fputs(line.toUtf8().constData(), stdout);
  std::fputc('\n', stdout);
  std::fflush(stdout);
}

void ScriptRunner::onMessage(int level, const QString& text) {
  const QString prefix = QString(levelName(level)) + ": ";
  for (const QString& l : text.split('\n')) pending_.push_back(prefix + l);
  if (static_cast<core::MessageLevel>(level) == core::MessageLevel::Error && pendingError_.isEmpty()) pendingError_ = text.section('\n', 0, 0);
}

ScriptRunner::LineResult ScriptRunner::runLine(const QString& rawLine, int lineNo) {
  LineResult r;
  r.line = lineNo;
  r.text = stripComment(rawLine);
  if (r.text.isEmpty()) return r;
  pending_.clear();
  pendingError_.clear();
  QElapsedTimer t;
  t.start();
  const bool accepted = window_.commandLine()->submit(r.text);
  settle();
  r.millis = static_cast<double>(t.nsecsElapsed()) / 1e6;
  r.messages = pending_;
  if (!accepted && pendingError_.isEmpty()) {
    // the manager rejected the line: the host message (if any) says why
    for (const QString& m : pending_)
      if (m.startsWith("warn: ") || m.startsWith("error: ")) pendingError_ = m.section(": ", 1);
    if (pendingError_.isEmpty()) pendingError_ = "command rejected (unknown command, disabled, or not accepted by the active command)";
  }
  r.ok = pendingError_.isEmpty();
  r.error = pendingError_;
  return r;
}

void ScriptRunner::pauseAfter(const QString& command) {
  if (options_.stepDelayMs <= 0) return;
  if (command.startsWith("CHECK", Qt::CaseInsensitive)) return;  // checks change nothing visible
  if (auto* v = window_.viewer()) {
    // outside a sketch show the model from the home view (a finished sketch leaves the camera
    // looking straight at its plane); inside a sketch keep the sketch camera
    if (!v->inSketchMode()) v->setStandardView(core::StandardView::Home, false);
    v->fitAll(false);
  }
  window_.update();
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < options_.stepDelayMs)
    QCoreApplication::processEvents(QEventLoop::AllEvents, std::max<int>(1, options_.stepDelayMs - int(t.elapsed())));
}

int ScriptRunner::run() {
  results_.clear();
  log_.clear();
  errors_ = 0;
  commands_ = 0;
  totalMillis_ = 0;
  if (lines_.isEmpty()) return 2;
  if (!options_.logPath.isEmpty()) {
    logFile_ = std::make_unique<QFile>(options_.logPath);
    if (logFile_->open(QIODevice::WriteOnly | QIODevice::Text)) logStream_ = std::make_unique<QTextStream>(logFile_.get());
    else {
      std::fprintf(stderr, "fabricad: cannot write log %s\n", options_.logPath.toUtf8().constData());
      logFile_.reset();
    }
  }
  log(QString("# FabriCAD script %1, %2, %3 lines").arg(name_, QDateTime::currentDateTime().toString(Qt::ISODate)).arg(lines_.size()));
  bool stopped = false;
  for (int i = 0; i < lines_.size(); ++i) {
    LineResult r = runLine(lines_[i], i + 1);
    if (r.text.isEmpty()) continue;
    ++commands_;
    totalMillis_ += r.millis;
    results_.push_back(r);
    log(QString("%1  %2 %3  %4 ms")
            .arg(r.line, 4)
            .arg(r.ok ? "ok   " : "ERROR", -5)
            .arg(r.text.leftJustified(48))
            .arg(r.millis, 8, 'f', 1));
    for (const QString& m : r.messages) log("        " + m);
    if (r.ok) pauseAfter(r.text);
    if (!r.ok) {
      ++errors_;
      log(QString("        line %1 failed: %2").arg(r.line).arg(r.error));
      if (!options_.keepGoing) {
        stopped = true;
        break;
      }
    }
  }
  log(QString("# %1 commands, %2 ok, %3 failed%4, %5 ms total")
          .arg(commands_)
          .arg(commands_ - errors_)
          .arg(errors_)
          .arg(stopped ? " (stopped at the first error)" : "")
          .arg(totalMillis_, 0, 'f', 1));
  logStream_.reset();
  logFile_.reset();
  return errors_ == 0 ? 0 : 1;
}

}  // namespace fabricad::app
