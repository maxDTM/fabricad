// SPDX-License-Identifier: LGPL-2.1-or-later
#include "drawing/HoleTable.h"

#include "core/Document.h"
#include "core/Json.h"
#include "drawing/Callouts.h"
#include "drawing/DrawingView.h"
#include "drawing/Projection.h"
#include "drawing/Tolerance.h"
#include "part/Naming.h"
#include "part/features/StandardPart.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <QJsonArray>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <gp_Circ.hxx>

#include <set>

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

namespace {

/// Bodies of standard parts (fasteners, washers, nuts, inserts): their bores are not holes of
/// the part.
std::set<core::ObjectId> standardPartBodies(const core::Document& doc) {
  std::set<core::ObjectId> out;
  for (const auto& f : doc.features())
    if (dynamic_cast<const part::StandardPartFeature*>(f.get()))
      for (core::ObjectId b : f->createdBodies()) out.insert(b);
  return out;
}

/// PRODUCTION-2: holes whose circles are not in the view because something in front hides them
/// (a screw head over a tapped hole). For every body of the view: one circle per hole axis
/// parallel to the view direction, the one nearest the viewer, when the hole opens towards the
/// viewer (blind holes from the back stay out, as in the view).
std::vector<Curve2D> occludedHoleCircles(const core::Document& doc, const DrawingView& view, const std::set<core::ObjectId>& skip) {
  std::vector<Curve2D> out;
  if (view.type == ViewType::Section || view.type == ViewType::Detail || !view.breaks.empty()) return out;
  std::set<core::ObjectId> bodies;
  for (const Curve2D& cv : view.curves)
    if (cv.sourceBody != core::kInvalidId && !skip.count(cv.sourceBody)) bodies.insert(cv.sourceBody);
  const gp_Dir toViewer = view.frame.Direction();
  for (core::ObjectId bodyId : bodies) {
    const core::Body* b = doc.body(bodyId);
    const TopoDS_Shape& shape = doc.bodyShape(bodyId);
    if (!b || shape.IsNull()) continue;
    const gp_Trsf w = doc.worldPlacement(b->component);
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(shape, TopAbs_EDGE, edges);
    struct Cand {
      int edge;
      Point2 c;
      double r, height;
      gp_Pnt local;
      gp_Dir localUp;
    };
    std::vector<Cand> best;
    for (int i = 1; i <= edges.Extent(); ++i) {
      BRepAdaptor_Curve ac(TopoDS::Edge(edges(i)));
      if (ac.GetType() != GeomAbs_Circle || std::abs(ac.LastParameter() - ac.FirstParameter() - 2 * M_PI) > 1e-6) continue;
      const gp_Circ circ = ac.Circle();
      const gp_Dir axisW = circ.Axis().Direction().Transformed(w);
      if (!axisW.IsParallel(toViewer, 1e-6)) continue;
      const gp_Pnt cw = circ.Location().Transformed(w);
      const Point2 c2 = worldToView(view.frame, cw);
      const double h = gp_Vec(cw.XYZ()).Dot(gp_Vec(toViewer));
      auto it = std::find_if(best.begin(), best.end(), [&](const Cand& x) { return std::hypot(x.c.x - c2.x, x.c.y - c2.y) < 1e-3; });
      const gp_Dir up = toViewer.Transformed(w.Inverted());
      if (it == best.end()) best.push_back({i, c2, circ.Radius(), h, circ.Location(), up});
      else if (h > it->height + 1e-6 || (std::abs(h - it->height) <= 1e-6 && circ.Radius() > it->r)) *it = {i, c2, circ.Radius(), h, circ.Location(), up};
    }
    if (best.empty()) continue;
    BRepClass3d_SolidClassifier cls(shape);
    const part::NameMap names = part::currentNames(doc, bodyId);
    for (const Cand& k : best) {
      cls.Perform(k.local.Translated(gp_Vec(k.localUp) * 0.01), 1e-7);
      if (cls.State() != TopAbs_OUT) continue;  // the hole does not open towards the viewer
      Curve2D cv = Curve2D::circle(k.c, k.r, LineClass::Hidden);
      cv.sourceBody = bodyId;
      cv.sourceEdge = k.edge;
      cv.sourceName = part::nameOf(names, edges(k.edge));
      out.push_back(cv);
    }
  }
  return out;
}

}  // namespace

std::vector<HoleTableRow> holeTableRows(const core::Document& doc, const DrawingView& view, const core::UnitSystem& units) {
  struct Found {
    Point2 c;
    HoleInfo info;
    std::string callout;
  };
  std::vector<Found> found;
  const std::set<core::ObjectId> standard = standardPartBodies(doc);
  std::vector<Curve2D> curves;
  curves.reserve(view.curves.size());
  for (const Curve2D& cv : view.curves)
    if (!standard.count(cv.sourceBody)) curves.push_back(cv);
  for (const Curve2D& cv : occludedHoleCircles(doc, view, standard))
    if (std::none_of(curves.begin(), curves.end(), [&](const Curve2D& x) {
          return x.isCircle() && (x.cls == LineClass::Visible || x.cls == LineClass::Hidden) && std::hypot(x.center.x - cv.center.x, x.center.y - cv.center.y) < 1e-3;
        }))
      curves.push_back(cv);
  for (const Curve2D& cv : curves) {
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
