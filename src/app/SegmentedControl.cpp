// FabriCAD app: segmented control.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/SegmentedControl.h"

#include "app/ThemeManager.h"
#include "theme/Tokens.h"

#include <QComboBox>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>

namespace fabricad::app {

using theme::Tok;

SegmentedControl::SegmentedControl(QWidget* parent) : QWidget(parent) {
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  setAttribute(Qt::WA_Hover);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setProperty("segmented", true);
  connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this] { update(); });
}

void SegmentedControl::setOptions(const QStringList& labels) {
  labels_ = labels;
  if (!combo_) index_ = labels_.isEmpty() ? -1 : std::clamp(index_, 0, count() - 1);
  updateGeometry();
  update();
}

int SegmentedControl::currentIndex() const { return combo_ ? combo_->currentIndex() : index_; }

void SegmentedControl::setCurrentIndex(int index) {
  if (index < 0 || index >= count() || index == currentIndex()) return;
  if (combo_) {
    combo_->setCurrentIndex(index);  // its currentIndexChanged drives the tool
  } else {
    index_ = index;
  }
  painted_ = index;
  update();
  emit currentIndexChanged(index);
}

void SegmentedControl::bindCombo(QComboBox* combo) {
  combo_ = combo;
  if (!combo) return;
  QStringList labels;
  for (int i = 0; i < combo->count(); ++i) labels << combo->itemText(i);
  setOptions(labels);
  setToolTip(combo->toolTip());
  setEnabled(combo->isEnabled());
  connect(combo, &QComboBox::currentIndexChanged, this, [this](int) { sync(); });
}

void SegmentedControl::sync() {
  if (combo_ && combo_->count() != count()) bindCombo(combo_);
  if (painted_ != currentIndex()) update();
}

QRect SegmentedControl::cellRect(int i) const {
  const int n = std::max(1, count());
  const int w = width();
  const int x0 = i * w / n, x1 = (i + 1) * w / n;
  return QRect(x0, 0, x1 - x0, height());
}

int SegmentedControl::cellAt(const QPoint& p) const {
  for (int i = 0; i < count(); ++i)
    if (cellRect(i).contains(p)) return i;
  return -1;
}

QSize SegmentedControl::sizeHint() const {
  const QFontMetrics fm(theme::font(theme::TextStyle::LabelSm));
  int w = 0;
  for (const QString& l : labels_) w = std::max(w, fm.horizontalAdvance(l.toUpper()) + 16);
  return {std::max(1, count()) * w, kHeight};
}

QSize SegmentedControl::minimumSizeHint() const {
  const QFontMetrics fm(theme::font(theme::TextStyle::LabelSm));
  int w = 0;
  for (const QString& l : labels_) w = std::max(w, fm.horizontalAdvance(l.toUpper()) + 6);
  return {std::max(1, count()) * w, kHeight};
}

void SegmentedControl::paintEvent(QPaintEvent*) {
  QPainter p(this);
  const int cur = currentIndex();
  painted_ = cur;
  const bool on = isEnabled();
  const QRect r = rect();
  p.fillRect(r, theme::color(Tok::SurfaceInput));
  p.setFont(theme::font(theme::TextStyle::LabelSm));
  for (int i = 0; i < count(); ++i) {
    const QRect c = cellRect(i);
    if (i == cur) {
      p.fillRect(c, theme::color(Tok::AccentSelFill));
    } else if (i == hover_ && on) {
      p.fillRect(c, theme::color(Tok::SurfaceHover));
    }
    p.setPen(!on ? theme::color(Tok::TextDisabled) : i == cur ? theme::color(Tok::TextPrimary) : theme::color(Tok::TextSecondary));
    p.drawText(c.adjusted(2, 0, -2, 0), Qt::AlignCenter, labels_[i]);
  }
  // outline and dividers (border.control), then the checked cell's indicator border on top
  p.setPen(QPen(theme::color(on ? Tok::BorderControl : Tok::BorderSubtle), 1));
  p.setBrush(Qt::NoBrush);
  p.drawRect(r.adjusted(0, 0, -1, -1));
  for (int i = 1; i < count(); ++i) {
    const int x = cellRect(i).left();
    p.drawLine(x, 0, x, height() - 1);
  }
  if (cur >= 0 && cur < count() && on) {
    p.setPen(QPen(theme::color(Tok::AccentIndicator), 1));
    p.drawRect(cellRect(cur).adjusted(0, 0, -1, -1));
  }
  if (hasFocus()) {  // 2 px focus ring (inside: the control has no outer margin)
    p.setPen(QPen(theme::color(Tok::AccentFocus), 2));
    const QRect c = cur >= 0 ? cellRect(cur) : r;
    p.drawRect(c.adjusted(1, 1, -1, -1));
  }
}

void SegmentedControl::mousePressEvent(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton) return QWidget::mousePressEvent(e);
  const int i = cellAt(e->position().toPoint());
  if (i >= 0) setCurrentIndex(i);
  e->accept();
}

void SegmentedControl::mouseMoveEvent(QMouseEvent* e) {
  const int i = cellAt(e->position().toPoint());
  if (i != hover_) {
    hover_ = i;
    update();
  }
  QWidget::mouseMoveEvent(e);
}

void SegmentedControl::leaveEvent(QEvent* e) {
  hover_ = -1;
  update();
  QWidget::leaveEvent(e);
}

void SegmentedControl::keyPressEvent(QKeyEvent* e) {
  const int cur = currentIndex();
  switch (e->key()) {
    case Qt::Key_Left:
    case Qt::Key_Up: setCurrentIndex(std::max(0, cur - 1)); break;
    case Qt::Key_Right:
    case Qt::Key_Down: setCurrentIndex(std::min(count() - 1, cur + 1)); break;
    case Qt::Key_Home: setCurrentIndex(0); break;
    case Qt::Key_End: setCurrentIndex(count() - 1); break;
    default: QWidget::keyPressEvent(e); return;
  }
  e->accept();
}

void SegmentedControl::focusInEvent(QFocusEvent* e) {
  update();
  QWidget::focusInEvent(e);
}

void SegmentedControl::focusOutEvent(QFocusEvent* e) {
  update();
  QWidget::focusOutEvent(e);
}

}  // namespace fabricad::app
