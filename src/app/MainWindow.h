// FabriCAD application shell. OWNER: ui agent.
//
// Fusion-style single window (docs/research/ux.md section 1):
//   application bar   [File v][Save][Undo][Redo]  [Bracket *][Lever][Bracket Drawing] [+]   [S][?]
//   ribbon            [DESIGN v] SOLID | SKETCH (contextual) | TOOLS ... with group dropdowns
//   browser (dock)  | viewport (OCCT, ViewCube, nav bar, command dialog overlay)
//   timeline (bottom dock, full width)
//   status bar        prompt | message | selection | sketch DOF | units | cmd> ______
//
// Multi-document tabs (DocumentTab): every tab owns its Document (undo stack, selection),
// ToolManager, viewer (camera), browser and timeline state; drawings open as their own tab
// linked to the design's document. The members document_/manager_/viewer_/browser_/... always
// point at the active tab's objects; activating another tab rewires the ribbon, command line,
// search, status bar and docks. MainWindow implements core::IAppHost, owns the ToolRegistry and
// calls each module's registerModule() plus registerAppTools()/registerScriptTools().
// Startup is fast: no GL before show(); main() calls initializeViewer() from the event loop.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "app/AppState.h"
#include "app/DocumentTab.h"
#include "core/AppHost.h"
#include "core/Document.h"
#include "core/Tool.h"

#include <QMainWindow>
#include <QPointer>

#include <functional>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>

class QDockWidget;
class QLabel;
class QLockFile;
class QMenu;
class QStackedWidget;
class QTabBar;
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

  // --- active tab's objects ---------------------------------------------------------------
  fabricad::core::Document& document() { return *document_; }
  fabricad::core::ToolRegistry& tools() { return registry_; }
  fabricad::core::ToolManager& toolManager() { return *manager_; }
  /// Null while a drawing tab is active.
  viewer::ViewerWidget* viewer() const { return viewer_; }
  Ribbon* ribbon() const { return ribbon_; }
  BrowserTree* browser() const { return browser_; }
  TimelineWidget* timeline() const { return timeline_; }
  CommandDialog* commandDialog() const { return dialog_; }
  CommandLine* commandLine() const { return commandLine_; }
  CommandSearch* commandSearch() const { return search_; }
  RecentFiles& recentFiles() { return recent_; }

  // --- document tabs ------------------------------------------------------------------------
  DocumentTab* activeTab() const { return active_; }
  const std::vector<std::unique_ptr<DocumentTab>>& tabs() const { return tabs_; }
  int tabCount() const { return static_cast<int>(tabs_.size()); }
  DocumentTab* tabAt(int index) const;
  int indexOfTab(const DocumentTab* tab) const;
  QTabBar* tabBar() const { return tabBar_; }
  /// New design tab (empty document when null); activates it.
  DocumentTab* addDesignTab(std::shared_ptr<core::Document> doc = nullptr);
  /// The drawing tab of a design (created on demand); activates it.
  DocumentTab* openDrawingTab(DocumentTab& design);
  /// Design tab showing `doc`, or its first drawing tab when `kind` says so (nullptr if none).
  DocumentTab* tabForDocument(const core::Document& doc, DocumentTab::Kind kind) const;
  /// Switch tabs (cancels the active command of the tab being left, finishes an open sketch).
  void activateTab(DocumentTab* tab);
  void activateTab(int index) { activateTab(tabAt(index)); }
  /// Tab by 1-based number or (case-insensitive) title; nullptr if none.
  DocumentTab* findTab(const std::string& spec) const;
  /// Close a tab. Closing a design tab (or a document's last tab) closes the document: one
  /// prompt for unsaved changes, then its drawing tabs close too. Returns false if the user
  /// cancelled. The window always keeps one (possibly new) tab.
  bool closeTab(DocumentTab* tab);
  /// Show or hide the browser panel and remember it for design tabs (drawings hide it).
  void setBrowserVisible(bool on);
  QDockWidget* browserDock() const { return browserDock_; }
  bool closeTab(int index) { return closeTab(tabAt(index)); }

  /// Create the OCCT GL view of the active tab (call once the window is shown). Tabs created
  /// or activated afterwards get theirs too.
  void initializeViewer();
  /// Headless/scripted mode: no modal dialogs (askYesNo returns the default, file dialogs
  /// are skipped, unsaved-changes prompts answer "discard").
  void setInteractive(bool on) { interactive_ = on; }
  bool isInteractive() const { return interactive_; }

  // --- file operations -----------------------------------------------------------------
  /// New design in a new tab (Fusion: File > New Design).
  bool newDocument();
  bool open();
  /// .fcad loads into a new tab (a pristine, never-touched empty tab is reused); .step/.stp/.stl
  /// are inserted into the active design through part.import-file (a new tab when it has content).
  bool openFile(const QString& path, QString* error = nullptr);
  bool save();
  bool saveAs();
  bool saveTo(const QString& path, QString* error = nullptr);
  /// Ask to save unsaved changes of the active document. False = the user cancelled.
  bool maybeSave();

  // --- autosave / crash recovery (README "Autosave") --------------------------------------
  /// Write a recovery copy of every modified document now (normally driven by timers: 5 s after
  /// an undoable change, every 2 minutes). Does not touch paths or modified flags. True when a
  /// copy was written.
  bool autosaveNow();
  /// Recovery file the active document autosaves to.
  QString recoveryFilePath() const;
  /// Remove the active document's recovery copy (after a clean save / close).
  void clearRecoveryFile();
  /// Offer to recover every autosave left behind by a previous (crashed) session, each into its
  /// own tab. Interactive only; FABRICAD_NO_RECOVERY=1 disables it. Returns true when at least
  /// one document was recovered.
  bool offerRecovery();
  static QString recoveryDirectory();
  /// This instance's own recovery folder (recovery/<session id>/, guarded by a lock file so other
  /// running instances leave it alone).
  QString sessionRecoveryDirectory() const;
  /// Recovery files of sessions that are no longer running (their lock is gone or stale), plus
  /// legacy files directly in recoveryDirectory(); newest first. Live instances are skipped.
  QStringList orphanedRecoveryFiles() const;
  /// Recover one autosave into a tab (the pristine start-up tab or a new one). The recovered
  /// document is marked modified and autosaved to this session right away; the old file is
  /// removed only once that copy exists. Unreadable files are removed. True on success.
  bool recoverFile(const QString& path, QString* error = nullptr);

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
  /// OK for the active command (Enter / dialog OK button). False (with a message) when no
  /// command is active or its dialog has OK disabled.
  bool acceptActiveTool();
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
  /// Design -> Drawing opens (or activates) the drawing tab linked to the active design;
  /// Drawing -> Design activates the design tab of the drawing's document.
  void setWorkspace(fabricad::core::Workspace ws) override;
  void setContextualTab(const std::string& tabName, bool active) override;
  void echoCommand(const std::string& text) override;
  QWidget* mainWindow() override { return this; }
  void refreshToolDialog() override;
  void setToolAcceptEnabled(bool enabled) override;

 signals:
  /// Every showMessage() call (scripting log). `level` is a core::MessageLevel.
  void messageShown(int level, const QString& text);
  void activeTabChanged(DocumentTab* tab);

 protected:
  void closeEvent(QCloseEvent* e) override;
  bool eventFilter(QObject* o, QEvent* e) override;

 private:
  void registerModules();
  void buildUi();
  QWidget* buildAppBar();
  void buildFileMenu(QMenu* menu);
  void buildStatusBar();
  void connectShellSignals();
  void connectTabSignals(DocumentTab& tab);
  DocumentTab* createTab(DocumentTab::Kind kind, std::shared_ptr<core::Document> doc);
  void destroyTab(DocumentTab* tab);
  void syncActivePointers();
  void refreshTabBar();
  bool maybeSave(DocumentTab& tab);
  bool autosave(DocumentTab& tab);
  bool ensureSessionRecoveryDirectory();
  void releaseSessionRecoveryDirectory();
  QString recoveryFilePath(const DocumentTab& tab) const;
  void clearRecoveryFile(DocumentTab& tab);
  void clearStatusMessage();
  void scheduleTabBarRefresh();
  void clearTimelineFrameUnlessSelected();
  bool tabDragging() const;
  void closeOneTab(DocumentTab* tab);  ///< close one tab without prompting
  void onToolActivated(const std::string& id);
  void onToolFinished(const std::string& id, bool accepted);
  void presentDialog();
  void updateTitle();
  void updateUndoButtons();
  void updateSelectionStatus();
  void scheduleSelectionStatus();
  void updateSketchStatus();
  void scheduleStateRefresh();
  void scheduleFailureReport();
  /// Show the first feature that newly failed since the last check in the status bar (DOC-08).
  void reportNewFailures(bool announce);
  void applyUiState(DocumentTab& tab);
  void storeUiState(DocumentTab& tab);
  void scheduleAutosave();
  void afterDocumentReplaced();
  void viewCubeMenu(const QPoint& globalPos);
  void layoutOverlays();
  bool isCanvasFocus(QWidget* focus) const;
  bool isIdleTool() const;

  fabricad::core::ToolRegistry registry_;
  std::vector<std::unique_ptr<DocumentTab>> tabs_;
  DocumentTab* active_ = nullptr;
  // the active tab's objects (never null for document_/manager_/dialog_; viewer_/browser_/
  // timeline_ are null in drawing tabs)
  fabricad::core::Document* document_ = nullptr;
  fabricad::core::ToolManager* manager_ = nullptr;
  fabricad::core::Workspace workspace_ = fabricad::core::Workspace::Design;
  viewer::ViewerWidget* viewer_ = nullptr;
  BrowserTree* browser_ = nullptr;
  TimelineWidget* timeline_ = nullptr;
  CommandDialog* dialog_ = nullptr;
  NavBar* navBar_ = nullptr;
  SketchPalette* palette_ = nullptr;

  Ribbon* ribbon_ = nullptr;
  QDockWidget* browserDock_ = nullptr;
  QStackedWidget* browserStack_ = nullptr;
  QDockWidget* timelineDock_ = nullptr;
  QStackedWidget* timelineStack_ = nullptr;
  QStackedWidget* central_ = nullptr;
  CommandLine* commandLine_ = nullptr;
  CommandSearch* search_ = nullptr;
  MarkingMenu* marking_ = nullptr;
  QToolButton* undoButton_ = nullptr;
  QToolButton* redoButton_ = nullptr;
  QTabBar* tabBar_ = nullptr;
  QLabel* promptLabel_ = nullptr;
  QLabel* messageLabel_ = nullptr;
  core::MessageLevel messageLevel_ = core::MessageLevel::Info;  ///< level of the text in messageLabel_
  const core::Document* failureDoc_ = nullptr;  ///< feature failure on display (cleared once fixed)
  core::ObjectId failureFeature_ = core::kInvalidId;
  QString failureText_;
  bool tabDragActive_ = false;          ///< left button down on the tab bar (a tab may be dragged)
  bool tabBarRefreshDeferred_ = false;  ///< a tab bar refresh waits for the release
  bool tabBarRefreshPending_ = false;
  bool timelineFrameFollowsSelection_ = true;  ///< false while a "Find in Timeline" frame shows
  QLabel* selectionLabel_ = nullptr;
  QLabel* sketchLabel_ = nullptr;
  QLabel* unitsLabel_ = nullptr;
  QTimer* messageTimer_ = nullptr;
  QTimer* autosaveTimer_ = nullptr;     ///< single shot, 5 s after an undoable change
  QTimer* autosavePeriodic_ = nullptr;  ///< every 2 minutes
  QString sessionId_;                   ///< names this instance's recovery folder
  std::unique_ptr<QLockFile> sessionLock_;
  QPointer<QObject> sketchSession_;
  QMetaObject::Connection sketchConn_;

  std::map<std::string, std::function<void()>> directActions_;
  RecentFiles recent_;
  QStringList recentCommands_;
  std::vector<std::string> echo_;
  bool interactive_ = true;
  bool acceptEnabled_ = true;
  bool stateRefreshPending_ = false;
  bool failureReportPending_ = false;
  std::map<const core::Document*, std::set<core::ObjectId>> failedFeatures_;  ///< DOC-08 baseline
  bool selectionStatusPending_ = false;  ///< coalesces rename/recompute -> status label
  bool glEnabled_ = false;        ///< initializeViewer() was called: new tabs get a GL view too
  bool browserDockWanted_ = true; ///< user's dock visibility (hidden automatically in drawing tabs)
  bool timelineDockWanted_ = true; ///< user's timeline visibility (design tabs only)
  bool switchingTabs_ = false;
  quint64 activation_ = 0;
  quint64 dialogActivation_ = 0;
  const void* lastKeyEvent_ = nullptr;
  quint64 lastKeyTimestamp_ = 0;
};

}  // namespace fabricad::app
