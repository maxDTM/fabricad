// FabriCAD app: title-bar widgets (angled document tabs, workspace selector, keycaps).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/TitleBar.h"

#include "app/Icons.h"
#include "app/ThemeManager.h"
#include "theme/Tokens.h"

#include <QActionGroup>
#include <QApplication>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>

namespace fabricad::app {

using theme::Tok;

const WorkspaceShortcut& workspaceShortcut(WorkspaceChoice c) {
  for (const WorkspaceShortcut& s : kWorkspaceShortcuts)
    if (s.choice == c) return s;
  return kWorkspaceShortcuts[0];
}

QString keycapText(const QString& keySequence) {
  QString t = QKeySequence(keySequence).toString(QKeySequence::NativeText);
  if (t.isEmpty()) t = keySequence;
  t.replace('+', ' ');
  return t;
}

Keycap::Keycap(const QString& keys, QWidget* parent) : QLabel(parent) {
  setObjectName("Keycap");
  setKeys(keys);
  setAlignment(Qt::AlignCenter);
}

// =========================================================================================
// DocumentTabBar
// =========================================================================================

DocumentTabBar::DocumentTabBar(QWidget* parent) : QTabBar(parent) {
  setObjectName("DocumentTabs");
  setMouseTracking(true);
  setDrawBase(false);
  setExpanding(false);
  setDocumentMode(true);
  // repaint our tabs with the new tokens
  connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this] { update(); });
}

QString DocumentTabBar::plainText(const QString& tabText, bool* modified) {
  const bool mod = tabText.endsWith(QLatin1StringView(" *"));
  if (modified) *modified = mod;
  return mod ? tabText.left(tabText.size() - 2) : tabText;
}

QSize DocumentTabBar::tabSizeHint(int index) const {
  bool modified = false;
  const QString text = plainText(tabText(index), &modified);
  const QFontMetrics fm(font());
  int w = 2 * kSlant + 12 + fm.horizontalAdvance(text) + 10 /*dot*/ + 8;
  if (tabsClosable()) w += 18;
  return QSize(std::clamp(w, 120, 260), kHeight);
}

QSize DocumentTabBar::minimumTabSizeHint(int index) const {
  return QSize(std::min(120, tabSizeHint(index).width()), kHeight);
}

QPainterPath DocumentTabBar::tabPath(int index) const {
  const QRectF r = QRectF(tabRect(index)).adjusted(0.5, 0.5, -0.5, 0);
  QPainterPath p;
  p.moveTo(r.left(), r.bottom());
  p.lineTo(r.left() + kSlant, r.top());
  p.lineTo(r.right() - kSlant, r.top());
  p.lineTo(r.right(), r.bottom());
  p.closeSubpath();
  return p;
}

int DocumentTabBar::tabAtPath(const QPoint& pos) const {
  const int cur = currentIndex();
  if (cur >= 0 && tabPath(cur).contains(pos)) return cur;  // the front tab owns its slants
  for (int i = 0; i < count(); ++i)
    if (tabPath(i).contains(pos)) return i;
  return tabAt(pos);
}

void DocumentTabBar::paintEvent(QPaintEvent* e) {
  if (dragging_) {
    // a tab is being moved: QTabBar animates the others and draws the moving one itself
    QTabBar::paintEvent(e);
    return;
  }
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QColor accent = theme::color(Tok::AccentIndicator);
  for (int i = 0; i < count(); ++i) {
    if (!isTabVisible(i)) continue;
    const QRect r = tabRect(i);
    if (!r.intersects(e->rect())) continue;
    const bool active = i == currentIndex();
    const bool hover = i == hover_ && !active;
    const QPainterPath path = tabPath(i);
    if (active || hover) p.fillPath(path, theme::color(active ? Tok::SurfacePanel : Tok::SurfaceHover));
    if (active) {
      // 2 px accent along the top edge
      QPen pen(accent, 2);
      pen.setCapStyle(Qt::FlatCap);
      p.setPen(pen);
      p.drawLine(QPointF(r.left() + kSlant + 0.5, r.top() + 1), QPointF(r.right() - kSlant - 0.5, r.top() + 1));
      // sides
      QPen side(theme::color(Tok::BorderDefault), 1);
      p.setPen(side);
      p.drawLine(QPointF(r.left() + 0.5, r.bottom() + 0.5), QPointF(r.left() + kSlant + 0.5, r.top() + 0.5));
      p.drawLine(QPointF(r.right() - kSlant - 0.5, r.top() + 0.5), QPointF(r.right() - 0.5, r.bottom() + 0.5));
    } else if (i + 1 != currentIndex()) {
      // slanted divider on the right of an inactive tab (the active neighbour draws its own)
      QPen side(theme::color(Tok::BorderDefault), 1);
      p.setPen(side);
      p.drawLine(QPointF(r.right() - kSlant - 0.5, r.top() + 9.5), QPointF(r.right() - 2.5, r.bottom() - 7.5));
    }
    bool modified = false;
    const QString text = plainText(tabText(i), &modified);
    QRect textRect = r.adjusted(kSlant + 10, 0, -(kSlant + 6), 0);
    if (QWidget* btn = tabButton(i, QTabBar::RightSide); btn && btn->isVisible())
      textRect.setRight(std::min(textRect.right(), btn->geometry().left() - 4));
    if (modified) textRect.setRight(textRect.right() - 12);
    p.setFont(font());
    p.setPen(theme::color(active || hover ? Tok::TextPrimary : Tok::TextSecondary));
    const QString shown = fontMetrics().elidedText(text, elideMode() == Qt::ElideNone ? Qt::ElideRight : elideMode(),
                                                   textRect.width());
    p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, shown);
    if (modified) {
      // neutral unsaved marker (decision 11): gold stays reserved for live operations
      const int x = textRect.left() + fontMetrics().horizontalAdvance(shown) + 8;
      p.setPen(Qt::NoPen);
      p.setBrush(theme::color(Tok::TextSecondary));
      p.drawEllipse(QPointF(x, r.center().y() + 0.5), 3.0, 3.0);
    }
  }
}

void DocumentTabBar::mousePressEvent(QMouseEvent* e) {
  if (e->button() == Qt::LeftButton) {
    pressed_ = tabAtPath(e->position().toPoint());
    pressPos_ = e->position().toPoint();
    dragging_ = false;
  }
  QTabBar::mousePressEvent(e);
}

void DocumentTabBar::mouseMoveEvent(QMouseEvent* e) {
  const int h = tabAtPath(e->position().toPoint());
  if (h != hover_) {
    hover_ = h;
    update();
  }
  if (pressed_ >= 0 && isMovable() && (e->buttons() & Qt::LeftButton) && !dragging_ &&
      (e->position().toPoint() - pressPos_).manhattanLength() >= QApplication::startDragDistance()) {
    dragging_ = true;
    update();
  }
  QTabBar::mouseMoveEvent(e);
}

void DocumentTabBar::mouseReleaseEvent(QMouseEvent* e) {
  QTabBar::mouseReleaseEvent(e);
  if (e->button() == Qt::LeftButton) {
    pressed_ = -1;
    if (dragging_) {
      dragging_ = false;
      update();
    }
  }
}

void DocumentTabBar::leaveEvent(QEvent* e) {
  hover_ = -1;
  update();
  QTabBar::leaveEvent(e);
}

// =========================================================================================
// WorkspaceSelector
// =========================================================================================

WorkspaceSelector::WorkspaceSelector(QWidget* parent) : QToolButton(parent) {
  setObjectName("WorkspaceSwitcher");
  setPopupMode(QToolButton::InstantPopup);
  setFocusPolicy(Qt::NoFocus);
  setToolTip("Workspace");
  setAccessibleName("Workspace");
  auto* h = new QHBoxLayout(this);
  h->setContentsMargins(8, 0, 6, 0);
  h->setSpacing(6);
  icon_ = new QLabel(this);
  icon_->setObjectName("WorkspaceIcon");
  name_ = new QLabel(this);
  name_->setObjectName("WorkspaceName");
  keycap_ = new Keycap(QString(), this);
  arrow_ = new QLabel(QString::fromUtf8("▾"), this);
  arrow_->setObjectName("WorkspaceArrow");
  for (QLabel* l : {icon_, name_, static_cast<QLabel*>(keycap_), arrow_}) {
    l->setAttribute(Qt::WA_TransparentForMouseEvents);
    h->addWidget(l, 0, Qt::AlignVCenter);
  }
  menu_ = new QMenu(this);
  menu_->setObjectName("WorkspaceMenu");
  auto* group = new QActionGroup(menu_);
  for (const WorkspaceShortcut& s : kWorkspaceShortcuts) {
    QAction* a = menu_->addAction(resourceIcon(s.icon), QString("%1\t%2").arg(s.name, QKeySequence(s.shortcut).toString(QKeySequence::NativeText)));
    a->setCheckable(true);
    a->setData(static_cast<int>(s.choice));
    a->setObjectName(QString("Workspace%1").arg(s.name));
    group->addAction(a);
    connect(a, &QAction::triggered, this, [this, c = s.choice] { emit requested(c); });
  }
  setMenu(menu_);
  // the button draws no text of its own (its labels do); the QSS hides Qt's menu arrow
  setChoice(WorkspaceChoice::Part);
  connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this] { setChoice(choice_); });
}

void WorkspaceSelector::setChoice(WorkspaceChoice c) {
  choice_ = c;
  const WorkspaceShortcut& s = workspaceShortcut(c);
  icon_->setPixmap(resourceIcon(s.icon).pixmap(QSize(16, 16), devicePixelRatioF()));
  name_->setText(s.name);
  keycap_->setKeys(s.shortcut);
  setProperty("workspace", QString(s.name).toLower());
  setToolTip(QString("Workspace: %1 (%2)").arg(s.name, s.shortcut));
  for (QAction* a : menu_->actions()) a->setChecked(a->data().toInt() == static_cast<int>(c));
  updateGeometry();
}

QSize WorkspaceSelector::sizeHint() const {
  QSize s = layout() ? layout()->sizeHint() : QSize(120, 28);
  return QSize(std::max(s.width(), 120), 28);
}

}  // namespace fabricad::app
