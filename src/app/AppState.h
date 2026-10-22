// FabriCAD app: small widget-free helpers (recent files, persisted per-document UI state,
// status-bar selection text). Unit-tested in tests/test_app_logic.cpp.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/ShapeRef.h"
#include "core/Types.h"
#include "core/ViewerContext.h"

#include <QJsonObject>
#include <QStringList>

#include <set>
#include <string>
#include <vector>

namespace fabricad::core {
class Document;
}

namespace fabricad::app {

/// Most-recently-used file list (front = newest), deduplicated, bounded.
class RecentFiles {
 public:
  explicit RecentFiles(int maxEntries = 10) : max_(maxEntries) {}
  void add(const QString& path);
  void remove(const QString& path);
  void clear() { list_.clear(); }
  const QStringList& list() const { return list_; }
  void setList(const QStringList& l);
  /// QSettings persistence ("recentFiles").
  void load();
  void save() const;
  /// Menu labels for `paths` (same order): the file name, plus the parent folder when another
  /// entry has the same file name, plus more folders while two labels still collide
  /// ("chair.fcad (a/chair)" vs "chair.fcad (b/chair)"; functional DOC-11).
  static QStringList labels(const QStringList& paths);

 private:
  QStringList list_;
  int max_;
};

/// Per-document view state saved in Document::extensionData("ui").
struct UiState {
  std::set<core::ObjectId> hiddenFeatures;
  bool showOrigin = false;
  bool showSketches = true;
  bool showConstruction = true;
  bool showJoints = true;
  core::DisplayMode displayMode = core::DisplayMode::ShadedEdges;
  core::ProjectionMode projection = core::ProjectionMode::Orthographic;

  QJsonObject toJson() const;
  static UiState fromJson(const QJsonObject& o);
  bool operator==(const UiState&) const = default;
};

const char* displayModeName(core::DisplayMode m);
core::DisplayMode displayModeFromName(const std::string& s);

/// "Face of Body1", "3 Edges", "Sketch1", "2 items"; empty for no selection.
std::string describeSelection(const core::Document& doc, const std::vector<core::ShapeRef>& items);
/// Human name of a ref's owner ("Body1", "Sketch2", "XY Plane", "Bracket").
std::string ownerName(const core::Document& doc, const core::ShapeRef& r);
/// "Face"/"Faces", "Sketch Curve"/"Sketch Curves"...
std::string kindLabel(core::ShapeKind k, bool plural = false);

}  // namespace fabricad::app
