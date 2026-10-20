// FabriCAD core: built-in physical material table (name, density, display colour).
//
// Body::material / Body::density / Body::color are set from one of these entries by the
// browser's "Physical Material" menu; the BOM and the Measure tool read them back. The table is
// deliberately small and generic; a custom material is any free-text name with its own density.
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
};

/// The built-in library, grouped by category, stable order.
const std::vector<Material>& materialLibrary();
/// Case-insensitive lookup by name; nullptr when unknown.
const Material* findMaterial(const std::string& name);
/// Distinct categories in library order.
std::vector<std::string> materialCategories();

}  // namespace fabricad::core
