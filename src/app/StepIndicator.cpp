// FabriCAD app: step indicator.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/StepIndicator.h"

#include "app/CardFrame.h"
#include "app/ThemeManager.h"
#include "theme/Tokens.h"

#include <QMouseEvent>
#include <QPainter>

namespace fabricad::app {

using theme::Tok;

namespace {
constexpr int kGap = 4;
}

StepIndicator::StepIndicator(QWidget* parent) : QWidget(parent) {
  setObjectName("StepIndicator");
  setFixedHeight(kHeight);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setMouseTracking(true);
  connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this] { update(); });
}

void StepIndicator::setSteps(const std::vector<Step>& steps) {
  if (steps == steps_) return;
  steps_ = steps;
  update();
}

int StepIndicator::currentStep() const {
  for (size_t i = 0; i < steps_.size(); ++i)
    if (steps_[i].state == State::Current) return static_cast<int>(i);
  return -1;
}

QRect StepIndicator::stepRect(int i) const {
  const int n = std::max<int>(1, static_cast<int>(steps_.size()));
  const int w = width() + kGap;
  const int x0 = i * w / n, x1 = (i + 1) * w / n - kGap;
  return QRect(x0, 0, x1 - x0, height());
}

QSize StepIndicator::sizeHint() const { return {240, kHeight}; }
QSize StepIndicator::minimumSizeHint() const { return {60, kHeight}; }

void StepIndicator::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setFont(theme::font(theme::TextStyle::Caption));
  const QFontMetrics fm = p.fontMetrics();
  for (size_t i = 0; i < steps_.size(); ++i) {
    const Step& s = steps_[i];
    const QRect r = stepRect(static_cast<int>(i));
    Tok bar = Tok::BorderDefault, text = Tok::TextTertiary;
    int barH = 2;
    if (s.state == State::Done) {
      bar = Tok::AccentIndicator;
      text = Tok::TextSecondary;
    } else if (s.state == State::Current) {
      bar = Tok::LiveBase;
      text = Tok::LiveText;
      barH = std::max(2, CardFrame::liveHairline());
    }
    p.fillRect(QRect(r.left(), 2, r.width(), barH), theme::color(bar));
    p.setPen(theme::color(text));
    const QString label = QStringLiteral("%1 %2").arg(i + 1, 2, 10, QLatin1Char('0')).arg(s.label);
    p.drawText(r.adjusted(0, 6, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(label.toUpper(), Qt::ElideRight, r.width()));
  }
}

void StepIndicator::mousePressEvent(QMouseEvent* e) {
  for (size_t i = 0; i < steps_.size(); ++i)
    if (stepRect(static_cast<int>(i)).contains(e->position().toPoint()) && steps_[i].state == State::Done) {
      emit stepClicked(static_cast<int>(i));
      e->accept();
      return;
    }
  QWidget::mousePressEvent(e);
}

void StepIndicator::mouseMoveEvent(QMouseEvent* e) {
  bool clickable = false;
  for (size_t i = 0; i < steps_.size(); ++i)
    if (stepRect(static_cast<int>(i)).contains(e->position().toPoint()) && steps_[i].state == State::Done) clickable = true;
  setCursor(clickable ? Qt::PointingHandCursor : Qt::ArrowCursor);
  QWidget::mouseMoveEvent(e);
}

}  // namespace fabricad::app
