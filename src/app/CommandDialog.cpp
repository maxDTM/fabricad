// FabriCAD app: command dialog host.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/CommandDialog.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>
#include <QTextEdit>
#include <QVBoxLayout>

namespace fabricad::app {

namespace {
constexpr int kWidth = 300;
constexpr int kTopMargin = 150;  // below the ViewCube
constexpr int kRightMargin = 12;
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

  bodyLayout_ = new QVBoxLayout();
  bodyLayout_->setContentsMargins(0, 0, 0, 0);
  v->addLayout(bodyLayout_);

  message_ = new QLabel(this);
  message_->setObjectName("CommandDialogMessage");
  message_->setWordWrap(true);
  message_->setContentsMargins(10, 0, 10, 0);
  message_->hide();
  v->addWidget(message_);

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
  dismiss();
  titleText_->setText(title.toUpper());
  titleIcon_->setPixmap(icon.isNull() ? QPixmap() : icon.pixmap(18, 18));
  if (body) {
    body->setParent(this);
    bodyLayout_->addWidget(body);
    body_ = body;
    body->show();
  }
  buttonRow_->setVisible(buttons);
  setAcceptEnabled(true);
  setMessage({});
  adjustSize();
  reposition();
  show();
  raise();
}

void CommandDialog::dismiss() {
  if (body_) {
    QWidget* b = body_;
    body_.clear();
    bodyLayout_->removeWidget(b);
    b->hide();
    b->deleteLater();  // tools may still hold QPointers; never delete synchronously
  }
  hide();
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
  if (isVisible()) adjustSize();
}

void CommandDialog::reposition() {
  if (!viewport_) return;
  adjustSize();
  const QRect area = viewport_->rect();
  QPoint p = pos();
  if (!userMoved_) p = QPoint(area.width() - width() - kRightMargin, std::min(kTopMargin, std::max(8, area.height() / 5)));
  p.setX(std::clamp(p.x(), 0, std::max(0, area.width() - width())));
  p.setY(std::clamp(p.y(), 0, std::max(0, area.height() - std::min(height(), area.height()))));
  move(p);
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
      reposition();
      return true;
    }
    if (e->type() == QEvent::MouseButtonRelease) {
      dragging_ = false;
      return true;
    }
    if (e->type() == QEvent::MouseButtonDblClick) {
      userMoved_ = false;  // double-click the title: dock back to the right edge
      reposition();
      return true;
    }
  }
  return QFrame::eventFilter(o, e);
}

}  // namespace fabricad::app
