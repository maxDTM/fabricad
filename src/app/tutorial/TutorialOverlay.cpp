// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/tutorial/TutorialOverlay.h"

#include "theme/Tokens.h"

#include <QChildEvent>
#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>

#include <cmath>

namespace fabricad::app::tutorial {

TutorialOverlay::TutorialOverlay(QWidget* window) : QWidget(window) {
  setObjectName("TutorialOverlay");
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setAttribute(Qt::WA_NoSystemBackground);
  setAttribute(Qt::WA_TranslucentBackground);
  setFocusPolicy(Qt::NoFocus);
  window->installEventFilter(this);
  pulseAnim_ = new QVariantAnimation(this);
  pulseAnim_->setStartValue(0.0);
  pulseAnim_->setEndValue(1.0);
  pulseAnim_->setDuration(1400);
  pulseAnim_->setLoopCount(-1);
  connect(pulseAnim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
    pulse_ = v.toDouble();
    if (!spot_.isNull() && ring_) update(local(spot_).adjusted(-12, -12, 12, 12));
  });
  ghostAnim_ = new QVariantAnimation(this);
  ghostAnim_->setStartValue(0.0);
  ghostAnim_->setEndValue(1.0);
  ghostAnim_->setEasingCurve(QEasingCurve::InOutCubic);
  sync();
}

void TutorialOverlay::sync() {
  QWidget* w = parentWidget();
  if (!w) return;
  if (geometry() != w->rect()) setGeometry(w->rect());
  raise();
  emit raised();  // the bubble and the welcome card go back on top
}

bool TutorialOverlay::eventFilter(QObject* o, QEvent* e) {
  if (o == parentWidget()) {
    switch (e->type()) {
      case QEvent::Resize: sync(); break;
      case QEvent::ChildAdded:
      case QEvent::ZOrderChange:
        // a later child (dock, card, palette) would paint over the overlay: raise again, queued so
        // the new child is in place first
        if (e->type() == QEvent::ChildAdded && static_cast<QChildEvent*>(e)->child() == this) break;
        QMetaObject::invokeMethod(this, [this] { sync(); }, Qt::QueuedConnection);
        break;
      default: break;
    }
  }
  return QWidget::eventFilter(o, e);
}

void TutorialOverlay::setSpotlight(const QRect& globalRect, bool ring) {
  if (spot_ == globalRect && ring_ == ring) return;
  spot_ = globalRect;
  ring_ = ring;
  if (!spot_.isNull() && ring_) {
    if (pulseAnim_->state() != QAbstractAnimation::Running) pulseAnim_->start();
  } else {
    pulseAnim_->stop();
  }
  update();
}

void TutorialOverlay::setDimEnabled(bool on) {
  if (dim_ == on) return;
  dim_ = on;
  update();
}

void TutorialOverlay::setSuppressDim(bool on) {
  if (suppressDim_ == on) return;
  suppressDim_ = on;
  update();
}

void TutorialOverlay::setBubble(const QRect& globalRect, const QPoint& aim) {
  if (bubble_ == globalRect && aim_ == aim) return;
  bubble_ = globalRect;
  aim_ = aim;
  update();
}

// --- ghost cursor -------------------------------------------------------------------------------

bool TutorialOverlay::ghostRunning() const { return ghostIndex_ >= 0; }

void TutorialOverlay::playGhost(const QPoint& fromGlobal, std::vector<GhostStep> steps) {
  stopGhost();
  if (steps.empty()) return;
  ghost_ = std::move(steps);
  ghostIndex_ = -1;
  ghostPos_ = ghostFrom_ = fromGlobal;
  nextGhostStep();
}

void TutorialOverlay::stopGhost() {
  ghostAnim_->stop();
  ghostAnim_->disconnect(this);
  const bool was = ghostIndex_ >= 0;
  ghostIndex_ = -1;
  ghost_.clear();
  ghostText_.clear();
  ripple_ = -1;
  update();
  if (was) emit ghostFinished();
}

void TutorialOverlay::nextGhostStep() {
  ghostAnim_->stop();
  ghostAnim_->disconnect(this);
  ++ghostIndex_;
  if (ghostIndex_ >= static_cast<int>(ghost_.size())) {
    // linger a moment at the end, then disappear
    ghostIndex_ = static_cast<int>(ghost_.size()) - 1;
    QMetaObject::invokeMethod(this, [this] { stopGhost(); }, Qt::QueuedConnection);
    return;
  }
  const GhostStep s = ghost_[ghostIndex_];
  ghostFrom_ = ghostPos_;
  ripple_ = -1;
  const bool moves = s.kind == GhostStep::Move || s.kind == GhostStep::Click || s.kind == GhostStep::DoubleClick ||
                     s.kind == GhostStep::RightClick;
  const int travel = moves ? static_cast<int>(std::clamp(std::hypot(s.to.x() - ghostPos_.x(), s.to.y() - ghostPos_.y()) * 1.2, 350.0, 900.0)) : 0;
  const int click = (s.kind == GhostStep::Click || s.kind == GhostStep::RightClick) ? 380 : s.kind == GhostStep::DoubleClick ? 620 : 0;
  const int typing = s.kind == GhostStep::Type ? std::clamp(static_cast<int>(s.text.size()) * 120, 400, 2000) + 400
                     : s.kind == GhostStep::Key ? 750 : 0;
  const int total = travel + click + typing;
  ghostAnim_->setDuration(std::max(1, total));
  ghostAnim_->setEasingCurve(QEasingCurve::Linear);
  connect(ghostAnim_, &QVariantAnimation::valueChanged, this, [this, s, travel, click, typing, total](const QVariant& v) {
    const double ms = v.toDouble() * total;
    if (travel > 0 && ms <= travel) {
      const double t = QEasingCurve(QEasingCurve::InOutCubic).valueForProgress(ms / travel);
      ghostPos_ = ghostFrom_ + (s.to - ghostFrom_) * t;
      ripple_ = -1;
    } else if (click > 0 && ms <= travel + click) {
      ghostPos_ = s.to;
      double r = (ms - travel) / click;
      if (s.kind == GhostStep::DoubleClick) r = std::fmod(r * 2, 1.0);
      ripple_ = r;
    } else if (typing > 0) {
      ripple_ = -1;
      if (s.kind == GhostStep::Type) {
        const int n = static_cast<int>(std::ceil((ms - travel) / 120.0));
        ghostText_ = s.text.left(std::clamp(n, 1, static_cast<int>(s.text.size())));
      } else {
        ghostText_ = s.text;
      }
    }
    update();
  });
  connect(ghostAnim_, &QVariantAnimation::finished, this, [this] {
    ghostText_.clear();
    nextGhostStep();
  });
  ghostAnim_->start();
}

// --- painting ------------------------------------------------------------------------------------

void TutorialOverlay::paintEvent(QPaintEvent*) {
  using theme::Tok;
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QRect spot = spot_.isNull() ? QRect() : local(spot_).adjusted(-4, -4, 4, 4);
  if (dim_ && !suppressDim_) {
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    path.addRect(rect());
    if (!spot.isNull()) path.addRect(spot);
    p.fillPath(path, theme::color(Tok::TutorialDim));
  }
  if (!spot.isNull() && ring_) {
    // pulsing 2 px ring (accent.focus), plus a fading outer echo
    QColor c = theme::color(Tok::AccentFocus);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(c, 2));
    p.drawRect(QRectF(spot).adjusted(0.5, 0.5, -0.5, -0.5));
    const double grow = 2 + 8 * pulse_;
    c.setAlphaF(0.55 * (1 - pulse_));
    p.setPen(QPen(c, 2));
    p.drawRect(QRectF(spot).adjusted(-grow, -grow, grow, grow));
  }
  // notch: a small triangle from the bubble edge toward the anchor
  if (!bubble_.isNull() && !aim_.isNull()) {
    const QRect b = local(bubble_);
    const QPoint a = mapFromGlobal(aim_);
    if (!b.contains(a)) {
      QPointF base1, base2, tip;
      const double h = 9;
      if (a.y() < b.top()) {
        const double x = std::clamp<double>(a.x(), b.left() + 16, b.right() - 16);
        base1 = {x - h, double(b.top()) + 0.5};
        base2 = {x + h, double(b.top()) + 0.5};
        tip = {x, b.top() - h};
      } else if (a.y() > b.bottom()) {
        const double x = std::clamp<double>(a.x(), b.left() + 16, b.right() - 16);
        base1 = {x - h, double(b.bottom()) + 0.5};
        base2 = {x + h, double(b.bottom()) + 0.5};
        tip = {x, b.bottom() + h + 1};
      } else if (a.x() < b.left()) {
        const double y = std::clamp<double>(a.y(), b.top() + 16, b.bottom() - 16);
        base1 = {double(b.left()) + 0.5, y - h};
        base2 = {double(b.left()) + 0.5, y + h};
        tip = {b.left() - h, y};
      } else {
        const double y = std::clamp<double>(a.y(), b.top() + 16, b.bottom() - 16);
        base1 = {double(b.right()) + 0.5, y - h};
        base2 = {double(b.right()) + 0.5, y + h};
        tip = {b.right() + h + 1, y};
      }
      QPainterPath tri;
      tri.moveTo(base1);
      tri.lineTo(tip);
      tri.lineTo(base2);
      p.setPen(QPen(theme::color(Tok::BorderDefault), 1));
      p.setBrush(theme::color(Tok::SurfaceOverlay));
      p.drawPath(tri);
    }
  }
  if (ghostIndex_ >= 0) {
    const QPointF g = mapFromGlobal(ghostPos_);
    if (ripple_ >= 0) {
      QColor c = theme::color(Tok::AccentFocus);
      c.setAlphaF(0.8 * (1 - ripple_));
      p.setPen(QPen(c, 2));
      p.setBrush(Qt::NoBrush);
      const double r = 4 + 18 * ripple_;
      p.drawEllipse(g, r, r);
    }
    // arrow cursor: text.primary fill, bg.app outline (reads on both themes and on the model)
    QPainterPath arrow;
    arrow.moveTo(0, 0);
    arrow.lineTo(0, 17);
    arrow.lineTo(4.5, 13);
    arrow.lineTo(7.5, 20);
    arrow.lineTo(10.5, 18.8);
    arrow.lineTo(7.6, 12);
    arrow.lineTo(13, 12);
    arrow.closeSubpath();
    p.save();
    p.translate(g);
    p.setPen(QPen(theme::color(Tok::BgApp), 1.5));
    p.setBrush(theme::color(Tok::TextPrimary));
    p.drawPath(arrow);
    p.restore();
    if (!ghostText_.isEmpty()) {
      QFont f = theme::font(theme::TextStyle::Keycap);
      f.setCapitalization(QFont::MixedCase);
      p.setFont(f);
      const QFontMetrics fm(f);
      const QRect tr(QPoint(static_cast<int>(g.x()) + 16, static_cast<int>(g.y()) + 18), QSize(fm.horizontalAdvance(ghostText_) + 12, fm.height() + 6));
      p.setPen(QPen(theme::color(Tok::BorderDefault), 1));
      p.setBrush(theme::color(Tok::SurfaceHeader));
      p.drawRect(QRectF(tr).adjusted(0.5, 0.5, -0.5, -0.5));
      p.setPen(theme::color(Tok::TextPrimary));
      p.drawText(tr, Qt::AlignCenter, ghostText_);
    }
  }
}

}  // namespace fabricad::app::tutorial
