// FabriCAD app: S-box.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/CommandSearch.h"

#include "theme/Tokens.h"

#include "app/Icons.h"
#include "app/ToolbarModel.h"
#include "core/AppHost.h"
#include "core/Json.h"

#include <QGuiApplication>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QScreen>
#include <QSettings>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace fabricad::app {

using theme::Tok;

bool isPrefixMatch(const core::ToolInfo& info, const std::string& query) {
  auto lower = [](std::string v) {
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return v;
  };
  std::string q = lower(query);
  while (!q.empty() && q.front() == ' ') q.erase(q.begin());
  while (!q.empty() && q.back() == ' ') q.pop_back();
  if (q.empty()) return true;
  const std::string name = lower(info.name);
  if (name.starts_with(q)) return true;
  for (size_t i = 0; i < name.size(); ++i)
    if ((name[i] == ' ' || name[i] == '-' || name[i] == '/') && name.compare(i + 1, q.size(), q) == 0) return true;
  for (const std::string& a : info.aliases)
    if (lower(a).starts_with(q)) return true;
  const std::string id = lower(info.id);
  return id.starts_with(q) || id.find("." + q) != std::string::npos;
}

std::vector<std::string> searchCommandIds(const core::ToolRegistry& reg, const std::string& query,
                                          const QStringList& pinned, const QStringList& recent, int max,
                                          const std::function<int(const core::ITool*)>& rank) {
  std::vector<std::string> out;
  auto push = [&](const std::string& id) {
    const core::ITool* t = reg.tool(id);
    if (!t || t->info().hidden) return;
    if (std::find(out.begin(), out.end(), id) == out.end() && static_cast<int>(out.size()) < max) out.push_back(id);
  };
  bool blank = std::all_of(query.begin(), query.end(), [](char c) { return c == ' '; });
  if (blank) {
    for (const QString& p : pinned) push(p.toStdString());
    for (const QString& r : recent) push(r.toStdString());
    return out;
  }
  std::vector<core::ITool*> found = reg.search(query, rank ? max * 3 : max);
  if (rank) {
    std::stable_sort(found.begin(), found.end(), [&](const core::ITool* a, const core::ITool* b) { return rank(a) < rank(b); });
    // APP-10: a disabled command stays listed (last) only when the query is a prefix of its
    // name, a word of it or an alias; fuzzy matches of disabled commands are noise
    std::erase_if(found, [&](const core::ITool* t) { return rank(t) >= kDisabledRank && !isPrefixMatch(t->info(), query); });
  }
  for (core::ITool* t : found) push(t->info().id);
  return out;
}

CommandSearch::CommandSearch(core::ToolManager& manager, QWidget* parent)
    : QFrame(parent, Qt::Popup), manager_(&manager) {
  setObjectName("SearchPopup");
  setAttribute(Qt::WA_StyledBackground);
  setFixedWidth(380);
  auto* v = new QVBoxLayout(this);
  v->setContentsMargins(6, 6, 6, 6);
  edit_ = new QLineEdit(this);
  edit_->setPlaceholderText("Search commands (name, alias)...");
  edit_->setClearButtonEnabled(true);
  edit_->installEventFilter(this);
  v->addWidget(edit_);
  list_ = new QTreeWidget(this);
  list_->setColumnCount(3);
  list_->setHeaderHidden(true);
  list_->setRootIsDecorated(false);
  list_->setIconSize(QSize(18, 18));
  list_->setFocusPolicy(Qt::NoFocus);
  list_->header()->setStretchLastSection(false);
  list_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  list_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  list_->header()->setSectionResizeMode(2, QHeaderView::Fixed);
  list_->header()->resizeSection(2, 120);
  list_->setTextElideMode(Qt::ElideRight);
  list_->setContextMenuPolicy(Qt::CustomContextMenu);
  list_->setMinimumHeight(260);
  v->addWidget(list_);
  connect(edit_, &QLineEdit::textChanged, this, [this] { updateResults(); });
  connect(list_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* it, int) { runItem(it); });
  connect(list_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* it, int) { runItem(it); });
  connect(list_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& p) {
    QTreeWidgetItem* it = list_->itemAt(p);
    if (!it) return;
    const std::string id = it->data(0, Qt::UserRole).toString().toStdString();
    QMenu m(this);
    m.addAction(pinned_.contains(core::qs(id)) ? "Unpin" : "Pin to Shortcuts", this, [this, id] { togglePin(id); });
    m.exec(list_->viewport()->mapToGlobal(p));
  });
  pinned_ = QSettings().value("search/pinned").toStringList();
}

void CommandSearch::togglePin(const std::string& id) {
  const QString q = core::qs(id);
  if (pinned_.contains(q)) pinned_.removeAll(q);
  else pinned_.append(q);
  QSettings().setValue("search/pinned", pinned_);
  updateResults();
}

void CommandSearch::setQuery(const QString& q) {
  edit_->setText(q);
  updateResults();
}

std::vector<std::string> CommandSearch::results() const {
  std::vector<std::string> out;
  for (int i = 0; i < list_->topLevelItemCount(); ++i)
    out.push_back(list_->topLevelItem(i)->data(0, Qt::UserRole).toString().toStdString());
  return out;
}

void CommandSearch::updateResults() {
  list_->clear();
  const core::ToolContext ctx = manager_->makeContext();
  const core::Workspace ws = ctx.host ? ctx.host->workspace() : core::Workspace::Design;
  auto rank = [&ctx, ws](const core::ITool* t) {
    if (!t->isEnabled(ctx)) return kDisabledRank;
    return t->info().workspace == ws ? 0 : 1;
  };
  for (const std::string& id :
       searchCommandIds(manager_->registry(), edit_->text().toStdString(), pinned_, recent_, 20, rank)) {
    core::ITool* t = manager_->registry().tool(id);
    const core::ToolInfo& info = t->info();
    auto* it = new QTreeWidgetItem(list_);
    it->setIcon(0, toolIcon(info));
    it->setText(0, core::qs(info.name) + (pinned_.contains(core::qs(id)) ? "  ★" : ""));
    it->setText(1, core::qs(info.shortcut));
    it->setText(2, core::qs(info.tab.empty() ? info.group : info.tab + (info.group.empty() ? "" : " > " + info.group)));
    it->setToolTip(0, core::qs(toolTooltip(info)));
    it->setData(0, Qt::UserRole, core::qs(id));
    it->setForeground(1, theme::color(Tok::TextSecondary));
    it->setForeground(2, theme::color(Tok::TextTertiary));
    if (!t->isEnabled(ctx)) {
      it->setDisabled(true);
    }
  }
  for (int i = 0; i < list_->topLevelItemCount(); ++i)
    if (!list_->topLevelItem(i)->isDisabled()) {
      list_->setCurrentItem(list_->topLevelItem(i));
      break;
    }
}

void CommandSearch::runItem(QTreeWidgetItem* item) {
  if (!item || item->isDisabled()) return;
  const std::string id = item->data(0, Qt::UserRole).toString().toStdString();
  hide();
  emit runRequested(id);
}

void CommandSearch::popup(const QPoint& globalPos) {
  edit_->clear();
  updateResults();
  adjustSize();
  QRect r(globalPos - QPoint(40, 20), size());
  if (QScreen* s = QGuiApplication::screenAt(globalPos)) {
    const QRect a = s->availableGeometry();
    r.moveRight(std::min(r.right(), a.right()));
    r.moveBottom(std::min(r.bottom(), a.bottom()));
    r.moveLeft(std::max(r.left(), a.left()));
    r.moveTop(std::max(r.top(), a.top()));
  }
  move(r.topLeft());
  show();
  edit_->setFocus();
}

void CommandSearch::popupBelow(const QRect& anchor, const QRect& bounds) {
  edit_->clear();
  updateResults();
  constexpr int kMargin = 8;
  QRect area = bounds;
  if (QScreen* s = QGuiApplication::screenAt(anchor.center())) area = area.intersected(s->availableGeometry());
  if (area.isEmpty()) area = bounds;
  setFixedWidth(std::max(240, std::min(380, area.width() - 2 * kMargin)));
  adjustSize();
  QRect r(QPoint(anchor.left(), anchor.bottom() + 4), size());
  // left edge on the box; shifted left when it would pass the window's right edge
  if (r.right() > area.right() - kMargin) r.moveRight(area.right() - kMargin);
  if (r.left() < area.left() + kMargin) r.moveLeft(area.left() + kMargin);
  if (r.bottom() > area.bottom() - kMargin) r.setBottom(area.bottom() - kMargin);
  if (r.height() < 120) r.moveBottom(area.bottom() - kMargin);
  move(r.topLeft());
  resize(r.size());
  show();
  edit_->setFocus();
}

bool CommandSearch::eventFilter(QObject* o, QEvent* e) {
  if (o == edit_ && e->type() == QEvent::KeyPress) {
    auto* k = static_cast<QKeyEvent*>(e);
    auto step = [this](int d) {
      int i = list_->currentItem() ? list_->indexOfTopLevelItem(list_->currentItem()) : -1;
      for (int n = 0; n < list_->topLevelItemCount(); ++n) {
        i = (i + d + list_->topLevelItemCount()) % list_->topLevelItemCount();
        if (!list_->topLevelItem(i)->isDisabled()) break;
      }
      if (i >= 0) list_->setCurrentItem(list_->topLevelItem(i));
    };
    switch (k->key()) {
      case Qt::Key_Down: step(1); return true;
      case Qt::Key_Up: step(-1); return true;
      case Qt::Key_Return:
      case Qt::Key_Enter: runItem(list_->currentItem()); return true;
      case Qt::Key_Escape: hide(); return true;
      default: break;
    }
  }
  return QFrame::eventFilter(o, e);
}

}  // namespace fabricad::app
