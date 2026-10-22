// FabriCAD app: AutoCAD-style command box (ux.md 9.1), lives in the status bar.
//   cmd> EXTRUDE 10          aliases/ids with arguments -> ToolManager::runCommandLine
//   cmd> 10,20 / @50<30      while a tool is active the text goes to ITool::commandLineInput
//   cmd> (empty Enter)       repeat last command; with a tool active: emptyEnter() (the
//                            window forwards it like Enter on the canvas, APP-06)
// Ctrl+Z / Ctrl+Y with nothing to undo in the box undo/redo the model (APP-08).
// Autocomplete over tool aliases and names, Up/Down history, a history pane (HISTORY button or
// F2 while focused) showing echoed commands, prompts and the full command output. Output of more
// than one line (LIST, INFO, CHECK failures) opens the pane by itself when it answers a command
// typed in the box: the status-bar message cell only has room for the first line (p1-check 2).
// The pane is an overlay of the main window above the status bar, kept inside the window.
// Toggled with Ctrl+9.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Tool.h"

#include <QFrame>
#include <QPlainTextEdit>
#include <QPointer>
#include <QStringList>
#include <QWidget>

class QCompleter;
class QLabel;
class QLineEdit;
class QStringListModel;
class QToolButton;

namespace fabricad::app {

class CommandLine : public QWidget {
  Q_OBJECT
 public:
  CommandLine(core::ToolManager& manager, QWidget* parent = nullptr);
  /// Rebind to another tab's tool manager (multi-document tabs).
  void setToolManager(core::ToolManager* m) { manager_ = m; }
  core::ToolManager& manager() const { return *manager_; }

  QLineEdit* edit() const { return edit_; }
  /// Rebuild the completion list from the registry.
  void refreshCompletions();
  /// Append a line to the history pane ("> LINE", "Specify next point").
  void appendHistory(const QString& line);
  const QStringList& history() const { return lines_; }
  void setPrompt(const QString& prompt);
  /// Command output (status messages): into the history; output of several lines opens the
  /// history pane when it answers a command typed in the box (or `reveal`).
  void appendOutput(const QString& text, bool reveal = false);
  /// History pane (tests, the status-bar message cell opens it on click).
  void showHistoryPane();
  void hideHistoryPane();
  bool isHistoryPaneVisible() const;
  QPlainTextEdit* historyView() const { return text_; }
  /// True while a command typed in the box runs (its messages answer it).
  bool isSubmitting() const { return submitting_ > 0; }
  /// Run the text as if typed + Enter (tests and scripting).
  bool submit(const QString& text);
  /// Focus the box for typing after the ':' shortcut: the ':' that triggered it must not
  /// land in the box (a second delivery after the focus change, APP-07).
  void focusForTyping();

 signals:
  /// Enter on an empty box while a tool is active: forward it to the tool (APP-06).
  void emptyEnter();

 protected:
  bool eventFilter(QObject* o, QEvent* e) override;

 private:
  void toggleHistoryPane();
  void ensurePane();
  void placePane();

  core::ToolManager* manager_;
  QLabel* label_ = nullptr;
  QLineEdit* edit_ = nullptr;
  QToolButton* historyButton_ = nullptr;
  QPointer<QFrame> pane_;          ///< overlay of the main window (created on first use)
  QPointer<QPlainTextEdit> text_;
  QPointer<QWidget> paneHost_;     ///< window the pane is an overlay of
  QCompleter* completer_ = nullptr;
  QStringListModel* completions_ = nullptr;
  QStringList lines_;
  QStringList typed_;  ///< commands typed by the user (Up/Down)
  int typedPos_ = -1;
  bool swallowColon_ = false;
  int submitting_ = 0;
};

}  // namespace fabricad::app
