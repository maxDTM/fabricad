// FabriCAD app: icon lookup.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/Icons.h"

#include "core/Feature.h"
#include "theme/Tokens.h"
#include "core/Tool.h"

#include <QFile>
#include <QHash>
#include <QIconEngine>
#include <QLabel>
#include <QMenu>
#include <QWidgetAction>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QStringList>

namespace fabricad::app {

namespace {

// badge hues (icon.badge.* tokens); resolved at paint time so a theme switch needs no setIcon
constexpr theme::Tok kBadgeTokens[] = {theme::Tok::IconBadgeBlue,   theme::Tok::IconBadgeGreen, theme::Tok::IconBadgeAmber,
                                       theme::Tok::IconBadgeViolet, theme::Tok::IconBadgeTeal,  theme::Tok::IconBadgeRed,
                                       theme::Tok::IconBadgeSlate,  theme::Tok::IconBadgeCyan};

theme::Tok badgeToken(const QString& key) {
  return kBadgeTokens[qHash(key) % (sizeof(kBadgeTokens) / sizeof(kBadgeTokens[0]))];
}

/// Draws a square badge at any size (crisp at 16 and 32 px).
class BadgeEngine : public QIconEngine {
 public:
  BadgeEngine(QString text, theme::Tok tok) : text_(std::move(text)), tok_(tok) {}
  void paint(QPainter* p, const QRect& rect, QIcon::Mode mode, QIcon::State) override {
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const QColor c = theme::color(mode == QIcon::Disabled ? theme::Tok::TextDisabled : tok_);
    const QRectF r = QRectF(rect).adjusted(rect.width() * 0.08, rect.height() * 0.08, -rect.width() * 0.08,
                                           -rect.height() * 0.08);
    QPainterPath path;
    path.addRect(r);
    QColor fill = c;
    fill.setAlpha(38);
    p->fillPath(path, fill);
    p->setPen(QPen(c, std::max(1.0, rect.width() / 16.0)));
    p->drawPath(path);
    QFont f = p->font();
    f.setBold(true);
    f.setPixelSize(std::max(6, static_cast<int>(r.height() * (text_.size() > 1 ? 0.42 : 0.55))));
    p->setFont(f);
    p->setPen(c);
    p->drawText(r, Qt::AlignCenter, text_);
    p->restore();
  }
  QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
    QPixmap pm(size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    paint(&p, QRect(QPoint(0, 0), size), mode, state);
    return pm;
  }
  QIconEngine* clone() const override { return new BadgeEngine(text_, tok_); }

 private:
  QString text_;
  theme::Tok tok_;
};

QHash<QString, QIcon>& cache() {
  static QHash<QString, QIcon> c;
  return c;
}

}  // namespace

QString badgeText(const QString& label) {
  const QStringList words = label.split(QRegularExpression("[\\s\\-_./]+"), Qt::SkipEmptyParts);
  if (words.isEmpty()) return "?";
  if (words.size() == 1) {
    const QString& w = words.front();
    return w.size() >= 2 ? QString(w[0].toUpper()) + w[1].toLower() : w.toUpper();
  }
  return QString(words[0][0].toUpper()) + words[1][0].toUpper();
}

QColor badgeColor(const QString& key) { return theme::color(badgeToken(key)); }

QString resourceIconPath(const QString& nameOrPath) {
  if (nameOrPath.isEmpty()) return {};
  const QString path = nameOrPath.startsWith(':') || nameOrPath.contains('/') ? nameOrPath
                                                                              : ":/icons/" + nameOrPath + ".svg";
  return QFile::exists(path) ? path : QString();
}

QIcon resourceIcon(const QString& nameOrPath) {
  if (nameOrPath.isEmpty()) return {};
  auto& c = cache();
  const QString key = "res:" + nameOrPath;
  if (auto it = c.find(key); it != c.end()) return *it;
  const QString path = resourceIconPath(nameOrPath);
  QIcon icon;
  if (!path.isEmpty()) icon = QIcon(path);
  c.insert(key, icon);
  return icon;
}

QIcon iconOr(const std::string& nameOrPath, const std::string& label) {
  QIcon i = resourceIcon(QString::fromStdString(nameOrPath));
  if (!i.isNull()) return i;
  const QString l = QString::fromStdString(label.empty() ? nameOrPath : label);
  auto& c = cache();
  const QString key = "badge:" + l;
  if (auto it = c.find(key); it != c.end()) return *it;
  QIcon badge(new BadgeEngine(badgeText(l), badgeToken(l)));
  c.insert(key, badge);
  return badge;
}

QString toolIconPath(const core::ToolInfo& info) {
  QString id = QString::fromStdString(info.id);
  if (QString p = resourceIconPath(QString(id).replace('.', '-')); !p.isEmpty()) return p;
  if (QString p = resourceIconPath(QString::fromStdString(info.icon)); !p.isEmpty()) return p;
  // tool id suffix as icon name ("part.extrude" -> extrude.svg)
  const qsizetype dot = id.indexOf('.');
  return resourceIconPath(dot < 0 ? id : id.mid(dot + 1));
}

QIcon toolIcon(const core::ToolInfo& info) {
  if (const QString p = toolIconPath(info); !p.isEmpty()) return resourceIcon(p);
  return iconOr("", info.name.empty() ? info.id : info.name);
}

QString featureIconPath(const std::string& iconName, const std::string& type, const std::string& folder) {
  if (QString p = resourceIconPath(QString::fromStdString(iconName)); !p.isEmpty()) return p;
  const QString t = QString::fromStdString(type).toLower();
  if (QString p = resourceIconPath("feature-" + t); !p.isEmpty()) return p;
  if (type == "Sketch") return resourceIconPath("sketch");
  if (folder == "Construction") {
    if (t.contains("axis")) return resourceIconPath("axis");
    if (t.contains("point")) return resourceIconPath("point");
    return resourceIconPath("plane");
  }
  if (folder == "Joints") return resourceIconPath("joint");
  return {};
}

QIcon featureIcon(const std::string& iconName, const std::string& type, const std::string& folder) {
  if (const QString p = featureIconPath(iconName, type, folder); !p.isEmpty()) return resourceIcon(p);
  return iconOr("", type);
}

QIcon featureIcon(const core::Feature& f) { return featureIcon(f.iconName(), f.type(), f.browserFolder()); }

QAction* addMenuHeader(QMenu* menu, const QString& text) {
  if (!menu->actions().isEmpty()) menu->addSeparator();
  auto* a = new QWidgetAction(menu);
  auto* label = new QLabel(text, menu);
  label->setObjectName("MenuHeader");
  a->setDefaultWidget(label);
  a->setText(text);
  a->setEnabled(false);
  menu->addAction(a);
  return a;
}

}  // namespace fabricad::app
