#include "app/PopupAnchor.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDebug>
#include <QGuiApplication>
#include <QMenu>
#include <QMouseEvent>
#include <QScreen>
#include <QToolButton>

#include <algorithm>
#include <cmath>

namespace fabricad::app {

QPoint popupPosUnder(const QRect& anchor, const QSize& size, const QRect& avail, bool rtl) {
  const int w = size.width(), h = size.height();
  int x = rtl ? anchor.right() + 1 - w : anchor.left();
  int y = anchor.bottom() + 1;
  if (!avail.isValid() || avail.isEmpty()) return {x, y};
  const int bottom = avail.bottom() + 1, right = avail.right() + 1;
  if (y + h > bottom) {
    if (anchor.top() - h >= avail.top()) y = anchor.top() - h;  // flip above the label
    else y = std::max(avail.top(), bottom - h);                // neither fits: as low as possible
  }
  x = std::max(avail.left(), std::min(x, right - w));
  return {x, y};
}

namespace {
PopupAnchor* g_instance = nullptr;
bool debugOn() {
  static const bool on = qEnvironmentVariableIntValue("FABRICAD_POPUP_DEBUG") != 0;
  return on;
}
QToolButton* dropdownButton(QObject* o) {
  auto* b = qobject_cast<QToolButton*>(o);
  return b && b->menu() && b->isEnabled() ? b : nullptr;
}
}  // namespace

PopupAnchor::PopupAnchor(QObject* parent) : QObject(parent) {}

PopupAnchor& PopupAnchor::install() {
  if (!g_instance) {
    g_instance = new PopupAnchor(qApp);
    qApp->installEventFilter(g_instance);
  }
  return *g_instance;
}

PopupAnchor* PopupAnchor::instance() { return g_instance; }

QPoint PopupAnchor::correction(const QWidget* w) const {
  if (!w) return {};
  return corr_.value(w->window(), QPoint());
}

QRect PopupAnchor::globalRect(const QWidget* w) const {
  return QRect(w->mapToGlobal(QPoint(0, 0)) + correction(w), w->size());
}

bool PopupAnchor::showDropdown(QToolButton* b) {
  if (!b || !b->menu()) return false;
  rightOpened_ = b;
  b->showMenu();  // the button's own path: down state, split-arrow state, aboutToShow
  rightOpened_ = nullptr;
  return true;
}

QWidget* PopupAnchor::anchorFor(QMenu* m) const {
  if (rightOpened_ && rightOpened_->menu() == m) return rightOpened_;
  if (pressed_ && pressed_->menu() == m && pressed_->isVisible()) return pressed_;
  if (auto* f = qobject_cast<QToolButton*>(QApplication::focusWidget()); f && f->menu() == m) return f;
  if (auto* p = qobject_cast<QToolButton*>(m->parentWidget()); p && p->menu() == m && p->isVisible()) return p;
  return nullptr;
}

void PopupAnchor::place(QWidget* popup, QWidget* anchor) {
  const QRect a = globalRect(anchor);
  QSize size = popup->size();
  if (auto* m = qobject_cast<QMenu*>(popup)) size = size.expandedTo(m->sizeHint());
  QScreen* s = QGuiApplication::screenAt(a.center());
  if (!s) s = anchor->screen();
  const QRect avail = s ? s->availableGeometry() : QRect();
  if (auto* combo = qobject_cast<QComboBox*>(anchor); combo && combo->view()) {
    // the list opens under the combo now, not where Qt first put it: make sure all its rows
    // (up to maxVisibleItems) fit (the theme uses plain list popups, combobox-popup: 0)
    QAbstractItemView* view = combo->view();
    const int rows = std::min(combo->count(), std::max(1, combo->maxVisibleItems()));
    int rowsH = 0;
    for (int i = 0; i < rows; ++i) rowsH += std::max(1, view->sizeHintForRow(i));
    const int chrome = std::max(0, popup->height() - view->viewport()->height());
    int want = rowsH + chrome;
    if (avail.isValid()) want = std::min(want, avail.height());
    if (debugOn())
      qInfo().noquote() << "combo popup rowsH" << rowsH << "chrome" << chrome << "popup" << popup->size() << "viewport"
                        << view->viewport()->size() << "row0" << view->visualRect(combo->model()->index(0, 0));
    if (want > size.height()) {
      size.setHeight(want);
      popup->resize(size);
    }
  }
  const QPoint pos = popupPosUnder(a, size, avail, anchor->layoutDirection() == Qt::RightToLeft);
  if (debugOn())
    qInfo().noquote() << "popup" << popup->metaObject()->className() << "anchor" << anchor->objectName() << a << "qt pos"
                      << popup->pos() << "-> " << pos << "correction" << correction(anchor);
  popup->move(pos);
  lastPlacement_ = QRect(pos, size);
}

bool PopupAnchor::eventFilter(QObject* o, QEvent* e) {
  switch (e->type()) {
    case QEvent::MouseButtonPress: {
      auto* w = qobject_cast<QWidget*>(o);
      if (!w) break;
      auto* me = static_cast<QMouseEvent*>(e);
      QWidget* win = w->window();
      if (win->windowType() != Qt::Popup) {
        // server-reported global (root coordinates) minus what Qt maps from its cached window
        // position: non-zero when that cache is stale (Xwayland on a tiling compositor)
        const QPointF mapped = QPointF(w->mapToGlobal(QPoint(0, 0))) + me->position();
        const QPointF d = me->globalPosition() - mapped;
        const QPoint off(static_cast<int>(std::lround(d.x())), static_cast<int>(std::lround(d.y())));
        const QPoint use = std::abs(off.x()) <= 1 && std::abs(off.y()) <= 1 ? QPoint() : off;
        if (!corr_.contains(win)) connect(win, &QObject::destroyed, this, [this, win] { corr_.remove(win); });
        corr_.insert(win, use);
      }
      if (auto* b = qobject_cast<QToolButton*>(w)) pressed_ = b;
      if (me->button() == Qt::RightButton) {
        // a right click on a button with a dropdown opens that dropdown (left click: the tool)
        if (QToolButton* b = dropdownButton(o)) {
          showDropdown(b);
          return true;
        }
      }
      break;
    }
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
      if (static_cast<QMouseEvent*>(e)->button() == Qt::RightButton && dropdownButton(o)) return true;
      break;
    case QEvent::ContextMenu:
      // the right click opened the dropdown; no context menu of a parent on top of it
      if (dropdownButton(o)) return true;
      break;
    case QEvent::Show: {
      if (auto* m = qobject_cast<QMenu*>(o)) {
        if (QWidget* a = anchorFor(m)) place(m, a);
        break;
      }
      // a QComboBox list (its private container is a popup window, child of the combo)
      auto* w = qobject_cast<QWidget*>(o);
      if (w && w->isWindow() && w->windowType() == Qt::Popup)
        if (auto* combo = qobject_cast<QComboBox*>(w->parentWidget())) place(w, combo);
      break;
    }
    default: break;
  }
  return false;
}

}  // namespace fabricad::app
