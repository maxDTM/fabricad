// SPDX-License-Identifier: LGPL-2.1-or-later
#include "drawing/HoleTable.h"

#include "core/Document.h"
#include "core/Json.h"
#include "drawing/Callouts.h"
#include "drawing/DrawingView.h"
#include "drawing/Tolerance.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <map>

namespace fabricad::drawing {

using core::idFromJson;
using core::idToJson;

double HoleTable::width() const {
  double w = 0;
  for (double c : columnWidths) w += c;
  return w;
}

double HoleTable::height() const { return rowHeight * static_cast<double>(rows.size() + 1); }

QJsonObject HoleTable::toJson() const {
  QJsonObject o;
  o["id"] = idToJson(id);
  o["view"] = idToJson(viewId);
  o["position"] = QJsonArray{position.x, position.y};
  return o;
}

HoleTable HoleTable::fromJson(const QJsonObject& o) {
  HoleTable t;
  t.id = idFromJson(o["id"]);
  t.viewId = idFromJson(o["view"]);
  const QJsonArray p = o["position"].toArray();
  if (p.size() == 2) t.position = {p[0].toDouble(), p[1].toDouble()};
  return t;
}

std::vector<HoleTableRow> holeTableRows(const core::Document& doc, const DrawingView& view, const core::UnitSystem& units) {
  struct Found {
    Point2 c;
    HoleInfo info;
    std::string callout;
  };
  std::vector<Found> found;
  for (const Curve2D& cv : view.curves) {
    if (cv.type != CurveType::Arc || std::abs(std::abs(cv.sweep) - 360.0) > 1e-6) continue;
    if (cv.cls != LineClass::Visible && cv.cls != LineClass::Hidden) continue;
    auto same = std::find_if(found.begin(), found.end(),
                             [&](const Found& f) { return std::hypot(f.c.x - cv.center.x, f.c.y - cv.center.y) < 1e-6; });
    // counterbore / countersink / bottom circle of a hole already listed: only a circle that
    // knows the hole feature (named edge) improves the entry
    if (same != found.end() && same->info.source.rfind("feature:", 0) == 0) continue;
    HoleInfo info = analyzeHole(doc, cv);
    if (!info.ok) continue;
    if (same != found.end() && info.source.rfind("feature:", 0) != 0) continue;
    // the drill circle and its counterbore are concentric: keep the one listed first, but a
    // hole is only one table row
    HoleCallout c;
    info.count = 1;
    std::string text;
    for (const RichLine& l : holeCalloutLines(info, c, units)) text += (text.empty() ? "" : " / ") + richLinePlainText(l);
    if (same != found.end()) *same = {cv.center, info, text};
    else found.push_back({cv.center, info, text});
  }
  // tags: one letter per distinct callout (in order of first appearance), numbers by position
  std::map<std::string, char> letter;
  std::map<char, int> count;
  char next = 'A';
  for (const Found& f : found)
    if (!letter.count(f.callout)) letter[f.callout] = next < 'Z' ? next++ : 'Z';
  std::vector<size_t> order(found.size());
  for (size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    const char la = letter[found[a].callout], lb = letter[found[b].callout];
    if (la != lb) return la < lb;
    if (std::abs(found[a].c.y - found[b].c.y) > 1e-6) return found[a].c.y > found[b].c.y;  // top row first
    return found[a].c.x < found[b].c.x;
  });
  for (const Found& f : found) ++count[letter[f.callout]];
  std::map<char, int> seq;
  std::vector<HoleTableRow> rows;
  for (size_t i : order) {
    const Found& f = found[i];
    const char l = letter[f.callout];
    HoleTableRow r;
    r.tag = std::string(1, l) + std::to_string(++seq[l]);
    r.x = f.c.x - view.bbox.min.x;  // datum: lower-left corner of the view
    r.y = f.c.y - view.bbox.min.y;
    r.diameter = f.info.diameter;
    r.callout = f.callout;
    r.quantity = count[l];
    rows.push_back(r);
  }
  return rows;
}

std::vector<std::string> holeTableCells(const HoleTableRow& row, const core::UnitSystem& units, int precision) {
  const int prec = precision >= 0 ? precision : units.precision;
  auto L = [&](double mm) { return formatValue(core::toDisplayValue(core::Quantity::mm(mm), units), prec); };
  return {row.tag, L(row.x), L(row.y), "\xC3\x98" + L(row.diameter), row.callout, std::to_string(row.quantity)};
}

}  // namespace fabricad::drawing
