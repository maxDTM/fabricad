// theme_tokens ctest (docs/plans/ui-redesign.md section 2.2): generated files in sync with
// tokens.json, WCAG contrast for every `pairs` row, ΔE00 floors for every `cvd` row (both themes),
// and the QSS template renders in both themes with no unresolved placeholders.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "theme/ColorMath.h"
#include "theme/QssTemplate.h"
#include "theme/Tokens.h"

#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QtTest>

using namespace fabricad::theme;

namespace {

constexpr Theme kThemes[] = {Theme::Dark, Theme::Light};

Rgba rgbaOf(Tok t, Theme th) { return fromU32(rgbaU32(t, th)); }

/// Opaque version of a token: translucent colours are composited over surface.panel.
Rgba opaqueOf(Tok t, Theme th) {
  const Rgba c = rgbaOf(t, th);
  return c.a < 1.0 ? composite(c, rgbaOf(Tok::SurfacePanel, th)) : c;
}

QString readFile(const QString& path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return {};
  return QString::fromUtf8(f.readAll());
}

}  // namespace

class TestThemeTokens : public QObject {
  Q_OBJECT
 private slots:
  void generatedFilesInSync() {
    const QString py = QStringLiteral(FABRICAD_PYTHON);
    if (py.isEmpty()) QSKIP("Python 3 not found: sync check skipped (the math checks below still run)");
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(py, {"-I", QStringLiteral(FABRICAD_SOURCE_DIR "/tools/theme/gen_tokens.py"), "--check", "--root",
                 QStringLiteral(FABRICAD_SOURCE_DIR)});
    QVERIFY(p.waitForFinished(60000));
    const QString out = QString::fromUtf8(p.readAll());
    if (p.exitCode() != 0) qWarning().noquote() << out;
    QCOMPARE(p.exitCode(), 0);
  }

  void colourMathReference() {
    // values from docs/plans/ui-redesign.md 4.2 / 4.7 (computed independently in Python)
    const Rgba white = fromU32(0xFFFFFFFFu);
    QVERIFY(qAbs(contrastRatio(white, fromU32(0x7C4DEBFFu)) - 5.10) < 0.01);
    QVERIFY(qAbs(contrastRatio(white, fromU32(0x8B5CF6FFu)) - 4.23) < 0.01);
    QVERIFY(qAbs(contrastRatio(fromU32(0x000000FFu), white) - 21.0) < 1e-9);
    const auto d = cvdDeltas(fromU32(0xF5B700FFu), fromU32(0xF2542DFFu));  // live vs warning, dark
    QVERIFY(qAbs(d[0] - 35.9) < 0.15);
    QVERIFY(qAbs(d[1] - 24.1) < 0.3);
    QVERIFY(qAbs(d[2] - 14.4) < 0.3);
    QVERIFY(qAbs(deltaE2000(white, white)) < 1e-9);
    // a translucent colour is composited before the ratio is taken
    QVERIFY(qAbs(contrastRatio(fromU32(0x00000000u), white) - 1.0) < 1e-9);
  }

  void contrastPairs() {
    QStringList failures;
    for (Theme th : kThemes)
      for (const auto& p : gen::kPairs) {
        const double r = contrastRatio(rgbaOf(p.fg, th), opaqueOf(p.bg, th));
        if (r + 1e-9 < p.minRatio)
          failures << QStringLiteral("%1 on %2 (%3): %4 < %5")
                          .arg(tokenName(p.fg), tokenName(p.bg), themeName(th))
                          .arg(r, 0, 'f', 2)
                          .arg(p.minRatio);
      }
    if (!failures.isEmpty()) qWarning().noquote() << failures.join('\n');
    QVERIFY(failures.isEmpty());
  }

  void colourVisionRows() {
    static const char* kSim[] = {"normal", "protan", "deutan", "tritan"};
    QStringList failures;
    for (Theme th : kThemes)
      for (const auto& row : gen::kCvd) {
        const auto d = cvdDeltas(opaqueOf(row.a, th), opaqueOf(row.b, th));
        for (int i = 0; i < 4; ++i)
          if (d[i] + 1e-9 < row.minDeltaE[i])
            failures << QStringLiteral("%1 vs %2 (%3, %4): ΔE00 %5 < %6")
                            .arg(tokenName(row.a), tokenName(row.b), themeName(th), kSim[i])
                            .arg(d[i], 0, 'f', 1)
                            .arg(row.minDeltaE[i]);
      }
    if (!failures.isEmpty()) qWarning().noquote() << failures.join('\n');
    QVERIFY(failures.isEmpty());
  }

  void skFreeGate() {
    // decision 8 (section 12): sk.free vs selection ΔE00 >= 10 (normal/protan/deutan), >= 6 tritan
    for (Theme th : kThemes) {
      const auto d = cvdDeltas(opaqueOf(Tok::SkFree, th), opaqueOf(Tok::SelEdge, th));
      QVERIFY2(d[0] >= 10 && d[1] >= 10 && d[2] >= 10 && d[3] >= 6, themeName(th));
      QVERIFY(contrastRatio(rgbaOf(Tok::SkFree, th), rgbaOf(Tok::VpBgTop, th)) >= 3.0);
      QVERIFY(contrastRatio(rgbaOf(Tok::SkFree, th), rgbaOf(Tok::VpBgBottom, th)) >= 3.0);
    }
  }

  void lookup() {
    QCOMPARE(find(u"surface.panel"), std::optional<Tok>(Tok::SurfacePanel));
    QCOMPARE(find(u"button.primary.bg"), std::optional<Tok>(Tok::AccentSolid));
    QVERIFY(!find(u"no.such.token"));
    QCOMPARE(QString(tokenName(Tok::SkFree)), QString("sk.free"));
    QCOMPARE(color(Tok::AccentSolid, Theme::Dark), QColor("#7C4DEB"));
    QCOMPARE(color(Tok::AccentSolid, Theme::Light), QColor("#6D3FE0"));
    QCOMPARE(color(Tok::ShadowCard, Theme::Dark).alpha(), 0x8C);
    QCOMPARE(findTextStyle(u"label.sm"), std::optional<TextStyle>(TextStyle::LabelSm));
    // the unsaved-document marker is neutral (decision 11), never gold
    QCOMPARE(*find(u"doc.unsavedDot"), Tok::TextSecondary);
  }

  void qssRendersInBothThemes() {
    const QString tmpl = readFile(QStringLiteral(FABRICAD_SOURCE_DIR "/resources/theme/app.qss.tmpl"));
    QVERIFY(!tmpl.isEmpty());
    static const QRegularExpression asset(QStringLiteral(R"(url\(:/ui/(dark|light)/([A-Za-z0-9_.-]+)\))"));
    for (Theme th : kThemes) {
      QStringList errors;
      const QString qss = renderQss(tmpl, th, &errors);
      QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
      QVERIFY(!qss.contains("@{"));
      QVERIFY(qss.contains(color(Tok::AccentSolid, th).name(QColor::HexRgb).toUpper()));
      int assets = 0;
      for (auto it = asset.globalMatch(qss); it.hasNext(); ++assets) {
        const auto m = it.next();
        QCOMPARE(m.captured(1), QString(themeName(th)));
        const QString file = QStringLiteral(FABRICAD_SOURCE_DIR "/resources/ui/") + m.captured(1) + '/' + m.captured(2);
        QVERIFY2(QFileInfo::exists(file), qPrintable(file));
      }
      QVERIFY(assets > 0);
    }
    QStringList errors;
    renderQss("a { color: @{nope}; }", Theme::Dark, &errors);
    QCOMPARE(errors.size(), 2);  // the unknown name, and the placeholder left behind
    QCOMPARE(renderQss("@{accent.solid/50}", Theme::Dark), QString("rgba(124, 77, 235, 128)"));
  }
};

QTEST_GUILESS_MAIN(TestThemeTokens)
#include "test_theme_tokens.moc"
