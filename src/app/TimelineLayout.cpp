// FabriCAD app: timeline geometry.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/TimelineLayout.h"

#include <algorithm>
#include <climits>
#include <cstdlib>

namespace fabricad::app {

std::vector<TimelineEntry> TimelineLayout::entriesFrom(const core::Document& doc) {
  std::vector<TimelineEntry> out;
  const auto& fs = doc.features();
  for (int i = 0; i < static_cast<int>(fs.size()); ++i) {
    const core::Feature& f = *fs[static_cast<size_t>(i)];
    if (!f.isTimelineVisible()) continue;
    TimelineEntry e;
    e.id = f.id();
    e.docIndex = i;
    e.name = f.name();
    e.type = f.type();
    e.folder = f.browserFolder();
    e.icon = f.iconName();
    e.status = f.status();
    e.statusMessage = f.statusMessage();
    e.suppressed = f.isSuppressed();
    e.active = i < doc.rollbackIndex();
    out.push_back(std::move(e));
  }
  return out;
}

void TimelineLayout::setEntries(std::vector<TimelineEntry> entries, int rollbackIndex) {
  entries_ = std::move(entries);
  rollback_ = rollbackIndex;
  if (featureCount_ == 0 && !entries_.empty()) featureCount_ = entries_.back().docIndex + 1;
}

int TimelineLayout::markerGap() const {
  int g = 0;
  for (const TimelineEntry& e : entries_)
    if (e.docIndex < rollback_) ++g;
  return g;
}

QRect TimelineLayout::itemRect(int i, int height) const {
  const int mg = markerGap();
  int x = kPad + i * (kItem + kGap);
  if (i >= mg) x += kMarker + kGap;
  return QRect(x, (height - kItem) / 2, kItem, kItem);
}

QRect TimelineLayout::markerRect(int height) const {
  const int mg = markerGap();
  const int x = kPad + mg * (kItem + kGap);
  return QRect(x, 2, kMarker, height - 4);
}

int TimelineLayout::gapX(int g) const {
  const int n = static_cast<int>(entries_.size());
  if (g == markerGap()) return markerRect().center().x();
  if (g < n) return itemRect(g).left() - kGap / 2;
  if (n == 0) return kPad;
  return itemRect(n - 1).right() + 1 + kGap / 2;
}

int TimelineLayout::totalWidth() const {
  return kPad * 2 + static_cast<int>(entries_.size()) * (kItem + kGap) + kMarker + kGap;
}

int TimelineLayout::hitItem(QPoint p, int height) const {
  for (int i = 0; i < static_cast<int>(entries_.size()); ++i)
    if (itemRect(i, height).adjusted(-kGap / 2, -2, kGap / 2, 2).contains(p)) return i;
  return -1;
}

bool TimelineLayout::hitMarker(QPoint p, int height) const { return markerRect(height).adjusted(-3, 0, 3, 0).contains(p); }

int TimelineLayout::gapForX(int x) const {
  const int n = static_cast<int>(entries_.size());
  int best = 0, bestD = INT_MAX;
  for (int g = 0; g <= n; ++g) {
    const int d = std::abs(gapX(g) - x);
    if (d < bestD) {
      bestD = d;
      best = g;
    }
  }
  return best;
}

int TimelineLayout::rollbackIndexForGap(int g) const {
  const int n = static_cast<int>(entries_.size());
  g = std::clamp(g, 0, n);
  if (g == 0) return 0;
  if (g == n) return std::max(featureCount_, entries_.back().docIndex + 1);
  return entries_[static_cast<size_t>(g - 1)].docIndex + 1;
}

int TimelineLayout::moveTargetForGap(int from, int g) const {
  const int n = static_cast<int>(entries_.size());
  if (from < 0 || from >= n) return -1;
  g = std::clamp(g, 0, n);
  if (g == from || g == from + 1) return -1;
  if (g > from + 1) return entries_[static_cast<size_t>(g - 1)].docIndex;  // after entry g-1
  return entries_[static_cast<size_t>(g)].docIndex;                        // before entry g
}

}  // namespace fabricad::app
