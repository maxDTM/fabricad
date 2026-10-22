// FabriCAD app: stand-in sidebar card slot (until SidebarHost, phase 2).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/CardSlot.h"

#include <QVBoxLayout>

#include <algorithm>

namespace fabricad::app {

SimpleCardSlot::SimpleCardSlot(QWidget* parent) : QWidget(parent) {
  setObjectName("CardSlot");
  auto* v = new QVBoxLayout(this);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(0);
  hide();
}

bool SimpleCardSlot::cardSlotAvailable() const {
  const QWidget* p = parentWidget();
  return p && p->isVisible();
}

void SimpleCardSlot::dockCard(QWidget* card) {
  if (card == card_) {
    if (card) card->show();
    return;
  }
  if (card_) {
    layout()->removeWidget(card_);
    card_->hide();
  }
  card_ = card;
  if (card) {
    card->setParent(this);
    layout()->addWidget(card);
    card->show();
    show();
  } else {
    hide();
  }
}

int SimpleCardSlot::cardSlotWidth() const {
  const QWidget* p = parentWidget();
  return p ? p->width() : width();
}

int SimpleCardSlot::cardSlotMaxHeight() const {
  // the browser below keeps at least 140 px
  const QWidget* p = parentWidget();
  return p ? std::max(200, p->height() - 140) : 600;
}

QRect SimpleCardSlot::cardSlotGlobalRect() const {
  const QWidget* p = parentWidget();
  if (!p || !p->isVisible()) return {};
  if (card_ && isVisible()) return QRect(mapToGlobal(QPoint(0, 0)), size());
  // empty slot: the top of the sidebar is the drop target
  return QRect(p->mapToGlobal(QPoint(0, 0)), QSize(p->width(), std::min(p->height(), 260)));
}

}  // namespace fabricad::app
