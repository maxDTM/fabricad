// FabriCAD viewer: viewport overlay widgets (see ViewportOverlays.h).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "viewer/ViewportOverlays.h"

#include "core/Document.h"
#include "core/Units.h"
#include "theme/Tokens.h"
#include "viewer/ViewerWidget.h"

#include <QContextMenuEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace fabricad::viewer {

using theme::Tok;

namespace {

constexpr double kDegRad = M_PI / 180.0;

theme::Theme themeOf(const ViewerWidget& v) { return v.viewerPalette().theme; }
QColor tok(const ViewerWidget& v, Tok t) { return theme::color(t, themeOf(v)); }
QColor tok(const ViewerWidget& v, Tok t, double alpha) {
  QColor c = tok(v, t);
  c.setAlphaF(c.alphaF() * alpha);
  return c;
}
bool lightTheme(const ViewerWidget& v) { return themeOf(v) == theme::Theme::Light; }

/// 1 device-pixel cosmetic pen (7.4 hairlines).
QPen hairline(const QColor& c) {
  QPen p(c, 0);
  p.setCosmetic(true);
  return p;
}

/// A 28 px overlay bar background: surface.panel 92 % + 1 px border.default.
void paintBarFrame(QPainter& p, const ViewerWidget& v, const QRect& r) {
  p.fillRect(r, tok(v, Tok::SurfacePanel, 0.92));
  p.setPen(hairline(tok(v, Tok::BorderDefault)));
  p.setBrush(Qt::NoBrush);
  p.drawRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5));
}

const char* axisLetter(int a) { return a == 0 ? "X" : a == 1 ? "Y" : "Z"; }
Tok axisTok(int a) { return a == 0 ? Tok::AxisX : a == 1 ? Tok::AxisY : Tok::AxisZ; }

gp_Dir axisDir(int a, bool positive) {
  const double s = positive ? 1.0 : -1.0;
  return a == 0 ? gp_Dir(s, 0, 0) : a == 1 ? gp_Dir(0, s, 0) : gp_Dir(0, 0, s);
}

/// Hexagon vertices (pointy top), counter-clockwise from the top.
std::array<QPointF, 6> hexVertices(QPointF c, double r) {
  std::array<QPointF, 6> v;
  for (int i = 0; i < 6; ++i) {
    const double a = (90.0 + 60.0 * i) * kDegRad;
    v[static_cast<size_t>(i)] = c + QPointF(r * std::cos(a), -r * std::sin(a));
  }
  return v;
}

constexpr const char* kViewButtons[] = {"ISO", "TOP", "FRONT", "RIGHT"};
constexpr core::StandardView kViewButtonViews[] = {core::StandardView::Iso, core::StandardView::Top, core::StandardView::Front,
                                                   core::StandardView::Right};

}  // namespace

// =============================================================================================
// OrientationDial
// =============================================================================================

OrientationDial::OrientationDial(ViewerWidget& viewer) : QWidget(viewer.overlayParent()), viewer_(viewer) {
  setObjectName("OrientationDial");
  setMouseTracking(true);
  setFocusPolicy(Qt::TabFocus);
  setAttribute(Qt::WA_NoSystemBackground);
  setToolTip({});
  resize(sizeHint());
}

void OrientationDial::setPlateVisible(bool on) {
  if (plate_ == on) return;
  plate_ = on;
  hover_ = {};
  resize(sizeHint());
  updateGeometry();
  update();
}

QSize OrientationDial::sizeHint() const {
  const int h = (plate_ ? kPlate : 0) + kReadout + kGap + kButtons;
  return {kWidth, h};
}

QPointF OrientationDial::plateCenter() const { return {kWidth / 2.0, kPlate / 2.0}; }

QPointF OrientationDial::tipPosition(int axis, bool positive) const {
  double x = 0, y = 0;
  projectAxis(axisDir(axis, positive), viewer_.viewDirection(), viewer_.upDirection(), x, y);
  const double len = plateRadius() * 0.62;
  return plateCenter() + QPointF(x * len, -y * len);
}

QRect OrientationDial::readoutRect() const {
  const int top = plate_ ? kPlate : 0;
  return {0, top, kWidth, kReadout};
}

QRect OrientationDial::buttonRect(int index) const {
  const int top = readoutRect().bottom() + 1 + kGap;
  // equal-ish cells: widths follow the labels (FRONT and RIGHT are wider)
  static constexpr int widths[] = {36, 36, 48, 48};  // 168 = kWidth
  int x = 0;
  for (int i = 0; i < index; ++i) x += widths[i];
  return {x, top, widths[index], kButtons};
}

OrientationDial::Hit OrientationDial::hitTest(const QPointF& p) const {
  Hit h;
  for (int i = 0; i < 4; ++i)
    if (buttonRect(i).contains(p.toPoint())) {
      h.part = Part::Button;
      h.button = i;
      return h;
    }
  if (!plate_) return h;
  const QPointF c = plateCenter();
  const double r = plateRadius();
  // axis tips first (nearest within 10 px; near tips win over far ones)
  double best = 10.0;
  double bestDepth = -2.0;
  for (int a = 0; a < 3; ++a)
    for (bool pos : {true, false}) {
      const QPointF t = tipPosition(a, pos);
      const double d = std::hypot(t.x() - p.x(), t.y() - p.y());
      double x = 0, y = 0, depth = 0;
      projectAxis(axisDir(a, pos), viewer_.viewDirection(), viewer_.upDirection(), x, y, &depth);
      if (d < best - 1e-9 || (d < 10.0 && std::abs(d - best) < 1e-9 && depth > bestDepth)) {
        best = d;
        bestDepth = depth;
        h.part = Part::AxisTip;
        h.axis = a;
        h.positive = pos;
      }
    }
  if (h.part == Part::AxisTip) return h;
  const QPointF d = p - c;
  const double dist = std::hypot(d.x(), d.y());
  const auto verts = hexVertices(c, r);
  for (int i = 0; i < 6; ++i) {
    const QPointF v = verts[static_cast<size_t>(i)];
    if (std::hypot(v.x() - p.x(), v.y() - p.y()) <= 11.0) {
      h.part = Part::Vertex;
      h.vertex = i;
      return h;
    }
  }
  if (dist <= r * 0.70) h.part = Part::Inside;
  else if (dist <= r + 2.0) h.part = Part::Ring;
  return h;
}

QString OrientationDial::readoutText() const {
  const AzEl ae = azimuthElevation(viewer_.viewDirection());
  int az = static_cast<int>(std::lround(ae.az)) % 360;
  if (az < 0) az += 360;
  return QStringLiteral("AZ %1°  EL %2°").arg(az, 3, 10, QLatin1Char('0')).arg(static_cast<int>(std::lround(ae.el)));
}

int OrientationDial::currentButton() const {
  const auto v = currentStandardView(viewer_.viewDirection(), viewer_.upDirection());
  if (!v) return -1;
  for (int i = 0; i < 4; ++i)
    if (kViewButtonViews[i] == *v) return i;
  return -1;
}

void OrientationDial::activate(const Hit& h) {
  switch (h.part) {
    case Part::Button: viewer_.setStandardView(kViewButtonViews[h.button], true); break;
    case Part::AxisTip: {
      core::StandardView v = axisView(h.axis, h.positive);
      // a second click on the same tip gives the opposite view
      if (const auto cur = currentStandardView(viewer_.viewDirection(), viewer_.upDirection()); cur && *cur == v)
        v = axisView(h.axis, !h.positive);
      viewer_.setStandardView(v, true);
      break;
    }
    case Part::Vertex: {
      const double angle = 90.0 + 60.0 * h.vertex;
      const gp_Dir corner = isoCornerNear(angle, viewer_.viewDirection(), viewer_.upDirection());
      const AzEl ae = azimuthElevation(corner);
      viewer_.setViewAzEl(ae.az, ae.el, true);
      break;
    }
    default: break;
  }
}

void OrientationDial::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  if (plate_) paintPlate(p);
  paintReadout(p);
}

void OrientationDial::paintPlate(QPainter& p) {
  const ViewerWidget& v = viewer_;
  const QPointF c = plateCenter();
  const double r = plateRadius();
  const auto verts = hexVertices(c, r);
  QPolygonF hex;
  for (const QPointF& pt : verts) hex << pt;
  p.setBrush(tok(v, Tok::SurfaceOverlay, 0.9));
  p.setPen(hairline(tok(v, Tok::BorderDefault)));
  p.drawPolygon(hex);
  if (hasFocus()) {
    p.setPen(QPen(tok(v, Tok::AccentFocus), 2));
    p.setBrush(Qt::NoBrush);
    QPolygonF ring;
    for (const QPointF& pt : hexVertices(c, r + 2.5)) ring << pt;
    p.drawPolygon(ring);
  }

  // tick ring: every 15 degrees, major every 45
  const double tr = r * 0.80;
  for (int deg = 0; deg < 360; deg += 15) {
    const bool major = deg % 45 == 0;
    const double a = deg * kDegRad;
    const QPointF dir(std::cos(a), -std::sin(a));
    const double len = major ? 7.0 : 3.5;
    p.setPen(QPen(major ? tok(v, Tok::BorderControl) : tok(v, Tok::TextDisabled), major ? 1.2 : 1.0));
    p.drawLine(c + dir * (tr - len), c + dir * tr);
  }
  // gold azimuth mark on the ring (diamond pointing inward)
  {
    const AzEl ae = azimuthElevation(v.viewDirection());
    const double a = ae.az * kDegRad;
    const QPointF dir(std::cos(a), -std::sin(a)), n(-dir.y(), dir.x());
    const QPointF tip = c + dir * (tr + 1.0);
    QPolygonF mark{tip - dir * 6.0, tip + n * 4.0, tip + dir * 3.0, tip - n * 4.0};
    p.setPen(hairline(tok(v, Tok::LiveInk)));
    p.setBrush(tok(v, Tok::LiveBase));
    p.drawPolygon(mark);
  }

  // axes, back to front by depth: lines, then tips (positive filled with letter, negative hollow)
  struct Tip {
    int axis;
    bool positive;
    double depth;
    QPointF at;
  };
  std::vector<Tip> tips;
  for (int a = 0; a < 3; ++a)
    for (bool pos : {true, false}) {
      double x = 0, y = 0, depth = 0;
      projectAxis(axisDir(a, pos), v.viewDirection(), v.upDirection(), x, y, &depth);
      tips.push_back({a, pos, depth, tipPosition(a, pos)});
    }
  std::sort(tips.begin(), tips.end(), [](const Tip& l, const Tip& r2) { return l.depth < r2.depth; });
  QFont f = theme::font(theme::TextStyle::Caption);
  f.setPixelSize(10);
  p.setFont(f);
  for (const Tip& t : tips) {
    const QColor col = tok(v, axisTok(t.axis), t.positive ? 1.0 : 0.55);
    p.setPen(QPen(col, t.positive ? 2.0 : 1.2, t.positive ? Qt::SolidLine : Qt::DashLine, Qt::FlatCap));
    p.drawLine(c, t.at);
    const bool hot = hover_.part == Part::AxisTip && hover_.axis == t.axis && hover_.positive == t.positive;
    const double s = t.positive ? 15.0 : 9.0;
    const QRectF box(t.at.x() - s / 2, t.at.y() - s / 2, s, s);
    if (hot) {
      // glow: a soft disc behind the hovered tip (cached gradient, no QGraphicsEffect)
      QRadialGradient g(t.at, s * 1.4);
      QColor gc = tok(v, axisTok(t.axis), 0.45);
      g.setColorAt(0.0, gc);
      gc.setAlphaF(0.0);
      g.setColorAt(1.0, gc);
      p.setPen(Qt::NoPen);
      p.setBrush(g);
      p.drawEllipse(t.at, s * 1.4, s * 1.4);
      p.setBrush(Qt::NoBrush);
      p.setPen(QPen(tok(v, Tok::TextPrimary), 1.2));
      p.drawRect(box.adjusted(-3, -3, 3, 3));
    }
    if (t.positive) {
      p.setPen(Qt::NoPen);
      p.setBrush(hot ? col.lighter(115) : col);
      p.drawRect(box);
      p.setPen(tok(v, Tok::AxisOnAxis));
      p.drawText(box, Qt::AlignCenter, QString::fromLatin1(axisLetter(t.axis)));
    } else {
      p.setBrush(tok(v, Tok::SurfaceOverlay));
      p.setPen(QPen(col, 1.4));
      p.drawRect(box);
    }
  }
  // centre hub
  p.setPen(Qt::NoPen);
  p.setBrush(tok(v, Tok::TextSecondary));
  p.drawEllipse(c, 2.2, 2.2);
  // hovered vertex: a small accent dot
  if (hover_.part == Part::Vertex) {
    const QPointF vp = hexVertices(c, r)[static_cast<size_t>(hover_.vertex)];
    p.setBrush(tok(v, Tok::AccentIndicator));
    p.drawEllipse(vp, 3.5, 3.5);
  }
}

void OrientationDial::paintReadout(QPainter& p) {
  const ViewerWidget& v = viewer_;
  p.setRenderHint(QPainter::Antialiasing, false);
  p.setFont(theme::font(theme::TextStyle::Caption));
  // a quiet plate behind the AZ / EL text so it reads over a body as well as the background
  const QString text = readoutText();
  const int tw = QFontMetrics(p.font()).horizontalAdvance(text) + 16;
  const QRect rr = readoutRect();
  p.fillRect(QRect(rr.center().x() - tw / 2, rr.top() + 3, tw, rr.height() - 6), tok(v, Tok::SurfacePanel, 0.85));
  p.setPen(tok(v, Tok::TextSecondary));
  p.drawText(rr, Qt::AlignCenter, text);
  // segmented view buttons
  const int current = currentButton();
  for (int i = 0; i < 4; ++i) {
    const QRect r = buttonRect(i);
    const bool on = i == current;
    const bool hot = hover_.part == Part::Button && hover_.button == i;
    p.fillRect(r, on ? tok(v, Tok::AccentSelFill) : hot ? tok(v, Tok::SurfaceHover) : tok(v, Tok::SurfacePanel, 0.92));
    p.setPen(hairline(on ? tok(v, Tok::AccentIndicator) : tok(v, Tok::BorderDefault)));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r.adjusted(0, 0, -1, -1));
    p.setPen(on ? tok(v, Tok::TextPrimary) : tok(v, Tok::TextSecondary));
    p.drawText(r, Qt::AlignCenter, QString::fromLatin1(kViewButtons[i]));
  }
}

void OrientationDial::mousePressEvent(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton) return QWidget::mousePressEvent(e);
  press_ = hitTest(e->position());
  pressPos_ = lastPos_ = e->position();
  dragging_ = false;
  const AzEl ae = azimuthElevation(viewer_.viewDirection());
  dragAz0_ = ae.az;
  dragEl0_ = ae.el;
  const QPointF d = e->position() - plateCenter();
  dragAngle0_ = std::atan2(-d.y(), d.x()) / kDegRad;
  e->accept();
}

void OrientationDial::mouseMoveEvent(QMouseEvent* e) {
  if (!(e->buttons() & Qt::LeftButton)) {
    const Hit h = hitTest(e->position());
    if (h.part != hover_.part || h.axis != hover_.axis || h.positive != hover_.positive || h.vertex != hover_.vertex ||
        h.button != hover_.button) {
      hover_ = h;
      setCursor(h.part == Part::None || h.part == Part::Inside ? Qt::ArrowCursor : Qt::PointingHandCursor);
      update();
    }
    return;
  }
  const QPointF pos = e->position();
  if (!dragging_ && exceedsDragThreshold(static_cast<int>(pos.x() - pressPos_.x()), static_cast<int>(pos.y() - pressPos_.y())))
    dragging_ = true;
  if (!dragging_) return;
  if (press_.part == Part::Ring || press_.part == Part::Vertex) {
    // drag the ring: azimuth follows the pointer around the centre
    const QPointF d = pos - plateCenter();
    const double ang = std::atan2(-d.y(), d.x()) / kDegRad;
    viewer_.setViewAzEl(dragAz0_ - (ang - dragAngle0_), dragEl0_, false);
  } else if (press_.part == Part::Inside || press_.part == Part::AxisTip) {
    const QPointF dl = pos - lastPos_;
    constexpr double kDegPerPx = 0.5;
    if (!dl.isNull()) viewer_.orbitBy(-dl.x() * kDegPerPx, -dl.y() * kDegPerPx, false);
  }
  lastPos_ = pos;
}

void OrientationDial::mouseReleaseEvent(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton) return QWidget::mouseReleaseEvent(e);
  if (!dragging_) {
    const Hit h = hitTest(e->position());
    if (h.part == press_.part && h.part != Part::None) activate(h);
  }
  dragging_ = false;
  press_ = {};
  update();
}

void OrientationDial::wheelEvent(QWheelEvent* e) {
  if (!plate_) return QWidget::wheelEvent(e);
  const int steps = e->angleDelta().y() / 120;
  if (steps == 0) return;
  const AzEl ae = azimuthElevation(viewer_.viewDirection());
  const double el = std::clamp(std::round(ae.el / 15.0) * 15.0 + 15.0 * steps, -90.0, 90.0);
  viewer_.setViewAzEl(ae.az, el, true);
  e->accept();
}

void OrientationDial::keyPressEvent(QKeyEvent* e) {
  switch (e->key()) {
    case Qt::Key_Left: viewer_.orbitBy(-15.0, 0.0); break;
    case Qt::Key_Right: viewer_.orbitBy(15.0, 0.0); break;
    case Qt::Key_Up: viewer_.orbitBy(0.0, -15.0); break;
    case Qt::Key_Down: viewer_.orbitBy(0.0, 15.0); break;
    case Qt::Key_Home: viewer_.setStandardView(core::StandardView::Home, true); break;
    default: return QWidget::keyPressEvent(e);
  }
  e->accept();
}

void OrientationDial::leaveEvent(QEvent* e) {
  hover_ = {};
  update();
  QWidget::leaveEvent(e);
}

void OrientationDial::contextMenuEvent(QContextMenuEvent* e) {
  viewer_.showOrientationMenu(e->globalPos());
  e->accept();
}

// =============================================================================================
// Breadcrumb
// =============================================================================================

namespace {
constexpr int kBarH = 28;
constexpr int kPad = 10;
}  // namespace

Breadcrumb::Breadcrumb(ViewerWidget& viewer) : QWidget(viewer.overlayParent()), viewer_(viewer) {
  setObjectName("Breadcrumb");
  setMouseTracking(true);
  setAttribute(Qt::WA_NoSystemBackground);
  resize(sizeHint());
}

void Breadcrumb::setCrumbs(const QStringList& crumbs) {
  if (crumbs == crumbs_) return;
  crumbs_ = crumbs;
  resize(sizeHint());
  updateGeometry();
  update();
}

void Breadcrumb::setLiveChip(const QString& caption, const QString& name) {
  if (caption == chipCaption_ && name == chipName_) return;
  chipCaption_ = caption;
  chipName_ = name;
  resize(sizeHint());
  updateGeometry();
  update();
}

QString Breadcrumb::chipText() const {
  if (!hasChip()) return {};
  return chipName_.isEmpty() ? chipCaption_.toUpper() : chipCaption_.toUpper() + ' ' + chipName_;
}

Breadcrumb::Layout Breadcrumb::computeLayout() const {
  Layout l;
  const QFontMetrics value(theme::font(theme::TextStyle::Value));
  const QFontMetrics cap(theme::font(theme::TextStyle::Caption));
  int x = kPad + 14;  // document glyph
  for (int i = 0; i < crumbs_.size(); ++i) {
    if (i > 0) {
      l.separators.emplace_back(x, 0, 16, kBarH);
      x += 16;
    }
    const int w = value.horizontalAdvance(crumbs_[i]) + 4;
    l.crumbs.emplace_back(x, 0, w, kBarH);
    x += w;
  }
  if (hasChip()) {
    if (!crumbs_.isEmpty()) {
      l.separators.emplace_back(x, 0, 16, kBarH);
      x += 16;
    }
    int w = 8 + 8 + 6 + cap.horizontalAdvance(chipCaption_.toUpper());
    if (!chipName_.isEmpty()) w += 6 + value.horizontalAdvance(chipName_);
    w += 8;
    l.chip = QRect(x, 4, w, kBarH - 8);
    x += w;
  }
  l.width = x + kPad;
  return l;
}

QSize Breadcrumb::sizeHint() const { return {computeLayout().width, kBarH}; }

QRect Breadcrumb::crumbRect(int index) const {
  const Layout l = computeLayout();
  return index >= 0 && index < static_cast<int>(l.crumbs.size()) ? l.crumbs[static_cast<size_t>(index)] : QRect();
}

void Breadcrumb::paintEvent(QPaintEvent*) {
  const ViewerWidget& v = viewer_;
  QPainter p(this);
  paintBarFrame(p, v, rect());
  const Layout l = computeLayout();
  p.setRenderHint(QPainter::Antialiasing, true);
  // document glyph: a small isometric cube outline
  {
    const QPointF c(kPad + 5, kBarH / 2.0);
    QPolygonF hex;
    for (int i = 0; i < 6; ++i) {
      const double a = (90 + 60 * i) * kDegRad;
      hex << c + QPointF(5.5 * std::cos(a), -5.5 * std::sin(a));
    }
    p.setPen(Qt::NoPen);
    p.setBrush(tok(v, Tok::TextSecondary));
    p.drawPolygon(hex);
  }
  p.setFont(theme::font(theme::TextStyle::Value));
  for (size_t i = 0; i < l.crumbs.size(); ++i) {
    const bool last = i + 1 == l.crumbs.size() && !hasChip();
    p.setPen(static_cast<int>(i) == hover_ ? tok(v, Tok::AccentText) : last ? tok(v, Tok::TextPrimary) : tok(v, Tok::TextPrimary));
    p.drawText(l.crumbs[i], Qt::AlignVCenter | Qt::AlignLeft, crumbs_[static_cast<int>(i)]);
  }
  p.setPen(QPen(tok(v, Tok::TextTertiary), 1.3));
  for (const QRect& s : l.separators) {
    const QPointF c = QRectF(s).center();
    p.drawLine(c + QPointF(-2, -4), c + QPointF(2, 0));
    p.drawLine(c + QPointF(2, 0), c + QPointF(-2, 4));
  }
  if (hasChip()) {
    const QRect chip = l.chip;
    const int bw = lightTheme(v) ? 2 : 1;  // light: gold borders are 2 px (4.2)
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(chip, tok(v, Tok::LiveFill));
    p.fillRect(QRect(chip.left(), chip.top(), chip.width(), bw), tok(v, Tok::LiveBase));
    p.fillRect(QRect(chip.left(), chip.bottom() - bw + 1, chip.width(), bw), tok(v, Tok::LiveBase));
    p.fillRect(QRect(chip.left(), chip.top(), bw, chip.height()), tok(v, Tok::LiveBase));
    p.fillRect(QRect(chip.right() - bw + 1, chip.top(), bw, chip.height()), tok(v, Tok::LiveBase));
    const QRect sq(chip.left() + 8, chip.center().y() - 3, 7, 7);
    p.fillRect(sq, tok(v, Tok::LiveBase));
    int x = sq.right() + 7;
    p.setFont(theme::font(theme::TextStyle::Caption));
    const QFontMetrics cap(p.font());
    p.setPen(tok(v, Tok::LiveText));
    const QString caption = chipCaption_.toUpper();
    p.drawText(QRect(x, chip.top(), cap.horizontalAdvance(caption) + 2, chip.height()), Qt::AlignVCenter | Qt::AlignLeft, caption);
    x += cap.horizontalAdvance(caption) + 6;
    if (!chipName_.isEmpty()) {
      p.setFont(theme::font(theme::TextStyle::Value));
      p.drawText(QRect(x, chip.top(), chip.right() - x, chip.height()), Qt::AlignVCenter | Qt::AlignLeft, chipName_);
    }
  }
}

void Breadcrumb::mouseMoveEvent(QMouseEvent* e) {
  int h = -1;
  const Layout l = computeLayout();
  for (size_t i = 0; i < l.crumbs.size(); ++i)
    if (l.crumbs[i].contains(e->position().toPoint())) h = static_cast<int>(i);
  if (h != hover_) {
    hover_ = h;
    setCursor(h >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    update();
  }
}

void Breadcrumb::mousePressEvent(QMouseEvent* e) {
  if (e->button() == Qt::LeftButton) {
    const Layout l = computeLayout();
    for (size_t i = 0; i < l.crumbs.size(); ++i)
      if (l.crumbs[i].contains(e->position().toPoint())) emit crumbClicked(static_cast<int>(i));
  }
  e->accept();
}

void Breadcrumb::leaveEvent(QEvent* e) {
  hover_ = -1;
  update();
  QWidget::leaveEvent(e);
}

// =============================================================================================
// SelectionFilterBar
// =============================================================================================

namespace {
constexpr int kFilterIcon = 30;
}

SelectionFilterBar::SelectionFilterBar(ViewerWidget& viewer) : QWidget(viewer.overlayParent()), viewer_(viewer) {
  setObjectName("SelectionFilterBar");
  setMouseTracking(true);
  setAttribute(Qt::WA_NoSystemBackground);
  resize(sizeHint());
}

const char* SelectionFilterBar::segmentLabel(Segment s) {
  static constexpr const char* names[] = {"BODIES", "FACES", "EDGES", "SKETCH PROFILES"};
  return names[s];
}

SelectionPriority SelectionFilterBar::segmentPriority(Segment s) {
  switch (s) {
    case Bodies: return SelectionPriority::Body;
    case Faces: return SelectionPriority::Face;
    case Edges: return SelectionPriority::Edge;
    default: return SelectionPriority::Sketch;
  }
}

core::ShapeKindMask SelectionFilterBar::segmentKinds(Segment s) {
  using core::ShapeKind;
  switch (s) {
    case Bodies: return core::ShapeKindMask(ShapeKind::Body) | ShapeKind::Component;
    case Faces: return core::ShapeKindMask(ShapeKind::Face);
    case Edges: return core::ShapeKindMask(ShapeKind::Edge);
    default: return core::ShapeKindMask(ShapeKind::Profile) | ShapeKind::SketchCurve | ShapeKind::Sketch;
  }
}

void SelectionFilterBar::refresh() {
  const core::ShapeKindMask avail = viewer_.availableKinds();
  bool changed = false;
  for (int i = 0; i < SegmentCount; ++i) {
    const bool vis = (avail & segmentKinds(static_cast<Segment>(i))) != 0;
    if (visible_[static_cast<size_t>(i)] != vis) changed = true;
    visible_[static_cast<size_t>(i)] = vis;
  }
  if (changed) {
    resize(sizeHint());
    updateGeometry();
  }
  update();
}

bool SelectionFilterBar::segmentActive(Segment s) const {
  if (!visible_[s]) return false;
  const SelectionPriority prio = viewer_.selectionPriority();
  if (prio != SelectionPriority::Default) return prio == segmentPriority(s);
  // no user priority: a tool that only takes this segment's kinds lights it (Extrude: profiles)
  const core::ShapeKindMask avail = viewer_.availableKinds();
  const core::ShapeKindMask brepAndSketch = segmentKinds(Bodies) | segmentKinds(Faces) | segmentKinds(Edges) | segmentKinds(Profiles);
  const core::ShapeKindMask relevant = avail & brepAndSketch;
  return relevant != 0 && (relevant & ~segmentKinds(s)) == 0;
}

QSize SelectionFilterBar::sizeHint() const {
  const QFontMetrics fm(theme::font(theme::TextStyle::Caption));
  int w = kFilterIcon;
  for (int i = 0; i < SegmentCount; ++i)
    if (visible_[static_cast<size_t>(i)]) w += fm.horizontalAdvance(QString::fromLatin1(segmentLabel(static_cast<Segment>(i)))) + 22;
  return {w + 1, kBarH};
}

QRect SelectionFilterBar::segmentRect(Segment s) const {
  if (!visible_[s]) return {};
  const QFontMetrics fm(theme::font(theme::TextStyle::Caption));
  int x = kFilterIcon;
  for (int i = 0; i < SegmentCount; ++i) {
    if (!visible_[static_cast<size_t>(i)]) continue;
    const int w = fm.horizontalAdvance(QString::fromLatin1(segmentLabel(static_cast<Segment>(i)))) + 22;
    if (i == s) return {x, 0, w, kBarH};
    x += w;
  }
  return {};
}

int SelectionFilterBar::segmentAt(const QPoint& p) const {
  for (int i = 0; i < SegmentCount; ++i)
    if (segmentRect(static_cast<Segment>(i)).contains(p)) return i;
  return -1;
}

void SelectionFilterBar::toggle(Segment s) {
  const SelectionPriority p = segmentPriority(s);
  viewer_.setSelectionPriority(viewer_.selectionPriority() == p ? SelectionPriority::Default : p);
  refresh();
}

void SelectionFilterBar::paintEvent(QPaintEvent*) {
  const ViewerWidget& v = viewer_;
  QPainter p(this);
  paintBarFrame(p, v, rect());
  // filter glyph (funnel)
  p.setRenderHint(QPainter::Antialiasing, true);
  {
    const QPointF c(kFilterIcon / 2.0, kBarH / 2.0);
    QPolygonF funnel{c + QPointF(-6, -5), c + QPointF(6, -5), c + QPointF(1.5, 0.5), c + QPointF(1.5, 5.5), c + QPointF(-1.5, 4),
                     c + QPointF(-1.5, 0.5)};
    p.setPen(Qt::NoPen);
    p.setBrush(tok(v, Tok::TextSecondary));
    p.drawPolygon(funnel);
  }
  p.setRenderHint(QPainter::Antialiasing, false);
  p.setFont(theme::font(theme::TextStyle::Caption));
  for (int i = 0; i < SegmentCount; ++i) {
    const auto s = static_cast<Segment>(i);
    const QRect r = segmentRect(s);
    if (r.isEmpty()) continue;
    const bool on = segmentActive(s);
    if (on) p.fillRect(r.adjusted(0, 1, 0, -1), tok(v, Tok::AccentSelFill));
    else if (i == hover_) p.fillRect(r.adjusted(0, 1, 0, -1), tok(v, Tok::SurfaceHover));
    p.setPen(hairline(tok(v, Tok::BorderSubtle)));
    p.drawLine(r.topLeft() + QPoint(0, 1), r.bottomLeft() - QPoint(0, 1));
    if (on) p.fillRect(QRect(r.left(), r.bottom() - 2, r.width(), 2), tok(v, Tok::AccentIndicator));
    p.setPen(on ? tok(v, Tok::TextPrimary) : i == hover_ ? tok(v, Tok::AccentText) : tok(v, Tok::TextSecondary));
    p.drawText(r, Qt::AlignCenter, QString::fromLatin1(segmentLabel(s)));
  }
}

void SelectionFilterBar::mouseMoveEvent(QMouseEvent* e) {
  const int h = segmentAt(e->position().toPoint());
  if (h != hover_) {
    hover_ = h;
    setCursor(h >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    update();
  }
}

void SelectionFilterBar::mousePressEvent(QMouseEvent* e) {
  if (e->button() == Qt::LeftButton) {
    const int s = segmentAt(e->position().toPoint());
    if (s >= 0) toggle(static_cast<Segment>(s));
  }
  e->accept();
}

void SelectionFilterBar::leaveEvent(QEvent* e) {
  hover_ = -1;
  update();
  QWidget::leaveEvent(e);
}

bool SelectionFilterBar::event(QEvent* e) {
  if (e->type() == QEvent::ToolTip) {
    auto* he = static_cast<QHelpEvent*>(e);
    const int s = segmentAt(he->pos());
    if (s < 0) {
      QToolTip::showText(he->globalPos(), QStringLiteral("Selection filter"), this);
    } else {
      const auto seg = static_cast<Segment>(s);
      const QString what = QString::fromLatin1(segmentLabel(seg)).toLower();
      QToolTip::showText(he->globalPos(),
                         segmentActive(seg) && viewer_.selectionPriority() == segmentPriority(seg)
                             ? QStringLiteral("Picking %1 only. Click again to pick everything.").arg(what)
                             : QStringLiteral("Pick %1 only (SELECT menu: priority)").arg(what),
                         this);
    }
    return true;
  }
  return QWidget::event(e);
}

// =============================================================================================
// CoordReadout
// =============================================================================================

namespace {
constexpr int kReadH = 26;
constexpr int kCellW = 94;   // 3 cells + unit = 320 (v6 mockup CoordReadout box)
constexpr int kUnitW = 38;
}  // namespace

CoordReadout::CoordReadout(ViewerWidget& viewer) : QWidget(viewer.overlayParent()), viewer_(viewer) {
  setObjectName("CoordReadout");
  setAttribute(Qt::WA_NoSystemBackground);
  setAttribute(Qt::WA_TransparentForMouseEvents);
  resize(sizeHint());
}

void CoordReadout::setPoint(const std::optional<gp_Pnt>& p) {
  if (p.has_value() == point_.has_value() && (!p || p->Distance(*point_) < 1e-9)) return;
  point_ = p;
  update();
}

QString CoordReadout::unitText() const {
  const core::UnitSystem u = viewer_.document() ? viewer_.document()->units() : core::UnitSystem{};
  return QString::fromLatin1(core::lengthUnitSymbol(u.length));
}

QString CoordReadout::cellText(int axis) const {
  const QString letter = QString::fromLatin1(axisLetter(axis));
  if (!point_) return letter + QStringLiteral(" —");
  const core::UnitSystem u = viewer_.document() ? viewer_.document()->units() : core::UnitSystem{};
  const double mm = axis == 0 ? point_->X() : axis == 1 ? point_->Y() : point_->Z();
  const double val = core::toDisplayValue(core::Quantity::mm(mm), u);
  return letter + ' ' + QString::number(std::abs(val) < 5e-7 ? 0.0 : val, 'f', std::clamp(u.precision, 0, 6));
}

QSize CoordReadout::sizeHint() const { return {3 * kCellW + kUnitW, kReadH}; }

void CoordReadout::paintEvent(QPaintEvent*) {
  const ViewerWidget& v = viewer_;
  QPainter p(this);
  paintBarFrame(p, v, rect());
  p.setFont(theme::font(theme::TextStyle::Value));
  for (int a = 0; a < 3; ++a) {
    const QRect cell(a * kCellW, 0, kCellW, kReadH);
    if (a > 0) {
      p.setPen(hairline(tok(v, Tok::BorderSubtle)));
      p.drawLine(cell.topLeft() + QPoint(0, 4), cell.bottomLeft() - QPoint(0, 4));
    }
    p.fillRect(QRect(cell.left() + 8, cell.center().y() - 5, 3, 10), tok(v, axisTok(a)));  // axis tick (with the letter)
    p.setPen(point_ ? tok(v, Tok::TextPrimary) : tok(v, Tok::TextTertiary));
    p.drawText(cell.adjusted(16, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, cellText(a));
  }
  const QRect unit(3 * kCellW, 0, kUnitW, kReadH);
  p.setPen(hairline(tok(v, Tok::BorderSubtle)));
  p.drawLine(unit.topLeft() + QPoint(0, 4), unit.bottomLeft() - QPoint(0, 4));
  p.setPen(tok(v, Tok::TextSecondary));
  p.drawText(unit, Qt::AlignCenter, unitText());
}

// =============================================================================================
// ScaleBar
// =============================================================================================

ScaleBar::ScaleBar(ViewerWidget& viewer) : QWidget(viewer.overlayParent()), viewer_(viewer) {
  setObjectName("ScaleBar");
  setAttribute(Qt::WA_NoSystemBackground);
  setAttribute(Qt::WA_TransparentForMouseEvents);
  resize(sizeHint());
}

QSize ScaleBar::sizeHint() const { return {200, 18}; }  // ruler 140 + label (v6 mockup ScaleBar box)

void ScaleBar::refresh() {
  const core::UnitSystem u = viewer_.document() ? viewer_.document()->units() : core::UnitSystem{};
  const double mmPerPx = viewer_.pixelSize(viewer_.viewCenter());
  const double unitPerPx = mmPerPx / core::lengthUnitToMm(u.length);
  const double len = scaleBarLength(unitPerPx, kRuler);
  const double px = len > 0 ? len / unitPerPx : 0.0;
  if (std::abs(len - length_) < 1e-12 && std::abs(px - pixels_) < 0.25) return;
  length_ = len;
  pixels_ = px;
  update();
}

QString ScaleBar::label() const {
  if (length_ <= 0) return {};
  const core::UnitSystem u = viewer_.document() ? viewer_.document()->units() : core::UnitSystem{};
  return QString::number(length_, 'g', 6) + ' ' + QString::fromLatin1(core::lengthUnitSymbol(u.length));
}

void ScaleBar::paintEvent(QPaintEvent*) {
  if (pixels_ <= 0) return;
  const ViewerWidget& v = viewer_;
  QPainter p(this);
  const double x1 = kRuler, x0 = kRuler - pixels_;
  const double y = height() / 2.0 + 3;
  const QColor ink = tok(v, Tok::TextSecondary);
  // the first half is a solid bar, ticks every 10 %
  p.fillRect(QRectF(x0, y - 2, pixels_ / 2.0, 2), ink);
  p.setPen(hairline(ink));
  p.drawLine(QPointF(x0, y), QPointF(x1, y));
  for (int i = 0; i <= 10; ++i) {
    const double x = std::round(x0 + pixels_ * i / 10.0) + 0.5;
    const double h = (i == 0 || i == 5 || i == 10) ? 6.0 : 3.0;
    p.drawLine(QPointF(x, y), QPointF(x, y - h));
  }
  p.setFont(theme::font(theme::TextStyle::Value));
  p.setPen(tok(v, Tok::TextSecondary));
  p.drawText(QRect(kRuler + 8, 0, width() - kRuler - 8, height()), Qt::AlignVCenter | Qt::AlignRight, label());
}

}  // namespace fabricad::viewer
