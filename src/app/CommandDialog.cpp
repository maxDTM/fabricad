// FabriCAD app: command dialog host.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/CommandDialog.h"

#include "viewer/ViewerWidget.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace fabricad::app {

namespace {
constexpr int kWidth = 300;
// Docked to the viewport's right edge (ux.md: "docked to viewport right edge"), below the
// ViewCube and its controls, like the sketch palette (SK-05).
constexpr int kRightMargin = 12;
constexpr int kTopMargin = 12;
constexpr int kDockTop = 150;  // below the ViewCube (same top as the sketch palette)
constexpr int kMaxBodyHeight = 380;  // taller bodies (Joint) scroll
constexpr int kWrapChars = 40;       // body labels longer than this word-wrap
}  // namespace

CommandDialog::CommandDialog(QWidget* viewport) : QFrame(viewport), viewport_(viewport) {
  setObjectName("CommandDialog");
  setAttribute(Qt::WA_StyledBackground);
  setFrameShape(QFrame::StyledPanel);
  setFixedWidth(kWidth);
  setFocusPolicy(Qt::StrongFocus);
  auto* v = new QVBoxLayout(this);
  v->setContentsMargins(0, 0, 0, 8);
  v->setSpacing(4);

  title_ = new QWidget(this);
  title_->setObjectName("CommandDialogTitle");
  title_->setAttribute(Qt::WA_StyledBackground);
  title_->setCursor(Qt::SizeAllCursor);
  title_->installEventFilter(this);
  auto* th = new QHBoxLayout(title_);
  th->setContentsMargins(8, 5, 8, 5);
  titleIcon_ = new QLabel(title_);
  titleText_ = new QLabel(title_);
  th->addWidget(titleIcon_);
  th->addWidget(titleText_, 1);
  v->addWidget(title_);

  scroll_ = new QScrollArea(this);
  scroll_->setObjectName("CommandDialogScroll");
  scroll_->setFrameShape(QFrame::NoFrame);
  scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll_->setWidgetResizable(true);
  scroll_->setFocusPolicy(Qt::NoFocus);
  inner_ = new QWidget(scroll_);
  bodyLayout_ = new QVBoxLayout(inner_);
  bodyLayout_->setContentsMargins(0, 0, 0, 0);
  scroll_->setWidget(inner_);
  inner_->setAutoFillBackground(false);  // the frame's white shows through
  scroll_->viewport()->setAutoFillBackground(false);
  inner_->installEventFilter(this);
  scroll_->hide();
  v->addWidget(scroll_);

  // The message line stays in the layout (empty when there is nothing to say) so that the
  // first prompt does not make the dialog jump.
  message_ = new QLabel(this);
  message_->setObjectName("CommandDialogMessage");
  message_->setWordWrap(true);
  message_->setContentsMargins(10, 0, 10, 0);
  message_->setMinimumHeight(message_->fontMetrics().lineSpacing());
  v->addWidget(message_);
  v->addStretch(1);  // extra (stable) height goes above the buttons

  buttonRow_ = new QWidget(this);
  auto* bh = new QHBoxLayout(buttonRow_);
  bh->setContentsMargins(10, 4, 10, 0);
  bh->addStretch(1);
  ok_ = new QPushButton("OK", buttonRow_);
  ok_->setObjectName("OkButton");
  ok_->setAutoDefault(false);
  cancel_ = new QPushButton("Cancel", buttonRow_);
  cancel_->setAutoDefault(false);
  bh->addWidget(ok_);
  bh->addWidget(cancel_);
  v->addWidget(buttonRow_);
  connect(ok_, &QPushButton::clicked, this, [this] {
    if (acceptEnabled_) emit accepted();
  });
  connect(cancel_, &QPushButton::clicked, this, &CommandDialog::cancelled);
  viewport_->installEventFilter(this);
  hide();
}

void CommandDialog::present(const QString& title, const QIcon& icon, QWidget* body, bool buttons) {
  clearBody();  // the next body takes over; the keyboard focus stays where it is
  stableHeight_ = 0;
  titleText_->setText(title.toUpper());
  titleIcon_->setPixmap(icon.isNull() ? QPixmap() : icon.pixmap(18, 18));
  if (body) {
    // long hints/prompts inside the body wrap instead of being clipped by the fixed width
    for (QLabel* l : body->findChildren<QLabel*>())
      if (!l->wordWrap() && l->pixmap().isNull() && l->text().size() > kWrapChars) l->setWordWrap(true);
    body->setParent(inner_);
    bodyLayout_->addWidget(body);
    body_ = body;
    body->show();
  }
  scroll_->setVisible(body != nullptr);
  scroll_->verticalScrollBar()->setValue(0);
  buttonRow_->setVisible(buttons);
  setAcceptEnabled(true);
  setMessage({});
  relayout();
  place();
  show();
  raise();
}

void CommandDialog::dismiss() {
  // Give the keyboard back to the viewport when the focus was inside the dialog, so the next
  // Esc reaches the canvas (and the tool) instead of whatever widget is next in the chain
  // (SK-08). Focus outside the dialog (the command line while typing) is never taken.
  if (isVisible() && viewport_) {
    QWidget* f = QApplication::focusWidget();
    if (f && (f == this || isAncestorOf(f))) viewport_->setFocus(Qt::OtherFocusReason);
  }
  clearBody();
  hide();
}

void CommandDialog::clearBody() {
  if (body_) {
    QWidget* b = body_;
    body_.clear();
    bodyLayout_->removeWidget(b);
    b->hide();
    b->setParent(nullptr);  // out of the scroll area right away (deleted below)
    b->deleteLater();       // tools may still hold QPointers; never delete synchronously
  }
  scroll_->hide();
}

void CommandDialog::setAcceptEnabled(bool enabled) {
  acceptEnabled_ = enabled;
  ok_->setEnabled(enabled);
}

void CommandDialog::setMessage(const QString& text, bool error) {
  message_->setText(text);
  message_->setVisible(!text.isEmpty());
  message_->setProperty("error", error);
  message_->style()->unpolish(message_);
  message_->style()->polish(message_);
  if (isVisible()) relayout();
}

void CommandDialog::relayout() {
  relayoutPending_ = false;
  if (!viewport_) return;
  const int vpH = viewport_->height();
  const int top = dockTop();
  const int innerW = kWidth - 2 * frameWidth();
  int contentH = 0;
  if (body_) {
    bodyLayout_->activate();
    contentH = inner_->hasHeightForWidth() ? inner_->heightForWidth(innerW) : inner_->sizeHint().height();
    contentH = std::max(contentH, inner_->minimumSizeHint().height());
  }
  // room the frame needs around the body
  int chrome = title_->sizeHint().height() + 8 + 4;
  chrome += std::max(message_->minimumHeight(), message_->heightForWidth(kWidth - 2 * frameWidth())) + 4;
  if (buttonRow_->isVisibleTo(this)) chrome += buttonRow_->sizeHint().height() + 4;
  const int cap = std::max(60, std::min(kMaxBodyHeight, vpH - top - kTopMargin - chrome));
  scroll_->setFixedHeight(body_ ? std::min(contentH, cap) : 0);
  if (QLayout* l = layout()) l->activate();
  const int natural = hasHeightForWidth() ? heightForWidth(kWidth) : sizeHint().height();
  // grow freely; never shrink while the same command runs (unless the viewport got smaller)
  const int h = std::max(natural, std::min(stableHeight_, std::max(natural, vpH - top - kTopMargin)));
  stableHeight_ = h;
  setFixedHeight(h);
}

void CommandDialog::place() {
  if (!viewport_) return;
  const QRect area = viewport_->rect();
  QPoint p = pos();
  if (!userMoved_) p = QPoint(area.width() - width() - kRightMargin, dockTop());
  p.setX(std::clamp(p.x(), 0, std::max(0, area.width() - width())));
  p.setY(std::clamp(p.y(), 0, std::max(0, area.height() - std::min(height(), area.height()))));
  move(p);
}

int CommandDialog::dockTop() const {
  if (!viewport_) return kTopMargin;
  // a short viewport: give the dialog the height rather than keep clear of the cube
  if (viewport_->height() < 2 * kDockTop + 200) return kTopMargin;
  // keep clear of the ViewCube's face-step arrows too (APP-12)
  if (auto* v = qobject_cast<const viewer::ViewerWidget*>(viewport_)) {
    const QRect cube = v->viewCubeRect();
    if (!cube.isEmpty()) return std::max(kDockTop, cube.bottom() + 8);
  }
  return kDockTop;
}

void CommandDialog::reposition() {
  if (!viewport_) return;
  relayout();
  place();
}

void CommandDialog::keyPressEvent(QKeyEvent* e) {
  if (e->key() == Qt::Key_Escape) {
    emit cancelled();
    return;
  }
  if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
    QWidget* f = QApplication::focusWidget();
    if (qobject_cast<QTextEdit*>(f) || qobject_cast<QPlainTextEdit*>(f)) return;
    if (acceptEnabled_) emit accepted();
    return;
  }
  QFrame::keyPressEvent(e);
}

bool CommandDialog::eventFilter(QObject* o, QEvent* e) {
  if (o == viewport_ && e->type() == QEvent::Resize) {
    if (isVisible()) reposition();
    return false;
  }
  if (o == inner_ && e->type() == QEvent::LayoutRequest && body_ && !relayoutPending_) {
    // the tool added/removed rows: refit once the layout has settled
    relayoutPending_ = true;
    QTimer::singleShot(0, this, [this] {
      if (relayoutPending_) {
        relayout();
        place();
      }
    });
    return false;
  }
  if (o == title_) {
    if (e->type() == QEvent::MouseButtonPress) {
      auto* me = static_cast<QMouseEvent*>(e);
      dragging_ = me->button() == Qt::LeftButton;
      dragOffset_ = me->globalPosition().toPoint() - pos();
      return true;
    }
    if (e->type() == QEvent::MouseMove && dragging_) {
      auto* me = static_cast<QMouseEvent*>(e);
      userMoved_ = true;
      move(me->globalPosition().toPoint() - dragOffset_);
      place();
      return true;
    }
    if (e->type() == QEvent::MouseButtonRelease) {
      dragging_ = false;
      return true;
    }
    if (e->type() == QEvent::MouseButtonDblClick) {
      userMoved_ = false;  // double-click the title: dock back to the right edge
      place();
      return true;
    }
  }
  return QFrame::eventFilter(o, e);
}

}  // namespace fabricad::app
