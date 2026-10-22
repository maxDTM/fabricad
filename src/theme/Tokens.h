// FabriCAD theme: design tokens at runtime (docs/plans/ui-redesign.md section 2.3).
// The tables come from resources/theme/tokens.json via tools/theme/gen_tokens.py (ThemeTokens.inc).
// Custom painters read colours here at paint time and repaint on ThemeManager::themeChanged;
// nothing should cache a QColor across a theme switch.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QColor>
#include <QFont>
#include <QString>

#include <array>
#include <cstdint>
#include <optional>

namespace fabricad::theme {

#include "theme/ThemeTokens.inc"

enum class Theme : std::uint8_t { Dark, Light };

/// The theme the app currently shows (set by app::ThemeManager; Dark until then).
Theme current();
void setCurrent(Theme t);
const char* themeName(Theme t);  ///< "dark" / "light"

QColor color(Tok t, Theme th);
inline QColor color(Tok t) { return color(t, current()); }
/// Same colour with its alpha multiplied by `alpha` (0..1).
QColor color(Tok t, double alpha);
std::array<float, 4> rgba(Tok t, Theme th);
inline std::array<float, 4> rgba(Tok t) { return rgba(t, current()); }
std::uint32_t rgbaU32(Tok t, Theme th);

const char* tokenName(Tok t);
/// A colour token ("surface.panel") or an alias ("button.primary.bg").
std::optional<Tok> find(QStringView name);
constexpr int tokenCount() { return static_cast<int>(Tok::Count); }

// ---- fonts (section 3)

/// Registers the bundled JetBrains Mono files from ":/fonts/" (QFontDatabase); returns how many
/// loaded. Safe to call more than once.
int loadBundledFonts();
/// The font for a text style: family chain, pixel size (× fontScale), weight, tracking and, for the
/// UI styles, QFont::AllUppercase. Values and user-entered names (`value`) keep their case.
QFont font(TextStyle s);
void setFontScale(double scale);  ///< ui/fontScale, 0.9 .. 1.3
double fontScale();
const char* textStyleName(TextStyle s);
std::optional<TextStyle> findTextStyle(QStringView name);

}  // namespace fabricad::theme
