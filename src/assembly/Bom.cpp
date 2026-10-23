// FabriCAD assembly: BOM builder.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "assembly/Bom.h"

#include "assembly/ComponentOps.h"
#include "core/ShapeUtils.h"

#include <algorithm>
#include <cctype>
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


/// Bodies (or none) and children of a component's definition; sub-assemblies count as rows.
BomTable buildStructuredBom(const Document& doc, const BomOptions& opt) {
  BomTable table;
  const core::Component* root = doc.component(opt.root);
  if (!root) return table;
  auto rowFor = [&](ObjectId comp, ObjectId parent, int level) {
    ObjectId def = definitionOf(doc, comp);
    const core::Component* c = doc.component(comp);
    const core::Component* d = doc.component(def);
    BomRow row;
    row.definition = def;
    row.parent = parent;
    row.name = d->name;
    row.partNumber = !c->partNumber.empty() ? c->partNumber : d->partNumber;
    row.description = !c->description.empty() ? c->description : d->description;
    row.material = !c->material.empty() ? c->material : materialOf(doc, def);
    row.revision = !c->revision.empty() ? c->revision : d->revision;
    row.vendor = !c->vendor.empty() ? c->vendor : d->vendor;
    row.cost = c->cost != 0.0 ? c->cost : d->cost;
    row.level = level;
    massAndVolume(doc, def, row.volume, row.mass, row.bodyCount);
    return row;
  };
  // mass of a sub-assembly instance: its own bodies plus its children's (recursively)
  std::function<void(ObjectId, double&, double&)> treeMass = [&](ObjectId comp, double& vol, double& mass) {
    ObjectId def = definitionOf(doc, comp);
    double v, m;
    int n;
    massAndVolume(doc, def, v, m, n);
    vol += v;
    mass += m;
    if (const core::Component* d = doc.component(def))
      for (ObjectId ch : d->children) treeMass(ch, vol, mass);
  };
  if (opt.includeRootBodies) {
    BomRow row = rowFor(opt.root, kInvalidId, 0);
    if (row.bodyCount > 0) {
      row.occurrences = {opt.root};
      row.quantity = 1;
      row.material = materialOf(doc, opt.root);
      table.rows.push_back(row);
    }
  }
  std::function<void(ObjectId, int, const std::string&, int)> walk = [&](ObjectId parent, int level, const std::string& prefix,
                                                                        int n) {
    const core::Component* pc = doc.component(definitionOf(doc, parent));
    if (!pc) return;
    // group the parent's children by definition (or external file), first-seen order
    std::vector<std::pair<std::string, std::vector<ObjectId>>> groups;
    for (ObjectId child : pc->children) {
      ObjectId def = definitionOf(doc, child);
      const core::Component* d = doc.component(def);
      if (!d) continue;
      std::string key = !d->externalPath.empty() ? "ext:" + d->externalPath : std::format("def:{}", def);
      auto it = std::find_if(groups.begin(), groups.end(), [&](const auto& g) { return g.first == key; });
      if (it == groups.end()) groups.push_back({key, {child}});
      else it->second.push_back(child);
    }
    for (const auto& [key, comps] : groups) {
      ObjectId def = definitionOf(doc, comps.front());
      const core::Component* d = doc.component(def);
      const bool assembly = d && !d->children.empty();
      BomRow row = rowFor(comps.front(), parent, level);
      if (row.bodyCount == 0 && !assembly && !opt.includeEmpty) continue;
      if (assembly) {
        row.volume = row.mass = 0;
        treeMass(comps.front(), row.volume, row.mass);
      }
      row.occurrences = comps;
      row.quantity = static_cast<int>(comps.size());
      row.itemPath = prefix.empty() ? std::to_string(++n) : prefix + "." + std::to_string(++n);
      table.rows.push_back(row);
      if (assembly) walk(comps.front(), level + 1, row.itemPath, 0);
    }
  };
  // the root's own bodies take item 1 when present; the top-level children follow
  if (!table.rows.empty()) table.rows.front().itemPath = "1";
  walk(opt.root, 0, "", static_cast<int>(table.rows.size()));
  int item = 1;
  for (BomRow& r : table.rows) {
    r.item = item++;
    table.totalQuantity += r.quantity;
  }
  return table;
}

}  // namespace

BomTable buildBom(const Document& doc, const BomOptions& opt) {
  BomTable table;
  const core::Component* root = doc.component(opt.root);
  if (!root) return table;

  if (opt.structured) return buildStructuredBom(doc, opt);
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
      row.revision = !c->revision.empty() ? c->revision : d->revision;
      row.vendor = !c->vendor.empty() ? c->vendor : d->vendor;
      row.cost = c->cost != 0.0 ? c->cost : d->cost;
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
      row.revision = root->revision;
      row.vendor = root->vendor;
      row.cost = root->cost;
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
    r.itemPath = std::to_string(r.item);
    table.totalQuantity += r.quantity;
  }
  return table;
}

const std::vector<BomColumn>& defaultBomColumns() {
  static const std::vector<BomColumn> cols = {BomColumn::Item,     BomColumn::Name, BomColumn::PartNumber, BomColumn::Description,
                                              BomColumn::Material, BomColumn::Qty,  BomColumn::Mass};
  return cols;
}

namespace {
struct ColumnName {
  BomColumn col;
  const char* key;
  const char* header;
};
constexpr ColumnName kColumns[] = {
    {BomColumn::Item, "item", "Item"},
    {BomColumn::Level, "level", "Level"},
    {BomColumn::Name, "name", "Name"},
    {BomColumn::PartNumber, "partNumber", "Part Number"},
    {BomColumn::Description, "description", "Description"},
    {BomColumn::Material, "material", "Material"},
    {BomColumn::Qty, "qty", "Qty"},
    {BomColumn::Mass, "mass", "Mass [g]"},
    {BomColumn::MassTotal, "massTotal", "Mass total [g]"},
    {BomColumn::Volume, "volume", "Volume [cm3]"},
    {BomColumn::Revision, "revision", "Revision"},
    {BomColumn::Vendor, "vendor", "Vendor"},
    {BomColumn::Cost, "cost", "Cost"},
    {BomColumn::CostTotal, "costTotal", "Cost total"},
};
std::string lowerAlnum(const std::string& s) {
  std::string out;
  for (char c : s)
    if (std::isalnum(static_cast<unsigned char>(c))) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}
std::string numberText(double v) { return v > 0 ? std::format("{:.3f}", v) : std::string(); }
}  // namespace

std::string bomColumnHeader(BomColumn c) {
  for (const ColumnName& n : kColumns)
    if (n.col == c) return n.header;
  return {};
}

std::string bomColumnKey(BomColumn c) {
  for (const ColumnName& n : kColumns)
    if (n.col == c) return n.key;
  return {};
}

bool parseBomColumns(const std::string& text, std::vector<BomColumn>& out, std::string* err) {
  out.clear();
  if (text.empty()) {
    out = defaultBomColumns();
    return true;
  }
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t comma = text.find(',', pos);
    if (comma == std::string::npos) comma = text.size();
    const std::string word = lowerAlnum(text.substr(pos, comma - pos));
    pos = comma + 1;
    if (word.empty()) continue;
    bool found = false;
    for (const ColumnName& n : kColumns)
      if (word == lowerAlnum(n.key) || word == lowerAlnum(n.header) || (word == "quantity" && n.col == BomColumn::Qty) ||
          (word == "partno" && n.col == BomColumn::PartNumber) || (word == "rev" && n.col == BomColumn::Revision)) {
        out.push_back(n.col);
        found = true;
        break;
      }
    if (!found) {
      if (err) {
        *err = "unknown BOM column '" + word + "' (one of";
        for (const ColumnName& n : kColumns) *err += std::string(" ") + n.key;
        *err += ")";
      }
      return false;
    }
  }
  if (out.empty()) out = defaultBomColumns();
  return true;
}

std::string bomCell(const BomRow& r, BomColumn c, bool structured) {
  switch (c) {
    case BomColumn::Item: return structured && !r.itemPath.empty() ? r.itemPath : std::to_string(r.item);
    case BomColumn::Level: return std::to_string(r.level + 1);
    case BomColumn::Name: return (structured ? std::string(2 * static_cast<size_t>(r.level), ' ') : std::string()) + r.name;
    // no part number given: the name, as the on-sheet parts list prints it (DRW2-ASM-OUTPUT-7)
    case BomColumn::PartNumber: return r.partNumber.empty() ? r.name : r.partNumber;
    case BomColumn::Description: return r.description;
    case BomColumn::Material: return r.material;
    case BomColumn::Qty: return std::to_string(r.quantity);
    case BomColumn::Mass: return numberText(r.mass);
    case BomColumn::MassTotal: return numberText(r.mass * r.quantity);
    case BomColumn::Volume: return numberText(r.volume * 1e-3);
    case BomColumn::Revision: return r.revision;
    case BomColumn::Vendor: return r.vendor;
    case BomColumn::Cost: return r.cost > 0 ? std::format("{:.2f}", r.cost) : std::string();
    case BomColumn::CostTotal: return r.cost > 0 ? std::format("{:.2f}", r.cost * r.quantity) : std::string();
  }
  return {};
}

std::string bomToCsv(const BomTable& table, const std::vector<BomColumn>& columns, bool structured) {
  std::string out;
  for (size_t i = 0; i < columns.size(); ++i) out += (i ? "," : "") + csvCell(bomColumnHeader(columns[i]));
  out += "\n";
  for (const BomRow& r : table.rows) {
    for (size_t i = 0; i < columns.size(); ++i) out += (i ? "," : "") + csvCell(bomCell(r, columns[i], structured));
    out += "\n";
  }
  return out;
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
