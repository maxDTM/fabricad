// Body / Component JSON (de)serialisation.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/Body.h"
#include "core/Component.h"
#include "core/Json.h"

#include <QJsonArray>

#include <algorithm>

namespace fabricad::core {

QJsonObject Body::toJson() const {
  QJsonObject o;
  o["id"] = idToJson(id);
  o["name"] = qs(name);
  o["component"] = idToJson(component);
  o["creator"] = idToJson(creator);
  o["visible"] = visible;
  o["color"] = QJsonArray{color[0], color[1], color[2], color[3]};
  if (!material.empty()) o["material"] = qs(material);
  if (density > 0) o["density"] = density;
  return o;
}

Body Body::fromJson(const QJsonObject& o) {
  Body b;
  b.id = idFromJson(o["id"]);
  b.name = ss(o["name"].toString());
  b.component = idFromJson(o["component"], kRootComponentId);
  b.creator = idFromJson(o["creator"]);
  b.visible = o["visible"].toBool(true);
  QJsonArray c = o["color"].toArray();
  if (c.size() == 4)
    for (int i = 0; i < 4; ++i) b.color[static_cast<size_t>(i)] = static_cast<float>(std::clamp(c[i].toDouble(), 0.0, 1.0));
  b.material = ss(o["material"].toString());
  b.density = o["density"].toDouble(0.0);
  return b;
}

QJsonObject Component::toJson() const {
  QJsonObject o;
  o["id"] = idToJson(id);
  o["name"] = qs(name);
  o["parent"] = idToJson(parent);
  o["children"] = core::toJson(children);
  if (definition != kInvalidId) o["definition"] = idToJson(definition);
  o["placement"] = trsfToJson(placement);
  o["grounded"] = grounded;
  o["visible"] = visible;
  if (!externalPath.empty()) o["externalPath"] = qs(externalPath);
  if (!partNumber.empty()) o["partNumber"] = qs(partNumber);
  if (!description.empty()) o["description"] = qs(description);
  if (!material.empty()) o["material"] = qs(material);
  return o;
}

Component Component::fromJson(const QJsonObject& o) {
  Component c;
  c.id = idFromJson(o["id"]);
  c.name = ss(o["name"].toString());
  c.parent = idFromJson(o["parent"]);
  c.children = idsFromJson(o["children"]);
  c.definition = idFromJson(o["definition"]);
  c.placement = trsfFromJson(o["placement"]);
  c.grounded = o["grounded"].toBool(false);
  c.visible = o["visible"].toBool(true);
  c.externalPath = ss(o["externalPath"].toString());
  c.partNumber = ss(o["partNumber"].toString());
  c.description = ss(o["description"].toString());
  c.material = ss(o["material"].toString());
  return c;
}

}  // namespace fabricad::core
