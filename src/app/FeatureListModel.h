// FabriCAD app: rows of the FEATURES panel (widget-free, unit-tested).
//
// docs/plans/feature-list-timeline.md (widget part) and docs/plans/ui-redesign.md 5.2 / 4.6.
// The model flattens Document::timelineItems() into painted rows:
//
//   Origin                 XY XZ YZ      (pseudo row: selects the origin planes)
//   v [] Base plate             3 [x]    group row: chevron, folder, count, tri-state box
//        Sketch1          150 x 100      feature rows: icon, name (as typed), value column
//        Extrude1             12 mm
//   > [] Back wall              4 [x]    collapsed group: members hidden
//     Extrude3                25 mm      live row (the feature being edited)
//   ==================== ROLLBACK ==     rollback bar (between the active and rolled-back rows)
//     Fillet2                  3 mm      rolled back
//   [ + INSERT FEATURE AT ROLLBACK ]
//
// Gaps are row boundaries: gap g lies directly above row g (g = rows().size() is the end).
// rollbackIndexForGap / dropForGap translate a gap into Document indices and run the core rules
// (Document::canMoveFeatureTo, canMoveGroup), so the widget never second-guesses them.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Document.h"
#include "core/Feature.h"
#include "core/TimelineGroup.h"

#include <QString>

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace fabricad::app {

struct FeatureRow {
  enum class Kind { Origin, Group, Feature, Live, Rollback, Insert };
  Kind kind = Kind::Feature;
  core::ObjectId id = core::kInvalidId;  ///< feature or group id
  int depth = 0;                         ///< 0 = top level
  core::ObjectId parent = core::kInvalidId;
  int first = -1;                        ///< feature: its index; group: index of its first feature
  int last = -1;                         ///< group: index of its last feature (feature: = first)
  QString name;                          ///< as typed
  QString value;                         ///< value column ("12 mm", "off", "XY XZ YZ")
  std::string icon, type, folder;        ///< for featureIcon()
  core::FeatureStatus status = core::FeatureStatus::Ok;
  QString statusMessage;
  bool suppressed = false;               ///< feature suppressed / group: every member suppressed
  bool rolledBack = false;               ///< below the rollback bar
  bool collapsed = false;                ///< group folded
  bool live = false;                     ///< the feature a command is editing (or the Live row)
  core::GroupSuppression groupState = core::GroupSuppression::None;
  int memberCount = 0;                   ///< group: features inside (nested included)

  bool isItem() const { return kind == Kind::Feature || kind == Kind::Group; }
};

/// Value column of a feature ("12 mm", "ø14 × 2", "1 × 45°", "150 × 100"); empty when there is
/// nothing short to say. Units follow the document (3.3: space before units, none before °).
QString featureValueText(const core::Document& doc, const core::Feature& f);

class FeatureListModel {
 public:
  /// A command creating a new feature: shown as a Live row at the rollback position.
  struct PendingLive {
    QString name;          ///< "Extrude" (tool name)
    std::string icon;      ///< tool icon path or name
    QString value;
    std::string type;      ///< feature type of a feature tool's working copy ("Extrude"): its row icon
    std::string folder;
  };

  void rebuild(const core::Document& doc);
  const std::vector<FeatureRow>& rows() const { return rows_; }
  int rowCount() const { return static_cast<int>(rows_.size()); }
  const FeatureRow* row(int r) const;

  /// Row of a feature / group (-1 when hidden by a collapsed group or unknown).
  int rowOfFeature(core::ObjectId id) const;
  int rowOfGroup(core::ObjectId id) const;
  int rowOfItem(core::ObjectId id) const;  ///< feature or group
  int rollbackRow() const;
  int insertRow() const;
  int liveRow() const;  ///< the Live row or the live feature row, -1 if none

  // --- state the view sets ---------------------------------------------------------------------
  void setLiveFeature(core::ObjectId id) { liveFeature_ = id; }
  core::ObjectId liveFeature() const { return liveFeature_; }
  void setPendingLive(std::optional<PendingLive> p) { pending_ = std::move(p); }
  const std::optional<PendingLive>& pendingLive() const { return pending_; }
  /// Search text: only matching features (and their groups) are listed. Empty = all.
  void setSearch(const QString& text) { search_ = text; }
  const QString& search() const { return search_; }
  void setShowOrigin(bool on) { showOrigin_ = on; }
  void setHideSuppressed(bool on) { hideSuppressed_ = on; }
  bool hideSuppressed() const { return hideSuppressed_; }
  void setProblemsOnly(bool on) { problemsOnly_ = on; }
  bool problemsOnly() const { return problemsOnly_; }

  // --- gaps --------------------------------------------------------------------------------
  /// Document rollback index for the bar dropped in gap g (0..rowCount()).
  int rollbackIndexForGap(int gap) const;
  /// Gap the bar sits in (the row index of the Rollback row).
  int rollbackGap() const { return rollbackRow(); }
  /// Gaps the bar may be dropped in, top to bottom (one per distinct rollback index).
  std::vector<int> rollbackGaps() const;

  struct Drop {
    bool move = false;     ///< false: no move (same place) or refused
    bool valid = true;     ///< false: refused, `reason` says why
    QString reason;
    int newIndex = -1;     ///< Document index in the final order (feature / first feature of a group)
    std::optional<core::ObjectId> targetGroup;  ///< feature drops only (nullopt = automatic)
    int gap = -1;
    int depth = 0;         ///< indentation of the drop line
  };
  /// Where row `from` (a Feature or Group row) lands when dropped in `gap`. `depthHint` is the
  /// indentation level under the pointer: at the end of an expanded group it chooses between
  /// "last member of the group" (deeper) and "after the group" (shallower).
  Drop dropForGap(const core::Document& doc, int from, int gap, int depthHint = 99) const;

  /// Feature index the next feature after gap g starts at (featureCount at the end).
  int featureIndexAtGap(int gap) const;

 private:
  bool matches(const core::Feature& f) const;
  std::vector<FeatureRow> rows_;
  int featureCount_ = 0;
  core::ObjectId liveFeature_ = core::kInvalidId;
  std::optional<PendingLive> pending_;
  QString search_;
  bool showOrigin_ = true;
  bool hideSuppressed_ = false;
  bool problemsOnly_ = false;
};

}  // namespace fabricad::app
