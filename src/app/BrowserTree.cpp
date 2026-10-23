// FabriCAD app: Browser tree.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/BrowserTree.h"

#include "assembly/tools/ComponentPropertiesTool.h"

#include "app/ThemeManager.h"
#include "theme/Tokens.h"

#include "app/AppearancePicker.h"
#include "app/MaterialPicker.h"

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
#include <QPainter>
#include <QStyledItemDelegate>
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

using theme::Tok;

using core::ObjectId;
using core::ShapeKind;
using core::ShapeRef;

namespace {
QString key(const char* prefix, ObjectId id) { return QString("%1:%2").arg(prefix).arg(id); }

/// Row painter of the MODEL tree (redesign 5.2): 16 px icon, the name (user names as typed in
/// the `value` style, folders in caps), a right-aligned meta text (count, PART, SOLID) in
/// text.tertiary and an ACTIVE badge (1 px accent.text border, caption). Colours from tokens at
/// paint time. Column 1 is the eye (16 px icon at the right edge).
class ModelRowDelegate : public QStyledItemDelegate {
 public:
  using QStyledItemDelegate::QStyledItemDelegate;
  QSize sizeHint(const QStyleOptionViewItem& o, const QModelIndex& i) const override {
    QSize s = QStyledItemDelegate::sizeHint(o, i);
    s.setHeight(BrowserTree::kRowH);
    return s;
  }
  void paint(QPainter* p, const QStyleOptionViewItem& o, const QModelIndex& i) const override {
    if (i.column() == 1) {
      const QIcon eye = i.data(Qt::DecorationRole).value<QIcon>();
      if (!eye.isNull()) eye.paint(p, QRect(o.rect.center().x() - 8, o.rect.center().y() - 8, 16, 16));
      return;
    }
    const int kind = i.data(BrowserTree::RoleKind).toInt();
    QRect r = o.rect;
    const QIcon icon = i.data(Qt::DecorationRole).value<QIcon>();
    if (!icon.isNull()) icon.paint(p, QRect(r.left() + 2, r.center().y() - 8, 16, 16));
    int right = r.right() - 6;
    // meta and badge (right-aligned)
    const QString meta = i.data(BrowserTree::RoleMeta).toString();
    if (!meta.isEmpty()) {
      const QFont cf = theme::font(theme::TextStyle::Caption);
      const int w = QFontMetrics(cf).horizontalAdvance(meta.toUpper());
      p->setFont(cf);
      p->setPen(theme::color(Tok::TextTertiary));
      p->drawText(QRect(right - w, r.top(), w, r.height()), Qt::AlignVCenter | Qt::AlignRight, meta);
      right -= w + 10;
    }
    // name
    const bool folder = kind == BrowserTree::KindFolder;
    QFont f = theme::font(folder ? theme::TextStyle::Body : theme::TextStyle::Value);
    const QFont itemFont = i.data(Qt::FontRole).value<QFont>();
    if (i.data(Qt::FontRole).isValid()) {
      f.setStrikeOut(itemFont.strikeOut());
      f.setItalic(itemFont.italic());
      if (itemFont.bold() && !folder) f.setWeight(QFont::Bold);
    }
    const QVariant fg = i.data(Qt::ForegroundRole);
    QColor text = fg.isValid() ? fg.value<QBrush>().color() : theme::color(Tok::TextPrimary);
    if (o.state & QStyle::State_Selected && !fg.isValid()) text = theme::color(Tok::TextPrimary);
    const QString badge = i.data(BrowserTree::RoleBadge).toString();
    const QFontMetrics fm(f);
    const QFont bf = theme::font(theme::TextStyle::Caption);
    const int badgeW = badge.isEmpty() ? 0 : QFontMetrics(bf).horizontalAdvance(badge.toUpper()) + 10;
    const int nameLeft = r.left() + 26;
    const int nameMax = std::max(0, right - nameLeft - (badgeW ? badgeW + 8 : 0));
    const QString name = fm.elidedText(i.data(Qt::DisplayRole).toString(), Qt::ElideMiddle, nameMax);
    p->setFont(f);
    p->setPen(text);
    const int nameW = fm.horizontalAdvance(folder ? name.toUpper() : name);
    p->drawText(QRect(nameLeft, r.top(), nameMax, r.height()), Qt::AlignVCenter | Qt::AlignLeft, name);
    if (badgeW) {
      const QRect b(nameLeft + std::min(nameW, nameMax) + 8, r.center().y() - 8, badgeW, 16);
      p->setPen(theme::color(Tok::AccentText));
      p->setBrush(Qt::NoBrush);
      p->drawRect(b.adjusted(0, 0, -1, -1));
      p->setFont(bf);
      p->drawText(b, Qt::AlignCenter, badge);
    }
  }
};

const char* kOriginNames[] = {"O", "X", "Y", "Z", "XY", "XZ", "YZ"};
}  // namespace

BrowserTree::BrowserTree(core::Document& doc, viewer::ViewerWidget* viewer, QWidget* parent)
    : QTreeWidget(parent), doc_(doc), viewer_(viewer) {
  // item colours come from tokens when rows are built: rebuild after a live theme switch
  connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this] { scheduleRebuild(); });
  setObjectName("Browser");
  setColumnCount(2);
  setHeaderHidden(true);
  header()->setStretchLastSection(false);
  header()->setSectionResizeMode(0, QHeaderView::Stretch);
  header()->setSectionResizeMode(1, QHeaderView::Fixed);
  header()->resizeSection(1, 24);
  setIconSize(QSize(16, 16));
  setItemDelegate(new ModelRowDelegate(this));
  header()->resizeSection(1, 32);
  setSelectionMode(QAbstractItemView::ExtendedSelection);
  // UX-38: no implicit edit triggers. Rename only through F2 / the context menu / double-click on
  // bodies and components (startRename); a double-click on a sketch or feature only edits it.
  setEditTriggers(QAbstractItemView::NoEditTriggers);
  setMouseTracking(true);
  setUniformRowHeights(true);
  setIndentation(18);
  setFocusPolicy(Qt::StrongFocus);

  connect(this, &QTreeWidget::itemClicked, this, &BrowserTree::onItemClicked);
  connect(this, &QTreeWidget::itemSelectionChanged, this, &BrowserTree::onSelectionFromTree);
  connect(this, &QTreeWidget::itemChanged, this, &BrowserTree::onItemRenamed);
  connect(this, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int column) {
    if (column == 1 || !item) return;
    const int kind = item->data(0, RoleKind).toInt();
    if (kind == KindFeature) {
      // the edit runs in the canvas: drop the keyboard focus, so the row shows no focus frame
      // that reads as an inline rename (UX-38)
      clearFocus();
      emit editFeatureRequested(item->data(0, RoleId).toULongLong());
    }
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
  // the root shows the dirty marker: refresh just its label after a save / undo to clean (DOC-09)
  connect(&doc_, &core::Document::modifiedChanged, this, [this](bool modified) {
    if (pending_) return;  // a rebuild is on its way
    QTreeWidgetItem* root = topLevelItem(0);
    if (!root || root->data(0, RoleKind).toInt() != KindRoot) return;
    const QSignalBlocker block(this);
    root->setText(0, core::qs(doc_.name()) + (modified ? " *" : ""));
  });
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
  if (!visible) item->setForeground(0, theme::color(Tok::TextSecondary));
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
      it->setData(0, RoleMeta, "Solid");
      it->setData(0, RoleId, QVariant::fromValue<qulonglong>(id));
      it->setData(0, RoleKey, key("body", id));
      it->setFlags(it->flags() | Qt::ItemIsEditable);
      if (!b->material.empty()) it->setToolTip(0, core::qs(b->name + " - " + b->material));
      // a part with one body works on it: ACTIVE, as the breadcrumb's last crumb (v6, P5-MOCKUP-11)
      if (root && comp->children.empty() && live.size() == 1 && doc_.activeComponent() == compId)
        it->setData(0, RoleBadge, "Active");
      updateEye(it, true, b->visible);
    }
  }
  if (comp->isOccurrence()) {
    // ASM-03: an occurrence owns no bodies; list the definition's bodies read-only (Fusion shows
    // them under every occurrence). Clicking one selects the occurrence.
    std::vector<ObjectId> shown = assembly::displayedBodies(doc_, compId);
    if (!shown.empty()) {
      QTreeWidgetItem* bf = addFolder(node, compId, "Bodies", false, true);
      for (ObjectId id : shown) {
        const core::Body* b = doc_.body(id);
        if (!b) continue;
        auto* it = new QTreeWidgetItem(bf);
        it->setText(0, core::qs(b->name));
        it->setIcon(0, resourceIcon("body"));
        it->setData(0, RoleKind, KindOccurrenceBody);
        it->setData(0, RoleId, QVariant::fromValue<qulonglong>(compId));
        it->setData(0, RoleKey, QString("occbody:%1:%2").arg(compId).arg(id));
        it->setToolTip(0, core::qs(std::format("{} (from {})", b->name, assembly::componentLabel(doc_, assembly::definitionOf(doc_, compId)))));
      }
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
          it->setForeground(0, theme::color(Tok::StatusError));
          it->setIcon(0, resourceIcon("error"));
        } else if (f->status() == core::FeatureStatus::Warning) {
          it->setForeground(0, theme::color(Tok::StatusWarning));
          it->setIcon(0, resourceIcon("warning"));
        }
        if (!active || f->isSuppressed()) {
          QFont ft = it->font(0);
          ft.setItalic(true);
          ft.setStrikeOut(f->isSuppressed());
          it->setFont(0, ft);
          it->setForeground(0, theme::color(Tok::TextDisabled));
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
      it->setData(0, RoleBadge, "Active");
    }
    if (c->grounded) it->setData(0, RoleMeta, "Grounded");
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
  root->setData(0, RoleMeta, doc_.rootComponent().children.empty() ? "Part" : "Assembly");
  if (doc_.activeComponent() == core::kRootComponentId) {
    QFont f = root->font(0);
    f.setBold(true);
    root->setFont(0, f);
    if (!doc_.rootComponent().children.empty()) root->setData(0, RoleBadge, "Active");
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
    if (it->data(0, RoleKind).toInt() == KindFolder && it->childCount() > 0) it->setData(0, RoleMeta, QString::number(it->childCount()));
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
    case KindOccurrenceBody:
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
    const bool canPaste = assembly::clipboardHasContent(doc_);  // components or bodies (G3 PART-05)
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
    menu.addAction("Properties...", this, [this, id] { editComponentProperties(id); });  // COMPPROPS card when registered
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
  // searchable picker with custom materials and the user library (production-features P1)
  mat->addAction("Choose...", this, [this, bodyId] {
    const core::Body* body = doc_.body(bodyId);
    if (!body) return;
    if (auto m = pickMaterial(this, body->material)) {
      const bool lib = core::findMaterial(m->name) != nullptr;
      if (auto cmd = makeSetBodyMaterial(doc_, bodyId, m->name, m->density, lib)) doc_.undoStack().push(std::move(cmd));
    }
  });
  mat->addSeparator();
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
  // the same picker as the A command (APP-04); several selected bodies get one colour
  std::vector<ObjectId> ids;
  for (QTreeWidgetItem* it : selectedItems())
    if (it->data(0, RoleKind).toInt() == KindBody) ids.push_back(it->data(0, RoleId).toULongLong());
  if (std::find(ids.begin(), ids.end(), bodyId) == ids.end()) ids = {bodyId};
  pickAppearance(this, doc_, ids);
}

void BrowserTree::keyboardSearch(const QString&) {
  // Fusion's browser has no type-ahead: letters are command shortcuts (A = Appearance), APP-04
}

void BrowserTree::editComponentProperties(ObjectId componentId) {
  // the fields of the COMPPROPS card (assembly/tools/ComponentPropertiesTool.h) as a dialog
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
  auto* rev = new QLineEdit(core::qs(c->revision), &dlg);
  rev->setPlaceholderText("e.g. B");
  auto* vendor = new QLineEdit(core::qs(c->vendor), &dlg);
  auto* cost = new QLineEdit(c->cost > 0 ? QString::number(c->cost) : QString(), &dlg);
  cost->setPlaceholderText("unit cost");
  form->addRow("Part number", pn);
  form->addRow("Description", desc);
  form->addRow("Material (BOM)", mat);
  form->addRow("Revision", rev);
  form->addRow("Vendor", vendor);
  form->addRow("Cost", cost);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  form->addRow(buttons);
  if (dlg.exec() != QDialog::Accepted) return;
  assembly::ComponentProps p;
  p.partNumber = core::ss(pn->text().trimmed());
  p.description = core::ss(desc->text().trimmed());
  p.material = core::ss(mat->currentText().trimmed());
  p.revision = core::ss(rev->text().trimmed());
  p.vendor = core::ss(vendor->text().trimmed());
  bool ok = false;
  const double v = cost->text().trimmed().toDouble(&ok);
  p.cost = ok && v > 0 ? v : 0.0;
  if (auto cmd = assembly::makeSetComponentProps(doc_, componentId, p)) doc_.undoStack().push(std::move(cmd));
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

void BrowserTree::drawRow(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
  const QRect row(0, option.rect.top(), viewport()->width(), option.rect.height());
  const bool selected = selectionModel() && selectionModel()->isSelected(index);
  const bool hover = option.state & QStyle::State_MouseOver;
  painter->fillRect(row, theme::color(selected ? Tok::AccentSelFill : hover ? Tok::SurfaceHover : Tok::SurfacePanel));
  if (selected) painter->fillRect(QRect(0, row.top(), 3, row.height()), theme::color(Tok::AccentIndicator));
  if (hasFocus() && currentIndex().row() == index.row() && currentIndex().parent() == index.parent()) {
    painter->setPen(QPen(theme::color(Tok::AccentFocus), 2));
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(QRectF(row).adjusted(1, 1, -1, -1));
  }
  QStyleOptionViewItem opt = option;
  opt.state &= ~(QStyle::State_Selected | QStyle::State_MouseOver | QStyle::State_HasFocus);
  QTreeWidget::drawRow(painter, opt, index);
}

void BrowserTree::leaveEvent(QEvent* e) {
  if (!hoverKey_.isEmpty()) {
    hoverKey_.clear();
    doc_.selection().setPreselection(std::nullopt);
  }
  QTreeWidget::leaveEvent(e);
}

}  // namespace fabricad::app
