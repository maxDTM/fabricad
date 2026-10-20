// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/Registry.h"

#include "core/Feature.h"

namespace fabricad::core {

FeatureFactory& FeatureFactory::instance() {
  static FeatureFactory f;
  return f;
}

void FeatureFactory::registerType(const std::string& type, Creator creator) { creators_[type] = std::move(creator); }

bool FeatureFactory::has(const std::string& type) const { return creators_.count(type) > 0; }

std::unique_ptr<Feature> FeatureFactory::create(const std::string& type) const {
  auto it = creators_.find(type);
  if (it == creators_.end()) return nullptr;
  return it->second();
}

std::vector<std::string> FeatureFactory::types() const {
  std::vector<std::string> out;
  for (const auto& [k, v] : creators_) out.push_back(k);
  return out;
}

void FeatureFactory::clear() { creators_.clear(); }

}  // namespace fabricad::core
