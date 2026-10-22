// FabriCAD app: headless / batch script runner (fabricad --script file.fcs, docs/SCRIPTING.md).
//
// A script is a text file with one command-line entry per line, run exactly as if the user had
// typed it into the command box (CommandLine::submit -> ToolManager::runCommandLine: aliases
// with arguments, typed input for the active command, shell verbs such as SELECT / PICK / OK).
// Blank lines are skipped; '#', '//' and ';' start comments (also after a command when preceded
// by white space). The event loop is pumped after every line so queued actions (new tab, open,
// recompute) have settled before the next one. Every line is timed and logged together with the
// messages the command produced; a line fails when the command was rejected or produced an
// error message (CHECK failures are errors), and the run stops there unless keepGoing is set.
// Queries (CHECK, INFO, LIST, ECHO, OPTIONS) post nothing and skip the settle; in batch mode
// (core::isBatchMode) the settle is a single pass (backend-deferred "per-command settle floor").
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

class QFile;
class QTextStream;

namespace fabricad::app {

class AppShell;

class ScriptRunner : public QObject {
  Q_OBJECT
 public:
  struct Options {
    QString logPath;        ///< empty: log to stdout
    bool keepGoing = false; ///< continue after a failed line
    bool quiet = false;     ///< no stdout echo when a log file is given
    /// Pause after every command (not after CHECK lines) for this many ms while the event loop
    /// runs, after fitting the 3D view (home view outside sketches): makes a visible run watchable / recordable. 0 = no pause.
    int stepDelayMs = 0;
  };
  struct LineResult {
    int line = 0;           ///< 1-based line number in the script
    QString text;           ///< the command as submitted (comment stripped)
    double millis = 0;      ///< wall time including the event-loop settle
    bool ok = true;
    QStringList messages;   ///< "info: ...", "warn: ...", "error: ..." produced by the command
    QString error;          ///< first error text when !ok
  };

  /// `shell` is MainWindow (GUI run) or HeadlessHost (`--script --offscreen`, no widgets).
  explicit ScriptRunner(AppShell& shell, QObject* parent = nullptr);
  ~ScriptRunner() override;

  void setOptions(const Options& o) { options_ = o; }
  const Options& options() const { return options_; }

  /// Load a script file (UTF-8). False with `error` when unreadable.
  bool loadFile(const QString& path, QString* error = nullptr);
  /// Use these lines instead of a file (tests).
  void setLines(const QStringList& lines, const QString& name = "<script>");
  const QString& scriptName() const { return name_; }

  /// Run every line now. Returns the process exit code: 0 all lines ok, 1 a line failed,
  /// 2 nothing to run (file unreadable).
  int run();
  /// Run one command line (public for tests). Skips comments/blank lines (ok, empty text).
  LineResult runLine(const QString& rawLine, int lineNo);

  const std::vector<LineResult>& results() const { return results_; }
  int errorCount() const { return errors_; }
  int commandCount() const { return commands_; }
  double totalMillis() const { return totalMillis_; }
  /// The full log text produced so far (what the log file / stdout received).
  const QStringList& logLines() const { return log_; }

  /// Comment / whitespace stripping: "EXTRUDE 10  # join" -> "EXTRUDE 10"; "" for comments.
  static QString stripComment(const QString& line);

 private:
  void log(const QString& line);
  void pauseAfter(const QString& command);
  void onMessage(int level, const QString& text);

  AppShell& shell_;
  Options options_;
  QStringList lines_;
  QString name_;
  std::vector<LineResult> results_;
  QStringList log_;
  QStringList pending_;   ///< messages of the line being run
  QString pendingError_;
  int errors_ = 0;
  int commands_ = 0;
  double totalMillis_ = 0;
  std::unique_ptr<QFile> logFile_;
  std::unique_ptr<QTextStream> logStream_;
};

}  // namespace fabricad::app
