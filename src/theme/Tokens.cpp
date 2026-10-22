// FabriCAD theme: design tokens at runtime.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "theme/Tokens.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QScreen>

#include <algorithm>
#include <atomic>

namespace fabricad::theme {

static_assert(std::size(gen::kTokName) == static_cast<size_t>(Tok::Count));
static_assert(std::size(gen::kDark) == static_cast<size_t>(Tok::Count));
static_assert(std::size(gen::kLight) == static_cast<size_t>(Tok::Count));
static_assert(std::size(gen::kFonts) == static_cast<size_t>(TextStyle::Count));

namespace {
std::atomic<Theme> gCurrent{Theme::Dark};
double gFontScale = 1.0;
}  // namespace

Theme current() { return gCurrent.load(); }
void setCurrent(Theme t) { gCurrent.store(t); }
const char* themeName(Theme t) { return t == Theme::Dark ? "dark" : "light"; }

std::uint32_t rgbaU32(Tok t, Theme th) {
  const auto i = static_cast<size_t>(t);
  if (i >= static_cast<size_t>(Tok::Count)) return 0xFF00FFFFu;
  return th == Theme::Dark ? gen::kDark[i] : gen::kLight[i];
}

QColor color(Tok t, Theme th) {
  const std::uint32_t v = rgbaU32(t, th);
  return QColor::fromRgb(int((v >> 24) & 0xFF), int((v >> 16) & 0xFF), int((v >> 8) & 0xFF), int(v & 0xFF));
}

QColor color(Tok t, double alpha) {
  QColor c = color(t);
  c.setAlphaF(std::clamp(c.alphaF() * alpha, 0.0, 1.0));
  return c;
}

std::array<float, 4> rgba(Tok t, Theme th) {
  const std::uint32_t v = rgbaU32(t, th);
  return {float((v >> 24) & 0xFF) / 255.f, float((v >> 16) & 0xFF) / 255.f, float((v >> 8) & 0xFF) / 255.f,
          float(v & 0xFF) / 255.f};
}

const char* tokenName(Tok t) {
  const auto i = static_cast<size_t>(t);
  return i < static_cast<size_t>(Tok::Count) ? gen::kTokName[i] : "";
}

std::optional<Tok> find(QStringView name) {
  for (size_t i = 0; i < std::size(gen::kTokName); ++i)
    if (name == QLatin1StringView(gen::kTokName[i])) return static_cast<Tok>(i);
  for (const auto& a : gen::kAliases)
    if (name == QLatin1StringView(a.name)) return a.target;
  return std::nullopt;
}

int loadBundledFonts() {
  static int loaded = -1;
  if (loaded >= 0) return loaded;
  loaded = 0;
  for (const char* f : gen::kFontFiles)
    if (QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/") + QLatin1StringView(f)) >= 0) ++loaded;
  return loaded;
}

void setFontScale(double scale) { gFontScale = std::clamp(scale, 0.9, 1.3); }
double fontScale() { return gFontScale; }

const char* textStyleName(TextStyle s) {
  const auto i = static_cast<size_t>(s);
  return i < std::size(gen::kFonts) ? gen::kFonts[i].name : "";
}

std::optional<TextStyle> findTextStyle(QStringView name) {
  for (size_t i = 0; i < std::size(gen::kFonts); ++i)
    if (name == QLatin1StringView(gen::kFonts[i].name)) return static_cast<TextStyle>(i);
  return std::nullopt;
}

QFont font(TextStyle s) {
  const auto& spec = gen::kFonts[std::min(static_cast<size_t>(s), std::size(gen::kFonts) - 1)];
  QStringList families;
  for (const char* f : gen::kFontFamilies) families << QString::fromLatin1(f);
  QFont f;
  f.setFamilies(families);
  f.setStyleHint(QFont::Monospace);
  f.setFixedPitch(true);
  f.setHintingPreference(QFont::PreferNoHinting);
  // sizes are logical px; QFont::setPixelSize is integral, so go through points at the screen's
  // logical DPI to keep the half-pixel styles (10.5, 12.5, 9.5)
  double dpi = 96.0;
  if (auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
    if (QScreen* sc = app->primaryScreen()) dpi = sc->logicalDotsPerInchY();
  if (dpi <= 0) dpi = 96.0;
  f.setPointSizeF(spec.px * gFontScale * 72.0 / dpi);
  f.setWeight(static_cast<QFont::Weight>(spec.weight));
  if (spec.tracking != 0.0) f.setLetterSpacing(QFont::AbsoluteSpacing, spec.tracking);
  f.setCapitalization(spec.caps ? QFont::AllUppercase : QFont::MixedCase);
  return f;
}

}  // namespace fabricad::theme
