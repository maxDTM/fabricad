// FabriCAD app: light theme.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/Theme.h"

#include <QApplication>
#include <QFile>
#include <QFont>
#include <QStyleFactory>
#include <QToolTip>

namespace fabricad::app {

QPalette lightPalette() {
  const QColor window("#F2F3F5"), base("#FFFFFF"), altBase("#F7F7F8"), text("#2B2E33"), button("#FFFFFF"),
      highlight("#0696D7"), highlightedText("#FFFFFF"), placeholder("#8A9099"), disabledText("#A9AEB5"),
      mid("#C3C8CF"), dark("#8A9099"), light("#FFFFFF"), shadow("#6B7079");
  QPalette p;
  for (QPalette::ColorGroup g : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
    const bool dis = g == QPalette::Disabled;
    const QColor fg = dis ? disabledText : text;
    p.setColor(g, QPalette::Window, window);
    p.setColor(g, QPalette::WindowText, fg);
    p.setColor(g, QPalette::Base, dis ? altBase : base);
    p.setColor(g, QPalette::AlternateBase, altBase);
    p.setColor(g, QPalette::Text, fg);
    p.setColor(g, QPalette::Button, button);
    p.setColor(g, QPalette::ButtonText, fg);
    p.setColor(g, QPalette::BrightText, QColor("#FFFFFF"));
    p.setColor(g, QPalette::Highlight, dis ? QColor("#C9D7E6") : highlight);
    p.setColor(g, QPalette::HighlightedText, highlightedText);
    p.setColor(g, QPalette::ToolTipBase, QColor("#FFFFFF"));
    p.setColor(g, QPalette::ToolTipText, text);
    p.setColor(g, QPalette::PlaceholderText, placeholder);
    p.setColor(g, QPalette::Link, highlight);
    p.setColor(g, QPalette::LinkVisited, QColor("#1F74D1"));
    p.setColor(g, QPalette::Light, light);
    p.setColor(g, QPalette::Midlight, QColor("#E1E4E8"));
    p.setColor(g, QPalette::Mid, mid);
    p.setColor(g, QPalette::Dark, dark);
    p.setColor(g, QPalette::Shadow, shadow);
    p.setColor(g, QPalette::Accent, highlight);
  }
  return p;
}

void applyLightTheme(QApplication& app) {
  app.setStyle(QStyleFactory::create("Fusion"));
  const QPalette pal = lightPalette();
  app.setPalette(pal);
  // the platform theme's font (often monospace or very large) must not leak into the chrome
  QFont f("Sans Serif");
  f.setStyleHint(QFont::SansSerif);
  f.setPointSizeF(9.0);
  app.setFont(f);
  // platform themes also hand out per-class fonts (item views, menus, tabs...); override those
  for (const char* cls : {"QAbstractItemView", "QHeaderView", "QMenu", "QMenuBar", "QTabBar", "QLabel", "QPushButton",
                          "QToolButton", "QLineEdit", "QComboBox", "QAbstractSpinBox", "QCheckBox", "QRadioButton",
                          "QGroupBox", "QDockWidget", "QStatusBar", "QTextEdit", "QPlainTextEdit", "QMessageBox"})
    QApplication::setFont(f, cls);
  if (QFile qss(":/theme-light.qss"); qss.open(QIODevice::ReadOnly)) app.setStyleSheet(QString::fromUtf8(qss.readAll()));
  QToolTip::setPalette(pal);
  QToolTip::setFont(f);
}

}  // namespace fabricad::app
