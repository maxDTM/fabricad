// FabriCAD theme: QSS template renderer.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "theme/QssTemplate.h"

#include <QRegularExpression>

#include <algorithm>

namespace fabricad::theme {

QString qssColor(const QColor& c) {
  if (c.alpha() == 255) return c.name(QColor::HexRgb).toUpper();
  return QStringLiteral("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());  // colour-lint: ok (formats token colours)
}

QString uiAsset(const QString& name, Theme th) {
  return QStringLiteral(":/ui/") + QLatin1StringView(themeName(th)) + QLatin1Char('/') + name;
}

QString renderQss(const QString& tmplIn, Theme th, QStringList* errors) {
  static const QRegularExpression re(QStringLiteral(R"(@\{([A-Za-z0-9_.]+)(?:/(\d{1,3}))?\})"));
  static const QRegularExpression comment(QStringLiteral(R"(/\*.*?\*/)"),
                                          QRegularExpression::DotMatchesEverythingOption);
  QString tmpl = tmplIn;
  tmpl.remove(comment);  // comments may describe the placeholder syntax
  QString out;
  out.reserve(tmpl.size() + tmpl.size() / 4);
  qsizetype last = 0;
  for (auto it = re.globalMatch(tmpl); it.hasNext();) {
    const QRegularExpressionMatch m = it.next();
    out += QStringView(tmpl).mid(last, m.capturedStart() - last);
    last = m.capturedEnd();
    const QString name = m.captured(1);
    if (name == QLatin1StringView("theme") && m.captured(2).isEmpty()) {
      out += QLatin1StringView(themeName(th));
      continue;
    }
    const std::optional<Tok> tok = find(name);
    if (!tok) {
      if (errors) *errors << QStringLiteral("unknown placeholder @{%1}").arg(name);
      out += m.captured(0);
      continue;
    }
    QColor c = color(*tok, th);
    if (!m.captured(2).isEmpty()) c.setAlphaF(std::clamp(c.alphaF() * m.captured(2).toInt() / 100.0, 0.0, 1.0));
    out += qssColor(c);
  }
  out += QStringView(tmpl).mid(last);
  if (errors && out.contains(QStringLiteral("@{"))) *errors << QStringLiteral("unresolved '@{' left in the output");
  return out;
}

}  // namespace fabricad::theme
