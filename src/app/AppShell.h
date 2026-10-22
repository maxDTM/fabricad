// FabriCAD app: AppShell, the shell as seen by the application and script tools (AppTools,
// ScriptTools) and by the ScriptRunner. Two implementations:
//
//   MainWindow     the GUI (tabs, viewer, dialogs, command line, status bar)
//   HeadlessHost   `fabricad --script file.fcs --offscreen` without any widget: documents and
//                  their ToolManagers only (docs/plans/backend-deferred.md "Headless runner")
//
// GUI-only entries have empty defaults, so the headless runner simply has no dialogs, no 3D
// view and no direct actions. Everything a script can observe (documents, tabs, messages,
// the active command's prompt and OK state) goes through this interface.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "app/TitleBar.h"
#include "core/AppHost.h"
#include "core/Types.h"

#include <QString>

#include <functional>
#include <string>
#include <vector>

class QObject;
class QWidget;

namespace fabricad::core {
class Document;
class ToolManager;
class ToolRegistry;
}  // namespace fabricad::core

namespace fabricad::viewer {
class ViewerWidget;
}

namespace fabricad::app {

class AppShell {
 public:
  virtual ~AppShell() = default;

  // --- the active design -------------------------------------------------------------------
  virtual bool hasDocument() const = 0;
  virtual core::Document& document() = 0;
  virtual core::ToolManager& toolManager() = 0;
  virtual core::ToolRegistry& tools() = 0;
  virtual void showMessage(const std::string& text, core::MessageLevel level = core::MessageLevel::Info) = 0;
  /// Context object for queued calls (tools that replace the document run after they finished).
  virtual QObject* shellObject() = 0;
  /// The 3D view of the active tab (null headless and in a drawing tab).
  virtual fabricad::viewer::ViewerWidget* viewer() const { return nullptr; }

  // --- files ----------------------------------------------------------------------------------
  virtual bool newDocument() = 0;
  virtual bool openFile(const QString& path, QString* error = nullptr) = 0;
  /// Ask to save unsaved changes (GUI); true = go on.
  virtual bool maybeSave() { return true; }
  virtual bool saveTo(const QString& path, QString* error = nullptr) = 0;
  /// Save to the document's file (GUI: Save As when it has none).
  virtual bool save() = 0;
  virtual bool saveAs() { return false; }
  /// The Open file dialog (GUI).
  virtual void openWithDialog() {}

  // --- edit -----------------------------------------------------------------------------------
  virtual void deleteSelection() = 0;
  virtual void toggleSelectionVisibility() = 0;
  virtual void showAllObjects() = 0;
  virtual void showParameters() {}

  // --- the active command ---------------------------------------------------------------------
  /// OK / Enter for the active command; false (with a message) when none or OK is disabled.
  virtual bool acceptActiveTool() = 0;
  virtual QString promptText() const = 0;

  // --- tabs (CHECK tabs, LIST tabs, TAB, CLOSE, INFO) -------------------------------------------
  struct TabInfo {
    QString title;          ///< "Untitled", "chair *", "chair Drawing"
    bool design = true;
    std::string filePath;
  };
  virtual int tabCount() const = 0;
  virtual int activeTabIndex() const = 0;
  virtual TabInfo tabInfo(int index) const = 0;
  /// "2" (1-based) or a tab title / design name; -1 when nothing matches.
  virtual int findTabIndex(const std::string& spec) const = 0;
  /// Deferred: the tool that asks runs inside the tab's own ToolManager.
  virtual void closeTabLater(int index) = 0;
  virtual void activateTabLater(int index) = 0;

  // --- scripts (ScriptRunner) -------------------------------------------------------------------
  /// One command-line entry, as typed into the command box.
  virtual bool submitCommandLine(const QString& text) = 0;
  /// Receives every message (level as int, text) while a script runs; empty = none.
  virtual void setScriptMessageSink(std::function<void(int, const QString&)> sink) = 0;
  /// SCREENSHOT target (null headless).
  virtual QWidget* screenshotTarget(bool /*viewportOnly*/) { return nullptr; }
  /// --step-delay: show the model before the pause (GUI).
  virtual void prepareStepPause() {}

  // --- GUI only --------------------------------------------------------------------------------
  /// A command that never cancels the active one (view commands).
  virtual void addDirectAction(const std::string& /*id*/, std::function<void()> /*fn*/) {}
  virtual void toggleCommandLine() {}
  virtual void openCommandSearch() {}
  virtual void showShortcutsHelp() {}
  virtual void setWorkspaceChoice(WorkspaceChoice /*c*/) {}
  /// Preferences dialog page (0 general, 1 document; see PreferencesDialog::Page).
  virtual void showPreferencesPage(int /*page*/) {}
  /// The colour dialog for these bodies (modal; opened after the tool finished).
  virtual void pickAppearanceFor(const std::vector<core::ObjectId>& /*bodies*/) {}
  /// The 3D view is in sketch mode (Appearance is disabled there).
  virtual bool viewerInSketchMode() const { return false; }
};

}  // namespace fabricad::app
