// FabriCAD app: icon lookup for tools, features and browser items.
//
// Resolution order: an explicit resource path (":/icons/x.svg"), then ":/icons/<name>.svg",
// then a generated monochrome badge (rounded square with the first letters of the label,
// colour derived from the name) so every command has a distinct icon even before artwork
// exists. Icons are cached; QIcon renders SVGs lazily at the requested size.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

#include <string>

namespace fabricad::core {
struct ToolInfo;
class Feature;
}  // namespace fabricad::core

namespace fabricad::app {

/// Resource icon by bare name ("save") or path; null QIcon if it does not exist.
QIcon resourceIcon(const QString& nameOrPath);
/// Icon for `nameOrPath`, falling back to a generated badge showing `label`'s initials.
QIcon iconOr(const std::string& nameOrPath, const std::string& label);
QIcon toolIcon(const core::ToolInfo& info);
/// Timeline / browser icon of a feature (type based, folder fallback).
QIcon featureIcon(const core::Feature& f);
QIcon featureIcon(const std::string& iconName, const std::string& type, const std::string& folder);
/// Badge text for a label: "Extrude" -> "Ex", "Offset Plane" -> "OP".
QString badgeText(const QString& label);
/// Stable accent colour for a name (used by generated badges and timeline cells).
QColor badgeColor(const QString& key);

}  // namespace fabricad::app
