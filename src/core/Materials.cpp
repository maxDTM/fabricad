// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/Materials.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <cctype>
#include <memory>

namespace fabricad::core {

namespace {

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

QJsonObject toJson(const Material& m) {
  QJsonObject o;
  o["name"] = QString::fromStdString(m.name);
  o["category"] = QString::fromStdString(m.category);
  o["density"] = m.density;
  o["color"] = QJsonArray{m.color[0], m.color[1], m.color[2], m.color[3]};
  if (!m.standard.empty()) o["standard"] = QString::fromStdString(m.standard);
  if (m.yieldMPa > 0) o["yield"] = m.yieldMPa;
  if (m.costPerKg > 0) o["costPerKg"] = m.costPerKg;
  return o;
}

bool fromJson(const QJsonObject& o, Material& m) {
  m.name = o["name"].toString().trimmed().toStdString();
  if (m.name.empty()) return false;
  m.category = o["category"].toString("Other").toStdString();
  if (m.category.empty()) m.category = "Other";
  m.density = o["density"].toDouble(0.0);
  if (!(m.density >= 0)) m.density = 0;
  const QJsonArray c = o["color"].toArray();
  if (c.size() >= 3)
    for (int i = 0; i < 4; ++i)
      m.color[static_cast<size_t>(i)] = static_cast<float>(std::clamp(i < c.size() ? c[i].toDouble(1.0) : 1.0, 0.0, 1.0));
  m.standard = o["standard"].toString().toStdString();
  m.yieldMPa = o["yield"].toDouble(0.0);
  m.costPerKg = o["costPerKg"].toDouble(0.0);
  m.source = "user";
  return true;
}

std::vector<Material> readUserFile(const std::string& path) {
  std::vector<Material> out;
  QFile f(QString::fromStdString(path));
  if (!f.open(QIODevice::ReadOnly)) return out;
  const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
  const QJsonArray arr = doc.isArray() ? doc.array() : doc.object()["materials"].toArray();
  for (const QJsonValue& v : arr) {
    Material m;
    if (fromJson(v.toObject(), m)) out.push_back(std::move(m));
  }
  return out;
}

bool writeUserFile(const std::string& path, const std::vector<Material>& mats, std::string* err) {
  QDir().mkpath(QFileInfo(QString::fromStdString(path)).absolutePath());
  QJsonArray arr;
  for (const Material& m : mats) arr.append(toJson(m));
  QJsonObject root;
  root["format"] = "fabricad-materials";
  root["version"] = 1;
  root["materials"] = arr;
  const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
  QSaveFile f(QString::fromStdString(path));
  if (!f.open(QIODevice::WriteOnly) || f.write(bytes) < 0 || !f.commit()) {
    if (err) *err = "Cannot write " + path;
    return false;
  }
  return true;
}

std::unique_ptr<std::vector<Material>>& cache() {
  static std::unique_ptr<std::vector<Material>> lib;
  return lib;
}

}  // namespace

const std::vector<Material>& builtinMaterials() {
  // densities: common engineering handbook values (g/cm^3); yield: typical minimum (MPa);
  // costs are left 0 (unknown) so nobody prices a part from a guess
  static const std::vector<Material> lib = {
      {"Steel", "Metal", 7.85, {0.62f, 0.63f, 0.66f, 1.0f}, "ASTM A36 / S235", 250},
      {"Stainless Steel", "Metal", 8.00, {0.72f, 0.73f, 0.75f, 1.0f}, "AISI 304 / 1.4301", 215},
      {"Cast Iron", "Metal", 7.20, {0.42f, 0.42f, 0.44f, 1.0f}, "EN-GJL-250", 165},
      {"Aluminum 6061", "Metal", 2.70, {0.80f, 0.81f, 0.83f, 1.0f}, "EN AW-6061-T6", 276},
      {"Aluminum 7075", "Metal", 2.81, {0.78f, 0.79f, 0.82f, 1.0f}, "EN AW-7075-T6", 503},
      {"Brass", "Metal", 8.50, {0.80f, 0.65f, 0.30f, 1.0f}, "CW614N (CuZn39Pb3)", 250},
      {"Bronze", "Metal", 8.80, {0.70f, 0.50f, 0.30f, 1.0f}, "CC493K", 140},
      {"Copper", "Metal", 8.96, {0.78f, 0.47f, 0.33f, 1.0f}, "Cu-ETP CW004A", 70},
      {"Titanium", "Metal", 4.51, {0.60f, 0.60f, 0.63f, 1.0f}, "Grade 2", 275},
      {"Zinc", "Metal", 7.14, {0.70f, 0.72f, 0.74f, 1.0f}, "ZAMAK 3", 208},
      {"ABS Plastic", "Plastic", 1.04, {0.90f, 0.90f, 0.88f, 1.0f}, "ISO 1043 ABS", 40},
      {"PLA", "Plastic", 1.25, {0.95f, 0.80f, 0.30f, 1.0f}, "ISO 1043 PLA", 60},
      {"Nylon 6/6", "Plastic", 1.14, {0.93f, 0.92f, 0.86f, 1.0f}, "ISO 1043 PA66", 70},
      {"Polycarbonate", "Plastic", 1.20, {0.80f, 0.90f, 0.95f, 0.6f}, "ISO 1043 PC", 62},
      {"Acrylic (PMMA)", "Plastic", 1.18, {0.85f, 0.92f, 0.97f, 0.5f}, "ISO 1043 PMMA", 70},
      {"POM (Delrin)", "Plastic", 1.41, {0.96f, 0.96f, 0.94f, 1.0f}, "ISO 1043 POM-H", 70},
      {"PTFE", "Plastic", 2.20, {0.98f, 0.98f, 0.98f, 1.0f}, "ISO 1043 PTFE", 20},
      {"Rubber", "Plastic", 1.10, {0.15f, 0.15f, 0.15f, 1.0f}, "EPDM 70 Shore A", 0},
      // 3D printing (PRODUCTION-8): filament / powder / resin densities of the printed material
      {"PETG", "Plastic", 1.27, {0.55f, 0.75f, 0.85f, 1.0f}, "ISO 1043 PETG (FDM)", 50},
      {"ASA", "Plastic", 1.07, {0.92f, 0.92f, 0.90f, 1.0f}, "ISO 1043 ASA (FDM)", 45},
      {"TPU 95A", "Plastic", 1.21, {0.25f, 0.25f, 0.28f, 1.0f}, "ISO 1043 TPU, Shore 95A (FDM)", 0},
      {"Nylon CF (PA-CF)", "Plastic", 1.18, {0.22f, 0.22f, 0.24f, 1.0f}, "PA6/PA12 + chopped carbon fibre (FDM)", 90},
      {"PA12 (SLS/MJF)", "Plastic", 1.01, {0.45f, 0.45f, 0.47f, 1.0f}, "ISO 1043 PA12, sintered", 48},
      {"Resin (SLA standard)", "Plastic", 1.18, {0.70f, 0.72f, 0.74f, 1.0f}, "Methacrylate photopolymer", 50},
      {"Polypropylene", "Plastic", 0.90, {0.92f, 0.92f, 0.90f, 1.0f}, "ISO 1043 PP", 30},
      {"HDPE", "Plastic", 0.95, {0.94f, 0.94f, 0.92f, 1.0f}, "ISO 1043 PE-HD", 25},
      {"Oak", "Wood", 0.75, {0.72f, 0.53f, 0.30f, 1.0f}, "Quercus robur", 0},
      {"Pine", "Wood", 0.50, {0.85f, 0.70f, 0.45f, 1.0f}, "Pinus sylvestris C24", 0},
      {"Plywood", "Wood", 0.60, {0.80f, 0.66f, 0.42f, 1.0f}, "EN 636 (birch)", 0},
      {"MDF", "Wood", 0.75, {0.70f, 0.60f, 0.45f, 1.0f}, "EN 622-5", 0},
      {"Baltic Birch Plywood", "Wood", 0.68, {0.86f, 0.75f, 0.55f, 1.0f}, "EN 636 (birch, all plies), 12/18 mm", 0},
      {"Maple (hard)", "Wood", 0.70, {0.90f, 0.80f, 0.62f, 1.0f}, "Acer saccharum", 0},
      {"Walnut", "Wood", 0.61, {0.42f, 0.30f, 0.22f, 1.0f}, "Juglans nigra", 0},
      {"Birch", "Wood", 0.67, {0.88f, 0.78f, 0.60f, 1.0f}, "Betula pendula", 0},
      {"Beech", "Wood", 0.72, {0.85f, 0.68f, 0.50f, 1.0f}, "Fagus sylvatica", 0},
      {"Cherry", "Wood", 0.56, {0.72f, 0.45f, 0.32f, 1.0f}, "Prunus serotina", 0},
      {"OSB", "Wood", 0.62, {0.80f, 0.68f, 0.45f, 1.0f}, "EN 300 OSB/3", 0},
      {"Glass", "Other", 2.50, {0.75f, 0.88f, 0.92f, 0.4f}, "Soda-lime EN 572", 0},
      {"Concrete", "Other", 2.40, {0.65f, 0.65f, 0.62f, 1.0f}, "C25/30", 0},
      {"Carbon Fibre", "Other", 1.60, {0.20f, 0.20f, 0.22f, 1.0f}, "CFRP quasi-isotropic", 600},
      {"Water", "Other", 1.00, {0.40f, 0.60f, 0.90f, 0.4f}, "", 0},
  };
  return lib;
}

std::string userMaterialLibraryPath() {
  QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
  if (base.isEmpty()) base = QDir::homePath() + "/.config";
  return (base + "/FabriCAD/materials.json").toStdString();
}

void reloadMaterialLibrary() { cache().reset(); }

const std::vector<Material>& materialLibrary() {
  auto& lib = cache();
  if (lib) return *lib;
  lib = std::make_unique<std::vector<Material>>(builtinMaterials());
  for (Material& u : readUserFile(userMaterialLibraryPath())) {
    auto it = std::find_if(lib->begin(), lib->end(), [&](const Material& m) { return lower(m.name) == lower(u.name); });
    if (it != lib->end()) *it = std::move(u);
    else lib->push_back(std::move(u));
  }
  return *lib;
}

const Material* findMaterial(const std::string& name) {
  const std::string key = lower(name);
  for (const Material& m : materialLibrary())
    if (lower(m.name) == key) return &m;
  return nullptr;
}

std::vector<std::string> materialCategories() {
  std::vector<std::string> out;
  for (const Material& m : materialLibrary())
    if (std::find(out.begin(), out.end(), m.category) == out.end()) out.push_back(m.category);
  return out;
}

std::vector<const Material*> searchMaterials(const std::string& filter, const std::string& category) {
  std::vector<const Material*> out;
  const std::string f = lower(filter);
  const std::string cat = lower(category);
  for (const Material& m : materialLibrary()) {
    if (!cat.empty() && lower(m.category) != cat) continue;
    if (!f.empty() && lower(m.name).find(f) == std::string::npos && lower(m.category).find(f) == std::string::npos &&
        lower(m.standard).find(f) == std::string::npos)
      continue;
    out.push_back(&m);
  }
  return out;
}

bool saveUserMaterial(const Material& m, std::string* err) {
  if (m.name.empty()) {
    if (err) *err = "a material needs a name";
    return false;
  }
  const std::string path = userMaterialLibraryPath();
  std::vector<Material> mats = readUserFile(path);
  Material copy = m;
  copy.source = "user";
  if (copy.category.empty()) copy.category = "Other";
  auto it = std::find_if(mats.begin(), mats.end(), [&](const Material& x) { return lower(x.name) == lower(m.name); });
  if (it != mats.end()) *it = copy;
  else mats.push_back(copy);
  if (!writeUserFile(path, mats, err)) return false;
  reloadMaterialLibrary();
  return true;
}

bool removeUserMaterial(const std::string& name, std::string* err) {
  const std::string path = userMaterialLibraryPath();
  std::vector<Material> mats = readUserFile(path);
  const auto n = std::erase_if(mats, [&](const Material& x) { return lower(x.name) == lower(name); });
  if (n == 0) return false;
  if (!writeUserFile(path, mats, err)) return false;
  reloadMaterialLibrary();
  return true;
}

}  // namespace fabricad::core
