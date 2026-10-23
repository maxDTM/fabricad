// FabriCAD drawing: callout value types, persistence, the per-sheet container, refresh and the
// baseline / chain dimension builders. Layout lives in CalloutRenderer.cpp, hole recognition in
// HoleAnalysis.cpp.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "drawing/Callouts.h"

#include "core/Document.h"
#include "core/Json.h"
#include "drawing/DrawingView.h"
#include "drawing/Sheet.h"

#include <QJsonArray>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace fabricad::drawing {

using core::idFromJson;
using core::idToJson;
using core::ObjectId;
using core::qs;
using core::ss;

namespace {
QJsonArray pt(const Point2& p) { return QJsonArray{p.x, p.y}; }
Point2 ptFrom(const QJsonValue& v, const Point2& fb = {}) {
  auto a = v.toArray();
  return a.size() >= 2 ? Point2{a[0].toDouble(), a[1].toDouble()} : fb;
}
template <class T>
QJsonArray arrayOf(const std::vector<T>& v) {
  QJsonArray a;
  for (const auto& e : v) a.push_back(e.toJson());
  return a;
}
template <class T>
std::vector<T> vectorOf(const QJsonValue& v) {
  std::vector<T> out;
  for (const auto& e : v.toArray()) out.push_back(T::fromJson(e.toObject()));
  return out;
}
template <class T>
T* findIn(std::vector<T>& v, ObjectId id) {
  for (auto& e : v)
    if (e.id == id) return &e;
  return nullptr;
}
template <class T>
bool eraseIn(std::vector<T>& v, ObjectId id) {
  auto it = std::remove_if(v.begin(), v.end(), [&](const T& e) { return e.id == id; });
  if (it == v.end()) return false;
  v.erase(it, v.end());
  return true;
}
std::string lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
}  // namespace

// ---------------------------------------------------------------------------------------
// AnchorPoint
// ---------------------------------------------------------------------------------------

AnchorPoint AnchorPoint::onView(const DrawingView& v, const Point2& sheetPos) {
  AnchorPoint a;
  a.viewId = v.id;
  a.local = v.fromSheet(sheetPos);
  a.sheet = sheetPos;
  return a;
}

AnchorPoint AnchorPoint::free(const Point2& sheetPos) {
  AnchorPoint a;
  a.sheet = sheetPos;
  return a;
}

Point2 AnchorPoint::resolve(const Sheet& sheet) const {
  if (viewId != core::kInvalidId)
    if (const DrawingView* v = sheet.view(viewId)) return v->toSheet(local);
  return this->sheet;
}

QJsonObject AnchorPoint::toJson() const {
  QJsonObject o;
  if (viewId != core::kInvalidId) {
    o["view"] = idToJson(viewId);
    o["local"] = pt(local);
  }
  o["sheet"] = pt(sheet);
  return o;
}

AnchorPoint AnchorPoint::fromJson(const QJsonObject& o) {
  AnchorPoint a;
  a.viewId = o.contains("view") ? idFromJson(o["view"]) : core::kInvalidId;
  a.local = ptFrom(o["local"]);
  a.sheet = ptFrom(o["sheet"]);
  return a;
}

// ---------------------------------------------------------------------------------------
// JSON of the value types
// ---------------------------------------------------------------------------------------

QJsonObject HoleCallout::toJson() const {
  QJsonObject o;
  o["id"] = idToJson(id);
  o["view"] = idToJson(viewId);
  o["ref"] = ref.toJson();
  o["position"] = pt(position);
  if (thread) o["thread"] = true;
  if (!threadOverride.empty()) o["threadOverride"] = qs(threadOverride);
  if (threadClass != "6H") o["threadClass"] = qs(threadClass);
  if (!overrideText.empty()) o["override"] = qs(overrideText);
  if (countOverride > 0) o["count"] = countOverride;
  if (precision >= 0) o["precision"] = precision;
  if (!fastenerLine) o["fastenerLine"] = false;
  return o;
}

HoleCallout HoleCallout::fromJson(const QJsonObject& o) {
  HoleCallout c;
  c.id = idFromJson(o["id"]);
  c.viewId = idFromJson(o["view"]);
  c.ref = DimRef::fromJson(o["ref"].toObject());
  c.position = ptFrom(o["position"]);
  c.thread = o["thread"].toBool(false);
  c.threadOverride = ss(o["threadOverride"].toString());
  c.threadClass = ss(o["threadClass"].toString("6H"));
  c.overrideText = ss(o["override"].toString());
  c.countOverride = o["count"].toInt(0);
  c.precision = o["precision"].toInt(-1);
  c.fastenerLine = o["fastenerLine"].toBool(true);
  return c;
}

static QJsonObject rowToJson(const FcfRow& r) {
  QJsonObject o;
  o["characteristic"] = gdtCharacteristicName(r.characteristic);
  if (r.diameterZone) o["diameter"] = true;
  o["tolerance"] = r.tolerance;
  if (!r.toleranceText.empty()) o["toleranceText"] = qs(r.toleranceText);
  if (r.modifier != GdtModifier::None) o["modifier"] = gdtModifierName(r.modifier);
  QJsonArray d;
  for (const auto& dr : r.datums) {
    QJsonObject x;
    x["letter"] = qs(dr.letter);
    if (dr.modifier != GdtModifier::None) x["modifier"] = gdtModifierName(dr.modifier);
    d.push_back(x);
  }
  if (!d.isEmpty()) o["datums"] = d;
  return o;
}

static FcfRow rowFromJson(const QJsonObject& o) {
  FcfRow r;
  gdtCharacteristicFromName(ss(o["characteristic"].toString()), r.characteristic);
  r.diameterZone = o["diameter"].toBool(false);
  r.tolerance = o["tolerance"].toDouble(0.1);
  r.toleranceText = ss(o["toleranceText"].toString());
  r.modifier = gdtModifierFromName(ss(o["modifier"].toString()));
  for (const auto& v : o["datums"].toArray()) {
    QJsonObject x = v.toObject();
    r.datums.push_back({ss(x["letter"].toString()), gdtModifierFromName(ss(x["modifier"].toString()))});
  }
  return r;
}

QJsonObject FeatureControlFrame::toJson() const {
  QJsonObject o;
  o["id"] = idToJson(id);
  QJsonArray r;
  for (const auto& row : rows) r.push_back(rowToJson(row));
  o["rows"] = r;
  o["position"] = pt(position);
  o["leader"] = leader;
  o["anchor"] = anchor.toJson();
  if (!above.empty()) o["above"] = qs(above);
  if (!below.empty()) o["below"] = qs(below);
  return o;
}

FeatureControlFrame FeatureControlFrame::fromJson(const QJsonObject& o) {
  FeatureControlFrame f;
  f.id = idFromJson(o["id"]);
  f.rows.clear();
  for (const auto& v : o["rows"].toArray()) f.rows.push_back(rowFromJson(v.toObject()));
  if (f.rows.empty()) f.rows.push_back(FcfRow{});
  f.position = ptFrom(o["position"]);
  f.leader = o["leader"].toBool(true);
  f.anchor = AnchorPoint::fromJson(o["anchor"].toObject());
  f.above = ss(o["above"].toString());
  f.below = ss(o["below"].toString());
  return f;
}

bool parseFcfRow(const std::string& text, FcfRow& out) {
  std::istringstream in(text);
  std::vector<std::string> tok;
  std::string t;
  while (in >> t) tok.push_back(t);
  if (tok.empty()) return false;
  FcfRow r;
  if (!gdtCharacteristicFromName(tok[0], r.characteristic)) return false;
  bool haveTol = false;
  for (size_t i = 1; i < tok.size(); ++i) {
    std::string w = tok[i];
    std::string l = lower(w);
    if (l == "dia" || l == "diameter" || w == "\xC3\x98") {
      r.diameterZone = true;
      continue;
    }
    if (w.rfind("\xC3\x98", 0) == 0) {  // "Ø0.05"
      r.diameterZone = true;
      w = w.substr(2);
      l = lower(w);
    }
    if (!haveTol) {
      try {
        size_t used = 0;
        double v = std::stod(w, &used);
        if (used == w.size()) {
          r.tolerance = v;
          haveTol = true;
          continue;
        }
      } catch (...) {
      }
    }
    GdtModifier m = gdtModifierFromName(w);
    bool single = w.size() == 1 || (w.size() == 3 && w[0] == '(' && w[2] == ')');
    if (single && m != GdtModifier::None) {
      // a single M/L/F/T/P after the tolerance (or after a datum letter) is a modifier
      if (r.datums.empty()) r.modifier = m;
      else r.datums.back().modifier = m;
      continue;
    }
    // datum letter (A..Z, possibly "A-B")
    bool letter = !w.empty() && std::isupper(static_cast<unsigned char>(w[0]));
    if (letter && r.datums.size() < 3) {
      r.datums.push_back({w, GdtModifier::None});
      continue;
    }
    if (l.size() > 1 && r.datums.empty()) {
      GdtModifier mm = gdtModifierFromName(l);
      if (mm != GdtModifier::None) {
        r.modifier = mm;
        continue;
      }
    }
    return false;
  }
  if (!haveTol) return false;
  out = r;
  return true;
}

QJsonObject DatumFeature::toJson() const {
  QJsonObject o;
  o["id"] = idToJson(id);
  o["letter"] = qs(letter);
  o["position"] = pt(position);
  o["anchor"] = anchor.toJson();
  return o;
}

DatumFeature DatumFeature::fromJson(const QJsonObject& o) {
  DatumFeature d;
  d.id = idFromJson(o["id"]);
  d.letter = ss(o["letter"].toString("A"));
  d.position = ptFrom(o["position"]);
  d.anchor = AnchorPoint::fromJson(o["anchor"].toObject());
  return d;
}

const char* surfaceProcessName(SurfaceProcess p) {
  switch (p) {
    case SurfaceProcess::Any: return "Any";
    case SurfaceProcess::RemovalRequired: return "RemovalRequired";
    case SurfaceProcess::RemovalProhibited: return "RemovalProhibited";
  }
  return "Any";
}

SurfaceProcess surfaceProcessFromName(const std::string& s) {
  std::string l = lower(s);
  if (l == "removalrequired" || l == "required" || l == "machined" || l == "mrr") return SurfaceProcess::RemovalRequired;
  if (l == "removalprohibited" || l == "prohibited" || l == "mrp") return SurfaceProcess::RemovalProhibited;
  return SurfaceProcess::Any;
}

QJsonObject SurfaceFinish::toJson() const {
  QJsonObject o;
  o["id"] = idToJson(id);
  o["process"] = surfaceProcessName(process);
  if (allAround) o["allAround"] = true;
  o["roughness"] = qs(roughness);
  if (!secondary.empty()) o["secondary"] = qs(secondary);
  if (!method.empty()) o["method"] = qs(method);
  if (!lay.empty()) o["lay"] = qs(lay);
  if (!allowance.empty()) o["allowance"] = qs(allowance);
  o["anchor"] = anchor.toJson();
  o["leader"] = leader;
  o["position"] = pt(position);
  return o;
}

SurfaceFinish SurfaceFinish::fromJson(const QJsonObject& o) {
  SurfaceFinish s;
  s.id = idFromJson(o["id"]);
  s.process = surfaceProcessFromName(ss(o["process"].toString()));
  s.allAround = o["allAround"].toBool(false);
  s.roughness = ss(o["roughness"].toString());
  s.secondary = ss(o["secondary"].toString());
  s.method = ss(o["method"].toString());
  s.lay = ss(o["lay"].toString());
  s.allowance = ss(o["allowance"].toString());
  s.anchor = AnchorPoint::fromJson(o["anchor"].toObject());
  s.leader = o["leader"].toBool(false);
  s.position = ptFrom(o["position"]);
  return s;
}

QJsonObject OrdinateDimension::toJson() const {
  QJsonObject o;
  o["id"] = idToJson(id);
  o["view"] = idToJson(viewId);
  o["axis"] = axis == OrdinateAxis::X ? "X" : "Y";
  o["origin"] = origin.toJson();
  o["originText"] = pt(originText);
  QJsonArray m;
  for (const auto& mem : members) {
    QJsonObject x;
    x["ref"] = mem.ref.toJson();
    x["text"] = pt(mem.textPos);
    m.push_back(x);
  }
  o["members"] = m;
  if (precision >= 0) o["precision"] = precision;
  return o;
}

OrdinateDimension OrdinateDimension::fromJson(const QJsonObject& o) {
  OrdinateDimension d;
  d.id = idFromJson(o["id"]);
  d.viewId = idFromJson(o["view"]);
  d.axis = o["axis"].toString() == "Y" ? OrdinateAxis::Y : OrdinateAxis::X;
  d.origin = DimRef::fromJson(o["origin"].toObject());
  d.originText = ptFrom(o["originText"]);
  for (const auto& v : o["members"].toArray()) {
    QJsonObject x = v.toObject();
    OrdinateMember m;
    m.ref = DimRef::fromJson(x["ref"].toObject());
    m.textPos = ptFrom(x["text"]);
    d.members.push_back(m);
  }
  d.precision = o["precision"].toInt(-1);
  return d;
}

namespace {
/// Measured point of a resolved reference: vertex, arc centre or segment midpoint.
Point2 targetPoint(const DimTarget& t) {
  if (t.isVertex) return t.point;
  if (t.curve.type == CurveType::Arc) return t.curve.center;
  return t.curve.midpoint();
}
}  // namespace

void updateOrdinate(OrdinateDimension& o, const DrawingView& view) {
  DimTarget origin = resolveDimRef(o.origin, view);
  o.valid = origin.ok;
  Point2 op = origin.ok ? targetPoint(origin) : Point2{};
  for (auto& m : o.members) {
    DimTarget t = resolveDimRef(m.ref, view);
    m.valid = origin.ok && t.ok;
    if (!m.valid) continue;
    Point2 p = targetPoint(t);
    m.value = o.axis == OrdinateAxis::X ? std::abs(p.x - op.x) : std::abs(p.y - op.y);
  }
}

// ---------------------------------------------------------------------------------------
// Baseline / chain
// ---------------------------------------------------------------------------------------

namespace {
struct RefPos {
  DimRef ref;
  Point2 local;
};
std::vector<RefPos> resolveAll(const DrawingView& view, const std::vector<DimRef>& refs) {
  std::vector<RefPos> out;
  for (const auto& r : refs) {
    DimTarget t = resolveDimRef(r, view);
    if (t.ok) out.push_back({r, targetPoint(t)});
  }
  return out;
}
}  // namespace

std::vector<Dimension> makeBaselineDimensions(const DrawingView& view, const DimRef& base, const std::vector<DimRef>& points,
                                              DimensionType type, const Point2& firstLine, double spacing) {
  std::vector<Dimension> out;
  DimTarget bt = resolveDimRef(base, view);
  if (!bt.ok) return out;
  const Point2 b = targetPoint(bt);
  const bool horiz = type != DimensionType::Vertical;
  auto along = [&](const Point2& p) { return horiz ? p.x : p.y; };
  std::vector<RefPos> pts = resolveAll(view, points);
  std::sort(pts.begin(), pts.end(), [&](const RefPos& x, const RefPos& y) {
    return std::abs(along(x.local) - along(b)) < std::abs(along(y.local) - along(b));
  });
  // stack away from the view: direction from the view centre towards the first line
  Point2 c = view.sheetBBox().center();
  double sgn = horiz ? (firstLine.y >= c.y ? 1.0 : -1.0) : (firstLine.x >= c.x ? 1.0 : -1.0);
  Point2 bs = view.toSheet(b);
  int i = 0;
  for (const auto& p : pts) {
    if (std::abs(along(p.local) - along(b)) < 1e-9) continue;
    Dimension d;
    d.viewId = view.id;
    d.type = horiz ? DimensionType::Horizontal : DimensionType::Vertical;
    d.refs = {base, p.ref};
    Point2 ps = view.toSheet(p.local);
    Point2 mid = (bs + ps) * 0.5;
    double off = sgn * spacing * i++;
    d.textPos = horiz ? Point2{mid.x, firstLine.y + off} : Point2{firstLine.x + off, mid.y};
    if (updateDimension(d, view)) out.push_back(std::move(d));
  }
  return out;
}

std::vector<Dimension> makeChainDimensions(const DrawingView& view, const std::vector<DimRef>& points, DimensionType type,
                                           const Point2& line) {
  std::vector<Dimension> out;
  const bool horiz = type != DimensionType::Vertical;
  std::vector<RefPos> pts = resolveAll(view, points);
  std::sort(pts.begin(), pts.end(), [&](const RefPos& x, const RefPos& y) {
    return horiz ? x.local.x < y.local.x : x.local.y < y.local.y;
  });
  for (size_t i = 0; i + 1 < pts.size(); ++i) {
    const RefPos& a = pts[i];
    const RefPos& b = pts[i + 1];
    if (std::abs(horiz ? b.local.x - a.local.x : b.local.y - a.local.y) < 1e-9) continue;
    Dimension d;
    d.viewId = view.id;
    d.type = horiz ? DimensionType::Horizontal : DimensionType::Vertical;
    d.refs = {a.ref, b.ref};
    Point2 mid = (view.toSheet(a.local) + view.toSheet(b.local)) * 0.5;
    d.textPos = horiz ? Point2{mid.x, line.y} : Point2{line.x, mid.y};
    if (updateDimension(d, view)) out.push_back(std::move(d));
  }
  return out;
}

// ---------------------------------------------------------------------------------------
// Revision table
// ---------------------------------------------------------------------------------------

double RevisionTable::width() const {
  double w = 0;
  for (double c : columnWidths) w += c;
  return w;
}

double RevisionTable::height() const { return rowHeight * static_cast<double>(rows.size() + 1); }

std::string RevisionTable::nextRev() const {
  // letters A..Z, then AA, AB ... (ASME Y14.35 skips I, O, Q, S, X, Z; we keep it simple)
  int n = static_cast<int>(rows.size());
  std::string s;
  do {
    s.insert(s.begin(), static_cast<char>('A' + n % 26));
    n = n / 26 - 1;
  } while (n >= 0);
  return s;
}

QJsonObject RevisionTable::toJson() const {
  QJsonObject o;
  o["id"] = idToJson(id);
  o["position"] = pt(position);
  QJsonArray cols, widths, rs;
  for (const auto& c : columns) cols.push_back(qs(c));
  for (double w : columnWidths) widths.push_back(w);
  for (const auto& r : rows) rs.push_back(QJsonArray{qs(r.rev), qs(r.description), qs(r.date), qs(r.approved)});
  o["columns"] = cols;
  o["widths"] = widths;
  o["rows"] = rs;
  o["rowHeight"] = rowHeight;
  o["textHeight"] = textHeight;
  return o;
}

RevisionTable RevisionTable::fromJson(const QJsonObject& o) {
  RevisionTable t;
  t.id = idFromJson(o["id"]);
  t.position = ptFrom(o["position"]);
  if (o.contains("columns")) {
    t.columns.clear();
    for (const auto& c : o["columns"].toArray()) t.columns.push_back(ss(c.toString()));
  }
  if (o.contains("widths")) {
    t.columnWidths.clear();
    for (const auto& w : o["widths"].toArray()) t.columnWidths.push_back(w.toDouble());
  }
  for (const auto& r : o["rows"].toArray()) {
    auto a = r.toArray();
    RevisionRow row;
    row.rev = ss(a.at(0).toString());
    row.description = ss(a.at(1).toString());
    row.date = ss(a.at(2).toString());
    row.approved = ss(a.at(3).toString());
    t.rows.push_back(row);
  }
  t.rowHeight = o["rowHeight"].toDouble(7.0);
  t.textHeight = o["textHeight"].toDouble(3.0);
  return t;
}

Point2 defaultRevisionTablePosition(const Sheet& sheet, const RevisionTable& table) {
  BBox2 f = sheet.frameRect();
  return {f.max.x - table.width(), f.min.y};
}

// ---------------------------------------------------------------------------------------
// CalloutSet
// ---------------------------------------------------------------------------------------

bool CalloutSet::empty() const {
  return holeCallouts.empty() && featureFrames.empty() && datums.empty() && surfaceFinishes.empty() && ordinates.empty() &&
         revisionTables.empty() && holeTables.empty();
}

std::vector<ObjectId> CalloutSet::ids() const {
  std::vector<ObjectId> out;
  for (const auto& e : holeCallouts) out.push_back(e.id);
  for (const auto& e : featureFrames) out.push_back(e.id);
  for (const auto& e : datums) out.push_back(e.id);
  for (const auto& e : surfaceFinishes) out.push_back(e.id);
  for (const auto& e : ordinates) out.push_back(e.id);
  for (const auto& e : revisionTables) out.push_back(e.id);
  for (const auto& e : holeTables) out.push_back(e.id);
  return out;
}

bool CalloutSet::contains(ObjectId id) const {
  auto v = ids();
  return std::find(v.begin(), v.end(), id) != v.end();
}

bool CalloutSet::erase(ObjectId id) {
  return eraseIn(holeCallouts, id) || eraseIn(featureFrames, id) || eraseIn(datums, id) || eraseIn(surfaceFinishes, id) ||
         eraseIn(ordinates, id) || eraseIn(revisionTables, id) ||
         eraseIn(holeTables, id);
}

std::vector<ObjectId> CalloutSet::entitiesOf(ObjectId viewId) const {
  std::vector<ObjectId> out;
  if (viewId == core::kInvalidId) return out;
  for (const auto& e : holeCallouts)
    if (e.viewId == viewId) out.push_back(e.id);
  for (const auto& e : ordinates)
    if (e.viewId == viewId) out.push_back(e.id);
  for (const auto& e : featureFrames)
    if (e.anchor.viewId == viewId) out.push_back(e.id);
  for (const auto& e : datums)
    if (e.anchor.viewId == viewId) out.push_back(e.id);
  for (const auto& e : surfaceFinishes)
    if (e.anchor.viewId == viewId) out.push_back(e.id);
  for (const auto& e : holeTables)
    if (e.viewId == viewId) out.push_back(e.id);
  return out;
}

ObjectId CalloutSet::viewOf(ObjectId id) const {
  for (const auto& e : holeCallouts)
    if (e.id == id) return e.viewId;
  for (const auto& e : ordinates)
    if (e.id == id) return e.viewId;
  for (const auto& e : featureFrames)
    if (e.id == id) return e.anchor.viewId;
  for (const auto& e : datums)
    if (e.id == id) return e.anchor.viewId;
  for (const auto& e : surfaceFinishes)
    if (e.id == id) return e.anchor.viewId;
  for (const auto& e : holeTables)
    if (e.id == id) return e.viewId;
  return core::kInvalidId;
}

bool CalloutSet::move(ObjectId id, const Point2& delta, const Sheet* sheet) {
  if (auto* c = holeCallout(id)) {
    c->position += delta;
    return true;
  }
  if (auto* f = featureFrame(id)) {
    f->position += delta;
    if (!f->leader) {
      // a frame without leader carries its anchor along
      if (f->anchor.viewId == core::kInvalidId) f->anchor.sheet += delta;
    }
    return true;
  }
  if (auto* d = datum(id)) {
    d->position += delta;
    return true;
  }
  if (auto* s = surfaceFinish(id)) {
    if (s->leader) s->position += delta;
    else if (s->anchor.viewId == core::kInvalidId) s->anchor.sheet += delta;
    else if (const DrawingView* v = sheet ? sheet->view(s->anchor.viewId) : nullptr)
      s->anchor.local = v->fromSheet(v->toSheet(s->anchor.local) + delta);  // slides along the view
    return true;
  }
  if (auto* o = ordinate(id)) {
    o->originText += delta;
    for (auto& m : o->members) m.textPos += delta;
    return true;
  }
  if (auto* t = revisionTable(id)) {
    t->position += delta;
    return true;
  }
  if (auto* t = holeTable(id)) {
    t->position += delta;
    return true;
  }
  return false;
}

void CalloutSet::followView(ObjectId viewId, const Point2& delta) {
  for (auto& c : holeCallouts)
    if (c.viewId == viewId) c.position += delta;
  for (auto& o : ordinates)
    if (o.viewId == viewId) {
      o.originText += delta;
      for (auto& m : o.members) m.textPos += delta;
    }
  for (auto& f : featureFrames)
    if (f.anchor.viewId == viewId) f.position += delta;
  for (auto& d : datums)
    if (d.anchor.viewId == viewId) d.position += delta;
  for (auto& s : surfaceFinishes)
    if (s.anchor.viewId == viewId && s.leader) s.position += delta;
}

HoleCallout* CalloutSet::holeCallout(ObjectId id) { return findIn(holeCallouts, id); }
FeatureControlFrame* CalloutSet::featureFrame(ObjectId id) { return findIn(featureFrames, id); }
DatumFeature* CalloutSet::datum(ObjectId id) { return findIn(datums, id); }
SurfaceFinish* CalloutSet::surfaceFinish(ObjectId id) { return findIn(surfaceFinishes, id); }
OrdinateDimension* CalloutSet::ordinate(ObjectId id) { return findIn(ordinates, id); }
RevisionTable* CalloutSet::revisionTable(ObjectId id) { return findIn(revisionTables, id); }
HoleTable* CalloutSet::holeTable(ObjectId id) { return findIn(holeTables, id); }

void CalloutSet::writeJson(QJsonObject& o) const {
  if (!holeCallouts.empty()) o["holeCallouts"] = arrayOf(holeCallouts);
  if (!featureFrames.empty()) o["featureFrames"] = arrayOf(featureFrames);
  if (!datums.empty()) o["datums"] = arrayOf(datums);
  if (!surfaceFinishes.empty()) o["surfaceFinishes"] = arrayOf(surfaceFinishes);
  if (!ordinates.empty()) o["ordinates"] = arrayOf(ordinates);
  if (!revisionTables.empty()) o["revisionTables"] = arrayOf(revisionTables);
  if (!holeTables.empty()) o["holeTables"] = arrayOf(holeTables);
}

CalloutSet CalloutSet::readJson(const QJsonObject& o) {
  CalloutSet s;
  s.holeCallouts = vectorOf<HoleCallout>(o["holeCallouts"]);
  s.featureFrames = vectorOf<FeatureControlFrame>(o["featureFrames"]);
  s.datums = vectorOf<DatumFeature>(o["datums"]);
  s.surfaceFinishes = vectorOf<SurfaceFinish>(o["surfaceFinishes"]);
  s.ordinates = vectorOf<OrdinateDimension>(o["ordinates"]);
  s.revisionTables = vectorOf<RevisionTable>(o["revisionTables"]);
  s.holeTables = vectorOf<HoleTable>(o["holeTables"]);
  return s;
}

void refreshCallouts(const core::Document& doc, Sheet& sheet) {
  for (auto& c : sheet.callouts.holeCallouts) {
    const DrawingView* v = sheet.view(c.viewId);
    c.lines.clear();
    c.info = HoleInfo{};
    c.orphaned = false;
    if (!v || !v->geometryValid) continue;
    DimTarget t = resolveDimRef(c.ref, *v);
    if (!t.ok || t.isVertex || t.curve.type != CurveType::Arc) {
      c.orphaned = true;
      continue;
    }
    c.info = analyzeHole(doc, t.curve);
    if (!c.info.ok) {
      // not recognised as a hole in the B-rep: at least the projected diameter
      c.info.ok = true;
      c.info.diameter = 2 * t.curve.radius;
      c.info.source = "projection";
    }
    c.lines = holeCalloutLines(c.info, c, doc.units());
  }
  for (auto& o : sheet.callouts.ordinates)
    if (const DrawingView* v = sheet.view(o.viewId)) updateOrdinate(o, *v);
  for (auto& t : sheet.callouts.holeTables) {
    const DrawingView* v = sheet.view(t.viewId);
    t.orphaned = !v || !v->geometryValid;
    t.rows = t.orphaned ? std::vector<HoleTableRow>{} : holeTableRows(doc, *v, doc.units());
  }
}

}  // namespace fabricad::drawing
