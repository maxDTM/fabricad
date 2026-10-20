// FabriCAD core: feature type factory and module registration entry point.
//
// Every module exposes exactly one function, declared in src/<module>/<Module>Module.h:
//     namespace fabricad::<module> { void registerModule(fabricad::core::ModuleContext& ctx); }
// The app calls them in dependency order at startup (sketch, part, assembly, drawing, viewer).
// registerModule() adds feature types (needed to load .fcad files) and tools (UI commands).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fabricad::core {

class Feature;
class ToolRegistry;

class FeatureFactory {
 public:
  using Creator = std::function<std::unique_ptr<Feature>()>;

  /// Process-wide factory (Document::fromJson needs it without extra plumbing).
  static FeatureFactory& instance();

  void registerType(const std::string& type, Creator creator);
  bool has(const std::string& type) const;
  /// nullptr if the type is unknown.
  std::unique_ptr<Feature> create(const std::string& type) const;
  std::vector<std::string> types() const;
  void clear();

 private:
  std::map<std::string, Creator> creators_;
};

/// Handed to each module's registerModule().
struct ModuleContext {
  FeatureFactory& features;
  ToolRegistry& tools;
};

}  // namespace fabricad::core
