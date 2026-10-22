// FabriCAD app: status bar cells.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/StatusCells.h"

#include "app/ThemeManager.h"
#include "app/TitleBar.h"
#include "theme/Tokens.h"

#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>

#include <cmath>

namespace fabricad::app {

using theme::TextStyle;
using theme::Tok;

// ---- ElideLabel ---------------------------------------------------------------------------

ElideLabel::ElideLabel(QWidget* parent) : QLabel(parent) {
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
}

QSize ElideLabel::minimumSizeHint() const { return QSize(60, QLabel::minimumSizeHint().height()); }

QSize ElideLabel::sizeHint() const {
  const QSize s = QLabel::sizeHint();
  return QSize(std::min(s.width(), 520), s.height());
}

void ElideLabel::paintEvent(QPaintEvent*) {
  QPainter p(this);
  const QRect r = contentsRect().adjusted(margin() + indent() * 0, 0, -margin(), 0);
  p.setPen(palette().color(foregroundRole()));
  p.setFont(font());
  p.drawText(r, Qt::AlignVCenter | Qt::AlignLeft, fontMetrics().elidedText(text(), Qt::ElideRight, r.width()));
}

// ---- StatusCommandCell -----------------------------------------------------------------------

StatusCommandCell::StatusCommandCell(QWidget* parent) : QWidget(parent) {
  setObjectName("StatusCommand");
  setVisible(false);
  connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this] { update(); });
}

void StatusCommandCell::setCommand(const QString& name) {
  name_ = name;
  setVisible(!name.isEmpty());
  setToolTip(name.isEmpty() ? QString() : QString("Active command: %1").arg(name));
  updateGeometry();
  update();
}

QSize StatusCommandCell::sizeHint() const {
  return QSize(3 + 8 + 8 + 6 + fontMetrics().horizontalAdvance(name_) + 10, 22);
}

void StatusCommandCell::paintEvent(QPaintEvent*) {
  QPainter p(this);
  const QRect r = rect();
  p.fillRect(QRect(r.left(), r.top(), 3, r.height()), theme::color(Tok::LiveBase));
  // gold square with an ink edge (light theme rule: gold always carries live.ink)
  const QRect sq(r.left() + 3 + 8, r.center().y() - 3, 7, 7);
  p.fillRect(sq, theme::color(Tok::LiveBase));
  if (theme::current() == theme::Theme::Light) {
    p.setPen(theme::color(Tok::LiveInk));
    p.drawRect(sq.adjusted(0, 0, -1, -1));
  }
  p.setPen(theme::color(Tok::LiveText));
  p.setFont(font());
  p.drawText(r.adjusted(sq.right() + 7, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, name_);
}

// ---- KeyHints ----------------------------------------------------------------------------------

KeyHints::KeyHints(QWidget* parent) : QWidget(parent) {
  setObjectName("StatusHints");
  connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this] { update(); });
}

QFont KeyHints::keyFont() const { return theme::font(TextStyle::Keycap); }

void KeyHints::setHints(std::vector<Hint> hints) {
  hints_ = std::move(hints);
  setVisible(!hints_.empty());
  updateGeometry();
  update();
}

QString KeyHints::text() const {
  QStringList parts;
  for (const auto& [k, l] : hints_) parts << keycapText(k) + " " + l;
  return parts.join("  ");
}

QSize KeyHints::sizeHint() const {
  const QFontMetrics kf(keyFont());
  const QFontMetrics lf(font());
  int w = 6;
  for (const auto& [k, l] : hints_) w += kf.horizontalAdvance(keycapText(k)) + 10 + 5 + lf.horizontalAdvance(l) + 14;
  return QSize(w, 22);
}

void KeyHints::paintEvent(QPaintEvent*) {
  QPainter p(this);
  const QFont kfnt = keyFont();
  const QFontMetrics kf(kfnt);
  const QFontMetrics lf(font());
  int x = 6;
  const int h = height();
  for (const auto& [k, l] : hints_) {
    const QString key = keycapText(k);
    const int kw = kf.horizontalAdvance(key) + 10;
    if (x + kw > width()) break;
    const QRect cap(x, (h - 16) / 2, kw, 16);
    p.fillRect(cap, theme::color(Tok::SurfaceHeader));
    p.setPen(theme::color(Tok::BorderDefault));
    p.drawRect(cap.adjusted(0, 0, -1, -1));
    p.setFont(kfnt);
    p.setPen(theme::color(Tok::TextSecondary));
    p.drawText(cap, Qt::AlignCenter, key);
    x += kw + 5;
    const int lw = lf.horizontalAdvance(l);
    p.setFont(font());
    p.setPen(theme::color(Tok::TextTertiary));
    p.drawText(QRect(x, 0, lw + 2, h), Qt::AlignVCenter | Qt::AlignLeft, l);
    x += lw + 14;
  }
}

// ---- StatusCell ---------------------------------------------------------------------------------

StatusCell::StatusCell(const QString& caption, QWidget* parent) : QWidget(parent) {
  setObjectName("StatusCellBox");
  setAttribute(Qt::WA_StyledBackground);
  auto* h = new QHBoxLayout(this);
  h->setContentsMargins(8, 0, 8, 0);
  h->setSpacing(5);
  caption_ = new QLabel(caption, this);
  caption_->setObjectName("StatusCell");
  value_ = new QLabel(this);
  value_->setObjectName("StatusValue");
  value_->setProperty("textCase", "as-is");  // units and numbers as typed
  h->addWidget(caption_);
  h->addWidget(value_);
  value_->setVisible(false);
  caption_->setVisible(!caption.isEmpty());
}

void StatusCell::setCaption(const QString& c) {
  caption_->setText(c);
  caption_->setVisible(!c.isEmpty());
}

void StatusCell::setValue(const QString& v) {
  value_->setText(v);
  value_->setVisible(!v.isEmpty());
}

QString StatusCell::caption() const { return caption_->text(); }
QString StatusCell::value() const { return value_->text(); }

void StatusCell::setOn(bool on) {
  if (on == on_ && property("on").isValid()) return;
  on_ = on;
  setProperty("on", on);
  for (QWidget* w : {static_cast<QWidget*>(this), static_cast<QWidget*>(caption_), static_cast<QWidget*>(value_)}) {
    w->setProperty("on", on);
    w->style()->unpolish(w);
    w->style()->polish(w);
  }
}

void StatusCell::setClickAction(std::function<void()> fn) {
  click_ = std::move(fn);
  setCursor(click_ ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void StatusCell::mouseReleaseEvent(QMouseEvent* e) {
  if (click_ && e->button() == Qt::LeftButton && rect().contains(e->position().toPoint())) {
    click_();
    return;
  }
  QWidget::mouseReleaseEvent(e);
}

// ---- RebuildMeter ----------------------------------------------------------------------------------

RebuildMeter::RebuildMeter(QWidget* parent) : QWidget(parent) {
  setObjectName("StatusRebuild");
  setToolTip("Time of the last recompute");
  connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this] { update(); });
}

int RebuildMeter::barsFor(qint64 ms) {
  if (ms < 0) return 0;
  // 1, 3, 10, 30, 100, 300, 1000, 2000 ms
  static constexpr qint64 kSteps[] = {1, 3, 10, 30, 100, 300, 1000, 2000};
  int n = 1;
  for (int i = 1; i < 8; ++i)
    if (ms >= kSteps[i]) n = i + 1;
  return n;
}

void RebuildMeter::setMilliseconds(qint64 ms) {
  ms_ = ms;
  setToolTip(ms < 0 ? QString("No recompute yet") : QString("Last recompute: %1 ms").arg(ms));
  updateGeometry();
  update();
}

QSize RebuildMeter::sizeHint() const {
  const QString label = ms_ < 0 ? QString("REBUILD") : QString("REBUILD %1 ms").arg(ms_);
  return QSize(8 + 8 * 3 + 6 + fontMetrics().horizontalAdvance(label) + 8 + fontMetrics().horizontalAdvance("0000"),
               22);
}

void RebuildMeter::paintEvent(QPaintEvent*) {
  QPainter p(this);
  const int h = height();
  const int lit = barsFor(ms_);
  const QColor on = theme::color(ms_ >= 1000 ? Tok::StatusWarning : Tok::TextSecondary);
  const QColor off = theme::color(Tok::BorderDefault);
  int x = 8;
  for (int i = 0; i < 8; ++i) {
    p.fillRect(QRect(x, (h - 10) / 2, 2, 10), i < lit ? on : off);
    x += 3;
  }
  x += 6;
  // caption in caps, the number and unit as typed
  QFont caps = font();
  p.setFont(caps);
  p.setPen(theme::color(Tok::TextTertiary));
  const QString cap = QStringLiteral("Rebuild");
  p.drawText(QRect(x, 0, width() - x, h), Qt::AlignVCenter | Qt::AlignLeft, cap);
  x += fontMetrics().horizontalAdvance(cap) + 6;
  if (ms_ >= 0) {
    QFont val = theme::font(TextStyle::Value);
    val.setPixelSize(font().pixelSize() > 0 ? font().pixelSize() + 1 : val.pixelSize());
    p.setFont(val);
    p.setPen(theme::color(Tok::TextPrimary));
    p.drawText(QRect(x, 0, width() - x, h), Qt::AlignVCenter | Qt::AlignLeft, QString("%1 ms").arg(ms_));
  }
}

}  // namespace fabricad::app
