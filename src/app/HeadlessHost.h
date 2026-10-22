// FabriCAD app: the headless script host (`fabricad --script file.fcs --offscreen`,
// docs/plans/backend-deferred.md "Headless runner", COMPARISON.md recommendation 2).
//
// Runs a .fcs script without MainWindow and without a single widget: the module registries,
// the app and script verbs (AppTools / ScriptTools through AppShell), and per tab one Document
// with its ToolManager and a core::NullViewerContext. What a script can observe behaves as in
// the GUI: tabs (NEW, OPEN, DRAWING, TAB, CLOSE), the OK state of the active command, the
// "X failed: ..." message after a recompute (MainWindow::reportNewFailures), the document name
// after OPEN / SAVE. There are no dialogs (questions take their default answer), no 3D view
// (PICK uses the B-rep, as in an offscreen GUI run) and no screenshots.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "app/AppShell.h"
#include "core/AppHost.h"
#include "core/Tool.h"

#include <QObject>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace fabricad::core {
class NullViewerContext;
}

namespace fabricad::app {

class HeadlessHost : public QObject, public core::IAppHost, public AppShell {
  Q_OBJECT
 public:
  explicit HeadlessHost(QObject* parent = nullptr);
  ~HeadlessHost() override;

  // --- IAppHost ---------------------------------------------------------------------------------
  void setPrompt(const std::string& text) override { prompt_ = text; }
  void showMessage(const std::string& text, core::MessageLevel level = core::MessageLevel::Info) override;
  bool askYesNo(const std::string&, const std::string&, bool defaultAnswer) override { return defaultAnswer; }
  core::Workspace workspace() const override;
  void setWorkspace(core::Workspace ws) override;
  void setContextualTab(const std::string&, bool) override {}
  void echoCommand(const std::string&) override {}
  void refreshToolDialog() override {}
  void setToolAcceptEnabled(bool enabled) override { acceptEnabled_ = enabled; }

  // --- AppShell ---------------------------------------------------------------------------------
  bool hasDocument() const override { return active_ >= 0; }
  core::Document& document() override;
  core::ToolManager& toolManager() override;
  core::ToolRegistry& tools() override { return registry_; }
  QObject* shellObject() override { return this; }
  bool newDocument() override;
  bool openFile(const QString& path, QString* error = nullptr) override;
  bool saveTo(const QString& path, QString* error = nullptr) override;
  bool save() override;
  void deleteSelection() override;
  void toggleSelectionVisibility() override;
  void showAllObjects() override;
  bool acceptActiveTool() override;
  QString promptText() const override { return QString::fromStdString(prompt_); }
  int tabCount() const override { return static_cast<int>(tabs_.size()); }
  int activeTabIndex() const override { return active_; }
  TabInfo tabInfo(int index) const override;
  int findTabIndex(const std::string& spec) const override;
  void closeTabLater(int index) override;
  void activateTabLater(int index) override;
  bool submitCommandLine(const QString& text) override;
  void setScriptMessageSink(std::function<void(int, const QString&)> sink) override { sink_ = std::move(sink); }

  /// Close a tab now (closing a design closes its drawings; the last tab is replaced by a new
  /// Untitled design, as in the GUI).
  void closeTab(int index);
  void activateTab(int index);

 private:
  struct Tab;
  Tab& tab(int index) const { return *tabs_[static_cast<size_t>(index)]; }
  Tab& active() const { return tab(active_); }
  int addTab(std::shared_ptr<core::Document> doc, bool drawing);
  int tabFor(const core::Document& doc, bool drawing) const;
  bool isIdleTool() const;
  void deleteRefs(const std::vector<core::ShapeRef>& refs);
  /// MainWindow::reportNewFailures: an error message for features that failed since the last
  /// check (only while no command is running); `announce` false only takes the baseline.
  void reportNewFailures(bool announce);

  core::ToolRegistry registry_;
  std::vector<std::unique_ptr<Tab>> tabs_;
  int active_ = -1;
  std::string prompt_;
  bool acceptEnabled_ = true;
  std::function<void(int, const QString&)> sink_;
  std::map<const core::Document*, std::set<core::ObjectId>> failed_;
};

}  // namespace fabricad::app
