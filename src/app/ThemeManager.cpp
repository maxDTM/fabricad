// FabriCAD app: theme manager.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/ThemeManager.h"

#include "theme/QssTemplate.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QSplitter>
#include <QTreeView>
#include <QEvent>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QSettings>
#include <QStatusBar>
#include <QStyleFactory>
#include <QStyleHints>
#include <QTimer>
#include <QToolTip>
#include <QWidget>

namespace fabricad::app {

using theme::TextStyle;
using theme::Tok;

namespace {

constexpr const char* kStyleProp = "_fcAppliedTextStyle";

struct NameRule {
  const char* objectName;
  TextStyle style;
};

// objectName -> text style (section 3.2). Everything else inherits the application font (`body`).
constexpr NameRule kNameRules[] = {
    {"RibbonGroupLabel", TextStyle::Caption},   {"MenuHeader", TextStyle::Caption},
    {"RibbonTool", TextStyle::LabelSm},         {"FinishSketchButton", TextStyle::LabelSm},
    {"StatusDim", TextStyle::LabelSm},          {"CommandLine", TextStyle::Value},
    {"CommandHistory", TextStyle::Value},       {"fcValueBox", TextStyle::Value},
    // user-entered names are shown as typed (decision 3): documents, features, bodies
    {"Browser", TextStyle::Value},              {"DocumentTabs", TextStyle::Value},
    {"Timeline", TextStyle::Value},             {"TimelineStrip", TextStyle::Value},
    {"CommandDialogTitle", TextStyle::Title},
    // redesign phase 1 shell (section 5.1)
    {"AppTitle", TextStyle::Header},            {"WorkspaceSwitcher", TextStyle::Body},
    {"SearchBox", TextStyle::LabelSm},          {"Keycap", TextStyle::Keycap},
    {"StatusCommand", TextStyle::LabelSm},      {"StatusPrompt", TextStyle::LabelSm},
    {"StatusCell", TextStyle::LabelSm},         {"StatusHints", TextStyle::LabelSm},
    {"StatusRebuild", TextStyle::LabelSm},
    {"CommandLinePrompt", TextStyle::Value},    {"CommandLineHistory", TextStyle::Caption},
    // messages often carry user data (paths, names): shown as typed (p0-check 3)
    {"StatusMessage", TextStyle::Value},
    {"PreferencesPageTitle", TextStyle::Header},
};

/// Inside Qt's own file dialog everything that shows file system names is user data (p0-check 2):
/// the file list, the sidebar, the "Look in" combo and its popup. Headers and labels stay caps.
bool showsFileNames(const QWidget* w) {
  const QWidget* win = w->window();
  if (!qobject_cast<const QFileDialog*>(win)) {
    // the "Look in" popup and the completer popup are their own windows
    const QWidget* p = w->parentWidget();
    while (p && !qobject_cast<const QFileDialog*>(p)) p = p->parentWidget();
    if (!p) return false;
  }
  if (qobject_cast<const QHeaderView*>(w)) return false;
  return qobject_cast<const QAbstractItemView*>(w) || qobject_cast<const QComboBox*>(w);
}

}  // namespace

ThemeManager& ThemeManager::instance() {
  static ThemeManager* m = new ThemeManager;  // lives for the process (the app object owns nothing here)
  return *m;
}

QString ThemeManager::modeName(Mode m) {
  switch (m) {
    case Mode::Dark: return QStringLiteral("dark");
    case Mode::Light: return QStringLiteral("light");
    case Mode::System: return QStringLiteral("system");
  }
  return QStringLiteral("dark");
}

bool ThemeManager::parseMode(QStringView s, Mode* out) {
  const QString v = s.trimmed().toString().toLower();
  Mode m;
  if (v == "dark") m = Mode::Dark;
  else if (v == "light") m = Mode::Light;
  else if (v == "system" || v == "follow" || v == "auto") m = Mode::System;
  else return false;
  if (out) *out = m;
  return true;
}

theme::Theme ThemeManager::resolve(Mode m) const {
  if (m == Mode::Light) return theme::Theme::Light;
  if (m == Mode::Dark) return theme::Theme::Dark;
  // Follow system (Qt 6.5+); an unknown scheme counts as dark
  if (auto* hints = QGuiApplication::styleHints(); hints && hints->colorScheme() == Qt::ColorScheme::Light)
    return theme::Theme::Light;
  return theme::Theme::Dark;
}

QPalette ThemeManager::palette(theme::Theme th) {
  auto c = [th](Tok t) { return theme::color(t, th); };
  QPalette p;
  for (QPalette::ColorGroup g : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
    const bool dis = g == QPalette::Disabled;
    const QColor fg = c(dis ? Tok::TextDisabled : Tok::TextPrimary);
    p.setColor(g, QPalette::Window, c(Tok::SurfacePanel));
    p.setColor(g, QPalette::WindowText, fg);
    p.setColor(g, QPalette::Base, c(dis ? Tok::SurfacePanel : Tok::SurfaceInput));
    p.setColor(g, QPalette::AlternateBase, c(Tok::SurfaceHeader));
    p.setColor(g, QPalette::Text, fg);
    p.setColor(g, QPalette::Button, c(Tok::SurfaceRaised));
    p.setColor(g, QPalette::ButtonText, fg);
    p.setColor(g, QPalette::BrightText, c(Tok::AccentOnSolid));
    p.setColor(g, QPalette::Highlight, c(dis ? Tok::SurfacePressed : Tok::AccentSolid));
    p.setColor(g, QPalette::HighlightedText, c(dis ? Tok::TextDisabled : Tok::AccentOnSolid));
    p.setColor(g, QPalette::ToolTipBase, c(Tok::SurfaceOverlay));
    p.setColor(g, QPalette::ToolTipText, c(Tok::TextPrimary));
    p.setColor(g, QPalette::PlaceholderText, c(Tok::TextTertiary));
    p.setColor(g, QPalette::Link, c(Tok::AccentText));
    p.setColor(g, QPalette::LinkVisited, c(Tok::AccentIndicator));
    p.setColor(g, QPalette::Light, c(Tok::SurfaceHover));
    p.setColor(g, QPalette::Midlight, c(Tok::BorderSubtle));
    p.setColor(g, QPalette::Mid, c(Tok::BorderDefault));
    p.setColor(g, QPalette::Dark, c(Tok::BorderControl));
    p.setColor(g, QPalette::Shadow, c(Tok::BgApp));
    p.setColor(g, QPalette::Accent, c(Tok::AccentSolid));
  }
  return p;
}

QString ThemeManager::styleSheet(theme::Theme th, QStringList* errors) {
  QFile f(QStringLiteral(":/theme/app.qss.tmpl"));
  if (!f.open(QIODevice::ReadOnly)) {
    if (errors) *errors << QStringLiteral(":/theme/app.qss.tmpl is missing");
    return {};
  }
  return theme::renderQss(QString::fromUtf8(f.readAll()), th, errors);
}

std::optional<TextStyle> ThemeManager::textStyleFor(const QWidget* w) {
  if (!w) return std::nullopt;
  if (const QVariant v = w->property("textStyle"); v.isValid())
    if (auto s = theme::findTextStyle(v.toString())) return s;
  const QString name = w->objectName();
  if (!name.isEmpty())
    for (const NameRule& r : kNameRules)
      if (name == QLatin1StringView(r.objectName)) return r.style;
  const QWidget* parent = w->parentWidget();
  if (parent && qobject_cast<const QLabel*>(w)) {
    if (parent->objectName() == QLatin1StringView("CommandDialogTitle")) return TextStyle::Title;
    if (qobject_cast<const QStatusBar*>(parent)) return TextStyle::LabelSm;
  }
  if (showsFileNames(w)) return TextStyle::Value;
  // completer popups (file dialog, command line) list what the user is typing: as typed
  if (const auto* view = qobject_cast<const QAbstractItemView*>(w); view && view->isWindow() &&
      !qobject_cast<const QComboBox*>(view->parentWidget()))
    return TextStyle::Value;
  if (qobject_cast<const QHeaderView*>(w)) return TextStyle::Caption;
  // labels that show values or user text (section 3.1 exceptions) use the `value` style
  if (w->property("textCase").toString() == QLatin1StringView("as-is")) return TextStyle::Value;
  return std::nullopt;
}

void ThemeManager::applyTextStyle(QWidget* w) {
  if (!w) return;
  const QVariant applied = w->property(kStyleProp);
  // a font the code set itself wins (it is not one of ours)
  if (w->testAttribute(Qt::WA_SetFont) && !applied.isValid()) return;
  std::optional<TextStyle> s = textStyleFor(w);
  if (!s) {
    if (applied.isValid()) {  // the rule no longer applies (objectName changed): back to inherited
      w->setProperty(kStyleProp, QVariant());
      w->setFont(QFont());
      w->setAttribute(Qt::WA_SetFont, false);
    }
    return;
  }
  QFont f = theme::font(*s);
  if (w->property("textCase").toString() == QLatin1StringView("as-is")) f.setCapitalization(QFont::MixedCase);
  w->setProperty(kStyleProp, static_cast<int>(*s));
  if (w->font() != f) w->setFont(f);
}

void ThemeManager::applyFonts() {
  const QFont body = theme::font(TextStyle::Body);
  const QFont value = theme::font(TextStyle::Value);
  const QFont caption = theme::font(TextStyle::Caption);
  QApplication::setFont(body);
  // platform themes also hand out per-class fonts (item views, menus, tabs...); override those
  for (const char* cls : {"QAbstractItemView", "QMenu", "QMenuBar", "QTabBar", "QLabel", "QPushButton", "QToolButton",
                          "QComboBox", "QCheckBox", "QRadioButton", "QGroupBox", "QDockWidget", "QStatusBar",
                          "QMessageBox"})
    QApplication::setFont(body, cls);
  QApplication::setFont(caption, "QHeaderView");
  // values, expressions and typed text keep their case (section 3.1)
  for (const char* cls : {"QLineEdit", "QAbstractSpinBox", "QPlainTextEdit", "QTextEdit"}) QApplication::setFont(value, cls);
  QToolTip::setFont(body);
  for (QWidget* w : QApplication::allWidgets()) applyTextStyle(w);
}

void ThemeManager::apply() {
  if (!app_ || applying_) return;
  applying_ = true;
  theme_ = resolve(mode_);
  theme::setCurrent(theme_);
  const QPalette pal = palette(theme_);
  QApplication::setPalette(pal);
  QToolTip::setPalette(pal);
  QStringList errors;
  const QString qss = styleSheet(theme_, &errors);
  for (const QString& e : errors) qWarning("theme: %s", qPrintable(e));
  app_->setStyleSheet(qss);  // repolishes every widget  colour-lint: ok (the rendered token QSS)
  // fonts after the stylesheet: installing the style-sheet style re-initialises Qt's per-class
  // font table from the platform theme, which would drop the class fonts set before it
  applyFonts();
  applying_ = false;
  emit themeChanged(theme_);
  for (QWidget* w : QApplication::topLevelWidgets()) w->update();
}

void ThemeManager::install(QApplication& app, bool readSettings) {
  app_ = &app;
  app.setStyle(QStyleFactory::create("Fusion"));
  theme::loadBundledFonts();
  if (readSettings) {
    QSettings s;
    Mode m = Mode::Dark;
    if (parseMode(s.value("ui/theme", "dark").toString(), &m)) mode_ = m;
    bool ok = false;
    const double pct = s.value("ui/fontScale", 100).toDouble(&ok);
    if (ok) theme::setFontScale(pct / 100.0);
  }
  if (const QString env = qEnvironmentVariable("FABRICAD_THEME"); !env.isEmpty()) {
    Mode m;
    if (parseMode(env, &m)) mode_ = m;
  }
  if (!installed_) {
    installed_ = true;
    app.installEventFilter(this);
    if (auto* hints = QGuiApplication::styleHints())
      connect(hints, &QStyleHints::colorSchemeChanged, this, [this] {
        if (mode_ == Mode::System && resolve(mode_) != theme_) apply();
      });
  }
  apply();
  // qt6ct applies its own font/palette from a queued call; apply ours again after it
  QTimer::singleShot(0, &app, [this] { apply(); });
}

void ThemeManager::setMode(Mode m, bool persist) {
  if (persist) QSettings().setValue("ui/theme", modeName(m));
  const bool changed = m != mode_;
  mode_ = m;
  if (changed || resolve(m) != theme_) apply();
}

bool ThemeManager::eventFilter(QObject* watched, QEvent* event) {
  if (event->type() == QEvent::Polish && watched->isWidgetType()) applyTextStyle(static_cast<QWidget*>(watched));
  if (event->type() == QEvent::Show)
    if (auto* dlg = qobject_cast<QFileDialog*>(watched); dlg && !dlg->property("_fcSized").toBool()) {
      // bold mono caps is ~25 % wider than the platform font: give Qt's file dialog room so the
      // sidebar and the size/type/date columns are not elided (p0-check 8)
      dlg->setProperty("_fcSized", true);
      dlg->resize(std::max(dlg->width(), 1000), std::max(dlg->height(), 600));
      if (auto* split = dlg->findChild<QSplitter*>("splitter"); split && split->count() == 2)
        split->setSizes({200, std::max(400, dlg->width() - 240)});
      if (auto* tree = dlg->findChild<QTreeView*>("treeView"))
        if (QHeaderView* h = tree->header(); h && h->count() >= 4) {
          h->resizeSection(1, 100);
          h->resizeSection(2, 130);
          h->resizeSection(3, 170);
          h->resizeSection(0, std::max(260, dlg->width() - 200 - 400 - 40));
        }
    }
  return QObject::eventFilter(watched, event);
}

}  // namespace fabricad::app
