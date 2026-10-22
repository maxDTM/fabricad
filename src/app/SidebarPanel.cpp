// FabriCAD app: sidebar panel and its header.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/SidebarPanel.h"

#include "app/Icons.h"
#include "app/ThemeManager.h"
#include "theme/Tokens.h"

#include <QHBoxLayout>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <QVBoxLayout>

namespace fabricad::app {

using theme::Tok;

namespace {
constexpr int kTickX = 10;
constexpr int kGlyphX = 20;
constexpr int kTitleX = 42;
constexpr int kChevronW = 22;
}  // namespace

PanelHeader::PanelHeader(const QString& title, const QString& glyph, QWidget* parent)
    : QWidget(parent), title_(title), glyph_(glyph) {
  setObjectName("PanelHeader");
  setProperty("panel", title.toLower());
  setFixedHeight(kHeight);
  setFocusPolicy(Qt::TabFocus);
  setCursor(Qt::PointingHandCursor);
  setAttribute(Qt::WA_Hover);
  tools_ = new QHBoxLayout(this);
  tools_->setContentsMargins(0, 1, kChevronW + 4, 1);
  tools_->setSpacing(2);
  tools_->addStretch(1);
  setToolTip(QString("%1: click to collapse or expand").arg(title));
  connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this] { update(); });
}

QSize PanelHeader::sizeHint() const { return QSize(260, kHeight); }

void PanelHeader::setCount(int n) {
  if (count_ == n) return;
  count_ = n;
  update();
}

void PanelHeader::setCollapsed(bool c) {
  if (collapsed_ == c) return;
  collapsed_ = c;
  update();
}

QToolButton* PanelHeader::addTool(const QString& objectName, const QString& icon, const QString& tooltip) {
  auto* b = new QToolButton(this);
  b->setObjectName(objectName);
  b->setProperty("panelTool", true);
  b->setIcon(resourceIcon(icon));
  b->setIconSize(QSize(16, 16));
  b->setFixedSize(26, 26);
  b->setAutoRaise(true);
  b->setToolTip(tooltip);
  b->setFocusPolicy(Qt::TabFocus);
  b->setCursor(Qt::ArrowCursor);
  tools_->addWidget(b);
  return b;
}

int PanelHeader::toolsLeft() const {
  int left = width() - kChevronW - 4;
  for (int i = 0; i < tools_->count(); ++i)
    if (QWidget* w = tools_->itemAt(i)->widget(); w && w->isVisible()) left = std::min(left, w->x());
  return left;
}

QRect PanelHeader::titleRect() const {
  const QFontMetrics fm(theme::font(theme::TextStyle::Header));
  return QRect(kTitleX, 0, fm.horizontalAdvance(title_.toUpper()), height());
}

void PanelHeader::paintEvent(QPaintEvent*) {
  QPainter p(this);
  const QRect r = rect();
  // band one step off the rows, strong rules above and below
  p.fillRect(r, theme::color(hover_ ? Tok::SurfaceHover : Tok::SurfaceHeader));
  p.fillRect(QRect(0, 0, r.width(), 1), theme::color(Tok::BorderStrong));
  p.fillRect(QRect(0, r.height() - 1, r.width(), 1), theme::color(Tok::BorderStrong));
  // 3 px accent tick
  p.fillRect(QRect(kTickX, 10, 3, r.height() - 20), theme::color(Tok::AccentIndicator));
  // section glyph
  if (!glyph_.isEmpty()) resourceIcon(glyph_).paint(&p, QRect(kGlyphX, (r.height() - 16) / 2, 16, 16));
  // title
  const QFont f = theme::font(theme::TextStyle::Header);
  p.setFont(f);
  p.setPen(theme::color(Tok::TextPrimary));
  const QFontMetrics fm(f);
  const QString upper = title_.toUpper();
  const int tw = fm.horizontalAdvance(upper);
  p.drawText(QRect(kTitleX, 0, tw + 2, r.height()), Qt::AlignVCenter | Qt::AlignLeft, title_);
  // count chip
  if (count_ >= 0) {
    const QFont cf = theme::font(theme::TextStyle::Keycap);
    const QFontMetrics cfm(cf);
    const QString n = QString::number(count_);
    const int cw = std::max(18, cfm.horizontalAdvance(n) + 10);
    const QRect chip(kTitleX + tw + 8, (r.height() - 18) / 2, cw, 18);
    if (chip.right() < toolsLeft() - 4) {
      p.setPen(theme::color(Tok::BorderDefault));
      p.setBrush(theme::color(Tok::SurfacePanel));
      p.drawRect(chip.adjusted(0, 0, -1, -1));
      p.setFont(cf);
      p.setPen(theme::color(Tok::TextSecondary));
      p.drawText(chip, Qt::AlignCenter, n);
    }
  }
  // chevron: down when open, right when collapsed
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(QPen(theme::color(hover_ ? Tok::AccentText : Tok::TextSecondary), 1.6));
  const QPointF c(r.width() - kChevronW / 2.0 - 4, r.height() / 2.0);
  QPainterPath path;
  if (collapsed_) {
    path.moveTo(c + QPointF(-2, -4));
    path.lineTo(c + QPointF(2, 0));
    path.lineTo(c + QPointF(-2, 4));
  } else {
    path.moveTo(c + QPointF(-4, -2));
    path.lineTo(c + QPointF(0, 2));
    path.lineTo(c + QPointF(4, -2));
  }
  p.setBrush(Qt::NoBrush);
  p.drawPath(path);
  if (hasFocus() && keyFocus_) {
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(theme::color(Tok::AccentFocus), 2));
    p.drawRect(QRectF(r).adjusted(1, 1, -1, -1));
  }
}

void PanelHeader::mousePressEvent(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton) return;
  setCollapsed(!collapsed_);
  emit toggled(collapsed_);
}

void PanelHeader::keyPressEvent(QKeyEvent* e) {
  if (e->key() == Qt::Key_Space || e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
    setCollapsed(!collapsed_);
    emit toggled(collapsed_);
    return;
  }
  QWidget::keyPressEvent(e);
}

void PanelHeader::focusInEvent(QFocusEvent* e) {
  keyFocus_ = e->reason() == Qt::TabFocusReason || e->reason() == Qt::BacktabFocusReason;
  update();
  QWidget::focusInEvent(e);
}

void PanelHeader::focusOutEvent(QFocusEvent* e) {
  keyFocus_ = false;
  update();
  QWidget::focusOutEvent(e);
}

void PanelHeader::enterEvent(QEnterEvent* e) {
  hover_ = true;
  update();
  QWidget::enterEvent(e);
}

void PanelHeader::leaveEvent(QEvent* e) {
  hover_ = false;
  update();
  QWidget::leaveEvent(e);
}

// =========================================================================================

SidebarPanel::SidebarPanel(const QString& key, const QString& title, const QString& glyph, QWidget* parent)
    : QWidget(parent), key_(key) {
  setObjectName(QString("SidebarPanel_%1").arg(key));
  setAttribute(Qt::WA_StyledBackground);
  layout_ = new QVBoxLayout(this);
  layout_->setContentsMargins(0, 0, 0, 0);
  layout_->setSpacing(0);
  header_ = new PanelHeader(title, glyph, this);
  header_->setObjectName(QString("PanelHeader_%1").arg(key));
  layout_->addWidget(header_);
  connect(header_, &PanelHeader::toggled, this, [this](bool c) { setCollapsed(c); });
}

void SidebarPanel::setBody(QWidget* body) {
  if (body_) layout_->removeWidget(body_);
  body_ = body;
  body_->setParent(this);
  layout_->addWidget(body_, 1);
  body_->setVisible(!collapsed_);
}

void SidebarPanel::setCollapsed(bool c) {
  header_->setCollapsed(c);
  if (collapsed_ == c) return;
  collapsed_ = c;
  if (body_) body_->setVisible(!c);
  setMaximumHeight(c ? PanelHeader::kHeight : QWIDGETSIZE_MAX);
  emit collapsedChanged(c);
}

}  // namespace fabricad::app
