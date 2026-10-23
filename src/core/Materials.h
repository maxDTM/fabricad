// FabriCAD core: physical material library (name, density, display colour, standard, yield,
// cost).
//
// Body::material / Body::density / Body::color are set from one of these entries by the
// browser's "Physical Material" picker; the BOM, Mass Properties and the Measure tool read them
// back. The built-in table is small and generic; a user library
// `$XDG_CONFIG_HOME/FabriCAD/materials.json` (default ~/.config/FabriCAD/materials.json, same
// fields) is read on first use and merged over the built-ins by name (case-insensitive), so a
// user entry can override a built-in density. A custom material is any free-text name with its
// own density; "Save to library" adds it to the user file.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <array>
#include <string>
#include <vector>

namespace fabricad::core {

struct Material {
  std::string name;              ///< "Steel", "Aluminum 6061", "ABS Plastic"...
  std::string category;          ///< "Metal", "Plastic", "Wood", "Other"
  double density = 0.0;          ///< g/cm^3
  std::array<float, 4> color{0.73f, 0.74f, 0.77f, 1.0f};  ///< RGBA 0..1 appearance
  std::string standard;          ///< "EN AW-6061", "ASTM A36", "ISO 1043 PLA" (informative)
  double yieldMPa = 0.0;         ///< yield / tensile strength (informative), 0 = unknown
  double costPerKg = 0.0;        ///< material cost per kg, 0 = unknown
  std::string source = "builtin";  ///< "builtin" | "user"
};

/// The library: built-ins (grouped by category, stable order) merged with the user library.
const std::vector<Material>& materialLibrary();
/// The built-in table only.
const std::vector<Material>& builtinMaterials();
/// Case-insensitive lookup by name; nullptr when unknown.
const Material* findMaterial(const std::string& name);
/// Distinct categories in library order.
std::vector<std::string> materialCategories();
/// Materials whose name, category or standard contains `filter` (case-insensitive), optionally
/// restricted to one category (empty = all). Library order.
std::vector<const Material*> searchMaterials(const std::string& filter, const std::string& category = {});

/// Path of the user library file (may not exist).
std::string userMaterialLibraryPath();
/// Add or replace (by name) a material in the user library file and reload. False with `err`
/// when the file cannot be written.
bool saveUserMaterial(const Material& m, std::string* err = nullptr);
/// Remove a user material by name (built-ins cannot be removed). True when one was removed.
bool removeUserMaterial(const std::string& name, std::string* err = nullptr);
/// Drop the cached library so the next materialLibrary() re-reads the user file.
void reloadMaterialLibrary();

}  // namespace fabricad::core
