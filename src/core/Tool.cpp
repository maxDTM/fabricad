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
  if (active_ && tool->isInstant() && tool->info().keepsActiveTool) {
    // Non-interrupting command (view/display): run beside the active tool with its own context.
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

bool ToolManager::runCommandLine(const std::string& line) {
  std::istringstream in(line);
  std::string first;
  in >> first;
  if (first.empty()) return repeatLast();
  if (active_) {
    if (active_->commandLineInput(context_, line)) return true;
  }
  ITool* tool = registry_.byAlias(first);
  if (!tool) {
    if (host_) host_->showMessage("Unknown command: " + first, MessageLevel::Warning);
    return false;
  }
  std::vector<std::string> args;
  std::string a;
  while (in >> a) args.push_back(a);
  return run(tool->info().id, std::move(args));
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
    viewer_->setHighlighted({});
    viewer_->setCursor(CursorShape::Arrow);
    if (document_) viewer_->setSelectionFilter(kAllKinds);
  }
  if (document_) document_->selection().setFilter(kAllKinds);
  if (host_) host_->setPrompt({});
  active_ = nullptr;
  if (tool->info().repeatable) {
    lastToolId_ = id;
    lastArgs_ = context_.args;
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
