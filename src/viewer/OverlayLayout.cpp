// FabriCAD viewer: overlay layout (pure geometry).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "viewer/OverlayLayout.h"

#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>

namespace fabricad::viewer {

namespace {
constexpr const char* kPlacementNames[] = {"sidebar", "floatRight", "floatLeft", "free"};
constexpr const char* kCornerNames[] = {"tl", "tr", "bl", "br"};

bool usable(const QRect& r) { return r.isValid() && !r.isEmpty(); }

QRect clampInto(QRect r, const QRect& area) {
  if (!usable(area)) return r;
  r.moveLeft(std::clamp(r.left(), area.left(), std::max(area.left(), area.right() - r.width() + 1)));
  r.moveTop(std::clamp(r.top(), area.top(), std::max(area.top(), area.bottom() - r.height() + 1)));
  return r;
}
}  // namespace

const char* placementName(CardPlacement p) { return kPlacementNames[static_cast<int>(p)]; }

std::optional<CardPlacement> parsePlacement(const QString& s) {
  for (int i = 0; i < 4; ++i)
    if (s.compare(QLatin1StringView(kPlacementNames[i]), Qt::CaseInsensitive) == 0) return static_cast<CardPlacement>(i);
  return std::nullopt;
}

// ---- FreePos ---------------------------------------------------------------------------------

QString FreePos::toString() const {
  if (!valid) return {};
  return QStringLiteral("%1;%2;%3;%4")
      .arg(QLatin1StringView(placementName(base)), QLatin1StringView(kCornerNames[anchor]))
      .arg(fx, 0, 'f', 4)
      .arg(fy, 0, 'f', 4);
}

FreePos FreePos::fromString(const QString& s) {
  FreePos p;
  const QStringList parts = s.split(';');
  if (parts.size() != 4) return p;
  auto base = parsePlacement(parts[0]);
  if (!base || *base == CardPlacement::Free) return p;
  int corner = -1;
  for (int i = 0; i < 4; ++i)
    if (parts[1] == QLatin1StringView(kCornerNames[i])) corner = i;
  bool okx = false, oky = false;
  const double fx = parts[2].toDouble(&okx), fy = parts[3].toDouble(&oky);
  if (corner < 0 || !okx || !oky || !std::isfinite(fx) || !std::isfinite(fy)) return p;
  p.base = *base;
  p.anchor = static_cast<Corner>(corner);
  p.fx = std::clamp(fx, 0.0, 1.0);
  p.fy = std::clamp(fy, 0.0, 1.0);
  p.valid = true;
  return p;
}

FreePos FreePos::fromRect(const QRect& card, const QRect& vp, CardPlacement base) {
  FreePos p;
  if (!usable(vp)) return p;
  p.base = base == CardPlacement::Free ? CardPlacement::FloatRight : base;
  const QPoint c = card.center();
  const bool right = c.x() > vp.center().x();
  const bool bottom = c.y() > vp.center().y();
  p.anchor = bottom ? (right ? BottomRight : BottomLeft) : (right ? TopRight : TopLeft);
  const double w = std::max(1, vp.width()), h = std::max(1, vp.height());
  const int dx = right ? vp.right() - card.right() : card.left() - vp.left();
  const int dy = bottom ? vp.bottom() - card.bottom() : card.top() - vp.top();
  p.fx = std::clamp(dx / w, 0.0, 1.0);
  p.fy = std::clamp(dy / h, 0.0, 1.0);
  p.valid = true;
  return p;
}

QRect FreePos::toRect(const QSize& size, const QRect& vp) const {
  QRect r(QPoint(0, 0), size);
  const int dx = static_cast<int>(std::lround(fx * vp.width()));
  const int dy = static_cast<int>(std::lround(fy * vp.height()));
  const bool right = anchor == TopRight || anchor == BottomRight;
  const bool bottom = anchor == BottomLeft || anchor == BottomRight;
  if (right) r.moveRight(vp.right() - dx);
  else r.moveLeft(vp.left() + dx);
  if (bottom) r.moveBottom(vp.bottom() - dy);
  else r.moveTop(vp.top() + dy);
  return clampInto(r, vp);
}

// ---- OverlayLayout ----------------------------------------------------------------------------

const char* OverlayLayout::slotName(Slot s) {
  static constexpr const char* names[] = {"orientation", "viewTools", "topBar", "card", "sketchPalette",
                                          "quickToolbar", "readout", "scaleBar", "navBar", "other"};
  return names[static_cast<int>(s)];
}

QRect OverlayLayout::inner() const {
  return viewport_.adjusted(opt_.margin, opt_.margin, -opt_.margin, -opt_.margin);
}

QRect OverlayLayout::rect(Slot s, const QString& id) const {
  for (const Region& r : regions_)
    if (r.slot == s && (id.isEmpty() || r.id == id)) return r.rect;
  return {};
}

int OverlayLayout::bottomReserved(int left, int right) const {
  int y = viewport_.bottom() - opt_.margin + 1;
  for (const Region& r : regions_) {
    if (r.slot != Slot::NavBar && r.slot != Slot::Readout && r.slot != Slot::ScaleBar) continue;
    if (r.rect.right() < left || r.rect.left() > right) continue;
    y = std::min(y, r.rect.top() - opt_.gap);
  }
  return y;  // first row that must stay free
}

QRect OverlayLayout::place(const Item& it) const {
  const QRect in = inner();
  const QSize s = it.size;
  const QRect orient = rect(Slot::Orientation);
  switch (it.slot) {
    case Slot::Orientation: return QRect(in.right() - s.width() + 1, in.top(), s.width(), s.height());
    case Slot::ViewTools: {
      const int y = usable(orient) ? orient.bottom() + opt_.gap + 1 : in.top();
      return QRect(in.right() - s.width() + 1, y, s.width(), s.height());
    }
    case Slot::TopBar: {
      const int right = usable(orient) ? orient.left() - opt_.gap - 1 : in.right();
      return QRect(in.left(), in.top(), std::max(0, std::min(s.width(), right - in.left() + 1)), s.height());
    }
    case Slot::QuickToolbar: {
      if (it.right) {
        const QRect tools = rect(Slot::ViewTools);
        const int x = usable(tools) ? tools.left() - opt_.gap - s.width() : in.right() - s.width() + 1;
        const int y = usable(orient) ? orient.bottom() + opt_.gap + 1 : in.top();
        return QRect(x, y, s.width(), s.height());
      }
      const QRect bar = rect(Slot::TopBar);
      return QRect(in.left(), usable(bar) ? bar.bottom() + opt_.gap + 1 : in.top(), s.width(), s.height());
    }
    case Slot::Readout: return QRect(in.left(), in.bottom() - s.height() + 1, s.width(), s.height());
    case Slot::ScaleBar: return QRect(in.right() - s.width() + 1, in.bottom() - s.height() + 1, s.width(), s.height());
    case Slot::Other: return findFree(s, in.center());
    default: return {};
  }
}

QRect OverlayLayout::placeCard(CardPlacement p, const QSize& size, int* maxHeight) const {
  const int b = opt_.cardBleed;
  const QRect in = inner();
  const QSize vs(std::max(0, size.width() - 2 * b), std::max(0, size.height() - 2 * b));  // visible card
  auto finish = [&](QRect visible) {
    // keep clear of the breadcrumb row
    const QRect bar = rect(Slot::TopBar);
    if (usable(bar) && visible.intersects(bar)) visible.moveTop(bar.bottom() + opt_.gap + 1);
    const int bottom = bottomReserved(visible.left(), visible.right());
    const int maxH = std::max(0, bottom - visible.top());
    if (maxHeight) *maxHeight = maxH + 2 * b;
    visible.setHeight(std::min(vs.height(), maxH));
    return visible.adjusted(-b, -b, b, b);
  };
  if (p == CardPlacement::Sidebar) {
    if (maxHeight) *maxHeight = 0;
    return {};
  }
  if (p == CardPlacement::Free) {
    if (!opt_.free.valid) return placeCard(CardPlacement::FloatRight, size, maxHeight);
    QRect r = opt_.free.toRect(size, viewport_);
    const int maxH = viewport_.bottom() - r.top() + 1;
    if (maxHeight) *maxHeight = maxH;
    r.setHeight(std::min(r.height(), maxH));
    return r;
  }
  const QRect quick = rect(Slot::QuickToolbar);
  if (p == CardPlacement::FloatLeft) {
    int left = in.left();
    if (usable(quick) && quick.center().x() < viewport_.center().x()) left = quick.right() + opt_.gap + 1;
    const QRect bar = rect(Slot::TopBar);
    const int top = usable(bar) ? bar.bottom() + opt_.gap + 1 : in.top();
    return finish(QRect(QPoint(left, top), vs));
  }
  // FloatRight: left of the tool column (and of a right quick toolbar)
  int right = in.right();
  if (const QRect tools = rect(Slot::ViewTools); usable(tools)) right = std::min(right, tools.left() - opt_.gap - 1);
  if (usable(quick) && quick.center().x() > viewport_.center().x()) right = std::min(right, quick.left() - opt_.gap - 1);
  const QRect orient = rect(Slot::Orientation);
  if (!usable(orient)) return finish(QRect(QPoint(right - vs.width() + 1, in.top()), vs));
  // A: left of the orientation widget, below its top edge (mockup v6), when the model keeps room
  const int rightA = std::min(right, orient.left() - 2 * opt_.gap - 1);
  const bool roomA = rightA - vs.width() + 1 - viewport_.left() >= opt_.minModelWidth;
  if (roomA) {
    const int topA = orient.top() + opt_.cardTopOffset;
    const int availA = bottomReserved(rightA - vs.width() + 1, rightA) - topA;
    if (availA >= std::min(vs.height(), opt_.minCardHeight)) return finish(QRect(QPoint(rightA - vs.width() + 1, topA), vs));
    // a short viewport: the same column from the top, so the card gets the height
    return finish(QRect(QPoint(rightA - vs.width() + 1, in.top()), vs));
  }
  // B: at the right edge, below the orientation widget
  const int topB = orient.bottom() + opt_.gap + 1;
  const int bottomB = bottomReserved(right - vs.width() + 1, right);
  if (bottomB - topB >= std::min(vs.height(), opt_.minCardHeight)) return finish(QRect(QPoint(right - vs.width() + 1, topB), vs));
  // C: a short viewport: left of the orientation widget from the top, so the card gets the height
  if (rightA - vs.width() + 1 >= in.left()) return finish(QRect(QPoint(rightA - vs.width() + 1, in.top()), vs));
  return finish(QRect(QPoint(right - vs.width() + 1, topB), vs));
}

QRect OverlayLayout::cardRectFor(CardPlacement p, const QSize& size) const { return placeCard(p, size, nullptr); }

QRect OverlayLayout::placePalette(const Item& it) const {
  const QRect in = inner();
  const QSize s = it.size;
  if (it.userMoved && usable(it.fixed)) return clampInto(QRect(it.fixed.topLeft(), s), viewport_);
  const QRect orient = rect(Slot::Orientation);
  int right = in.right();
  if (const QRect tools = rect(Slot::ViewTools); usable(tools)) right = std::min(right, tools.left() - opt_.gap - 1);
  const int top = usable(orient) ? orient.bottom() + opt_.gap + 1 : in.top();
  QRect r(QPoint(right - s.width() + 1, top), s);
  const QRect card = rect(Slot::Card).adjusted(opt_.cardBleed, opt_.cardBleed, -opt_.cardBleed, -opt_.cardBleed);
  if (!usable(card) || !r.intersects(card)) return r;
  // shared side: stack below the card
  QRect below(QPoint(card.right() - s.width() + 1, card.bottom() + opt_.gap + 1), s);
  if (below.bottom() < bottomReserved(below.left(), below.right()) && isFree(below, Slot::SketchPalette)) return below;
  // else the opposite side when it is free
  const QRect bar = rect(Slot::TopBar);
  QRect left(QPoint(in.left(), usable(bar) ? bar.bottom() + opt_.gap + 1 : in.top()), s);
  if (isFree(left, Slot::SketchPalette)) return left;
  // else left of the card
  return clampInto(QRect(QPoint(card.left() - opt_.gap - s.width(), card.top()), s), viewport_);
}

void OverlayLayout::compute() {
  regions_.clear();
  cardMaxH_ = 0;
  auto push = [this](const Item& it, const QRect& r) {
    if (usable(r)) regions_.push_back({it.slot, it.id, r});
  };
  // 0: regions their owners place (ViewCube, nav bar)
  for (const Item& it : items_)
    if (usable(it.fixed) && !it.userMoved) push(it, it.fixed);
  // 1: edge-attached overlays in priority order
  for (Slot s : {Slot::Orientation, Slot::ViewTools, Slot::TopBar, Slot::QuickToolbar, Slot::Readout, Slot::ScaleBar})
    for (const Item& it : items_)
      if (it.slot == s && !usable(it.fixed) && !it.size.isEmpty()) push(it, place(it));
  // 2: the card, 3: the sketch palette
  for (const Item& it : items_)
    if (it.slot == Slot::Card && !usable(it.fixed) && !it.size.isEmpty()) push(it, placeCard(opt_.card, it.size, &cardMaxH_));
  for (const Item& it : items_)
    if (it.slot == Slot::SketchPalette && (it.userMoved || !usable(it.fixed)) && !it.size.isEmpty()) push(it, placePalette(it));
  // 4: anything else that asked for a free place
  for (const Item& it : items_)
    if (it.slot == Slot::Other && !usable(it.fixed) && !it.size.isEmpty()) push(it, place(it));
}

QMargins OverlayLayout::insets() const {
  QMargins m;
  const QRect vp = viewport_;
  for (const Region& r : regions_) {
    switch (r.slot) {
      case Slot::Card:
      case Slot::SketchPalette:
      case Slot::QuickToolbar:
      case Slot::ViewTools:
        if (r.rect.center().x() > vp.center().x()) m.setRight(std::max(m.right(), vp.right() - r.rect.left() + 1));
        else m.setLeft(std::max(m.left(), r.rect.right() - vp.left() + 1));
        break;
      case Slot::TopBar: m.setTop(std::max(m.top(), r.rect.bottom() - vp.top() + 1)); break;
      case Slot::NavBar:
      case Slot::Readout:
      case Slot::ScaleBar: m.setBottom(std::max(m.bottom(), vp.bottom() - r.rect.top() + 1)); break;
      default: break;
    }
  }
  return m;
}

bool OverlayLayout::isFree(const QRect& r, std::optional<Slot> ignore) const {
  if (!viewport_.contains(r)) return false;
  for (const Region& g : regions_) {
    if (ignore && g.slot == *ignore) continue;
    if (g.rect.intersects(r)) return false;
  }
  return true;
}

QRect OverlayLayout::findFree(const QSize& size, const QPoint& near) const {
  const QRect in = inner();
  if (size.width() > in.width() || size.height() > in.height()) return {};
  QRect best;
  long long bestD = std::numeric_limits<long long>::max();
  constexpr int kStep = 8;
  for (int y = in.top(); y + size.height() - 1 <= in.bottom(); y += kStep)
    for (int x = in.left(); x + size.width() - 1 <= in.right(); x += kStep) {
      const QRect r(QPoint(x, y), size);
      const QPoint d = r.center() - near;
      const long long dist = 1LL * d.x() * d.x() + 1LL * d.y() * d.y();
      if (dist >= bestD || !isFree(r)) continue;
      bestD = dist;
      best = r;
    }
  return best;
}

}  // namespace fabricad::viewer
