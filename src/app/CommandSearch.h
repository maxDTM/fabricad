// FabriCAD app: "S" command search popup (ux.md 6.4).
// A search field with instant fuzzy search over the ToolRegistry (name, aliases, id,
// tooltip); with an empty query it lists pinned commands, then recently used ones. Enter
// runs the current row, Up/Down move, Esc closes. Right-click a row to pin/unpin it
// (persisted in QSettings "search/pinned").
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Tool.h"

#include <QFrame>
#include <QStringList>

#include <string>
#include <vector>

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace fabricad::app {

/// Ordered result ids for a query (empty query: pinned, then recent, deduplicated).
std::vector<std::string> searchCommandIds(const core::ToolRegistry& reg, const std::string& query,
                                          const QStringList& pinned, const QStringList& recent, int max = 20);

class CommandSearch : public QFrame {
  Q_OBJECT
 public:
  CommandSearch(core::ToolManager& manager, QWidget* parent = nullptr);
  /// Rebind to another tab's tool manager (multi-document tabs).
  void setToolManager(core::ToolManager* m) { manager_ = m; }
  core::ToolManager& manager() const { return *manager_; }

  void popup(const QPoint& globalPos);
  void setRecent(const QStringList& recent) { recent_ = recent; }
  const QStringList& pinned() const { return pinned_; }
  /// Current result ids (tests).
  std::vector<std::string> results() const;
  void setQuery(const QString& q);

 signals:
  void runRequested(const std::string& id);

 protected:
  bool eventFilter(QObject* o, QEvent* e) override;

 private:
  void updateResults();
  void runItem(QTreeWidgetItem* item);
  void togglePin(const std::string& id);

  core::ToolManager* manager_;
  QLineEdit* edit_ = nullptr;
  QTreeWidget* list_ = nullptr;
  QStringList pinned_;
  QStringList recent_;
};

}  // namespace fabricad::app
