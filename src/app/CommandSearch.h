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

#include <functional>
#include <string>
#include <vector>

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace fabricad::app {

/// Rank the S search gives a disabled command (searchCommandIds drops disabled fuzzy matches).
inline constexpr int kDisabledRank = 2;
/// The query (trimmed, case-insensitive) is a prefix of the name, of a word of the name, of an
/// alias or of the id (or of its part after the module dot).
bool isPrefixMatch(const core::ToolInfo& info, const std::string& query);
/// Ordered result ids for a query (empty query: pinned, then recent, deduplicated).
/// `rank` (optional) buckets the matches, lower first, keeping the score order inside a bucket:
/// the popup puts enabled tools of the current workspace first, then other enabled ones, then
/// disabled ones (APP-10). Without it the registry's score order is kept.
std::vector<std::string> searchCommandIds(const core::ToolRegistry& reg, const std::string& query,
                                          const QStringList& pinned, const QStringList& recent, int max = 20,
                                          const std::function<int(const core::ITool*)>& rank = {});

class CommandSearch : public QFrame {
  Q_OBJECT
 public:
  CommandSearch(core::ToolManager& manager, QWidget* parent = nullptr);
  /// Rebind to another tab's tool manager (multi-document tabs).
  void setToolManager(core::ToolManager* m) { manager_ = m; }
  core::ToolManager& manager() const { return *manager_; }

  void popup(const QPoint& globalPos);
  /// Open under `anchor` (the title-bar search box, global coordinates), left-aligned with it
  /// and kept inside `bounds` (the main window) and the screen (p1-check 5).
  void popupBelow(const QRect& anchor, const QRect& bounds);
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
