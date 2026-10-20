// FabriCAD core: Body record. The shape itself lives in the Document's recompute
// state (Document::bodyShape(id)), because it depends on the timeline position.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Types.h"

#include <QJsonObject>

#include <array>
#include <string>

namespace fabricad::core {

struct Body {
  ObjectId id = kInvalidId;
  std::string name;                        ///< "Body1"
  ObjectId component = kRootComponentId;   ///< owning component
  ObjectId creator = kInvalidId;           ///< feature that created it (kInvalidId for imports)
  bool visible = true;
  std::array<float, 4> color{0.73f, 0.74f, 0.77f, 1.0f};  ///< RGBA 0..1, default steel grey
  std::string material;                    ///< free text for now ("Steel", "ABS")
  double density = 0.0;                    ///< g/cm^3, 0 = unknown

  QJsonObject toJson() const;
  static Body fromJson(const QJsonObject& o);
};

}  // namespace fabricad::core
