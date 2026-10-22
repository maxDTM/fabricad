// ThemeManager (docs/plans/ui-redesign.md 2.3, 3, phase 0.3): Dark / Light / Follow system, live
// switch, palette + QSS from tokens, bundled JetBrains Mono, caps through the font only.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/MainWindow.h"
#include "app/ThemeManager.h"
#include "theme/QssTemplate.h"
#include "theme/Tokens.h"

#include <QApplication>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QStyleHints>
#include <QToolButton>
#include <QtTest>

using namespace fabricad;
using app::ThemeManager;
using theme::Tok;

class TestThemeManager : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase() {
    QStandardPaths::setTestModeEnabled(true);  // QSettings go to a throw-away location
    QSettings().remove("ui");
    qunsetenv("FABRICAD_THEME");
    ThemeManager::instance().install(*qApp, /*readSettings=*/true);
  }

  void defaultsToDark() {
    QCOMPARE(ThemeManager::instance().mode(), ThemeManager::Mode::Dark);
    QCOMPARE(ThemeManager::instance().theme(), theme::Theme::Dark);
    QCOMPARE(theme::current(), theme::Theme::Dark);
    QCOMPARE(qApp->palette().color(QPalette::Window), theme::color(Tok::SurfacePanel, theme::Theme::Dark));
  }

  void bundledFontLoaded() {
    QVERIFY(theme::loadBundledFonts() >= 3);
    QVERIFY(QFontDatabase::families().contains("JetBrains Mono"));
    const QFont body = theme::font(theme::TextStyle::Body);
    QCOMPARE(body.families().value(0), QString("JetBrains Mono"));
    QCOMPARE(QFontInfo(body).family(), QString("JetBrains Mono"));  // resolved, not a fallback
    QCOMPARE(body.weight(), QFont::Bold);
    QCOMPARE(body.capitalization(), QFont::AllUppercase);
    const QFont value = theme::font(theme::TextStyle::Value);
    QCOMPARE(value.capitalization(), QFont::MixedCase);  // values and user names as typed
    QCOMPARE(value.weight(), QFont::Medium);
    QCOMPARE(theme::font(theme::TextStyle::Header).weight(), QFont::ExtraBold);
    QVERIFY(qAbs(QFontInfo(theme::font(theme::TextStyle::Body)).pixelSize() - 12) <= 1);
    // the licence travels with the font
    QFile ofl(":/fonts/OFL.txt");
    QVERIFY(ofl.open(QIODevice::ReadOnly));
    QVERIFY(ofl.readAll().contains("SIL OPEN FONT LICENSE Version 1.1"));
  }

  void textStylesPerWidget() {
    QWidget host;
    auto* label = new QLabel("Extrude", &host);
    auto* edit = new QLineEdit("boss_height", &host);
    auto* caption = new QToolButton(&host);
    caption->setObjectName("RibbonGroupLabel");
    caption->setText("Create");
    auto* asIs = new QLabel("fx boss_height = 25 mm", &host);
    asIs->setProperty("textCase", "as-is");
    auto* styled = new QLabel("Features", &host);
    styled->setProperty("textStyle", "header");
    host.show();
    QCoreApplication::processEvents();
    QCOMPARE(label->font().capitalization(), QFont::AllUppercase);
    QCOMPARE(label->text(), QString("Extrude"));  // caps by the font, never by the string
    QCOMPARE(edit->font().capitalization(), QFont::MixedCase);
    QCOMPARE(edit->font().weight(), QFont::Medium);
    QCOMPARE(caption->font().pointSizeF(), theme::font(theme::TextStyle::Caption).pointSizeF());
    QCOMPARE(caption->font().capitalization(), QFont::AllUppercase);
    QCOMPARE(asIs->font().capitalization(), QFont::MixedCase);
    QCOMPARE(styled->font().weight(), QFont::ExtraBold);
    // a font the code set itself is left alone
    QLabel own("own");
    QFont f("DejaVu Sans");
    f.setPointSize(20);
    own.setFont(f);
    own.show();
    QCoreApplication::processEvents();
    QCOMPARE(own.font().pointSize(), 20);
  }

  void stylesheetsRenderCleanly() {
    for (theme::Theme th : {theme::Theme::Dark, theme::Theme::Light}) {
      QStringList errors;
      const QString qss = ThemeManager::styleSheet(th, &errors);
      QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
      QVERIFY(qss.size() > 1000);
      QVERIFY(!qss.contains("@{"));
      QVERIFY(qss.contains(QString(":/ui/%1/check.svg").arg(theme::themeName(th))));
      QVERIFY(QFile::exists(theme::uiAsset("check.svg", th)));
    }
  }

  void liveSwitch() {
    ThemeManager& tm = ThemeManager::instance();
    QSignalSpy spy(&tm, &ThemeManager::themeChanged);
    const QString darkQss = qApp->styleSheet();
    tm.setMode(ThemeManager::Mode::Light);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<theme::Theme>(), theme::Theme::Light);
    QCOMPARE(tm.theme(), theme::Theme::Light);
    QCOMPARE(theme::current(), theme::Theme::Light);
    QCOMPARE(qApp->palette().color(QPalette::Text), theme::color(Tok::TextPrimary, theme::Theme::Light));
    QVERIFY(qApp->styleSheet() != darkQss);
    QVERIFY(qApp->styleSheet().contains(theme::qssColor(theme::color(Tok::SurfacePanel, theme::Theme::Light))));
    QCOMPARE(QSettings().value("ui/theme").toString(), QString("light"));
    // same mode again: no re-apply
    tm.setMode(ThemeManager::Mode::Light);
    QCOMPARE(spy.count(), 1);
    // follow system: offscreen reports no scheme (Unknown), which counts as dark
    tm.setMode(ThemeManager::Mode::System);
    QCOMPARE(QSettings().value("ui/theme").toString(), QString("system"));
    const bool sysLight = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Light;
    QCOMPARE(tm.theme(), sysLight ? theme::Theme::Light : theme::Theme::Dark);
    tm.setMode(ThemeManager::Mode::Dark);
    QCOMPARE(tm.theme(), theme::Theme::Dark);
  }

  void parseModes() {
    ThemeManager::Mode m;
    QVERIFY(ThemeManager::parseMode(u"Light", &m) && m == ThemeManager::Mode::Light);
    QVERIFY(ThemeManager::parseMode(u"system", &m) && m == ThemeManager::Mode::System);
    QVERIFY(ThemeManager::parseMode(u"dark", &m) && m == ThemeManager::Mode::Dark);
    QVERIFY(!ThemeManager::parseMode(u"purple", &m));
    QCOMPARE(ThemeManager::modeName(ThemeManager::Mode::System), QString("system"));
  }

  // today's layout runs in both themes and survives a live switch with a command open
  void mainWindowBothThemes() {
    app::MainWindow w;
    w.setInteractive(false);
    w.show();
    QCoreApplication::processEvents();
    QVERIFY(w.runCommand("sketch.create", {"XY"}));
    QCoreApplication::processEvents();
    for (auto m : {ThemeManager::Mode::Light, ThemeManager::Mode::Dark, ThemeManager::Mode::Light}) {
      ThemeManager::instance().setMode(m, false);
      QCoreApplication::processEvents();
      const QImage shot = w.grab().toImage();
      QVERIFY(!shot.isNull());
      // the window background follows the theme
      const QColor want = theme::color(Tok::BgApp);
      const QColor got = shot.pixelColor(shot.width() / 2, 2);
      QVERIFY2(qAbs(got.lightness() - want.lightness()) < 40,
               qPrintable(QString("top bar %1 vs bg.app %2").arg(got.name(), want.name())));
    }
    ThemeManager::instance().setMode(ThemeManager::Mode::Dark, false);
    w.toolManager().cancel();
  }
};

QTEST_MAIN(TestThemeManager)
#include "test_theme_manager.moc"
