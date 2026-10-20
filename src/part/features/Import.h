// FabriCAD part: Import feature (STEP / STL files become bodies) and export helpers.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "part/PartFeature.h"

#include <TopoDS_Shape.hxx>

#include <string>
#include <vector>

namespace fabricad::part {

class ImportFeature : public PartFeature {
 public:
  static constexpr const char* kType = "Import";
  ImportFeature();
  void compute(core::RecomputeContext& ctx, Outcome& out) override;
  std::string browserFolder() const override { return {}; }
  void applyDefaultsForNewFeature(const core::Document&) override {}

  /// Read a STEP (.step/.stp) or STL (.stl) file into one shape per body. Errors in `error`.
  struct Imported {
    std::vector<TopoDS_Shape> shapes;
    std::vector<std::string> names;
  };
  static bool readFile(const std::string& path, Imported& out, std::string* error);

 private:
  std::string cachedPath_;
  long long cachedStamp_ = -1;
  Imported cache_;
};

/// Write shapes to a STEP file (AP214, mm). One product per shape.
bool exportStep(const std::vector<TopoDS_Shape>& shapes, const std::vector<std::string>& names, const std::string& path,
                std::string* error);
/// Write shapes to an STL file (binary by default) after meshing with the given deflection.
bool exportStl(const std::vector<TopoDS_Shape>& shapes, const std::string& path, bool binary, double linearDeflection,
               double angularDeflectionDeg, std::string* error);

}  // namespace fabricad::part
