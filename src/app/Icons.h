// FabriCAD app: icon lookup for tools, features and browser items.
//
// Artwork lives in resources/icons (24x24 viewBox, 1.5 px line icons, palette and naming
// rules in resources/icons/README.md). Resolution order for a tool: ":/icons/<id with '.'
// replaced by '-'>.svg" ("part.extrude" -> part-extrude.svg), then ToolInfo::icon (bare
// name or resource path), then the id suffix ("extrude.svg"). Features: Feature::iconName(),
// then "feature-<type lowercase>", then folder fallbacks (construction/joints). Only when
// none exists (plug-in tools without artwork) a generated badge (rounded square with the
// label's initials) is used; test_icons fails if any registered tool or feature type gets
// one. Icons are cached; QIcon renders SVGs lazily at the requested size.
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
/// Resource path for a bare name or path (":/icons/save.svg"); empty if it does not exist.
QString resourceIconPath(const QString& nameOrPath);
/// Icon for `nameOrPath`, falling back to a generated badge showing `label`'s initials.
QIcon iconOr(const std::string& nameOrPath, const std::string& label);
QIcon toolIcon(const core::ToolInfo& info);
/// Resource path toolIcon() uses (":/icons/part-extrude.svg"); empty when it falls back to a badge.
QString toolIconPath(const core::ToolInfo& info);
/// Timeline / browser icon of a feature (type based, folder fallback).
QIcon featureIcon(const core::Feature& f);
QIcon featureIcon(const std::string& iconName, const std::string& type, const std::string& folder);
/// Resource path featureIcon() uses; empty when it falls back to a badge.
QString featureIconPath(const std::string& iconName, const std::string& type, const std::string& folder);
/// Badge text for a label: "Extrude" -> "Ex", "Offset Plane" -> "OP".
QString badgeText(const QString& label);
/// Stable accent colour for a name (used by generated badges and timeline cells).
QColor badgeColor(const QString& key);

}  // namespace fabricad::app
