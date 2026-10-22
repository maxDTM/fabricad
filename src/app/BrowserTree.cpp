// FabriCAD app: Browser tree.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/BrowserTree.h"

#include "core/Materials.h"

#include "assembly/tools/ComponentTools.h"

#include "assembly/ComponentOps.h"

#include "app/AppActions.h"
#include "app/Icons.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Json.h"
#include "core/Selection.h"
#include "viewer/ViewerWidget.h"

#include <QContextMenuEvent>
#include <QHeaderView>
#include <QKeyEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMenu>
#include <QScrollBar>
#include <QTimer>

#include <algorithm>
#include <functional>

#include <format>

namespace fabricad::app {

using core::ObjectId;
using core::ShapeKind;
using core::ShapeRef;

namespace {
QString key(const char* prefix, ObjectId id) { return QString("%1:%2").arg(prefix).arg(id); }
const char* kOriginNames[] = {"O", "X", "Y", "Z", "XY", "XZ", "YZ"};
}  // namespace

BrowserTree::BrowserTree(core::Document& doc, viewer::ViewerWidget* viewer, QWidget* parent)
    : QTreeWidget(parent), doc_(doc), viewer_(viewer) {
  setObjectName("Browser");
  setColumnCount(2);
  setHeaderHidden(true);
  header()->setStretchLastSection(false);
  header()->setSectionResizeMode(0, QHeaderView::Stretch);
  header()->setSectionResizeMode(1, QHeaderView::Fixed);
  header()->resizeSection(1, 24);
  setIconSize(QSize(16, 16));
  setSelectionMode(QAbstractItemView::ExtendedSelection);
  // UX-38: no implicit edit triggers. Rename only through F2 / the context menu / double-click on
  // bodies and components (startRename); a double-click on a sketch or feature only edits it.
  setEditTriggers(QAbstractItemView::NoEditTriggers);
  setMouseTracking(true);
  setUniformRowHeights(true);
  setIndentation(14);
  setFocusPolicy(Qt::StrongFocus);

  connect(this, &QTreeWidget::itemClicked, this, &BrowserTree::onItemClicked);
  connect(this, &QTreeWidget::itemSelectionChanged, this, &BrowserTree::onSelectionFromTree);
  connect(this, &QTreeWidget::itemChanged, this, &BrowserTree::onItemRenamed);
  connect(this, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int column) {
    if (column == 1 || !item) return;
    const int kind = item->data(0, RoleKind).toInt();
    if (kind == KindFeature) emit editFeatureRequested(item->data(0, RoleId).toULongLong());
    else if (kind == KindBody || kind == KindComponent || kind == KindRoot) startRename(item);
  });
  connect(this, &QTreeWidget::itemEntered, this, [this](QTreeWidgetItem* item, int) {
    const QString k = item ? item->data(0, RoleKey).toString() : QString();
    if (k == hoverKey_) return;
    hoverKey_ = k;
    ShapeRef r = refForItem(item);
    doc_.selection().setPreselection(r.isValid() ? std::optional<ShapeRef>(r) : std::nullopt);
  });
  connect(this, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* i) {
    if (!rebuilding_) collapsed_.erase(i->data(0, RoleKey).toString());
  });
  connect(this, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* i) {
    if (!rebuilding_) collapsed_.insert(i->data(0, RoleKey).toString());
  });

  auto reb = [this] { scheduleRebuild(); };
  auto rebId = [this](ObjectId) { scheduleRebuild(); };
  connect(&doc_, &core::Document::featureAdded, this, [this](ObjectId, int) { scheduleRebuild(); });
  connect(&doc_, &core::Document::featureRemoved, this, [this](ObjectId, int) { scheduleRebuild(); });
  connect(&doc_, &core::Document::featureMoved, this, [this](ObjectId, int, int) { scheduleRebuild(); });
  connect(&doc_, &core::Document::featureChanged, this, rebId);
  connect(&doc_, &core::Document::featureStatusChanged, this, rebId);
  connect(&doc_, &core::Document::rollbackChanged, this, [this](int) { scheduleRebuild(); });
  connect(&doc_, &core::Document::bodyAdded, this, rebId);
  connect(&doc_, &core::Document::bodyRemoved, this, rebId);
  connect(&doc_, &core::Document::bodyChanged, this, rebId);
  connect(&doc_, &core::Document::componentAdded, this, rebId);
  connect(&doc_, &core::Document::componentRemoved, this, rebId);
  connect(&doc_, &core::Document::componentChanged, this, rebId);
  connect(&doc_, &core::Document::activeComponentChanged, this, rebId);
  connect(&doc_, &core::Document::documentReset, this, reb);
  connect(&doc_, &core::Document::nameChanged, this, reb);
  connect(&doc_.selection(), &core::SelectionModel::changed, this, &BrowserTree::syncSelectionFromModel);
  if (viewer_) connect(viewer_, &viewer::ViewerWidget::featureVisibilityChanged, this, reb);
  rebuildNow();
}

void BrowserTree::scheduleRebuild() {
  if (pending_) return;
  pending_ = true;
  QTimer::singleShot(0, this, [this] {
    if (pending_) rebuildNow();
  });
}

void BrowserTree::collectExpanded(QTreeWidgetItem* item, std::set<QString>& out) const {
  if (!item) return;
  if (item->isExpanded()) out.insert(item->data(0, RoleKey).toString());
  for (int i = 0; i < item->childCount(); ++i) collectExpanded(item->child(i), out);
}

void BrowserTree::updateEye(QTreeWidgetItem* item, bool hasEye, bool visible) {
  if (!hasEye) return;
  item->setIcon(1, resourceIcon(visible ? "eye" : "eye-off"));
  item->setToolTip(1, visible ? "Hide" : "Show");
  item->setData(1, Qt::UserRole, visible);
  if (!visible) item->setForeground(0, QColor("#9AA0A8"));
}

bool BrowserTree::folderVisible(ObjectId compId, const QString& folder) const {
  if (folder == "Origin") return viewer_ && viewer_->isOriginVisible();
  if (folder == "Bodies") {
    const auto bodies = doc_.bodiesOf(compId);
    if (bodies.empty()) return true;
    return std::any_of(bodies.begin(), bodies.end(), [this](ObjectId b) { return doc_.body(b) && doc_.body(b)->visible; });
  }
  return !viewer_ || viewer_->isFolderVisible(folder.toStdString());
}

QTreeWidgetItem* BrowserTree::addFolder(QTreeWidgetItem* parent, ObjectId compId, const QString& name, bool eye,
                                        bool visible) {
  auto* f = new QTreeWidgetItem(parent);
  f->setText(0, name);
  f->setIcon(0, resourceIcon("folder"));
  f->setData(0, RoleKind, KindFolder);
  f->setData(0, RoleId, QVariant::fromValue<qulonglong>(compId));
  f->setData(0, RoleFolder, name);
  f->setData(0, RoleKey, QString("folder:%1:%2").arg(compId).arg(name));
  f->setFlags(Qt::ItemIsEnabled);
  updateEye(f, eye, visible);
  return f;
}

void BrowserTree::fillComponent(QTreeWidgetItem* node, ObjectId compId, bool root) {
  const core::Component* comp = doc_.component(compId);
  if (!comp) return;
  if (root) {
    QTreeWidgetItem* origin = addFolder(node, compId, "Origin", true, folderVisible(compId, "Origin"));
    for (const char* n : kOriginNames) {
      auto* o = new QTreeWidgetItem(origin);
      const std::string s = n;
      o->setText(0, s == "O" ? "Origin Point" : s.size() == 1 ? QString("%1 Axis").arg(n) : QString("%1 Plane").arg(n));
      o->setIcon(0, resourceIcon(s == "O" ? "point" : s.size() == 1 ? "axis" : "plane"));
      o->setData(0, RoleKind, KindOrigin);
      o->setData(0, RoleOrigin, QString(n));
      o->setData(0, RoleKey, QString("origin:%1").arg(n));
    }
  }
  // bodies of this component
  const std::vector<ObjectId> bodies = doc_.bodiesOf(compId);
  std::vector<ObjectId> live;
  for (ObjectId b : bodies)
    if (!doc_.bodyShape(b).IsNull()) live.push_back(b);
  // the root shows an (empty) Bodies folder only while it has no child components (Fusion)
  if (!live.empty() || (root && comp->children.empty())) {
    QTreeWidgetItem* bf = addFolder(node, compId, "Bodies", !live.empty(), folderVisible(compId, "Bodies"));
    for (ObjectId id : live) {
      const core::Body* b = doc_.body(id);
      auto* it = new QTreeWidgetItem(bf);
      it->setText(0, core::qs(b->name));
      it->setIcon(0, resourceIcon("body"));
      it->setData(0, RoleKind, KindBody);
      it->setData(0, RoleId, QVariant::fromValue<qulonglong>(id));
      it->setData(0, RoleKey, key("body", id));
      it->setFlags(it->flags() | Qt::ItemIsEditable);
      if (!b->material.empty()) it->setToolTip(0, core::qs(b->name + " - " + b->material));
      updateEye(it, true, b->visible);
    }
  }
  {
    // features by browser folder, under the component they were created in (Fusion);
    // features without a (live) component are listed under the root
    for (const char* folderName : {"Sketches", "Construction", "Joints"}) {
      std::vector<const core::Feature*> fs;
      for (const auto& f : doc_.features()) {
        if (f->browserFolder() != folderName) continue;
        const ObjectId owner = f->component();
        const bool orphan = owner == core::kInvalidId || !doc_.component(owner);
        if (owner == compId || (root && orphan)) fs.push_back(f.get());
      }
      if (fs.empty()) continue;
      const bool fv = folderVisible(compId, folderName);
      QTreeWidgetItem* folder = addFolder(node, compId, folderName, true, fv);
      for (const core::Feature* f : fs) {
        auto* it = new QTreeWidgetItem(folder);
        it->setText(0, core::qs(f->name()));
        it->setIcon(0, featureIcon(*f));
        it->setData(0, RoleKind, KindFeature);
        it->setData(0, RoleId, QVariant::fromValue<qulonglong>(f->id()));
        it->setData(0, RoleKey, key("feat", f->id()));
        it->setFlags(it->flags() | Qt::ItemIsEditable);
        const bool active = doc_.isFeatureActive(f->id());
        QString tip = core::qs(f->name());
        if (!f->statusMessage().empty()) tip += "\n" + core::qs(f->statusMessage());
        it->setToolTip(0, tip);
        if (f->status() == core::FeatureStatus::Error) {
          it->setForeground(0, QColor("#D93025"));
          it->setIcon(0, resourceIcon("error"));
        } else if (f->status() == core::FeatureStatus::Warning) {
          it->setForeground(0, QColor("#B07C00"));
          it->setIcon(0, resourceIcon("warning"));
        }
        if (!active || f->isSuppressed()) {
          QFont ft = it->font(0);
          ft.setItalic(true);
          ft.setStrikeOut(f->isSuppressed());
          it->setFont(0, ft);
          it->setForeground(0, QColor("#9AA0A8"));
        }
        updateEye(it, true, viewer_ ? !viewer_->isFeatureHidden(f->id()) && fv : true);
      }
    }
  }
  // child components
  for (ObjectId childId : comp->children) {
    const core::Component* c = doc_.component(childId);
    if (!c) continue;
    auto* it = new QTreeWidgetItem(node);
    // Fusion labels every occurrence: "Bracket:1", "Bracket:2" (componentLabel adds the suffix
    // for occurrences; a plain component is its first occurrence)
    std::string label = assembly::componentLabel(doc_, childId);
    if (!c->isOccurrence()) label += ":1";
    it->setText(0, core::qs(label));
    it->setIcon(0, resourceIcon(c->grounded ? "assembly-ground" : "component"));  // pin on grounded components
    it->setData(0, RoleKind, KindComponent);
    it->setData(0, RoleId, QVariant::fromValue<qulonglong>(childId));
    it->setData(0, RoleKey, key("comp", childId));
    it->setFlags(it->flags() | Qt::ItemIsEditable);
    QString tip = core::qs(c->name);
    if (c->grounded) tip += " (grounded)";
    it->setToolTip(0, tip);
    if (doc_.activeComponent() == childId) {
      QFont f = it->font(0);
      f.setBold(true);
      it->setFont(0, f);
    }
    updateEye(it, true, c->visible);
    fillComponent(it, childId, false);
  }
}

void BrowserTree::rebuildNow() {
  pending_ = false;
  rebuilding_ = true;
  const QSignalBlocker block(this);
  const int scroll = verticalScrollBar() ? verticalScrollBar()->value() : 0;
  clear();
  auto* root = new QTreeWidgetItem(this);
  root->setText(0, core::qs(doc_.name()) + (doc_.isModified() ? " *" : ""));
  root->setIcon(0, resourceIcon("document"));
  root->setData(0, RoleKind, KindRoot);
  root->setData(0, RoleId, QVariant::fromValue<qulonglong>(core::kRootComponentId));
  root->setData(0, RoleKey, QString("comp:%1").arg(core::kRootComponentId));
  root->setFlags(root->flags() | Qt::ItemIsEditable);
  if (doc_.activeComponent() == core::kRootComponentId) {
    QFont f = root->font(0);
    f.setBold(true);
    root->setFont(0, f);
  }
  fillComponent(root, core::kRootComponentId, true);
  if (firstBuild_) {
    // Fusion default: folders collapsed except the bodies and sketches of the root
    firstBuild_ = false;
    collapsed_.insert(QString("folder:%1:Origin").arg(core::kRootComponentId));
    collapsed_.insert(QString("folder:%1:Construction").arg(core::kRootComponentId));
    collapsed_.insert(QString("folder:%1:Joints").arg(core::kRootComponentId));
  }
  std::function<void(QTreeWidgetItem*)> expand = [&](QTreeWidgetItem* it) {
    const QString k = it->data(0, RoleKey).toString();
    // origin children and leaves stay as they are; new folders open unless collapsed by the user
    if (it->childCount() > 0) it->setExpanded(!collapsed_.count(k));
    for (int i = 0; i < it->childCount(); ++i) expand(it->child(i));
  };
  expand(root);
  if (verticalScrollBar()) verticalScrollBar()->setValue(scroll);
  rebuilding_ = false;
  syncSelectionFromModel();
}

QTreeWidgetItem* BrowserTree::itemForKey(const QString& k) const {
  std::function<QTreeWidgetItem*(QTreeWidgetItem*)> find = [&](QTreeWidgetItem* it) -> QTreeWidgetItem* {
    if (it->data(0, RoleKey).toString() == k) return it;
    for (int i = 0; i < it->childCount(); ++i)
      if (QTreeWidgetItem* r = find(it->child(i))) return r;
    return nullptr;
  };
  for (int i = 0; i < topLevelItemCount(); ++i)
    if (QTreeWidgetItem* r = find(topLevelItem(i))) return r;
  return nullptr;
}

ShapeRef BrowserTree::refForItem(const QTreeWidgetItem* item) const {
  if (!item) return {};
  const ObjectId id = item->data(0, RoleId).toULongLong();
  switch (item->data(0, RoleKind).toInt()) {
    case KindBody: return ShapeRef::body(id);
    case KindComponent:
    case KindRoot: return ShapeRef::component(id);
    case KindFeature:
      if (const core::Feature* f = doc_.feature(id)) return featureRef(*f);
      return {};
    case KindOrigin: {
      const std::string n = item->data(0, RoleOrigin).toString().toStdString();
      if (n == "O") return ShapeRef::originPoint();
      return n.size() == 1 ? ShapeRef::originAxis(n) : ShapeRef::originPlane(n);
    }
    default: return {};
  }
}

void BrowserTree::setFolderVisible(ObjectId compId, const QString& folder, bool visible) {
  if (folder == "Origin") {
    if (viewer_) viewer_->setOriginVisible(visible);
  } else if (folder == "Bodies") {
    auto macro = std::make_unique<core::MacroCommand>(visible ? "Show bodies" : "Hide bodies");
    for (ObjectId b : doc_.bodiesOf(compId))
      if (doc_.body(b) && doc_.body(b)->visible != visible)
        macro->add(core::makeBodyEditCommand(doc_, b, [visible](core::Body& body) { body.visible = visible; }));
    if (!macro->empty()) doc_.undoStack().push(std::move(macro));
  } else if (viewer_) {
    viewer_->setFolderVisible(folder.toStdString(), visible);
  }
  scheduleRebuild();
}

void BrowserTree::toggleVisibility(QTreeWidgetItem* item) {
  if (!item || !item->data(1, Qt::UserRole).isValid()) return;
  const bool visible = item->data(1, Qt::UserRole).toBool();
  const ObjectId id = item->data(0, RoleId).toULongLong();
  switch (item->data(0, RoleKind).toInt()) {
    case KindFolder: setFolderVisible(id, item->data(0, RoleFolder).toString(), !visible); break;
    case KindBody:
    case KindComponent:
      if (auto cmd = app::toggleVisibility(doc_, viewer_, {refForItem(item)})) doc_.undoStack().push(std::move(cmd));
      break;
    case KindFeature:
      if (viewer_) {
        // a feature inside a hidden folder: showing it shows the folder too (Fusion behaviour)
        const core::Feature* f = doc_.feature(id);
        if (f && !visible && !viewer_->isFolderVisible(f->browserFolder())) {
          viewer_->setFolderVisible(f->browserFolder(), true);
          viewer_->setFeatureHidden(id, false);
        } else {
          viewer_->setFeatureHidden(id, visible);
        }
      }
      break;
    default: break;
  }
  scheduleRebuild();
}

void BrowserTree::onItemClicked(QTreeWidgetItem* item, int column) {
  if (column == 1) toggleVisibility(item);
}

void BrowserTree::onSelectionFromTree() {
  if (syncing_ || rebuilding_) return;
  std::vector<ShapeRef> refs;
  for (QTreeWidgetItem* it : selectedItems())
    if (ShapeRef r = refForItem(it); r.isValid()) refs.push_back(r);
  syncing_ = true;
  doc_.selection().set(refs);
  syncing_ = false;
}

void BrowserTree::syncSelectionFromModel() {
  if (syncing_) return;
  syncing_ = true;
  const QSignalBlocker block(this);
  clearSelection();
  for (const ShapeRef& r : doc_.selection().items()) {
    QString k;
    if (r.kind == ShapeKind::Body) k = key("body", r.owner);
    else if (r.kind == ShapeKind::Component) k = key("comp", r.owner);
    else if (ObjectId f = featureOfRef(r); f != core::kInvalidId) k = key("feat", f);
    else if (core::hasKind(ShapeKind::OriginPlane | ShapeKind::OriginAxis, r.kind)) k = "origin:" + core::qs(r.name);
    else if (r.kind == ShapeKind::OriginPoint) k = "origin:O";
    if (k.isEmpty()) continue;
    if (QTreeWidgetItem* it = itemForKey(k)) it->setSelected(true);
  }
  syncing_ = false;
  viewport()->update();
}

void BrowserTree::onItemRenamed(QTreeWidgetItem* item, int column) {
  if (rebuilding_ || column != 0 || !item) return;
  std::string name = item->text(0).trimmed().toStdString();
  const ObjectId id = item->data(0, RoleId).toULongLong();
  std::unique_ptr<core::UndoCommand> cmd;
  switch (item->data(0, RoleKind).toInt()) {
    case KindFeature: cmd = makeRenameFeature(doc_, id, name); break;
    case KindBody: cmd = makeRenameBody(doc_, id, name); break;
    case KindRoot:
      if (name.size() > 2 && name.ends_with(" *")) name.resize(name.size() - 2);
      cmd = makeRenameComponent(doc_, id, name);
      break;
    case KindComponent:
      if (const core::Component* c = doc_.component(id); c && name == assembly::componentLabel(doc_, id)) break;
      if (name.size() > 2 && name.ends_with(":1")) name.resize(name.size() - 2);
      cmd = makeRenameComponent(doc_, id, name);
      break;
    default: break;
  }
  if (cmd) doc_.undoStack().push(std::move(cmd));
  scheduleRebuild();
}

void BrowserTree::scrollToFeature(ObjectId id) {
  if (QTreeWidgetItem* it = itemForKey(key("feat", id))) {
    for (QTreeWidgetItem* p = it->parent(); p; p = p->parent()) p->setExpanded(true);
    scrollToItem(it);
    setCurrentItem(it);
  }
}

void BrowserTree::contextMenuEvent(QContextMenuEvent* e) {
  QTreeWidgetItem* item = itemAt(e->pos());
  if (!item) return;
  if (!item->isSelected()) {
    clearSelection();
    item->setSelected(true);
  }
  const int kind = item->data(0, RoleKind).toInt();
  const ObjectId id = item->data(0, RoleId).toULongLong();
  QMenu menu(this);
  if (item->data(1, Qt::UserRole).isValid()) {
    const bool vis = item->data(1, Qt::UserRole).toBool();
    menu.addAction(resourceIcon(vis ? "eye-off" : "eye"), vis ? "Hide\tV" : "Show\tV", this,
                   [this, item] { toggleVisibility(item); });
  }
  if (kind == KindFeature) {
    const core::Feature* f = doc_.feature(id);
    menu.addAction(resourceIcon("feature"), f && f->type() == "Sketch" ? "Edit Sketch" : "Edit Feature", this,
                   [this, id] { emit editFeatureRequested(id); });
    if (f)
      menu.addAction(f->isSuppressed() ? "Unsuppress" : "Suppress", this,
                     [this, id, s = !f->isSuppressed()] { emit suppressRequested(id, s); });
    menu.addAction("Find in Timeline", this, [this, id] { emit findInTimelineRequested(id); });
  }
  if (kind == KindComponent || kind == KindRoot) {
    menu.addAction(resourceIcon("component"), "Activate Component", this, [this, id] { doc_.setActiveComponent(id); });
    if (kind == KindComponent) {
      const core::Component* c = doc_.component(id);
      // through the tools, so the status message and the joint recompute run as from the toolbar
      if (c)
        menu.addAction(resourceIcon(c->grounded ? "assembly-unground" : "assembly-ground"),
                       c->grounded ? "Unground" : "Ground", this,
                       [this, g = !c->grounded] { emit runToolRequested(g ? "assembly.ground" : "assembly.unground"); });
      menu.addAction(resourceIcon("visibility"), "Isolate", this, [this] { emit runToolRequested("app.isolate"); });
      menu.addAction("Copy\tCtrl+C", this, [this] { emit runToolRequested("assembly.copy"); });
    }
    bool canPaste = false;
    for (ObjectId cid : assembly::componentClipboard())
      if (doc_.component(cid)) canPaste = true;
    QAction* paste = menu.addAction("Paste\tCtrl+V", this, [this, id] {
      doc_.setActiveComponent(id);  // paste into the clicked component
      emit runToolRequested("assembly.paste");
    });
    paste->setEnabled(canPaste);
    QAction* pasteNew = menu.addAction("Paste New\tCtrl+Shift+V", this, [this, id] {
      doc_.setActiveComponent(id);  // paste into the clicked component
      emit runToolRequested("assembly.paste-new");
    });
    pasteNew->setEnabled(canPaste);
    menu.addAction("Properties...", this, [this, id] { editComponentProperties(id); });
  }
  if (kind == KindBody) {
    menu.addAction(resourceIcon("component"), "Create Components from Bodies", this,
                   [this] { emit runToolRequested("assembly.components-from-bodies"); });
    menu.addAction(resourceIcon("visibility"), "Isolate", this, [this] { emit runToolRequested("app.isolate"); });
    addMaterialMenu(menu, id);
    menu.addAction("Appearance...", this, [this, id] { pickBodyAppearance(id); });
  }
  if (kind == KindFeature || kind == KindBody || kind == KindComponent || kind == KindRoot)
    menu.addAction("Rename\tF2", this, [this, item] { startRename(item); });
  if (kind == KindFeature || kind == KindBody || kind == KindComponent) {
    menu.addSeparator();
    menu.addAction(resourceIcon("delete"), "Delete\tDel", this, [this] {
      std::vector<ShapeRef> refs;
      for (QTreeWidgetItem* it : selectedItems())
        if (ShapeRef r = refForItem(it); r.isValid()) refs.push_back(r);
      emit deleteRequested(refs);
    });
  }
  if (!menu.isEmpty()) menu.exec(e->globalPos());
}

void BrowserTree::addMaterialMenu(QMenu& menu, ObjectId bodyId) {
  const core::Body* b = doc_.body(bodyId);
  if (!b) return;
  QMenu* mat = menu.addMenu(b->material.empty() ? "Physical Material" : QString("Physical Material (%1)").arg(core::qs(b->material)));
  for (const std::string& cat : core::materialCategories()) {
    QMenu* sub = mat->addMenu(core::qs(cat));
    for (const core::Material& m : core::materialLibrary()) {
      if (m.category != cat) continue;
      QAction* a = sub->addAction(core::qs(std::format("{}  ({:.2f} g/cm3)", m.name, m.density)), this, [this, bodyId, name = m.name] {
        if (auto cmd = makeSetBodyMaterial(doc_, bodyId, name, 0.0, true)) doc_.undoStack().push(std::move(cmd));
      });
      a->setCheckable(true);
      a->setChecked(m.name == b->material);
    }
  }
  mat->addSeparator();
  mat->addAction("Custom...", this, [this, bodyId] {
    const core::Body* body = doc_.body(bodyId);
    if (!body) return;
    QDialog dlg(this);
    dlg.setWindowTitle("Custom material");
    auto* form = new QFormLayout(&dlg);
    auto* name = new QLineEdit(core::qs(body->material), &dlg);
    auto* density = new QLineEdit(QString::number(body->density), &dlg);
    form->addRow("Name", name);
    form->addRow("Density (g/cm3)", density);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);
    if (dlg.exec() != QDialog::Accepted) return;
    if (auto cmd = makeSetBodyMaterial(doc_, bodyId, core::ss(name->text().trimmed()), density->text().toDouble(), false))
      doc_.undoStack().push(std::move(cmd));
  });
  if (!b->material.empty())
    mat->addAction("Remove material", this, [this, bodyId] {
      if (auto cmd = makeSetBodyMaterial(doc_, bodyId, "", 0.0, false)) doc_.undoStack().push(std::move(cmd));
    });
}

void BrowserTree::pickBodyAppearance(ObjectId bodyId) {
  const core::Body* b = doc_.body(bodyId);
  if (!b) return;
  QColor current = QColor::fromRgbF(b->color[0], b->color[1], b->color[2], b->color[3]);
  QColor c = QColorDialog::getColor(current, this, "Appearance of " + core::qs(b->name), QColorDialog::ShowAlphaChannel);
  if (!c.isValid()) return;
  std::array<float, 4> rgba{static_cast<float>(c.redF()), static_cast<float>(c.greenF()), static_cast<float>(c.blueF()),
                            static_cast<float>(c.alphaF())};
  if (auto cmd = makeSetBodyColor(doc_, bodyId, rgba)) doc_.undoStack().push(std::move(cmd));
}

void BrowserTree::editComponentProperties(ObjectId componentId) {
  const core::Component* c = doc_.component(componentId);
  if (!c) return;
  QDialog dlg(this);
  dlg.setWindowTitle("Properties of " + core::qs(c->name));
  auto* form = new QFormLayout(&dlg);
  auto* pn = new QLineEdit(core::qs(c->partNumber), &dlg);
  pn->setPlaceholderText("e.g. BRK-0012");
  auto* desc = new QLineEdit(core::qs(c->description), &dlg);
  desc->setPlaceholderText("e.g. Mounting bracket, left");
  auto* mat = new QComboBox(&dlg);
  mat->setEditable(true);
  mat->addItem("");
  for (const core::Material& m : core::materialLibrary()) mat->addItem(core::qs(m.name));
  mat->setCurrentText(core::qs(c->material));
  form->addRow("Part number", pn);
  form->addRow("Description", desc);
  form->addRow("Material (BOM)", mat);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  form->addRow(buttons);
  if (dlg.exec() != QDialog::Accepted) return;
  if (auto cmd = makeSetComponentProperties(doc_, componentId, core::ss(pn->text().trimmed()), core::ss(desc->text().trimmed()),
                                            core::ss(mat->currentText().trimmed())))
    doc_.undoStack().push(std::move(cmd));
}

void BrowserTree::keyPressEvent(QKeyEvent* e) {
  if (e->key() == Qt::Key_F2 && currentItem() && (currentItem()->flags() & Qt::ItemIsEditable)) {
    startRename(currentItem());
    return;
  }
  QTreeWidget::keyPressEvent(e);
}

void BrowserTree::startRename(QTreeWidgetItem* item) {
  if (!item || !(item->flags() & Qt::ItemIsEditable)) return;
  if (item->data(0, RoleKind).toInt() == KindComponent) {
    // edit the bare name, not the "Name:1" label
    if (const core::Component* c = doc_.component(item->data(0, RoleId).toULongLong())) {
      const QSignalBlocker block(this);
      item->setText(0, core::qs(c->name));
    }
  }
  editItem(item, 0);
}

void BrowserTree::closeEditor(QWidget* editor, QAbstractItemDelegate::EndEditHint hint) {
  QTreeWidget::closeEditor(editor, hint);
  scheduleRebuild();  // restore the "Name:1" label after a cancelled rename
}

void BrowserTree::leaveEvent(QEvent* e) {
  if (!hoverKey_.isEmpty()) {
    hoverKey_.clear();
    doc_.selection().setPreselection(std::nullopt);
  }
  QTreeWidget::leaveEvent(e);
}

}  // namespace fabricad::app
