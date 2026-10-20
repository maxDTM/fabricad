// FabriCAD application shell. OWNER: ui agent.
//
// Fusion-style single window (docs/research/ux.md section 1):
//   application bar   [File v][Save][Undo][Redo]  Document *            [S][?]
//   ribbon            [DESIGN v] SOLID | SKETCH (contextual) | TOOLS ... with group dropdowns
//   browser (dock)  | viewport (OCCT, ViewCube, nav bar, command dialog overlay)
//   timeline (bottom dock, full width)
//   status bar        prompt | message | selection | sketch DOF | units | cmd> ______
//
// MainWindow implements core::IAppHost and owns the Document, ToolRegistry and ToolManager.
// It calls each module's registerModule() plus registerAppTools() (view.*, app.*, select.*),
// routes keys per ShortcutRouter (single-letter shortcuts only with canvas focus), hosts
// command dialogs (ITool::createDialog) and wires the browser/timeline to the Document.
// Startup is fast: no GL before show(); main() calls initializeViewer() from the event loop.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "app/AppState.h"
#include "core/AppHost.h"
#include "core/Document.h"
#include "core/Tool.h"

#include <QMainWindow>
#include <QPointer>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class QDockWidget;
class QLabel;
class QMenu;
class QStackedWidget;
class QToolButton;
class QTimer;

namespace fabricad::viewer {
class ViewerWidget;
}

namespace fabricad::app {

class BrowserTree;
class CommandDialog;
class CommandLine;
class CommandSearch;
class MarkingMenu;
class NavBar;
class Ribbon;
class SketchPalette;
class TimelineWidget;

class MainWindow : public QMainWindow, public fabricad::core::IAppHost {
  Q_OBJECT
 public:
  /// Factory for the Drawing workspace's central widget (drawing module). Until one is set
  /// the workspace shows a placeholder page. See docs/integration-notes/ui.md.
  using WorkspaceFactory = std::function<QWidget*(core::Document&, core::ToolManager&, QWidget* parent)>;
  static void setDrawingWorkspaceFactory(WorkspaceFactory f);

  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

  fabricad::core::Document& document() { return *document_; }
  fabricad::core::ToolRegistry& tools() { return registry_; }
  fabricad::core::ToolManager& toolManager() { return *manager_; }
  viewer::ViewerWidget* viewer() const { return viewer_; }
  Ribbon* ribbon() const { return ribbon_; }
  BrowserTree* browser() const { return browser_; }
  TimelineWidget* timeline() const { return timeline_; }
  CommandDialog* commandDialog() const { return dialog_; }
  CommandLine* commandLine() const { return commandLine_; }
  CommandSearch* commandSearch() const { return search_; }
  RecentFiles& recentFiles() { return recent_; }

  /// Create the OCCT GL view (call once the window is shown).
  void initializeViewer();
  /// Headless/scripted mode: no modal dialogs (askYesNo returns the default, file dialogs
  /// are skipped, unsaved-changes prompts answer "discard").
  void setInteractive(bool on) { interactive_ = on; }
  bool isInteractive() const { return interactive_; }

  // --- file operations -----------------------------------------------------------------
  bool newDocument();
  bool open();
  /// .fcad loads; .step/.stp/.stl are inserted through part.import-file.
  bool openFile(const QString& path, QString* error = nullptr);
  bool save();
  bool saveAs();
  bool saveTo(const QString& path, QString* error = nullptr);
  /// Ask to save unsaved changes. False = the user cancelled.
  bool maybeSave();

  // --- autosave / crash recovery (README "Autosave") --------------------------------------
  /// Write a recovery copy of a modified document now (normally driven by timers: 5 s after an
  /// undoable change, every 2 minutes). Does not touch the document's path or modified flag.
  bool autosaveNow();
  /// Recovery file this document autosaves to.
  QString recoveryFilePath() const;
  /// Remove this document's recovery copy (after a clean save / close).
  void clearRecoveryFile();
  /// Offer to recover the newest autosave left behind by a previous session. Interactive only;
  /// FABRICAD_NO_RECOVERY=1 disables it. Returns true when a document was recovered.
  bool offerRecovery();
  static QString recoveryDirectory();

  // --- commands used by app tools, browser, timeline, marking menu ----------------------
  void editFeature(core::ObjectId id);
  /// Delete entities (with confirmation when bodies map to their creating features).
  void deleteRefs(const std::vector<core::ShapeRef>& refs);
  void deleteSelection();
  void toggleSelectionVisibility();
  void showAllObjects();
  void showParameters();
  void openCommandSearch();
  void toggleCommandLine();
  void focusCommandLine();
  void showShortcutsHelp();
  void runPseudoCommand(const std::string& cmd);
  /// Run a command from the UI (ribbon, S-box, marking menu, shortcuts). View commands
  /// registered with addDirectAction() run without cancelling the active tool.
  bool runCommand(const std::string& id, std::vector<std::string> args = {});
  /// `fn` runs instead of ToolManager::run(id) while a tool is active (camera/display
  /// commands must not interrupt a command, ux.md 5).
  void addDirectAction(const std::string& id, std::function<void()> fn);
  /// Marking menu at a position (RMB click) / gesture (RMB drag) in the viewport.
  void showMarkingMenu(const QPoint& globalPos);
  void markingGesture(const QPoint& pressGlobal, const QPoint& releaseGlobal);

  /// Keyboard routing entry point (event filter and tests). True = consumed.
  bool handleKeyPress(int key, int modifiers, const QString& text, bool autoRepeat, QWidget* focus);
  /// Status bar texts (tests).
  QString promptText() const;
  QString selectionText() const;
  QString sketchStatusText() const;
  QString messageText() const;
  const std::vector<std::string>& echoHistory() const { return echo_; }

  // --- IAppHost ------------------------------------------------------------------------
  void setPrompt(const std::string& text) override;
  void showMessage(const std::string& text, fabricad::core::MessageLevel level) override;
  bool askYesNo(const std::string& title, const std::string& question, bool defaultAnswer) override;
  fabricad::core::Workspace workspace() const override { return workspace_; }
  void setWorkspace(fabricad::core::Workspace ws) override;
  void setContextualTab(const std::string& tabName, bool active) override;
  void echoCommand(const std::string& text) override;
  QWidget* mainWindow() override { return this; }
  void refreshToolDialog() override;
  void setToolAcceptEnabled(bool enabled) override;

 protected:
  void closeEvent(QCloseEvent* e) override;
  bool eventFilter(QObject* o, QEvent* e) override;

 private:
  void registerModules();
  void buildUi();
  QWidget* buildAppBar();
  void buildFileMenu(QMenu* menu);
  void buildStatusBar();
  void connectSignals();
  void onToolActivated(const std::string& id);
  void onToolFinished(const std::string& id, bool accepted);
  void presentDialog();
  void updateTitle();
  void updateUndoButtons();
  void updateSelectionStatus();
  void updateSketchStatus();
  void scheduleStateRefresh();
  void applyUiState();
  void storeUiState();
  void scheduleAutosave();
  void afterDocumentReplaced();
  void viewCubeMenu(const QPoint& globalPos);
  void layoutOverlays();
  bool isCanvasFocus(QWidget* focus) const;
  bool isIdleTool() const;

  std::unique_ptr<fabricad::core::Document> document_;
  fabricad::core::ToolRegistry registry_;
  std::unique_ptr<fabricad::core::ToolManager> manager_;
  fabricad::core::Workspace workspace_ = fabricad::core::Workspace::Design;

  viewer::ViewerWidget* viewer_ = nullptr;
  Ribbon* ribbon_ = nullptr;
  BrowserTree* browser_ = nullptr;
  QDockWidget* browserDock_ = nullptr;
  TimelineWidget* timeline_ = nullptr;
  QDockWidget* timelineDock_ = nullptr;
  QStackedWidget* central_ = nullptr;
  QWidget* drawingPage_ = nullptr;
  CommandDialog* dialog_ = nullptr;
  CommandLine* commandLine_ = nullptr;
  CommandSearch* search_ = nullptr;
  MarkingMenu* marking_ = nullptr;
  NavBar* navBar_ = nullptr;
  SketchPalette* palette_ = nullptr;
  QToolButton* undoButton_ = nullptr;
  QToolButton* redoButton_ = nullptr;
  QLabel* docTab_ = nullptr;
  QLabel* promptLabel_ = nullptr;
  QLabel* messageLabel_ = nullptr;
  QLabel* selectionLabel_ = nullptr;
  QLabel* sketchLabel_ = nullptr;
  QLabel* unitsLabel_ = nullptr;
  QTimer* messageTimer_ = nullptr;
  QTimer* autosaveTimer_ = nullptr;     ///< single shot, 5 s after an undoable change
  QTimer* autosavePeriodic_ = nullptr;  ///< every 2 minutes
  QString lastRecoveryFile_;
  QPointer<QObject> sketchSession_;
  QMetaObject::Connection sketchConn_;

  std::map<std::string, std::function<void()>> directActions_;
  RecentFiles recent_;
  QStringList recentCommands_;
  std::vector<std::string> echo_;
  bool interactive_ = true;
  bool acceptEnabled_ = true;
  bool stateRefreshPending_ = false;
  bool viewerInitialized_ = false;
  quint64 activation_ = 0;
  quint64 dialogActivation_ = 0;
  const void* lastKeyEvent_ = nullptr;
  quint64 lastKeyTimestamp_ = 0;
};

}  // namespace fabricad::app
