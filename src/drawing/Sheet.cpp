// FabriCAD drawing: sheet formats, title block and Sheet (de)serialisation.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "drawing/Sheet.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "core/Json.h"

#include <QJsonArray>

#include <algorithm>
#include <cstdio>
#include <set>

namespace fabricad::drawing {

using core::idFromJson;
using core::idToJson;
using core::qs;
using core::ss;

// ---------------------------------------------------------------------------------------
// SheetFormat
// ---------------------------------------------------------------------------------------

const std::vector<SheetFormat>& sheetFormats() {
  static const std::vector<SheetFormat> formats = {
      {"A4", 297, 210, true},  {"A3", 420, 297, true},  {"A2", 594, 420, true},   {"A1", 841, 594, true},
      {"A0", 1189, 841, true}, {"ANSI A", 279.4, 215.9, true}, {"ANSI B", 431.8, 279.4, true},
      {"ANSI C", 558.8, 431.8, true}, {"ANSI D", 863.6, 558.8, true}, {"ANSI E", 1117.6, 863.6, true},
  };
  return formats;
}

std::optional<SheetFormat> sheetFormatByName(const std::string& name) {
  for (const auto& f : sheetFormats())
    if (f.name == name) return f;
  return std::nullopt;
}

std::string sheetFormatLabel(const SheetFormat& f) {
  char buf[96];
  std::snprintf(buf, sizeof buf, "%s (%g x %g mm)", f.name.c_str(), f.width, f.height);
  return buf;
}

QJsonObject SheetFormat::toJson() const {
  QJsonObject o;
  o["name"] = qs(name);
  o["width"] = width;
  o["height"] = height;
  o["landscape"] = landscape;
  return o;
}

SheetFormat SheetFormat::fromJson(const QJsonObject& o) {
  SheetFormat f;
  f.name = ss(o["name"].toString("A3"));
  f.width = o["width"].toDouble(420);
  f.height = o["height"].toDouble(297);
  f.landscape = o["landscape"].toBool(true);
  return f;
}

// ---------------------------------------------------------------------------------------
// TitleBlock
// ---------------------------------------------------------------------------------------

const std::vector<std::string>& TitleBlock::fieldNames() {
  static const std::vector<std::string> names = {"Title",    "PartNumber", "DrawnBy", "Date",     "Checked",
                                                 "Approved", "Scale",      "Sheet",   "Size",     "Rev",
                                                 "Material", "Mass",       "Company"};
  return names;
}

const std::vector<std::string>& titleBlockTokens() {
  static const std::vector<std::string> t = {"${material}", "${mass}",   "${mass:g,1}", "${partNumber}",
                                             "${revision}", "${description}", "${vendor}", "${title}"};
  return t;
}

std::string expandTitleBlockTokens(const std::string& value, const TitleBlockModel& model) {
  std::string out;
  size_t pos = 0;
  while (pos < value.size()) {
    const size_t open = value.find("${", pos);
    if (open == std::string::npos) {
      out += value.substr(pos);
      break;
    }
    const size_t close = value.find('}', open + 2);
    if (close == std::string::npos) {
      out += value.substr(pos);
      break;
    }
    out += value.substr(pos, open - pos);
    const std::string body = value.substr(open + 2, close - open - 2);
    std::string name = body.substr(0, body.find(':'));
    std::string key;
    for (char c : name)
      if (!std::isspace(static_cast<unsigned char>(c))) key += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (key == "mass" || key == "weight") {
      std::string unit = "kg";
      int decimals = 3;
      if (const size_t colon = body.find(':'); colon != std::string::npos) {
        std::string spec = body.substr(colon + 1);
        const size_t comma = spec.find(',');
        if (comma != std::string::npos) {
          decimals = std::clamp(std::atoi(spec.c_str() + comma + 1), 0, 9);
          spec = spec.substr(0, comma);
        }
        std::string u;
        for (char c : spec)
          if (!std::isspace(static_cast<unsigned char>(c))) u += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (u == "g" || u == "kg" || u == "lb") unit = u;
      }
      if (model.massGrams > 0) {
        const double v = unit == "g" ? model.massGrams : unit == "lb" ? model.massGrams / 453.59237 : model.massGrams / 1000.0;
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.*f %s", decimals, v, unit.c_str());
        out += buf;
      }
    } else if (key == "rev") {
      auto it = model.text.find("revision");
      if (it != model.text.end()) out += it->second;
    } else if (auto it = model.text.find(key == "partno" || key == "pn" ? "partnumber" : key); it != model.text.end()) {
      out += it->second;
    } else {
      out += value.substr(open, close - open + 1);  // unknown: verbatim
    }
    pos = close + 1;
  }
  return out;
}

bool TitleBlock::isSheetField(const std::string& name) { return name == "Scale" || name == "Sheet" || name == "Size"; }

std::string TitleBlock::field(const std::string& name) const {
  auto it = fields.find(name);
  return it == fields.end() ? std::string() : it->second;
}

void TitleBlock::setField(const std::string& name, const std::string& value) {
  if (value.empty()) fields.erase(name);
  else fields[name] = value;
}

QJsonObject TitleBlock::toJson() const {
  QJsonObject o;
  QJsonObject f;
  for (const auto& [k, v] : fields) f[qs(k)] = qs(v);
  o["fields"] = f;
  o["width"] = width;
  o["height"] = height;
  o["visible"] = visible;
  return o;
}

TitleBlock TitleBlock::fromJson(const QJsonObject& o) {
  TitleBlock t;
  QJsonObject f = o["fields"].toObject();
  for (auto it = f.begin(); it != f.end(); ++it) t.fields[ss(it.key())] = ss(it.value().toString());
  t.width = o["width"].toDouble(180);
  t.height = o["height"].toDouble(56);
  t.visible = o["visible"].toBool(true);
  return t;
}

// ---------------------------------------------------------------------------------------
// Sheet
// ---------------------------------------------------------------------------------------

BBox2 Sheet::frameRect() const {
  BBox2 b;
  b.add({bindingMargin, margin});
  b.add({format.width - margin, format.height - margin});
  return b;
}

BBox2 Sheet::titleBlockRect() const {
  BBox2 f = frameRect();
  BBox2 b;
  b.add({f.max.x - titleBlock.width, f.max.y - titleBlock.height});
  b.add(f.max);
  return b;
}

template <class T>
static T* findById(std::vector<T>& v, core::ObjectId id) {
  for (auto& e : v)
    if (e.id == id) return &e;
  return nullptr;
}
template <class T>
static const T* findById(const std::vector<T>& v, core::ObjectId id) {
  for (const auto& e : v)
    if (e.id == id) return &e;
  return nullptr;
}

DrawingView* Sheet::view(core::ObjectId id) { return findById(views, id); }
const DrawingView* Sheet::view(core::ObjectId id) const { return findById(views, id); }
Dimension* Sheet::dimension(core::ObjectId id) { return findById(dimensions, id); }
const Dimension* Sheet::dimension(core::ObjectId id) const { return findById(dimensions, id); }
TextNote* Sheet::note(core::ObjectId id) { return findById(notes, id); }
Balloon* Sheet::balloon(core::ObjectId id) { return findById(balloons, id); }
PartsList* Sheet::partsList(core::ObjectId id) { return findById(partsLists, id); }
CenterMark* Sheet::centerMark(core::ObjectId id) { return findById(centerMarks, id); }
CenterLine* Sheet::centerLine(core::ObjectId id) { return findById(centerLines, id); }

std::vector<core::ObjectId> Sheet::childrenOf(core::ObjectId viewId) const {
  std::vector<core::ObjectId> out;
  for (const auto& v : views)
    if (v.parent == viewId) out.push_back(v.id);
  return out;
}

std::vector<core::ObjectId> Sheet::entitiesOf(core::ObjectId viewId) const {
  std::vector<core::ObjectId> out;
  for (const auto& d : dimensions)
    if (d.viewId == viewId) out.push_back(d.id);
  for (const auto& m : centerMarks)
    if (m.viewId == viewId) out.push_back(m.id);
  for (const auto& l : centerLines)
    if (l.viewId == viewId) out.push_back(l.id);
  for (const auto& b : balloons)
    if (b.viewId == viewId) out.push_back(b.id);
  for (core::ObjectId id : callouts.entitiesOf(viewId)) out.push_back(id);
  return out;
}

bool Sheet::contains(core::ObjectId id) const {
  return view(id) || dimension(id) || findById(notes, id) || findById(centerMarks, id) || findById(centerLines, id) ||
         findById(balloons, id) || findById(partsLists, id) || callouts.contains(id);
}

std::string Sheet::nextViewLabel() const {
  std::set<std::string> used;
  for (const auto& v : views) used.insert(v.label);
  for (int i = 1;; ++i) {
    std::string l = "VIEW" + std::to_string(i);
    if (!used.count(l)) return l;
  }
}

std::string Sheet::nextLetterLabel() const {
  std::set<std::string> used;
  for (const auto& v : views) {
    if (v.type == ViewType::Section) used.insert(v.section.label);
    if (v.type == ViewType::Detail) used.insert(v.detail.label);
  }
  for (char c = 'A'; c <= 'Z'; ++c) {
    std::string l(1, c);
    if (!used.count(l)) return l;
  }
  for (int i = 1;; ++i) {
    std::string l = "A" + std::to_string(i);
    if (!used.count(l)) return l;
  }
}

template <class T>
static QJsonArray arrayOf(const std::vector<T>& v) {
  QJsonArray a;
  for (const auto& e : v) a.push_back(e.toJson());
  return a;
}
template <class T>
static std::vector<T> vectorOf(const QJsonValue& v) {
  std::vector<T> out;
  for (const auto& e : v.toArray()) out.push_back(T::fromJson(e.toObject()));
  return out;
}

QJsonObject Sheet::toJson() const {
  QJsonObject o;
  o["id"] = idToJson(id);
  o["name"] = qs(name);
  o["format"] = format.toJson();
  o["standard"] = projectionStandardName(standard);
  o["scale"] = scale;
  o["margin"] = margin;
  o["bindingMargin"] = bindingMargin;
  o["showZones"] = showZones;
  o["titleBlock"] = titleBlock.toJson();
  o["views"] = arrayOf(views);
  o["dimensions"] = arrayOf(dimensions);
  o["notes"] = arrayOf(notes);
  o["centerMarks"] = arrayOf(centerMarks);
  o["centerLines"] = arrayOf(centerLines);
  o["balloons"] = arrayOf(balloons);
  o["partsLists"] = arrayOf(partsLists);
  callouts.writeJson(o);
  return o;
}

Sheet Sheet::fromJson(const QJsonObject& o) {
  Sheet s;
  s.id = idFromJson(o["id"]);
  s.name = ss(o["name"].toString("Sheet1"));
  s.format = SheetFormat::fromJson(o["format"].toObject());
  s.standard = projectionStandardFromName(ss(o["standard"].toString()));
  s.scale = o["scale"].toDouble(1.0);
  s.margin = o["margin"].toDouble(10.0);
  s.bindingMargin = o["bindingMargin"].toDouble(20.0);
  s.showZones = o["showZones"].toBool(true);
  s.titleBlock = TitleBlock::fromJson(o["titleBlock"].toObject());
  s.views = vectorOf<DrawingView>(o["views"]);
  s.dimensions = vectorOf<Dimension>(o["dimensions"]);
  s.notes = vectorOf<TextNote>(o["notes"]);
  s.centerMarks = vectorOf<CenterMark>(o["centerMarks"]);
  s.centerLines = vectorOf<CenterLine>(o["centerLines"]);
  s.balloons = vectorOf<Balloon>(o["balloons"]);
  s.partsLists = vectorOf<PartsList>(o["partsLists"]);
  s.callouts = CalloutSet::readJson(o);
  return s;
}

}  // namespace fabricad::drawing
