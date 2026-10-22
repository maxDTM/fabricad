// FabriCAD theme: renders resources/theme/app.qss.tmpl for one theme.
// Placeholders: @{token-or-alias}, @{token-or-alias/NN} (alpha NN %), @{theme} ("dark"/"light").
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "theme/Tokens.h"

#include <QString>
#include <QStringList>

namespace fabricad::theme {

/// Replaces every placeholder; unknown names are left as-is and reported in *errors.
QString renderQss(const QString& tmpl, Theme th, QStringList* errors = nullptr);
/// A QSS colour value: #RRGGBB, or rgba(r, g, b, a) when translucent.
QString qssColor(const QColor& c);
/// ":/ui/<theme>/<name>" (arrows, checks; generated per theme).
QString uiAsset(const QString& name, Theme th);
inline QString uiAsset(const QString& name) { return uiAsset(name, current()); }

}  // namespace fabricad::theme
