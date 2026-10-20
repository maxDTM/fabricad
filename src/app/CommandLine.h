// FabriCAD app: AutoCAD-style command box (ux.md 9.1), lives in the status bar.
//   cmd> EXTRUDE 10          aliases/ids with arguments -> ToolManager::runCommandLine
//   cmd> 10,20 / @50<30      while a tool is active the text goes to ITool::commandLineInput
//   cmd> (empty Enter)       repeat last command
// Autocomplete over tool aliases and names, Up/Down history, a history pane (button or F2
// while focused) showing echoed commands and prompts. Toggled with Ctrl+9.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Tool.h"

#include <QStringList>
#include <QWidget>

class QCompleter;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
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
  /// Run the text as if typed + Enter (tests and scripting).
  bool submit(const QString& text);

 protected:
  bool eventFilter(QObject* o, QEvent* e) override;

 private:
  void toggleHistoryPane();

  core::ToolManager* manager_;
  QLabel* label_ = nullptr;
  QLineEdit* edit_ = nullptr;
  QToolButton* historyButton_ = nullptr;
  QPlainTextEdit* pane_ = nullptr;
  QCompleter* completer_ = nullptr;
  QStringListModel* completions_ = nullptr;
  QStringList lines_;
  QStringList typed_;  ///< commands typed by the user (Up/Down)
  int typedPos_ = -1;
};

}  // namespace fabricad::app
