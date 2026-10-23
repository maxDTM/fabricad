// FabriCAD assembly: BOM builder.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "assembly/Bom.h"

#include "assembly/ComponentOps.h"
#include "core/ShapeUtils.h"

#include <algorithm>
#include <format>
#include <functional>
#include <map>

namespace fabricad::assembly {

using core::Document;
using core::kInvalidId;
using core::ObjectId;

namespace {

std::string csvCell(const std::string& s) {
  if (s.find_first_of(",\"\n") == std::string::npos) return s;
  std::string out = "\"";
  for (char c : s) {
    if (c == '"') out += '"';
    out += c;
  }
  return out + "\"";
}

/// Material text for a component: its own field, else distinct body materials.
std::string materialOf(const Document& doc, ObjectId def) {
  const core::Component* c = doc.component(def);
  if (c && !c->material.empty()) return c->material;
  std::vector<std::string> mats;
  for (ObjectId b : displayedBodies(doc, def)) {
    const core::Body* body = doc.body(b);
    if (body && !body->material.empty() && std::find(mats.begin(), mats.end(), body->material) == mats.end())
      mats.push_back(body->material);
  }
  std::string out;
  for (const auto& m : mats) out += (out.empty() ? "" : ", ") + m;
  return out;
}

void massAndVolume(const Document& doc, ObjectId def, double& volume, double& mass, int& bodyCount) {
  volume = 0;
  mass = 0;
  bodyCount = 0;
  for (ObjectId b : displayedBodies(doc, def)) {
    const core::Body* body = doc.body(b);
    double v = core::volume(doc.bodyShape(b));
    volume += v;
    if (body && body->density > 0) mass += v * 1e-3 * body->density;  // mm^3 -> cm^3 (1e-3) * g/cm^3
    ++bodyCount;
  }
}

}  // namespace

BomTable buildBom(const Document& doc, const BomOptions& opt) {
  BomTable table;
  const core::Component* root = doc.component(opt.root);
  if (!root) return table;

  // group key -> row index; key = definition id, or "ext:<path>" for external links
  std::map<std::string, size_t> rows;
  auto keyFor = [&](ObjectId comp) {
    ObjectId def = definitionOf(doc, comp);
    const core::Component* c = doc.component(def);
    if (c && !c->externalPath.empty()) return "ext:" + c->externalPath;
    return std::format("def:{}", def);
  };
  auto addInstance = [&](ObjectId comp, int level) {
    ObjectId def = definitionOf(doc, comp);
    const core::Component* c = doc.component(comp);
    const core::Component* d = doc.component(def);
    if (!c || !d) return;
    double volume, mass;
    int bodies;
    massAndVolume(doc, def, volume, mass, bodies);
    if (bodies == 0 && !opt.includeEmpty) return;
    std::string key = keyFor(comp);
    auto it = rows.find(key);
    if (it == rows.end()) {
      BomRow row;
      row.definition = def;
      row.name = d->name;
      row.partNumber = !c->partNumber.empty() ? c->partNumber : d->partNumber;
      row.description = !c->description.empty() ? c->description : d->description;
      row.material = !c->material.empty() ? c->material : materialOf(doc, def);
      row.level = level;
      row.volume = volume;
      row.mass = mass;
      row.bodyCount = bodies;
      table.rows.push_back(row);
      it = rows.emplace(key, table.rows.size() - 1).first;
    }
    BomRow& row = table.rows[it->second];
    row.occurrences.push_back(comp);
    row.quantity += 1;
    row.level = std::min(row.level, level);
  };

  if (opt.includeRootBodies) {
    double volume, mass;
    int bodies;
    massAndVolume(doc, opt.root, volume, mass, bodies);
    if (bodies > 0) {
      BomRow row;
      row.definition = opt.root;
      row.occurrences = {opt.root};
      row.name = root->name;
      row.partNumber = root->partNumber;
      row.description = root->description;
      row.material = materialOf(doc, opt.root);
      row.quantity = 1;
      row.level = 0;
      row.volume = volume;
      row.mass = mass;
      row.bodyCount = bodies;
      table.rows.push_back(row);
      rows[keyFor(opt.root)] = table.rows.size() - 1;
    }
  }

  std::function<void(ObjectId, int)> walk = [&](ObjectId comp, int level) {
    const core::Component* c = doc.component(comp);
    if (!c) return;
    for (ObjectId child : c->children) {
      addInstance(child, level);
      // an occurrence's children are the definition's; do not descend into them twice
      const core::Component* cc = doc.component(child);
      if (opt.flatten && cc && cc->definition == kInvalidId) walk(child, level + 1);
    }
  };
  walk(opt.root, 0);

  int item = 1;
  for (BomRow& r : table.rows) {
    r.item = item++;
    table.totalQuantity += r.quantity;
  }
  return table;
}

std::string bomToCsv(const BomTable& table) {
  std::string out = "Item,Name,Part Number,Description,Material,Qty,Mass [g]\n";
  for (const BomRow& r : table.rows) {
    out += std::format("{},{},{},{},{},{},{}\n", r.item, csvCell(r.name),
                       // no part number given: the name, as the on-sheet parts list prints it (DRW2-ASM-OUTPUT-7)
                       csvCell(r.partNumber.empty() ? r.name : r.partNumber), csvCell(r.description),
                       csvCell(r.material), r.quantity, r.mass > 0 ? std::format("{:.3f}", r.mass) : std::string());
  }
  return out;
}

}  // namespace fabricad::assembly
