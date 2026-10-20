// FabriCAD core: Component (Fusion-style). The root component is the document.
// Child components form a tree; each has a placement relative to its parent.
// Occurrences: several components may share a `definition` (the component whose
// bodies/features they instance). The assembly module owns joints and the solver;
// core only stores the tree and the placements.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Types.h"

#include <QJsonObject>
#include <gp_Trsf.hxx>

#include <string>
#include <vector>

namespace fabricad::core {

struct Component {
  ObjectId id = kInvalidId;
  std::string name;                   ///< "Bracket" (UI shows "Bracket:1" for occurrences)
  ObjectId parent = kInvalidId;       ///< kInvalidId for the root
  std::vector<ObjectId> children;     ///< child component ids, in order
  ObjectId definition = kInvalidId;   ///< non-invalid: this is an occurrence of that component
  gp_Trsf placement;                  ///< relative to parent
  bool grounded = false;
  bool visible = true;
  std::string externalPath;           ///< non-empty: linked external .fcad file
  std::string partNumber;             ///< BOM data
  std::string description;
  std::string material;

  bool isRoot() const { return parent == kInvalidId; }
  bool isOccurrence() const { return definition != kInvalidId; }

  QJsonObject toJson() const;
  static Component fromJson(const QJsonObject& o);
};

}  // namespace fabricad::core
