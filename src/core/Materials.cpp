// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/Materials.h"

#include <algorithm>
#include <cctype>

namespace fabricad::core {

const std::vector<Material>& materialLibrary() {
  // densities: common engineering handbook values (g/cm^3)
  static const std::vector<Material> lib = {
      {"Steel", "Metal", 7.85, {0.62f, 0.63f, 0.66f, 1.0f}},
      {"Stainless Steel", "Metal", 8.00, {0.72f, 0.73f, 0.75f, 1.0f}},
      {"Cast Iron", "Metal", 7.20, {0.42f, 0.42f, 0.44f, 1.0f}},
      {"Aluminum 6061", "Metal", 2.70, {0.80f, 0.81f, 0.83f, 1.0f}},
      {"Aluminum 7075", "Metal", 2.81, {0.78f, 0.79f, 0.82f, 1.0f}},
      {"Brass", "Metal", 8.50, {0.80f, 0.65f, 0.30f, 1.0f}},
      {"Bronze", "Metal", 8.80, {0.70f, 0.50f, 0.30f, 1.0f}},
      {"Copper", "Metal", 8.96, {0.78f, 0.47f, 0.33f, 1.0f}},
      {"Titanium", "Metal", 4.51, {0.60f, 0.60f, 0.63f, 1.0f}},
      {"Zinc", "Metal", 7.14, {0.70f, 0.72f, 0.74f, 1.0f}},
      {"ABS Plastic", "Plastic", 1.04, {0.90f, 0.90f, 0.88f, 1.0f}},
      {"PLA", "Plastic", 1.25, {0.95f, 0.80f, 0.30f, 1.0f}},
      {"Nylon 6/6", "Plastic", 1.14, {0.93f, 0.92f, 0.86f, 1.0f}},
      {"Polycarbonate", "Plastic", 1.20, {0.80f, 0.90f, 0.95f, 0.6f}},
      {"Acrylic (PMMA)", "Plastic", 1.18, {0.85f, 0.92f, 0.97f, 0.5f}},
      {"POM (Delrin)", "Plastic", 1.41, {0.96f, 0.96f, 0.94f, 1.0f}},
      {"PTFE", "Plastic", 2.20, {0.98f, 0.98f, 0.98f, 1.0f}},
      {"Rubber", "Plastic", 1.10, {0.15f, 0.15f, 0.15f, 1.0f}},
      {"Oak", "Wood", 0.75, {0.72f, 0.53f, 0.30f, 1.0f}},
      {"Pine", "Wood", 0.50, {0.85f, 0.70f, 0.45f, 1.0f}},
      {"Plywood", "Wood", 0.60, {0.80f, 0.66f, 0.42f, 1.0f}},
      {"MDF", "Wood", 0.75, {0.70f, 0.60f, 0.45f, 1.0f}},
      {"Glass", "Other", 2.50, {0.75f, 0.88f, 0.92f, 0.4f}},
      {"Concrete", "Other", 2.40, {0.65f, 0.65f, 0.62f, 1.0f}},
      {"Carbon Fibre", "Other", 1.60, {0.20f, 0.20f, 0.22f, 1.0f}},
      {"Water", "Other", 1.00, {0.40f, 0.60f, 0.90f, 0.4f}},
  };
  return lib;
}

const Material* findMaterial(const std::string& name) {
  auto lower = [](std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
  };
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

}  // namespace fabricad::core
