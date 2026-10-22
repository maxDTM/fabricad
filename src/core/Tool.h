// FabriCAD core: Tool/Command registry.
//
// Every user-facing command (Extrude, Line, Joint, Base View, Fit, Undo...) is an ITool
// registered in the ToolRegistry under a unique id ("part.extrude", "sketch.line").
// The UI builds toolbars, menus, the S-key search, marking menus and shortcuts from the
// registry; the ToolManager activates tools and routes viewer events to the active one.
//
// Lifecycle of an interactive tool:
//   run(id) -> isEnabled? -> activate(ctx)      [tool sets prompt, selection filter,
//                                                 shows dialog via createDialog()]
//        ... mouse/key events, selectionChanged(), commandLineInput() ...
//   accept(ctx) (OK / Enter)  -> tool pushes an UndoCommand, then ctx.finish()
//   cancel(ctx) (Esc / Cancel) -> ctx.cancel()
//   deactivate(ctx) is always called last; ToolManager clears previews/highlights.
// One-shot tools (Fit, Undo, Toggle visibility) override isInstant() and do their work in
// activate(); the manager finishes them immediately.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/ShapeRef.h"
#include "core/Types.h"

#include <QObject>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class QWidget;

namespace fabricad::core {

class Document;
class SelectionModel;
class UndoStack;
class IViewerContext;
class IAppHost;
class ToolManager;

enum class MouseButton { None, Left, Middle, Right };
enum KeyModifier : int { ModNone = 0, ModShift = 1, ModCtrl = 2, ModAlt = 4 };

/// Mouse event translated by the viewer into world terms.
struct ViewerMouseEvent {
  MouseButton button = MouseButton::None;  ///< pressed/released button (None for move)
  int buttons = 0;                         ///< bitmask of held buttons (1 L, 2 M, 4 R) during move
  int modifiers = ModNone;                 ///< KeyModifier bits
  int x = 0, y = 0;                        ///< widget pixels
  Vec3 rayOrigin, rayDir;                  ///< world ray through the pixel
  bool hasPlanePoint = false;              ///< true in sketch mode (or when a tool set a work plane)
  Vec3 planePoint;                         ///< ray intersected with the sketch plane (world coords), grid-snapped if enabled
  bool hasPick = false;                    ///< something pickable under the cursor
  ShapeRef pick;                           ///< the entity under the cursor (honouring the selection filter)
  Vec3 pickPoint;                          ///< hit point on that entity
  double wheelDelta = 0;                   ///< wheel steps (+ = away from user)
  int clickCount = 1;                      ///< 2 for double click
};

struct ViewerKeyEvent {
  int key = 0;         ///< Qt::Key value
  int modifiers = ModNone;
  std::string text;    ///< typed text (may be empty)
  bool autoRepeat = false;
};

/// Static description used to build the UI.
struct ToolInfo {
  std::string id;             ///< unique, "module.name" e.g. "sketch.line"
  std::string name;           ///< "Line"
  std::string tooltip;        ///< one-line description
  std::string icon;           ///< icon resource name (":/icons/sketch-line.svg" or bare name)
  std::string shortcut;       ///< "L", "Ctrl+Z", "Shift+J" (Qt key sequence syntax), may be empty
  std::vector<std::string> aliases;  ///< command-line aliases: "LINE", "L"
  Workspace workspace = Workspace::Design;
  std::string tab;            ///< toolbar tab: "SOLID", "SURFACE", "SKETCH" (contextual), "ASSEMBLE", "TOOLS", "DRAWING"...
  std::string group;          ///< toolbar group within the tab: "CREATE", "MODIFY", "CONSTRAINTS"...
  int order = 100;            ///< ordering within the group (lower first)
  bool promoted = false;      ///< show as a big icon in the toolbar (else dropdown only)
  bool repeatable = true;     ///< Enter/Space repeats it
  bool needsDocument = true;  ///< disabled when no document is open
  bool hidden = false;        ///< not in toolbars/search (internal/contextual tools)
  /// Instant tools with this flag (camera/display commands: Fit, Home, Look At, Shaded...)
  /// run without cancelling the active tool (ux.md 5: view commands never interrupt).
  bool keepsActiveTool = false;
  /// Checkable command (selection priority, selection filters): current state for menus and
  /// toolbars. Empty = not checkable. Commands sharing a non-empty `exclusiveGroup` behave
  /// like radio items (the ribbon renders them as an exclusive action group).
  std::function<bool()> checked;
  std::string exclusiveGroup;
};

/// What a tool may use while active. Created by ToolManager for each activation.
class ToolContext {
 public:
  Document* document = nullptr;
  SelectionModel* selection = nullptr;
  UndoStack* undo = nullptr;
  IViewerContext* viewer = nullptr;
  IAppHost* host = nullptr;
  ToolManager* manager = nullptr;
  /// Arguments from the command line / scripting ("EXTRUDE 10" -> {"10"}); empty normally.
  std::vector<std::string> args;
  /// Preselection at activation time (copy of the selection when the tool started).
  std::vector<ShapeRef> preselection;

  /// End the tool successfully (after the tool committed its command).
  void finish();
  /// End the tool without effect.
  void cancel();
  /// Convenience forwards (null-safe).
  void setPrompt(const std::string& text);
  void showMessage(const std::string& text, bool error = false);
  bool hasDocument() const { return document != nullptr; }
};

class ITool {
 public:
  virtual ~ITool() = default;
  virtual const ToolInfo& info() const = 0;

  /// Enabled state for toolbars (document present, right workspace, selection ok...).
  virtual bool isEnabled(const ToolContext& ctx) const { return !info().needsDocument || ctx.document != nullptr; }
  /// One-shot: activate() does the work, manager finishes immediately.
  virtual bool isInstant() const { return false; }

  virtual void activate(ToolContext& ctx) = 0;
  virtual void deactivate(ToolContext& ctx) { (void)ctx; }
  /// OK / Enter. Default: finish.
  virtual void accept(ToolContext& ctx) { ctx.finish(); }
  /// Esc / Cancel. Default: cancel.
  virtual void cancel(ToolContext& ctx) { ctx.cancel(); }

  // Viewer events. Return true when consumed (the viewer then skips default behaviour
  // such as selection on LMB). Navigation (MMB/wheel) is handled by the viewer before.
  virtual bool mousePress(ToolContext& ctx, const ViewerMouseEvent& e) { (void)ctx; (void)e; return false; }
  virtual bool mouseMove(ToolContext& ctx, const ViewerMouseEvent& e) { (void)ctx; (void)e; return false; }
  virtual bool mouseRelease(ToolContext& ctx, const ViewerMouseEvent& e) { (void)ctx; (void)e; return false; }
  virtual bool mouseDoubleClick(ToolContext& ctx, const ViewerMouseEvent& e) { (void)ctx; (void)e; return false; }
  virtual bool keyPress(ToolContext& ctx, const ViewerKeyEvent& e) { (void)ctx; (void)e; return false; }
  /// The SelectionModel changed (user picked something).
  virtual void selectionChanged(ToolContext& ctx) { (void)ctx; }
  /// The document recomputed or changed under the tool.
  virtual void documentChanged(ToolContext& ctx) { (void)ctx; }
  /// Typed text from the command line / dynamic input ("10", "@50<30", "x=20"). Return true if consumed.
  virtual bool commandLineInput(ToolContext& ctx, const std::string& text) { (void)ctx; (void)text; return false; }

  /// Command dialog body (fields only; the shell adds title + OK/Cancel). nullptr = no dialog.
  /// Called once per activation after activate(). Ownership passes to the caller.
  virtual QWidget* createDialog(ToolContext& ctx, QWidget* parent) { (void)ctx; (void)parent; return nullptr; }
  /// Whether the shell shows OK/Cancel buttons for this tool's dialog.
  virtual bool wantsDialogButtons() const { return true; }
};

/// Registration + queries. One per application.
class ToolRegistry {
 public:
  /// Takes ownership. Replaces a tool with the same id (returns false then).
  bool registerTool(std::unique_ptr<ITool> tool);
  ITool* tool(const std::string& id) const;
  bool has(const std::string& id) const { return tool(id) != nullptr; }
  std::vector<ITool*> tools() const;
  /// Tools for a toolbar: filtered by workspace/tab/group (empty string = any), sorted by order.
  std::vector<ITool*> toolsFor(Workspace ws, const std::string& tab = {}, const std::string& group = {}) const;
  /// Distinct tab names of a workspace in first-seen order; groups within a tab.
  std::vector<std::string> tabs(Workspace ws) const;
  std::vector<std::string> groups(Workspace ws, const std::string& tab) const;
  /// Fuzzy search over name/aliases/id/tooltip (case-insensitive, subsequence match), best first.
  std::vector<ITool*> search(const std::string& query, int maxResults = 20) const;
  /// Exact alias or id match (case-insensitive), nullptr if none.
  ITool* byAlias(const std::string& alias) const;
  ITool* byShortcut(const std::string& shortcut) const;

 private:
  std::vector<std::unique_ptr<ITool>> tools_;
  std::map<std::string, size_t> byId_;
};

/// Activation and event routing. One per main window.
/// Split a command line into words: white space separates, "..." / '...' keep spaces inside a word
/// (names with spaces, paths) and the quotes are dropped. Used by ToolManager::runCommandLine.
std::vector<std::string> splitCommandLine(const std::string& line);

class ToolManager : public QObject {
  Q_OBJECT
 public:
  explicit ToolManager(ToolRegistry& registry, QObject* parent = nullptr);
  ~ToolManager() override;

  ToolRegistry& registry() { return registry_; }
  void setDocument(Document* doc);       ///< also rewires selection signals
  void setViewer(IViewerContext* viewer);
  void setHost(IAppHost* host);
  Document* document() const { return document_; }
  IViewerContext* viewer() const { return viewer_; }
  IAppHost* host() const { return host_; }

  /// Activate a tool by id; cancels the active one first (except for instant tools flagged
  /// ToolInfo::keepsActiveTool, which run alongside it). False if unknown/disabled.
  bool run(const std::string& id, std::vector<std::string> args = {});
  /// Parse a command line: "EXTRUDE 10" -> run(alias EXTRUDE, {"10"}); when a tool is
  /// active, the text goes to its commandLineInput() instead. Empty text repeats the last tool.
  bool runCommandLine(const std::string& line);
  void accept();                ///< OK / Enter
  void cancel();                ///< Esc
  /// Re-run the last repeatable tool, without the numeric arguments of its last run.
  bool repeatLast();
  /// The arguments a repeat replays: text such as file paths; numbers / expressions and
  /// "edit <id>" are dropped (PART-07).
  static std::vector<std::string> repeatableArgs(const std::vector<std::string>& args);
  ITool* activeTool() const { return active_; }
  const std::string& activeToolId() const;
  const std::string& lastToolId() const { return lastToolId_; }
  /// Last repeatable tool outside the contextual SKETCH tab (the "last solid command" that the
  /// marking menu offers to repeat outside a sketch).
  const std::string& lastModelToolId() const { return lastModelToolId_; }
  bool isActive() const { return active_ != nullptr; }
  ToolContext* activeContext() { return active_ ? &context_ : nullptr; }

  // Called by the viewer. Each returns true if the active tool consumed the event.
  bool mousePress(const ViewerMouseEvent& e);
  bool mouseMove(const ViewerMouseEvent& e);
  bool mouseRelease(const ViewerMouseEvent& e);
  bool mouseDoubleClick(const ViewerMouseEvent& e);
  bool keyPress(const ViewerKeyEvent& e);
  /// Shortcut dispatch: "L" -> run("sketch.line") if that tool is enabled in the current workspace.
  /// Shortcuts are compared as normalised QKeySequence portable text ("Del" == "Delete",
  /// "Ctrl+Enter" == "Ctrl+Return").
  bool triggerShortcut(const std::string& shortcut);

  /// Fresh context for queries (isEnabled checks in toolbars).
  ToolContext makeContext() const;

 signals:
  /// Emitted before ITool::activate() runs.
  void toolActivated(const std::string& id);
  /// Emitted after ITool::activate() returned with the tool still active (build the dialog here).
  void toolStarted(const std::string& id);
  void toolFinished(const std::string& id, bool accepted);
  void promptChanged(const std::string& text);

 private:
  friend class ToolContext;
  void finishActive(bool accepted);
  void onSelectionChanged();
  void onDocumentChanged();

  ToolRegistry& registry_;
  Document* document_ = nullptr;
  IViewerContext* viewer_ = nullptr;
  IAppHost* host_ = nullptr;
  ITool* active_ = nullptr;
  ToolContext context_;
  std::string lastToolId_;
  std::string lastModelToolId_;
  std::vector<std::string> lastArgs_;
  bool finishing_ = false;
  std::vector<QMetaObject::Connection> docConnections_;
};

/// Small helper base: stores the ToolInfo and implements info().
class ToolBase : public ITool {
 public:
  explicit ToolBase(ToolInfo info) : info_(std::move(info)) {}
  const ToolInfo& info() const override { return info_; }
  ToolInfo& mutableInfo() { return info_; }

 protected:
  ToolInfo info_;
};

/// Instant tool from a lambda (Fit, Undo, Home view...).
class LambdaTool : public ToolBase {
 public:
  LambdaTool(ToolInfo info, std::function<void(ToolContext&)> fn, std::function<bool(const ToolContext&)> enabled = {})
      : ToolBase(std::move(info)), fn_(std::move(fn)), enabled_(std::move(enabled)) {}
  bool isInstant() const override { return true; }
  bool isEnabled(const ToolContext& ctx) const override { return enabled_ ? enabled_(ctx) : ToolBase::isEnabled(ctx); }
  void activate(ToolContext& ctx) override { if (fn_) fn_(ctx); }

 private:
  std::function<void(ToolContext&)> fn_;
  std::function<bool(const ToolContext&)> enabled_;
};

}  // namespace fabricad::core
