// FabriCAD app: the theme manager (docs/plans/ui-redesign.md sections 2.3 and 3). Replaces the
// old fixed light theme (Theme.{h,cpp}).
//
// Holds the theme mode (Dark, Light, Follow system), and on apply() sets the Fusion style, the full
// QPalette from tokens, the fonts per class and objectName (bold monospace caps; values and user
// names as typed) and the QSS rendered from resources/theme/app.qss.tmpl. Emits themeChanged()
// after a live switch so custom painters repaint; they read theme::color() at paint time.
// The mode is stored in QSettings `ui/theme` (dark | light | system, default dark) and read once in
// main() before the window shows. FABRICAD_THEME=dark|light|system overrides it for one run.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "theme/Tokens.h"

#include <QObject>
#include <QPalette>
#include <QString>

class QApplication;
class QWidget;

namespace fabricad::app {

class ThemeManager : public QObject {
  Q_OBJECT
 public:
  enum class Mode { Dark, Light, System };

  static ThemeManager& instance();

  /// Once, from main(): loads the bundled fonts, reads ui/theme and ui/fontScale, applies, and
  /// re-applies after the event loop starts (qt6ct and similar platform themes push their own
  /// palette and fonts from a queued call made while QApplication was constructed).
  void install(QApplication& app, bool readSettings = true);
  bool installed() const { return installed_; }

  Mode mode() const { return mode_; }
  /// Live switch. With persist, the choice is stored in ui/theme.
  void setMode(Mode m, bool persist = true);
  /// The theme in effect (System resolved through QStyleHints::colorScheme(); unknown = dark).
  theme::Theme theme() const { return theme_; }

  /// Re-apply palette, QSS and fonts (after ui/fontScale changed).
  void reapply() { apply(); }

  static QString modeName(Mode m);
  static bool parseMode(QStringView s, Mode* out);

  /// Palette and stylesheet for a theme (pure; used by apply() and the tests).
  static QPalette palette(theme::Theme th);
  static QString styleSheet(theme::Theme th, QStringList* errors = nullptr);

  /// The text style a widget gets (property textStyle, then objectName and class rules), or
  /// nothing to keep the inherited application font.
  static std::optional<theme::TextStyle> textStyleFor(const QWidget* w);
  /// Applies textStyleFor(w) to w (does not touch fonts the code set explicitly).
  static void applyTextStyle(QWidget* w);

 signals:
  void themeChanged(fabricad::theme::Theme theme);

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  ThemeManager() = default;
  void apply();
  void applyFonts();
  theme::Theme resolve(Mode m) const;

  QApplication* app_ = nullptr;
  Mode mode_ = Mode::Dark;
  theme::Theme theme_ = theme::Theme::Dark;
  bool installed_ = false;
  bool applying_ = false;
};

}  // namespace fabricad::app
