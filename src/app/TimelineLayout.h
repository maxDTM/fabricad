// FabriCAD app: geometry and hit-testing of the timeline strip (widget-free, unit-tested).
//
// The strip shows one icon per timeline-visible feature, left to right, and the rollback
// marker in the gap after the last active feature. Gaps are numbered 0..n (0 = before the
// first entry); a gap maps to a Document rollback index / move target index.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Document.h"
#include "core/Feature.h"

#include <QPoint>
#include <QRect>

#include <string>
#include <vector>

namespace fabricad::app {

struct TimelineEntry {
  core::ObjectId id = core::kInvalidId;
  int docIndex = 0;          ///< index in Document::features()
  std::string name;
  std::string type;
  std::string folder;        ///< Feature::browserFolder()
  std::string icon;          ///< Feature::iconName()
  core::FeatureStatus status = core::FeatureStatus::Stale;
  std::string statusMessage;
  bool suppressed = false;
  bool active = true;        ///< before the rollback marker
};

class TimelineLayout {
 public:
  static constexpr int kItem = 26;    ///< icon cell size
  static constexpr int kGap = 4;
  static constexpr int kMarker = 10;  ///< marker cell width
  static constexpr int kPad = 6;

  /// Snapshot of the document's timeline.
  static std::vector<TimelineEntry> entriesFrom(const core::Document& doc);

  void setEntries(std::vector<TimelineEntry> entries, int rollbackIndex);
  const std::vector<TimelineEntry>& entries() const { return entries_; }
  int rollbackIndex() const { return rollback_; }

  /// Gap index (0..n) the marker currently sits in.
  int markerGap() const;
  /// Rectangle of entry i (marker cell inserted at markerGap()).
  QRect itemRect(int i, int height = 32) const;
  QRect markerRect(int height = 32) const;
  /// x of the left edge of gap g (where the marker / an insertion line is drawn).
  int gapX(int g) const;
  int totalWidth() const;

  /// Entry under a point (-1 = none).
  int hitItem(QPoint p, int height = 32) const;
  bool hitMarker(QPoint p, int height = 32) const;
  /// Nearest gap for an x coordinate.
  int gapForX(int x) const;
  /// Document rollback index that corresponds to gap g.
  int rollbackIndexForGap(int g) const;
  /// Document index to pass to Document::moveFeature when entry `from` is dropped in gap g
  /// (-1 when the drop does not move it).
  int moveTargetForGap(int from, int g) const;

 private:
  std::vector<TimelineEntry> entries_;
  int rollback_ = 0;
  int featureCount_ = 0;
  friend class TimelineLayoutTestAccess;

 public:
  /// Total document feature count (for gaps after hidden trailing features).
  void setFeatureCount(int n) { featureCount_ = n; }
};

}  // namespace fabricad::app
