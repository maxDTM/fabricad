// FabriCAD app: ui/* settings.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/UiSettings.h"

#include "app/ThemeManager.h"
#include "theme/Tokens.h"

#include <QSettings>

namespace fabricad::app {

UiSettings& UiSettings::instance() {
  static UiSettings* s = new UiSettings;  // process lifetime, like ThemeManager
  return *s;
}

const std::vector<UiSettings::Key>& UiSettings::keys() {
  static const std::vector<Key> k = {
      {"ui/theme", "dark", {"dark", "light", "system"}, 0, 0, true},
      {"ui/toolbarArea", "top", {"top", "left", "right"}, 0, 0, false},
      {"ui/sidebarSide", "left", {"left", "right"}, 0, 0, false},
      {"ui/cardPlacement", "floatRight", {"floatRight", "floatLeft", "sidebar", "free"}, 0, 0, false},
      {"ui/cardRememberDrag", true, {}, 0, 0, false},
      {"ui/cardFreePos", QString(), {}, 0, 0, false},
      {"ui/quickToolbar", "off", {"off", "left", "right"}, 0, 0, false},
      {"ui/orientation", "hexDial", {"hexDial", "viewCube"}, 0, 0, false},
      {"ui/timelineStyle", "featureList", {"featureList", "strip"}, 0, 0, false},
      {"ui/fontScale", 100, {}, 90, 130, true},
      {"ui/glow", true, {}, 0, 0, false},
      {"ui/scanlines", false, {}, 0, 0, false},
      {"ui/designFilter", "part", {"part", "assembly"}, 0, 0, true},
      {"ui/commandLine", true, {}, 0, 0, true},
  };
  return k;
}

const UiSettings::Key* UiSettings::find(const QString& key) {
  for (const Key& k : keys())
    if (k.key == key) return &k;
  return nullptr;
}

bool UiSettings::valid(const Key& k, const QVariant& v, QVariant* normalized) {
  if (!v.isValid()) return false;
  if (!k.choices.isEmpty()) {
    const QString s = v.toString();
    for (const QString& c : k.choices)
      if (c.compare(s, Qt::CaseInsensitive) == 0) {
        *normalized = c;
        return true;
      }
    return false;
  }
  if (k.min < k.max) {
    bool ok = false;
    const int i = v.toString().toInt(&ok);
    if (!ok || i < k.min || i > k.max) return false;
    *normalized = i;
    return true;
  }
  if (k.defaultValue.typeId() == QMetaType::Bool) {
    const QString s = v.toString().toLower();
    if (s == "true" || s == "1" || s == "on") *normalized = true;
    else if (s == "false" || s == "0" || s == "off") *normalized = false;
    else return false;
    return true;
  }
  *normalized = v.toString();
  return true;
}

QVariant UiSettings::value(const QString& key) const {
  const Key* k = find(key);
  if (!k) return {};
  QVariant out;
  const QVariant stored = QSettings().value(key);
  return stored.isValid() && valid(*k, stored, &out) ? out : k->defaultValue;
}

bool UiSettings::set(const QString& key, const QVariant& v) {
  const Key* k = find(key);
  QVariant norm;
  if (!k || !valid(*k, v, &norm)) return false;
  const bool changedValue = value(key) != norm || !QSettings().contains(key);
  QSettings().setValue(key, norm);
  if (key == "ui/theme") {
    ThemeManager::Mode m;
    if (ThemeManager::parseMode(norm.toString(), &m)) ThemeManager::instance().setMode(m, false);
  } else if (key == "ui/fontScale") {
    theme::setFontScale(norm.toInt() / 100.0);
    ThemeManager::instance().reapply();
  }
  if (changedValue) emit changed(key);
  return true;
}

void UiSettings::reset(const QString& key) {
  if (const Key* k = find(key)) set(key, k->defaultValue);
}

}  // namespace fabricad::app
