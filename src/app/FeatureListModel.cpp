// FabriCAD app: rows of the FEATURES panel.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/FeatureListModel.h"

#include "core/Json.h"
#include "core/Property.h"
#include "core/Units.h"

#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <Bnd_Box.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <cmath>

namespace fabricad::app {

using core::ObjectId;

namespace {

/// "12 mm", "45°" (3.3: a space before units, none before the degree sign).
QString quantityText(const core::Document& doc, const core::Property& p) {
  QString s = core::qs(core::formatQuantity(p.toQuantity(), doc.units()));
  if (s.endsWith(" deg")) s = s.left(s.size() - 4) + QChar(0x00B0);
  return s;
}

QString numberText(const core::Document& doc, double mmValue) {
  return core::qs(core::formatQuantity(core::Quantity::mm(mmValue), doc.units(), false));
}

const core::Property* numeric(const core::Feature& f, const char* name) {
  const core::Property* p = f.property(name);
  return p && p->isNumeric() && !p->flags().hidden ? p : nullptr;
}

/// Size of a sketch in its own plane ("150 × 100"); empty when it has no geometry.
QString sketchSize(const core::Document& doc, const core::Feature& f) {
  const core::IProfileSource* src = f.profileSource();
  if (!src) return {};
  try {
    const TopoDS_Shape edges = src->allEdges(false);
    if (edges.IsNull()) return {};
    gp_Trsf toLocal;
    toLocal.SetTransformation(src->sketchPlane());
    const TopoDS_Shape local = BRepBuilderAPI_Transform(edges, toLocal, true).Shape();
    Bnd_Box box;
    BRepBndLib::AddOptimal(local, box, false, false);
    if (box.IsVoid()) return {};
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    const double w = x1 - x0, h = y1 - y0;
    if (w < 1e-6 && h < 1e-6) return {};
    return numberText(doc, w) + QString::fromUtf8(" × ") + numberText(doc, h);
  } catch (...) {
    return {};
  }
}

}  // namespace

QString featureValueText(const core::Document& doc, const core::Feature& f) {
  if (f.isSuppressed()) return "off";
  const std::string& t = f.type();
  if (t == "Sketch") return sketchSize(doc, f);
  if (t == "Hole") {
    QString s;
    if (const core::Property* d = numeric(f, "diameter")) s = QString::fromUtf8("ø") + numberText(doc, d->toDouble());
    if (const core::Property* pts = f.property("points"); pts && pts->type() == core::PropertyType::RefList &&
                                                         pts->refs().size() > 1)
      s += QString::fromUtf8(" × %1").arg(pts->refs().size());
    return s;
  }
  if (t == "Chamfer") {
    const core::Property* d = numeric(f, "distance");
    const core::Property* type = f.property("chamferType");
    if (!d) return {};
    if (type && type->enumName() == "Distance and Angle")
      if (const core::Property* a = numeric(f, "angle")) return numberText(doc, d->toDouble()) + QString::fromUtf8(" × ") + quantityText(doc, *a);
    if (type && type->enumName() == "Two Distances")
      if (const core::Property* d2 = numeric(f, "distance2"))
        return numberText(doc, d->toDouble()) + QString::fromUtf8(" × ") + quantityText(doc, *d2);
    return quantityText(doc, *d);
  }
  if (t == "Extrude") {
    const core::Property* ext = f.property("extentType");
    if (ext && ext->enumName() == "All") return "all";
    if (ext && ext->enumName() == "To Object") return "to object";
  }
  // generic: the first main size of the feature
  for (const char* name : {"distance", "radius", "diameter", "thickness", "angle", "offset", "depth", "length", "height"})
    if (const core::Property* p = numeric(f, name)) return quantityText(doc, *p);
  for (const char* name : {"count", "quantity"})
    if (const core::Property* p = f.property(name); p && p->type() == core::PropertyType::Int)
      return QString::fromUtf8("× %1").arg(p->toInt());
  return {};
}

// =========================================================================================

const FeatureRow* FeatureListModel::row(int r) const {
  return r >= 0 && r < rowCount() ? &rows_[static_cast<size_t>(r)] : nullptr;
}

bool FeatureListModel::matches(const core::Feature& f) const {
  if (hideSuppressed_ && f.isSuppressed()) return false;
  if (problemsOnly_ && f.status() != core::FeatureStatus::Error && f.status() != core::FeatureStatus::Warning) return false;
  if (!search_.isEmpty() && !core::qs(f.name()).contains(search_, Qt::CaseInsensitive) &&
      !core::qs(f.type()).contains(search_, Qt::CaseInsensitive))
    return false;
  return true;
}

void FeatureListModel::rebuild(const core::Document& doc) {
  rows_.clear();
  featureCount_ = doc.featureCount();
  const int rb = doc.rollbackIndex();
  const bool filtering = !search_.isEmpty() || hideSuppressed_ || problemsOnly_;

  if (showOrigin_ && !filtering) {
    FeatureRow o;
    o.kind = FeatureRow::Kind::Origin;
    o.name = "Origin";
    o.icon = "origin";
    o.value = "XY XZ YZ";
    rows_.push_back(o);
  }

  // features that pass the filter (and the groups that hold one)
  std::set<ObjectId> shownGroups;
  if (filtering) {
    for (const auto& f : doc.features())
      if (f->isTimelineVisible() && matches(*f))
        for (ObjectId g : doc.groupChain(f->id())) shownGroups.insert(g);
  }

  int hideDeeperThan = -1;  // inside a collapsed group: skip rows deeper than this
  for (const core::TimelineItem& it : doc.timelineItems()) {
    if (hideDeeperThan >= 0) {
      if (it.depth > hideDeeperThan) continue;
      hideDeeperThan = -1;
    }
    FeatureRow r;
    r.id = it.id;
    r.depth = it.depth;
    r.parent = it.parent;
    if (it.kind == core::TimelineItem::Kind::Group) {
      const core::TimelineGroup* g = doc.group(it.id);
      if (!g) continue;
      if (filtering && !shownGroups.count(it.id)) continue;
      r.kind = FeatureRow::Kind::Group;
      r.name = core::qs(g->name);
      r.collapsed = g->collapsed && !filtering;
      const auto range = doc.groupRange(it.id);
      r.first = range.first;
      r.last = range.second;
      r.memberCount = static_cast<int>(doc.groupFeatures(it.id).size());
      r.groupState = doc.groupSuppression(it.id);
      r.suppressed = r.groupState == core::GroupSuppression::All;
      r.rolledBack = r.first >= rb;
      r.value = QString::number(r.memberCount);
      r.icon = "folder";
      if (r.collapsed) hideDeeperThan = it.depth;
    } else {
      const core::Feature* f = doc.feature(it.id);
      if (!f || !f->isTimelineVisible()) continue;
      if (filtering && !matches(*f)) continue;
      r.kind = FeatureRow::Kind::Feature;
      r.name = core::qs(f->name());
      r.first = r.last = doc.indexOf(f->id());
      r.icon = f->iconName();
      r.type = f->type();
      r.folder = f->browserFolder();
      r.status = f->status();
      r.statusMessage = core::qs(f->statusMessage());
      r.suppressed = f->isSuppressed();
      r.rolledBack = r.first >= rb;
      r.live = f->id() == liveFeature_;
      r.value = featureValueText(doc, *f);
    }
    rows_.push_back(std::move(r));
  }

  // rollback bar: above the first row whose features all lie at or after the marker
  int at = static_cast<int>(rows_.size());
  for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
    const FeatureRow& r = rows_[static_cast<size_t>(i)];
    if (r.isItem() && r.first >= rb) {
      at = i;
      break;
    }
  }
  if (pending_) {
    FeatureRow live;
    live.kind = FeatureRow::Kind::Live;
    live.name = pending_->name;
    live.icon = pending_->icon;
    live.value = pending_->value;
    live.live = true;
    live.first = live.last = rb;
    if (at > 0) {
      const FeatureRow& above = rows_[static_cast<size_t>(at - 1)];
      if (above.kind == FeatureRow::Kind::Feature) {
        live.depth = above.depth;
        live.parent = above.parent;
      } else if (above.kind == FeatureRow::Kind::Group && !above.collapsed) {
        live.depth = above.depth + 1;
        live.parent = above.id;
      }
    }
    rows_.insert(rows_.begin() + at, live);
    ++at;
  }
  FeatureRow bar;
  bar.kind = FeatureRow::Kind::Rollback;
  bar.first = bar.last = rb;
  rows_.insert(rows_.begin() + at, bar);

  FeatureRow ins;
  ins.kind = FeatureRow::Kind::Insert;
  rows_.push_back(ins);
}

int FeatureListModel::rowOfFeature(ObjectId id) const {
  for (int i = 0; i < rowCount(); ++i)
    if (rows_[static_cast<size_t>(i)].kind == FeatureRow::Kind::Feature && rows_[static_cast<size_t>(i)].id == id) return i;
  return -1;
}

int FeatureListModel::rowOfGroup(ObjectId id) const {
  for (int i = 0; i < rowCount(); ++i)
    if (rows_[static_cast<size_t>(i)].kind == FeatureRow::Kind::Group && rows_[static_cast<size_t>(i)].id == id) return i;
  return -1;
}

int FeatureListModel::rowOfItem(ObjectId id) const {
  const int f = rowOfFeature(id);
  return f >= 0 ? f : rowOfGroup(id);
}

int FeatureListModel::rollbackRow() const {
  for (int i = 0; i < rowCount(); ++i)
    if (rows_[static_cast<size_t>(i)].kind == FeatureRow::Kind::Rollback) return i;
  return -1;
}

int FeatureListModel::insertRow() const { return rowCount() - 1; }

int FeatureListModel::liveRow() const {
  for (int i = 0; i < rowCount(); ++i)
    if (rows_[static_cast<size_t>(i)].live) return i;
  return -1;
}

int FeatureListModel::featureIndexAtGap(int gap) const {
  for (int r = std::max(gap, 0); r < rowCount(); ++r) {
    const FeatureRow& row = rows_[static_cast<size_t>(r)];
    if (row.isItem()) return row.first;
  }
  return featureCount_;
}

int FeatureListModel::rollbackIndexForGap(int gap) const { return featureIndexAtGap(gap); }

std::vector<int> FeatureListModel::rollbackGaps() const {
  std::vector<int> gaps;
  int last = -1;
  const int firstGap = !rows_.empty() && rows_.front().kind == FeatureRow::Kind::Origin ? 1 : 0;
  for (int g = firstGap; g <= insertRow(); ++g) {
    if (const FeatureRow* r = row(g); r && (r->kind == FeatureRow::Kind::Rollback || r->kind == FeatureRow::Kind::Live)) continue;
    const int idx = rollbackIndexForGap(g);
    if (idx != last) gaps.push_back(g);
    last = idx;
  }
  return gaps;
}

FeatureListModel::Drop FeatureListModel::dropForGap(const core::Document& doc, int from, int gap, int depthHint) const {
  Drop d;
  d.gap = gap;
  const FeatureRow* src = row(from);
  if (!src || !src->isItem()) return d;
  if (!search_.isEmpty() || hideSuppressed_ || problemsOnly_) {
    d.valid = false;
    d.reason = "Clear the search and filters to reorder";
    return d;
  }
  const int firstGap = !rows_.empty() && rows_.front().kind == FeatureRow::Kind::Origin ? 1 : 0;
  gap = std::clamp(gap, firstGap, insertRow());
  d.gap = gap;
  // neighbours of the gap (item rows only)
  const FeatureRow* above = nullptr;
  const FeatureRow* below = nullptr;
  for (int r = gap - 1; r >= 0; --r)
    if (rows_[static_cast<size_t>(r)].isItem()) {
      above = &rows_[static_cast<size_t>(r)];
      break;
    }
  for (int r = gap; r < rowCount(); ++r)
    if (rows_[static_cast<size_t>(r)].isItem()) {
      below = &rows_[static_cast<size_t>(r)];
      break;
    }
  const int b = featureIndexAtGap(gap);  // insert before this feature index
  d.depth = below ? below->depth : (above ? above->depth : 0);

  if (src->kind == FeatureRow::Kind::Group) {
    const int s = src->first, e = src->last, k = e - s + 1;
    if (b >= s && b <= e + 1) return d;  // inside or next to itself: no move
    d.newIndex = b <= s ? b : b - k;
    std::string why;
    d.valid = doc.canMoveGroup(src->id, d.newIndex, &why);
    d.reason = core::qs(why);
    d.move = d.valid;
    d.depth = src->depth;
    return d;
  }

  // feature: which group does the gap put it in?
  std::optional<ObjectId> target;
  if (above && above->kind == FeatureRow::Kind::Group && !above->collapsed && below && below->parent == above->id) {
    target = above->id;  // first member of an expanded group
    d.depth = above->depth + 1;
  } else if (above && above->kind == FeatureRow::Kind::Group && above->collapsed) {
    target = above->parent;  // after a folded group, at its level
    d.depth = above->depth;
  } else if (above && above->depth > 0 && (!below || below->depth < above->depth || below->kind == FeatureRow::Kind::Group)) {
    // end of one or more groups: the pointer's indentation chooses the level
    const int deepest = above->depth;  // members of above->parent sit at this depth
    const int shallowest = below ? below->depth : 0;
    const int level = std::clamp(depthHint, shallowest, deepest);
    ObjectId g = above->parent;
    for (int lv = deepest; lv > level && g != core::kInvalidId; --lv) g = doc.groupOf(g);
    target = g;  // kInvalidId = top level
    d.depth = level;
  }
  const int old = src->first;
  d.newIndex = b <= old ? b : b - 1;
  const ObjectId currentGroup = doc.groupOf(src->id);
  if (d.newIndex == old && (!target || *target == currentGroup)) return d;  // nothing changes
  d.targetGroup = target;
  std::string why;
  d.valid = doc.canMoveFeatureTo(src->id, d.newIndex, target, &why);
  d.reason = core::qs(why);
  d.move = d.valid;
  return d;
}

}  // namespace fabricad::app
