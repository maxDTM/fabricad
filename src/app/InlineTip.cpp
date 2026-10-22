// FabriCAD app: inline tip.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/InlineTip.h"

#include "theme/Tokens.h"

#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

namespace fabricad::app {

using theme::Tok;

InlineTip::InlineTip(QWidget* window) : QFrame(window) {
  setObjectName("InlineTip");
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setFocusPolicy(Qt::NoFocus);
  auto* v = new QVBoxLayout(this);
  v->setContentsMargins(11, 6, 8, 6);
  v->setSpacing(1);
  title_ = new QLabel(this);
  title_->setObjectName("InlineTipTitle");
  title_->setFont(theme::font(theme::TextStyle::Body));
  text_ = new QLabel(this);
  text_->setObjectName("InlineTipText");
  text_->setFont(theme::font(theme::TextStyle::Caption));
  v->addWidget(title_);
  v->addWidget(text_);
  hide();
}

InlineTip* InlineTip::forWidget(QWidget* anchor) {
  QWidget* win = anchor->window();
  if (auto* t = win->findChild<InlineTip*>("InlineTip", Qt::FindDirectChildrenOnly)) return t;
  return new InlineTip(win);
}

QString InlineTip::title() const { return title_->text(); }
QString InlineTip::text() const { return text_->text(); }

void InlineTip::showBeside(QWidget* anchor, const QRect& rowRect, const QString& title, const QString& text, Kind kind) {
  kind_ = kind;
  title_->setFont(theme::font(theme::TextStyle::Body));
  text_->setFont(theme::font(theme::TextStyle::Caption));
  title_->setText(title);
  text_->setText(text);
  text_->setVisible(!text.isEmpty());
  QPalette pal = palette();
  pal.setColor(QPalette::WindowText, theme::color(Tok::TextPrimary));
  title_->setPalette(pal);
  pal.setColor(QPalette::WindowText, theme::color(Tok::TextSecondary));
  text_->setPalette(pal);
  adjustSize();
  QWidget* win = parentWidget();
  const QPoint rowTopRight = anchor->mapTo(win, rowRect.topRight());
  const QPoint rowTopLeft = anchor->mapTo(win, rowRect.topLeft());
  const QRect anchorRect(anchor->mapTo(win, QPoint(0, 0)), anchor->size());
  int x = anchorRect.right() + 6;
  if (x + width() > win->width() - 4) x = anchorRect.left() - width() - 6;  // sidebar on the right
  if (x < 4) x = std::max(4, rowTopLeft.x() + 24);                          // no room: over the list
  int y = rowTopRight.y() + (rowRect.height() - height()) / 2;
  y = std::clamp(y, 4, std::max(4, win->height() - height() - 4));
  move(x, y);
  raise();
  show();
}

void InlineTip::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.fillRect(rect(), theme::color(Tok::SurfaceOverlay));
  p.setPen(theme::color(Tok::BorderDefault));
  p.drawRect(rect().adjusted(0, 0, -1, -1));
  p.fillRect(QRect(0, 0, 3, height()), theme::color(kind_ == Kind::Error ? Tok::StatusError : Tok::AccentIndicator));
}

}  // namespace fabricad::app
