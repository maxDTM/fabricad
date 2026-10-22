// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/Tool.h"

#include "core/AppHost.h"
#include "core/Document.h"
#include "core/Log.h"
#include "core/Selection.h"
#include "core/UndoStack.h"
#include "core/ViewerContext.h"

#include <QKeySequence>

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>

namespace fabricad::core {

namespace {
std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

/// Canonical spelling of a shortcut ("Del" -> "Delete", "ctrl+enter" -> "Ctrl+Return"), lower-cased.
std::string normalizedShortcut(const std::string& s) {
  if (s.empty()) return {};
  QKeySequence seq = QKeySequence::fromString(QString::fromStdString(s), QKeySequence::PortableText);
  if (seq.isEmpty()) return lower(s);
  return lower(seq.toString(QKeySequence::PortableText).toStdString());
}

/// Subsequence fuzzy score: higher is better, <0 = no match.
int fuzzyScore(const std::string& queryLower, const std::string& textLower) {
  if (queryLower.empty()) return 0;
  if (textLower == queryLower) return 1000;
  if (textLower.rfind(queryLower, 0) == 0) return 800 - static_cast<int>(textLower.size());
  size_t pos = textLower.find(queryLower);
  if (pos != std::string::npos) return 600 - static_cast<int>(pos) - static_cast<int>(textLower.size());
  // subsequence
  size_t ti = 0;
  int gaps = 0;
  for (char qc : queryLower) {
    size_t found = textLower.find(qc, ti);
    if (found == std::string::npos) return -1;
    gaps += static_cast<int>(found - ti);
    ti = found + 1;
  }
  return 300 - gaps - static_cast<int>(textLower.size());
}
}  // namespace

// ---------------------------------------------------------------------------
// ToolContext
// ---------------------------------------------------------------------------

void ToolContext::finish() {
  if (manager) manager->finishActive(true);
}

void ToolContext::cancel() {
  if (manager) manager->finishActive(false);
}

void ToolContext::setPrompt(const std::string& text) {
  if (host) host->setPrompt(text);
  if (manager) emit manager->promptChanged(text);
}

void ToolContext::showMessage(const std::string& text, bool error) {
  if (host) host->showMessage(text, error ? MessageLevel::Error : MessageLevel::Info);
  else log(error ? LogLevel::Error : LogLevel::Info, text);
}

// ---------------------------------------------------------------------------
// ToolRegistry
// ---------------------------------------------------------------------------

bool ToolRegistry::registerTool(std::unique_ptr<ITool> tool) {
  if (!tool) return false;
  const std::string id = tool->info().id;
  auto it = byId_.find(id);
  if (it != byId_.end()) {
    FC_WARN("tool '{}' registered twice; replacing", id);
    tools_[it->second] = std::move(tool);
    return false;
  }
  byId_[id] = tools_.size();
  tools_.push_back(std::move(tool));
  return true;
}

ITool* ToolRegistry::tool(const std::string& id) const {
  auto it = byId_.find(id);
  return it == byId_.end() ? nullptr : tools_[it->second].get();
}

std::vector<ITool*> ToolRegistry::tools() const {
  std::vector<ITool*> out;
  for (const auto& t : tools_) out.push_back(t.get());
  return out;
}

std::vector<ITool*> ToolRegistry::toolsFor(Workspace ws, const std::string& tab, const std::string& group) const {
  std::vector<ITool*> out;
  for (const auto& t : tools_) {
    const ToolInfo& i = t->info();
    if (i.hidden || i.workspace != ws) continue;
    if (!tab.empty() && i.tab != tab) continue;
    if (!group.empty() && i.group != group) continue;
    out.push_back(t.get());
  }
  std::stable_sort(out.begin(), out.end(), [](ITool* a, ITool* b) { return a->info().order < b->info().order; });
  return out;
}

std::vector<std::string> ToolRegistry::tabs(Workspace ws) const {
  std::vector<std::string> out;
  for (ITool* t : toolsFor(ws)) {
    const std::string& tab = t->info().tab;
    if (tab.empty()) continue;
    if (std::find(out.begin(), out.end(), tab) == out.end()) out.push_back(tab);
  }
  return out;
}

std::vector<std::string> ToolRegistry::groups(Workspace ws, const std::string& tab) const {
  std::vector<std::string> out;
  for (ITool* t : toolsFor(ws, tab)) {
    const std::string& g = t->info().group;
    if (g.empty()) continue;
    if (std::find(out.begin(), out.end(), g) == out.end()) out.push_back(g);
  }
  return out;
}

std::vector<ITool*> ToolRegistry::search(const std::string& query, int maxResults) const {
  std::string q = lower(query);
  std::vector<std::pair<int, ITool*>> scored;
  for (const auto& t : tools_) {
    const ToolInfo& i = t->info();
    if (i.hidden) continue;
    int best = fuzzyScore(q, lower(i.name));
    for (const auto& a : i.aliases) best = std::max(best, fuzzyScore(q, lower(a)) - 10);
    best = std::max(best, fuzzyScore(q, lower(i.id)) - 50);
    if (!i.tooltip.empty()) best = std::max(best, fuzzyScore(q, lower(i.tooltip)) - 200);
    if (best >= 0) scored.emplace_back(best, t.get());
  }
  std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
  std::vector<ITool*> out;
  for (const auto& [s, t] : scored) {
    if (static_cast<int>(out.size()) >= maxResults) break;
    out.push_back(t);
  }
  return out;
}

ITool* ToolRegistry::byAlias(const std::string& alias) const {
  std::string a = lower(alias);
  for (const auto& t : tools_) {
    if (lower(t->info().id) == a) return t.get();
    for (const auto& al : t->info().aliases)
      if (lower(al) == a) return t.get();
  }
  return nullptr;
}

ITool* ToolRegistry::byShortcut(const std::string& shortcut) const {
  std::string s = normalizedShortcut(shortcut);
  for (const auto& t : tools_)
    if (!t->info().shortcut.empty() && normalizedShortcut(t->info().shortcut) == s) return t.get();
  return nullptr;
}

// ---------------------------------------------------------------------------
// ToolManager
// ---------------------------------------------------------------------------

ToolManager::ToolManager(ToolRegistry& registry, QObject* parent) : QObject(parent), registry_(registry) {}

ToolManager::~ToolManager() {
  if (active_) finishActive(false);
}

void ToolManager::setDocument(Document* doc) {
  if (active_) finishActive(false);
  for (auto& c : docConnections_) QObject::disconnect(c);
  docConnections_.clear();
  document_ = doc;
  if (document_) {
    docConnections_.push_back(connect(&document_->selection(), &SelectionModel::changed, this, &ToolManager::onSelectionChanged));
    docConnections_.push_back(connect(document_, &Document::recomputeFinished, this, &ToolManager::onDocumentChanged));
    docConnections_.push_back(connect(document_, &Document::documentReset, this, &ToolManager::onDocumentChanged));
  }
}

void ToolManager::setViewer(IViewerContext* viewer) { viewer_ = viewer; }
void ToolManager::setHost(IAppHost* host) { host_ = host; }

ToolContext ToolManager::makeContext() const {
  ToolContext ctx;
  ctx.document = document_;
  ctx.selection = document_ ? &document_->selection() : nullptr;
  ctx.undo = document_ ? &document_->undoStack() : nullptr;
  ctx.viewer = viewer_;
  ctx.host = host_;
  ctx.manager = const_cast<ToolManager*>(this);
  return ctx;
}

const std::string& ToolManager::activeToolId() const {
  static const std::string kNone;
  return active_ ? active_->info().id : kNone;
}

bool ToolManager::run(const std::string& id, std::vector<std::string> args) {
  ITool* tool = registry_.tool(id);
  if (!tool) {
    FC_WARN("unknown tool '{}'", id);
    return false;
  }
  ToolContext ctx = makeContext();
  if (!tool->isEnabled(ctx)) return false;
  if (tool->isInstant() && tool->info().keepsActiveTool) {
    // Non-interrupting command (view/display, shell verbs such as SELECT / INFO / CHECK): runs
    // beside the active tool with its own context and never becomes the active tool itself, so
    // it neither cancels a command nor disturbs the selection, prompt or filter of the idle state.
    ctx.args = std::move(args);
    if (ctx.selection) ctx.preselection = ctx.selection->items();
    if (host_) host_->echoCommand(id);
    tool->activate(ctx);
    return true;
  }
  if (active_) finishActive(false);

  context_ = ctx;
  context_.args = std::move(args);
  if (context_.selection) context_.preselection = context_.selection->items();
  active_ = tool;
  if (host_) host_->echoCommand(id);
  emit toolActivated(id);
  tool->activate(context_);
  if (active_ == tool && tool->isInstant()) {
    finishActive(true);
    return true;
  }
  if (active_ == tool) emit toolStarted(id);
  return true;
}

std::vector<std::string> splitCommandLine(const std::string& line) {
  // words separated by white space; "..." or '...' keeps spaces inside one word (names with
  // spaces, file paths), the quotes themselves are dropped; a quote glued to a word
  // (body:"LegFrame (2)") continues that word
  std::vector<std::string> words;
  std::string cur;
  bool inWord = false;
  char quote = 0;
  for (char ch : line) {
    if (quote) {
      if (ch == quote) quote = 0;
      else cur += ch;
      continue;
    }
    if (ch == '"' || ch == '\'') {
      quote = ch;
      inWord = true;
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(ch))) {
      if (inWord) words.push_back(cur);
      cur.clear();
      inWord = false;
      continue;
    }
    cur += ch;
    inWord = true;
  }
  if (inWord) words.push_back(cur);
  return words;
}

bool ToolManager::runCommandLine(const std::string& line) {
  const std::vector<std::string> words = splitCommandLine(line);
  std::string first = words.empty() ? std::string() : words.front();
  if (first.empty()) return repeatLast();
  if (active_) {
    // Shell verbs (OK, CANCEL, SELECT, PICK, FIT...): instant tools flagged keepsActiveTool run
    // beside the active tool and are never offered to it as typed input (a tool that consumes
    // any text, e.g. a name prompt, would otherwise swallow them).
    if (ITool* shell = registry_.byAlias(first); shell && shell->isInstant() && shell->info().keepsActiveTool)
      return run(shell->info().id, std::vector<std::string>(words.begin() + 1, words.end()));
    // A command name (FINISH, EXTRUDE 10, C) starts that command and ends the active one, as in
    // Fusion; everything else (values, coordinates, name=value, keywords) is typed input for the
    // active command. A document parameter that happens to share a name with an alias stays input.
    const bool isParameter = document_ && document_->parameter(first) != nullptr;
    if (ITool* other = registry_.byAlias(first); !other || isParameter) {
      if (active_->commandLineInput(context_, line)) return true;
    }
  }
  ITool* tool = registry_.byAlias(first);
  if (!tool) {
    if (host_) host_->showMessage("Unknown command: " + first, MessageLevel::Warning);
    return false;
  }
  // An alias shared by several tools (FILLET: sketch fillet / solid fillet, MIRROR, POINT...)
  // means the one that is enabled in the current context.
  const ToolContext probe = makeContext();
  if (!tool->isEnabled(probe)) {
    std::string lowered = first;
    for (char& ch : lowered) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    for (ITool* t : registry_.tools()) {
      if (t == tool || !t->isEnabled(probe)) continue;
      bool match = false;
      for (const std::string& al : t->info().aliases) {
        std::string l = al;
        for (char& ch : l) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        match = match || l == lowered;
      }
      if (match) {
        tool = t;
        break;
      }
    }
    if (!tool->isEnabled(probe)) {
      if (host_) host_->showMessage(tool->info().name + " is not available now", MessageLevel::Warning);
      return false;
    }
  }
  return run(tool->info().id, std::vector<std::string>(words.begin() + 1, words.end()));
}

void ToolManager::accept() {
  if (!active_) return;
  active_->accept(context_);
}

void ToolManager::cancel() {
  if (!active_) {
    if (document_) document_->selection().clear();
    return;
  }
  active_->cancel(context_);
}

std::vector<std::string> ToolManager::repeatableArgs(const std::vector<std::string>& args) {
  // G3 PART-07: repeating a command (Enter / Space / marking-menu Repeat) starts it fresh:
  // numeric values typed for the last run ("EXTRUDE -8", "OFFSETPLANE -12") are not replayed
  // into the new dialog, nor is "edit <id>". Text arguments (file paths) are kept.
  if (!args.empty() && args.front() == "edit") return {};
  std::vector<std::string> out;
  for (const std::string& a : args) {
    if (a.empty()) continue;
    const char c = a.front();
    const bool numeric = std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.' || c == '(' ||
                         c == '@' || c == '=';
    if (!numeric) out.push_back(a);
  }
  return out;
}

bool ToolManager::repeatLast() {
  if (lastToolId_.empty()) return false;
  ITool* t = registry_.tool(lastToolId_);
  if (!t || !t->info().repeatable) return false;
  return run(lastToolId_, lastArgs_);
}

void ToolManager::finishActive(bool accepted) {
  if (!active_ || finishing_) return;
  finishing_ = true;
  ITool* tool = active_;
  std::string id = tool->info().id;
  tool->deactivate(context_);
  if (viewer_) {
    viewer_->clearPreviews();
    viewer_->clearManipulators();
    viewer_->setHighlighted({});
    viewer_->setCursor(CursorShape::Arrow);
  }
  if (document_) document_->selection().setFilter(kAllKinds);
  // idle: the user's selection filters apply again
  if (viewer_ && document_) viewer_->setSelectionFilter(document_->selection().effectiveFilter());
  if (host_) host_->setPrompt({});
  active_ = nullptr;
  if (tool->info().repeatable) {
    lastToolId_ = id;
    lastArgs_ = repeatableArgs(context_.args);
    if (tool->info().tab != "SKETCH") lastModelToolId_ = id;
  }
  finishing_ = false;
  emit toolFinished(id, accepted);
}

void ToolManager::onSelectionChanged() {
  if (active_) active_->selectionChanged(context_);
}

void ToolManager::onDocumentChanged() {
  if (active_) active_->documentChanged(context_);
}

bool ToolManager::mousePress(const ViewerMouseEvent& e) { return active_ ? active_->mousePress(context_, e) : false; }
bool ToolManager::mouseMove(const ViewerMouseEvent& e) { return active_ ? active_->mouseMove(context_, e) : false; }
bool ToolManager::mouseRelease(const ViewerMouseEvent& e) { return active_ ? active_->mouseRelease(context_, e) : false; }
bool ToolManager::mouseDoubleClick(const ViewerMouseEvent& e) { return active_ ? active_->mouseDoubleClick(context_, e) : false; }

bool ToolManager::keyPress(const ViewerKeyEvent& e) {
  // Esc while a gizmo is being dragged aborts the drag only (the value returns to what it was
  // at DragBegin); the command and its dialog stay open. A second Esc cancels the command.
  if (e.key == Qt::Key_Escape && viewer_ && viewer_->isManipulatorDragging()) {
    viewer_->manipulatorCancel();
    return true;
  }
  if (active_ && active_->keyPress(context_, e)) return true;
  return false;
}

bool ToolManager::triggerShortcut(const std::string& shortcut) {
  Workspace ws = host_ ? host_->workspace() : Workspace::Design;
  std::string s = normalizedShortcut(shortcut);
  // Prefer tools of the current workspace; contextual SKETCH tools win when in sketch mode.
  ITool* best = nullptr;
  int bestRank = std::numeric_limits<int>::min();
  ToolContext ctx = makeContext();
  for (ITool* t : registry_.tools()) {
    if (t->info().shortcut.empty() || normalizedShortcut(t->info().shortcut) != s) continue;
    if (!t->isEnabled(ctx)) continue;
    int rank = 0;
    if (t->info().workspace == ws) rank += 2;
    if (viewer_ && viewer_->inSketchMode() && t->info().tab == "SKETCH") rank += 4;
    if (!(viewer_ && viewer_->inSketchMode()) && t->info().tab == "SKETCH") rank -= 3;
    if (rank > bestRank) {
      bestRank = rank;
      best = t;
    }
  }
  if (!best) return false;
  return run(best->info().id);
}

}  // namespace fabricad::core
