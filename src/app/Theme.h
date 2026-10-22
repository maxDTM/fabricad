// FabriCAD app: the light application theme (ux.md section 12).
// The desktop's platform theme (qt6ct, GTK) may supply a dark palette; FabriCAD's chrome is
// designed for light colours, so the palette is set explicitly for every role and colour
// group before the stylesheet (resources/theme-light.qss) is applied.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QPalette>

class QApplication;

namespace fabricad::app {

/// The full light palette (Active, Inactive and Disabled groups).
QPalette lightPalette();
/// Fusion style + lightPalette() + a fixed UI font + theme-light.qss + tooltip palette.
void applyLightTheme(QApplication& app);

}  // namespace fabricad::app
