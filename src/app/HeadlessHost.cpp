// FabriCAD app: headless script host.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/HeadlessHost.h"

#include "app/AppActions.h"
#include "app/AppTools.h"
#include "app/DocumentTab.h"
#include "app/PreferencesDialog.h"
#include "app/ScriptTools.h"
#include "assembly/AssemblyModule.h"
#include "core/Document.h"
#include "core/Feature.h"
#include "core/Json.h"
#include "core/Registry.h"
#include "core/Selection.h"
#include "core/UndoStack.h"
#include "core/ViewerContext.h"
#include "drawing/DrawingModule.h"
#include "part/PartModule.h"
#include "sketch/SketchModule.h"
#include "sketch/SketchSession.h"

#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>

#include <algorithm>
#include <format>

namespace fabricad::app {

using core::ObjectId;
using core::ShapeRef;

struct HeadlessHost::Tab {
  std::shared_ptr<core::Document> doc;
  std::unique_ptr<core::ToolManager> manager;
  std::unique_ptr<core::NullViewerContext> viewer;  ///< design tabs (as an offscreen ViewerWidget)
  bool drawing = false;
};

namespace {

QString canonicalPath(const QString& path) {
  const QFileInfo fi(path);
  const QString c = fi.canonicalFilePath();
  return c.isEmpty() ? QDir::cleanPath(fi.absoluteFilePath()) : c;
}

// as MainWindow (DOC-07): features that did not compute after a load
std::string failedFeatureSummary(const core::Document& doc) {
  std::vector<std::string> names;
  for (int i = 0; i < doc.featureCount(); ++i) {
    const core::Feature* f = doc.featureAt(i);
    if (f && f->status() == core::FeatureStatus::Error) names.push_back(f->name());
  }
  if (names.empty()) return {};
  std::string list;
  for (size_t i = 0; i < names.size() && i < 5; ++i) list += (i ? ", " : "") + names[i];
  if (names.size() > 5) list += std::format(" and {} more", names.size() - 5);
  return std::format("{} feature{} failed to compute: {}", names.size(), names.size() == 1 ? "" : "s", list);
}

bool isPristine(const core::Document& doc, long useCount) {
  return doc.featureCount() == 0 && !doc.isModified() && doc.filePath().empty() && isAutoDocumentName(doc.name()) &&
         useCount == 1 && !const_cast<core::Document&>(doc).undoStack().canUndo();
}

}  // namespace

HeadlessHost::HeadlessHost(QObject* parent) : QObject(parent) {
  core::ModuleContext ctx{core::FeatureFactory::instance(), registry_};
  sketch::registerModule(ctx);
  part::registerModule(ctx);
  assembly::registerModule(ctx);
  drawing::registerModule(ctx);
  registerAppTools(*this, registry_);
  registerScriptTools(*this, registry_);
  activateTab(addTab(nullptr, false));
}

HeadlessHost::~HeadlessHost() {
  if (sketch::SketchSession* s = sketch::SketchSession::current())
    for (const auto& t : tabs_)
      if (s->manager() == t->manager.get()) sketch::SketchSession::end();
  for (const auto& t : tabs_)
    if (t->manager->isActive()) t->manager->cancel();
}

int HeadlessHost::addTab(std::shared_ptr<core::Document> doc, bool drawing) {
  auto t = std::make_unique<Tab>();
  t->doc = doc ? std::move(doc) : std::make_shared<core::Document>();
  t->drawing = drawing;
  t->manager = std::make_unique<core::ToolManager>(registry_);
  t->manager->setDocument(t->doc.get());
  if (!drawing) {
    t->viewer = std::make_unique<core::NullViewerContext>();
    t->manager->setViewer(t->viewer.get());
  }
  t->manager->setHost(this);
  core::ToolManager* m = t->manager.get();
  connect(m, &core::ToolManager::toolActivated, this, [this](const std::string&) { acceptEnabled_ = true; });
  connect(m, &core::ToolManager::promptChanged, this, [this](const std::string& p) { prompt_ = p; });
  connect(m, &core::ToolManager::toolFinished, this, [this, m](const std::string& id, bool accepted) {
    acceptEnabled_ = true;
    // Fusion (as MainWindow): "New Drawing" opens the drawing document in its own tab
    if (accepted && id == "drawing.create" && active_ >= 0 && active().manager.get() == m && !active().drawing) {
      const std::shared_ptr<core::Document> doc = active().doc;
      QMetaObject::invokeMethod(this, [this, doc] {
        int t = tabFor(*doc, true);
        if (t < 0) t = addTab(doc, true);
        activateTab(t);
      }, Qt::QueuedConnection);
    }
  });
  tabs_.push_back(std::move(t));
  return static_cast<int>(tabs_.size()) - 1;
}

int HeadlessHost::tabFor(const core::Document& doc, bool drawing) const {
  for (int i = 0; i < tabCount(); ++i)
    if (tab(i).doc.get() == &doc && tab(i).drawing == drawing) return i;
  return -1;
}

void HeadlessHost::activateTab(int index) {
  if (index < 0 || index >= tabCount() || index == active_) return;
  if (active_ >= 0) {
    // leave the previous tab as MainWindow does: its command is cancelled, its sketch finished
    core::ToolManager* pm = active().manager.get();
    if (sketch::SketchSession* s = sketch::SketchSession::current(); s && s->manager() == pm) sketch::SketchSession::end();
    if (pm->isActive()) pm->cancel();
  }
  active_ = index;
  prompt_.clear();
  acceptEnabled_ = true;
}

core::Document& HeadlessHost::document() { return *active().doc; }
core::ToolManager& HeadlessHost::toolManager() { return *active().manager; }

core::Workspace HeadlessHost::workspace() const {
  return active_ >= 0 && active().drawing ? core::Workspace::Drawing : core::Workspace::Design;
}

void HeadlessHost::setWorkspace(core::Workspace ws) {
  if (active_ < 0 || ws == workspace()) return;
  const std::shared_ptr<core::Document> doc = active().doc;
  const bool drawing = ws == core::Workspace::Drawing;
  int t = tabFor(*doc, drawing);
  if (t < 0) t = addTab(doc, drawing);
  activateTab(t);
}

void HeadlessHost::showMessage(const std::string& text, core::MessageLevel level) {
  if (sink_) sink_(static_cast<int>(level), QString::fromStdString(text));
}

bool HeadlessHost::isIdleTool() const {
  const core::ITool* t = active_ >= 0 ? active().manager->activeTool() : nullptr;
  return t && t->info().hidden && t->info().id.ends_with(".select");  // as MainWindow
}

// ---- files ----------------------------------------------------------------------------------------

bool HeadlessHost::newDocument() {
  // "Untitled", "Untitled 2", ...: the lowest number no open document uses (DOC-10)
  std::set<std::string> used;
  for (const auto& t : tabs_) used.insert(t->doc->name());
  std::string name = "Untitled";
  for (int n = 2; used.count(name); ++n) name = "Untitled " + std::to_string(n);
  activateTab(addTab(nullptr, false));
  core::Document& doc = document();
  if (doc.name() != name) {
    doc.setName(name);
    doc.setModified(false);
  }
  if (auto u = defaultUnitsPreference(); u && !(*u == doc.units())) {
    doc.setUnits(*u);
    doc.setModified(false);
  }
  if (active().viewer) active().viewer->setStandardView(core::StandardView::Home, false);
  return true;
}

bool HeadlessHost::openFile(const QString& path, QString* error) {
  const QString suffix = QFileInfo(path).suffix().toLower();
  if (!QFileInfo::exists(path)) {
    if (error) *error = "file not found";
    return false;
  }
  if (suffix == "step" || suffix == "stp" || suffix == "stl") {
    if (!registry_.has("part.import-file")) {
      if (error) *error = "import is not available";
      return false;
    }
    if (active().drawing) activateTab(addTab(nullptr, false));
    const bool ok = toolManager().run("part.import-file", {path.toStdString()});
    if (ok && isAutoDocumentName(document().name())) document().setName(QFileInfo(path).completeBaseName().toStdString());
    return ok;
  }
  // already open in another tab: go there
  const QString canonical = canonicalPath(path);
  for (int i = 0; i < tabCount(); ++i)
    if (!tab(i).drawing && !tab(i).doc->filePath().empty() && canonicalPath(QString::fromStdString(tab(i).doc->filePath())) == canonical) {
      activateTab(i);
      return true;
    }
  int created = -1;
  if (active().drawing || !isPristine(*active().doc, active().doc.use_count())) {
    created = addTab(nullptr, false);
    activateTab(created);
  }
  if (toolManager().isActive()) toolManager().cancel();
  core::Document& doc = document();
  std::string err;
  if (!doc.load(canonical.toStdString(), &err)) {
    if (error) *error = QString::fromStdString(err);
    doc.clear();
    doc.setModified(false);
    if (created >= 0 && tabCount() > 1) closeTab(created);
    return false;
  }
  if (isAutoDocumentName(doc.name())) doc.setName(QFileInfo(path).completeBaseName().toStdString());
  doc.setModified(false);
  reportNewFailures(false);
  if (const std::string failed = failedFeatureSummary(doc); !failed.empty())
    showMessage("Opened " + QFileInfo(path).fileName().toStdString() + " with problems: " + failed, core::MessageLevel::Warning);
  return true;
}

bool HeadlessHost::saveTo(const QString& p, QString* error) {
  QString path = p;
  if (QFileInfo(path).suffix().isEmpty()) path += ".fcad";
  path = canonicalPath(path);
  core::Document& doc = document();
  // the document name follows the file unless the user named it (as MainWindow::saveTo)
  const std::string oldName = doc.name();
  const bool wasModified = doc.isModified();
  const std::string oldStem = doc.filePath().empty() ? std::string() : QFileInfo(QString::fromStdString(doc.filePath())).completeBaseName().toStdString();
  if (isAutoDocumentName(oldName) || (!oldStem.empty() && oldName == oldStem)) doc.setName(QFileInfo(path).completeBaseName().toStdString());
  std::string err;
  if (!doc.save(path.toStdString(), &err)) {
    if (error) *error = QString::fromStdString(err);
    if (doc.name() != oldName) doc.setName(oldName);
    doc.setModified(wasModified);
    return false;
  }
  showMessage("Saved " + path.toStdString(), core::MessageLevel::Info);
  return true;
}

bool HeadlessHost::save() {
  if (document().filePath().empty()) {
    showMessage("Save: the design has no file yet (SAVE <file.fcad>)", core::MessageLevel::Error);
    return false;
  }
  QString err;
  if (!saveTo(QString::fromStdString(document().filePath()), &err)) {
    showMessage("Save failed: " + err.toStdString(), core::MessageLevel::Error);
    return false;
  }
  return true;
}

// ---- edit -----------------------------------------------------------------------------------------

void HeadlessHost::deleteRefs(const std::vector<ShapeRef>& refs) {
  core::Document& doc = document();
  const DeletePlan plan = planDelete(doc, refs);
  if (plan.isEmpty()) {
    if (!refs.empty()) showMessage("Nothing to delete in the selection", core::MessageLevel::Info);
    return;
  }
  for (ObjectId id : plan.features) doc.selection().removeOwner(id);
  int keptBodies = 0;
  for (ObjectId c : plan.components)
    for (ObjectId b : doc.bodiesOf(c))
      if (!doc.bodyShape(b).IsNull()) ++keptBodies;
  if (auto cmd = makeDeleteCommand(doc, plan)) {
    const std::string summary = plan.summary(doc);
    cmd->setText("Delete " + summary);
    doc.selection().clear();
    doc.undoStack().push(std::move(cmd));
    if (keptBodies > 0)
      showMessage(std::format("Deleted {}; kept {} bod{} (moved to the parent component)", summary, keptBodies, keptBodies == 1 ? "y" : "ies"),
                  core::MessageLevel::Warning);
  }
}

void HeadlessHost::deleteSelection() {
  if (sketch::SketchSession* s = sketch::SketchSession::current();
      s && s->manager() == active().manager.get() && active().viewer && active().viewer->inSketchMode())
    if (s->deleteSelection()) return;
  deleteRefs(document().selection().items());
}

void HeadlessHost::toggleSelectionVisibility() {
  if (auto cmd = toggleVisibility(document(), nullptr, document().selection().items())) document().undoStack().push(std::move(cmd));
}

void HeadlessHost::showAllObjects() {
  if (auto cmd = showAll(document(), nullptr)) document().undoStack().push(std::move(cmd));
}

// ---- the active command -----------------------------------------------------------------------------

bool HeadlessHost::acceptActiveTool() {
  core::ToolManager& m = toolManager();
  if (!m.isActive() || isIdleTool()) {
    showMessage("OK: no command is active", core::MessageLevel::Warning);
    return false;
  }
  if (!acceptEnabled_) {
    showMessage("OK is disabled for " + m.activeTool()->info().name, core::MessageLevel::Error);
    return false;
  }
  m.accept();
  return true;
}

void HeadlessHost::reportNewFailures(bool announce) {
  if (active_ < 0) return;
  core::Document& doc = document();
  if (announce && toolManager().isActive() && !isIdleTool()) return;  // wait for the command's result
  std::set<ObjectId> now;
  const core::Feature* first = nullptr;
  std::set<ObjectId>& before = failed_[&doc];
  for (int i = 0; i < doc.featureCount(); ++i) {
    const core::Feature* f = doc.featureAt(i);
    if (!f || f->status() != core::FeatureStatus::Error) continue;
    now.insert(f->id());
    if (!first && !before.count(f->id())) first = f;
  }
  before = std::move(now);
  if (!announce || !first) return;
  std::string msg = first->name() + " failed";
  if (!first->statusMessage().empty()) msg += ": " + first->statusMessage();
  showMessage(msg, core::MessageLevel::Error);
}

bool HeadlessHost::submitCommandLine(const QString& text) {
  QString t = text.trimmed();
  while (t.startsWith(':')) t = t.mid(1).trimmed();
  core::ToolManager& m = toolManager();
  bool ok = true;
  if (t.isEmpty() && m.isActive()) {
    // an empty Enter belongs to the active command (APP-06)
    if (!isIdleTool() && acceptEnabled_) m.accept();
  } else {
    ok = m.runCommandLine(t.toStdString());
  }
  // queued work of the line (NEW, OPEN, DRAWING, TAB, CLOSE) runs before the failure check
  QCoreApplication::sendPostedEvents(this);
  reportNewFailures(true);
  return ok;
}

// ---- tabs -------------------------------------------------------------------------------------------

AppShell::TabInfo HeadlessHost::tabInfo(int index) const {
  TabInfo info;
  if (index < 0 || index >= tabCount()) return info;
  const Tab& t = tab(index);
  info.design = !t.drawing;
  info.filePath = t.doc->filePath();
  info.title = QString::fromStdString(t.doc->name());
  if (t.drawing) info.title += " Drawing";
  if (t.doc->isModified()) info.title += " *";
  return info;
}

int HeadlessHost::findTabIndex(const std::string& spec) const {
  if (spec.empty()) return -1;
  try {
    size_t used = 0;
    const int n = std::stoi(spec, &used);
    if (used == spec.size()) return n >= 1 && n <= tabCount() ? n - 1 : -1;
  } catch (...) {
  }
  const QString q = QString::fromStdString(spec);
  for (int i = 0; i < tabCount(); ++i) {
    QString title = tabInfo(i).title;
    if (title.endsWith(" *")) title.chop(2);
    if (title.compare(q, Qt::CaseInsensitive) == 0) return i;
  }
  for (int i = 0; i < tabCount(); ++i)
    if (!tab(i).drawing && QString::fromStdString(tab(i).doc->name()).compare(q, Qt::CaseInsensitive) == 0) return i;
  return -1;
}

void HeadlessHost::closeTabLater(int index) {
  if (index < 0 || index >= tabCount()) return;
  Tab* target = tabs_[static_cast<size_t>(index)].get();
  QMetaObject::invokeMethod(this, [this, target] {
    for (int i = 0; i < tabCount(); ++i)
      if (tabs_[static_cast<size_t>(i)].get() == target) {
        closeTab(i);
        return;
      }
  }, Qt::QueuedConnection);
}

void HeadlessHost::activateTabLater(int index) {
  if (index < 0 || index >= tabCount()) return;
  Tab* target = tabs_[static_cast<size_t>(index)].get();
  QMetaObject::invokeMethod(this, [this, target] {
    for (int i = 0; i < tabCount(); ++i)
      if (tabs_[static_cast<size_t>(i)].get() == target) {
        activateTab(i);
        return;
      }
  }, Qt::QueuedConnection);
}

void HeadlessHost::closeTab(int index) {
  if (index < 0 || index >= tabCount()) return;
  // closing a design closes the document, its drawings included (as MainWindow::closeTab)
  const core::Document* doc = tab(index).doc.get();
  const bool closesDocument = !tab(index).drawing || [&] {
    int n = 0;
    for (const auto& t : tabs_) n += t->doc.get() == doc;
    return n == 1;
  }();
  std::vector<Tab*> group;
  for (const auto& t : tabs_)
    if (closesDocument ? t->doc.get() == doc : t.get() == tabs_[static_cast<size_t>(index)].get()) group.push_back(t.get());
  Tab* activeTab = active_ >= 0 ? tabs_[static_cast<size_t>(active_)].get() : nullptr;
  for (Tab* t : group) {
    if (sketch::SketchSession* s = sketch::SketchSession::current(); s && s->manager() == t->manager.get()) sketch::SketchSession::end();
    if (t->manager->isActive()) t->manager->cancel();
  }
  // the neighbour to the left of the active tab takes over (Fusion)
  int pos = -1;
  for (int i = 0; i < tabCount(); ++i)
    if (tabs_[static_cast<size_t>(i)].get() == activeTab) pos = i;
  for (Tab* t : group)
    for (size_t i = 0; i < tabs_.size(); ++i)
      if (tabs_[i].get() == t) {
        tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(i));
        break;
      }
  if (closesDocument) failed_.erase(doc);
  if (tabs_.empty()) {
    active_ = -1;
    newDocument();
    return;
  }
  active_ = -1;
  for (int i = 0; i < tabCount(); ++i)
    if (tabs_[static_cast<size_t>(i)].get() == activeTab) active_ = i;
  if (active_ < 0) {
    const int next = std::clamp(pos - 1, 0, tabCount() - 1);
    active_ = next;
    prompt_.clear();
    acceptEnabled_ = true;
  }
}

}  // namespace fabricad::app
