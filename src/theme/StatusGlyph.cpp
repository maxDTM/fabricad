// FabriCAD theme: StatusGlyph implementation (see StatusGlyph.h).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "theme/StatusGlyph.h"

#include "theme/Tokens.h"

#include <QEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QWidget>

#include <cmath>

namespace fabricad::theme {

void paintStatusGlyph(QPainter& p, const QRectF& r, StatusLevel level) {
  if (level == StatusLevel::None) return;
  p.save();
  p.setRenderHint(QPainter::Antialiasing);
  const QPointF c = r.center();
  const double s = std::min(r.width(), r.height());
  const QColor ink = color(Tok::SurfaceInput);
  if (level == StatusLevel::Error) {
    QPolygonF oct;
    for (int i = 0; i < 8; ++i) {
      const double a = (22.5 + 45.0 * i) * M_PI / 180.0;
      oct << c + QPointF(std::cos(a), std::sin(a)) * (s / 2.0);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(color(Tok::StatusError));
    p.drawPolygon(oct);
    // a horizontal bar: "stop", readable at 12 px
    p.setBrush(ink);
    p.drawRect(QRectF(c.x() - s * 0.28, c.y() - s * 0.08, s * 0.56, s * 0.16));
  } else if (level == StatusLevel::Warning) {
    QPolygonF tri;
    tri << QPointF(c.x(), r.top() + s * 0.04) << QPointF(r.right() - s * 0.02, r.bottom() - s * 0.06)
        << QPointF(r.left() + s * 0.02, r.bottom() - s * 0.06);
    p.setPen(Qt::NoPen);
    p.setBrush(color(Tok::StatusWarning));
    p.drawPolygon(tri);
    p.setBrush(ink);
    p.drawRect(QRectF(c.x() - s * 0.06, c.y() - s * 0.16, s * 0.12, s * 0.30));
    p.drawRect(QRectF(c.x() - s * 0.06, c.y() + s * 0.20, s * 0.12, s * 0.12));
  } else {
    p.setPen(Qt::NoPen);
    p.setBrush(color(Tok::StatusInfo));
    p.drawEllipse(c, s / 2.0, s / 2.0);
    p.setBrush(ink);
    p.drawRect(QRectF(c.x() - s * 0.06, c.y() - s * 0.06, s * 0.12, s * 0.32));
    p.drawRect(QRectF(c.x() - s * 0.06, c.y() - s * 0.30, s * 0.12, s * 0.12));
  }
  p.restore();
}

namespace {
constexpr int kGlyph = 12;
constexpr int kGap = 6;

class GlyphWidget : public QWidget {
 public:
  explicit GlyphWidget(QLabel* label) : QWidget(label), label_(label) {
    setObjectName("StatusGlyph");
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFixedSize(kGlyph, kGlyph);
    label->installEventFilter(this);
  }
  StatusLevel level = StatusLevel::None;
  void place() {
    // first text line, at the label's left content edge
    const QMargins m = label_->contentsMargins();
    const int lineH = label_->fontMetrics().height();
    int top = m.top();
    if (!label_->wordWrap() || label_->height() <= lineH + m.top() + m.bottom() + 2) {
      if (label_->alignment() & Qt::AlignVCenter) top = (label_->height() - lineH) / 2;
    }
    move(m.left() + label_->margin(), top + (lineH - kGlyph) / 2);
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    paintStatusGlyph(p, QRectF(0.5, 0.5, kGlyph - 1, kGlyph - 1), level);
  }
  bool eventFilter(QObject* o, QEvent* e) override {
    if (o == label_ && (e->type() == QEvent::Resize || e->type() == QEvent::Show || e->type() == QEvent::FontChange))
      place();
    return false;
  }

 private:
  QLabel* label_;
};
}  // namespace

void setStatusGlyph(QLabel* label, StatusLevel level) {
  if (!label) return;
  auto* g = static_cast<GlyphWidget*>(label->findChild<QWidget*>("StatusGlyph", Qt::FindDirectChildrenOnly));
  if (level == StatusLevel::None) {
    if (g) {
      g->hide();
      g->level = level;
    }
    label->setIndent(-1);
    return;
  }
  if (!g) g = new GlyphWidget(label);
  g->level = level;
  label->setIndent(kGlyph + kGap);
  g->place();
  g->show();
  g->update();
}

StatusLevel statusGlyphOf(const QLabel* label) {
  if (!label) return StatusLevel::None;
  auto* g = static_cast<GlyphWidget*>(label->findChild<QWidget*>("StatusGlyph", Qt::FindDirectChildrenOnly));
  return g && g->isVisibleTo(label) ? g->level : StatusLevel::None;
}

}  // namespace fabricad::theme
