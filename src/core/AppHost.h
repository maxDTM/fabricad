// FabriCAD core: IAppHost, the application shell as seen by tools.
// Implemented by src/app (MainWindow). NullAppHost for tests.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Types.h"

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class QWidget;

namespace fabricad::core {

enum class MessageLevel { Info, Warning, Error };

class IAppHost {
 public:
  virtual ~IAppHost() = default;

  /// Status-bar prompt describing the next expected input ("Select a profile").
  virtual void setPrompt(const std::string& text) = 0;
  /// Transient message (status bar toast / console). Errors may also pop a dialog.
  virtual void showMessage(const std::string& text, MessageLevel level = MessageLevel::Info) = 0;
  /// Modal yes/no question. Headless hosts return `defaultAnswer`.
  virtual bool askYesNo(const std::string& title, const std::string& question, bool defaultAnswer = true) = 0;

  virtual Workspace workspace() const = 0;
  virtual void setWorkspace(Workspace ws) = 0;
  /// Show/hide and activate a contextual toolbar tab such as "SKETCH".
  virtual void setContextualTab(const std::string& tabName, bool active) = 0;
  /// Echo a command to the command-line history (AutoCAD style), for scripting/tests.
  virtual void echoCommand(const std::string& text) = 0;
  /// Main window (nullptr headless): dialog parent.
  virtual QWidget* mainWindow() { return nullptr; }
  /// Ask the shell to (re)build the command dialog for the active tool
  /// (after the tool changed what createDialog() would return).
  virtual void refreshToolDialog() = 0;
  /// Enable/disable the OK button of the active tool's dialog (validation).
  virtual void setToolAcceptEnabled(bool enabled) = 0;
};

class NullAppHost : public IAppHost {
 public:
  void setPrompt(const std::string& text) override { prompt_ = text; }
  void showMessage(const std::string& text, MessageLevel level) override { messages_.push_back({level, text}); }
  bool askYesNo(const std::string&, const std::string&, bool defaultAnswer) override { return yesNo_.value_or(defaultAnswer); }
  Workspace workspace() const override { return workspace_; }
  void setWorkspace(Workspace ws) override { workspace_ = ws; }
  void setContextualTab(const std::string& tab, bool active) override { contextualTab_ = active ? tab : ""; }
  void echoCommand(const std::string& text) override { history_.push_back(text); }
  void refreshToolDialog() override { ++dialogRefreshes_; }
  void setToolAcceptEnabled(bool e) override { acceptEnabled_ = e; }

  const std::string& prompt() const { return prompt_; }
  const std::vector<std::pair<MessageLevel, std::string>>& messages() const { return messages_; }
  const std::vector<std::string>& history() const { return history_; }
  const std::string& contextualTab() const { return contextualTab_; }
  bool acceptEnabled() const { return acceptEnabled_; }
  void setYesNoAnswer(std::optional<bool> a) { yesNo_ = a; }

 private:
  std::string prompt_;
  std::vector<std::pair<MessageLevel, std::string>> messages_;
  std::vector<std::string> history_;
  Workspace workspace_ = Workspace::Design;
  std::string contextualTab_;
  int dialogRefreshes_ = 0;
  bool acceptEnabled_ = true;
  std::optional<bool> yesNo_;
};

}  // namespace fabricad::core
