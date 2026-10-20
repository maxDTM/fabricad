// FabriCAD sketch: font outlines for sketch text (QPainterPath -> line / cubic segments).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sketch/SketchText.h"

#include <QFont>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QPainterPath>
#include <QString>

#include <map>
#include <mutex>
#include <tuple>

namespace fabricad::sketch {

namespace {

constexpr double kRefPixelSize = 200.0;  ///< glyphs are extracted at this size, then scaled

struct UnitOutline {
  std::vector<TextContour> contours;  ///< y up, cap height 1, anchor at origin
  double advance = 0;                 ///< in cap heights
};

using Key = std::tuple<std::string, std::string, bool, bool>;

QFont makeFont(const std::string& family, bool bold, bool italic) {
  QFont f(QString::fromStdString(family.empty() ? std::string(defaultTextFont()) : family));
  f.setPixelSize(static_cast<int>(kRefPixelSize));
  f.setBold(bold);
  f.setItalic(italic);
  f.setKerning(true);
  f.setHintingPreference(QFont::PreferNoHinting);
  f.setStyleStrategy(QFont::StyleStrategy(QFont::NoAntialias | QFont::ForceOutline));
  return f;
}

UnitOutline buildOutline(const Key& key) {
  UnitOutline out;
  const auto& [text, family, bold, italic] = key;
  if (text.empty() || !qobject_cast<QGuiApplication*>(QCoreApplication::instance())) return out;
  QFont font = makeFont(family, bold, italic);
  QPainterPath capPath;
  capPath.addText(QPointF(0, 0), font, QStringLiteral("H"));
  double cap = capPath.boundingRect().height();
  if (cap < 1e-6) cap = QFontMetricsF(font).capHeight();
  if (cap < 1e-6) return out;
  const double s = 1.0 / cap;
  QPainterPath path;
  path.addText(QPointF(0, 0), font, QString::fromStdString(text));
  out.advance = QFontMetricsF(font).horizontalAdvance(QString::fromStdString(text)) * s;
  auto tp = [&](double x, double y) { return Pt2{x * s, -y * s}; };  // Qt is y down
  TextContour cur;
  Pt2 last, start;
  auto closeContour = [&]() {
    if (cur.empty()) return;
    if (!last.near(start, 1e-9)) cur.push_back(TextSegment{false, last, last, start, start});
    out.contours.push_back(std::move(cur));
    cur.clear();
  };
  for (int i = 0; i < path.elementCount(); ++i) {
    QPainterPath::Element e = path.elementAt(i);
    switch (e.type) {
      case QPainterPath::MoveToElement:
        closeContour();
        start = last = tp(e.x, e.y);
        break;
      case QPainterPath::LineToElement: {
        Pt2 p = tp(e.x, e.y);
        if (!p.near(last, 1e-9)) cur.push_back(TextSegment{false, last, last, p, p});
        last = p;
        break;
      }
      case QPainterPath::CurveToElement: {
        if (i + 2 >= path.elementCount()) break;
        QPainterPath::Element c2 = path.elementAt(i + 1), e3 = path.elementAt(i + 2);
        Pt2 p1 = tp(e.x, e.y), p2 = tp(c2.x, c2.y), p3 = tp(e3.x, e3.y);
        if (!(p3.near(last, 1e-9) && p1.near(last, 1e-9) && p2.near(last, 1e-9)))
          cur.push_back(TextSegment{true, last, p1, p2, p3});
        last = p3;
        i += 2;
        break;
      }
      case QPainterPath::CurveToDataElement: break;
    }
  }
  closeContour();
  return out;
}

const UnitOutline& unitOutline(const Geometry& g) {
  static std::mutex mutex;
  static std::map<Key, UnitOutline> cache;
  Key key{g.text, g.font, g.bold, g.italic};
  std::lock_guard<std::mutex> lock(mutex);
  auto it = cache.find(key);
  if (it != cache.end()) return it->second;
  if (cache.size() > 256) cache.clear();
  return cache.emplace(key, buildOutline(key)).first->second;
}

}  // namespace

Pt2 TextSegment::point(double t) const {
  if (!cubic) return p0 + (p3 - p0) * t;
  double u = 1 - t;
  return p0 * (u * u * u) + p1 * (3 * u * u * t) + p2 * (3 * u * t * t) + p3 * (t * t * t);
}

const char* defaultTextFont() { return "DejaVu Sans"; }

std::vector<TextContour> textContours(const Geometry& g) {
  std::vector<TextContour> out;
  if (g.type != GeomType::Text || g.p.size() < 4) return out;
  const UnitOutline& u = unitOutline(g);
  const Pt2 anchor{g.p[0], g.p[1]};
  const double h = g.p[2], ang = g.p[3];
  const double c = std::cos(ang), s = std::sin(ang);
  auto xf = [&](Pt2 q) {
    if (g.mirrored) q.x = -q.x;
    q = q * h;
    return anchor + Pt2{q.x * c - q.y * s, q.x * s + q.y * c};
  };
  out.reserve(u.contours.size());
  for (const TextContour& uc : u.contours) {
    TextContour tc;
    tc.reserve(uc.size());
    for (const TextSegment& seg : uc) tc.push_back(TextSegment{seg.cubic, xf(seg.p0), xf(seg.p1), xf(seg.p2), xf(seg.p3)});
    out.push_back(std::move(tc));
  }
  return out;
}

std::vector<std::vector<Pt2>> textPolylines(const Geometry& g, int segmentsPerCurve) {
  std::vector<std::vector<Pt2>> out;
  for (const TextContour& c : textContours(g)) {
    std::vector<Pt2> pl;
    if (c.empty()) continue;
    pl.push_back(c.front().p0);
    for (const TextSegment& seg : c) {
      if (!seg.cubic) {
        pl.push_back(seg.p3);
        continue;
      }
      for (int i = 1; i <= segmentsPerCurve; ++i) pl.push_back(seg.point(static_cast<double>(i) / segmentsPerCurve));
    }
    out.push_back(std::move(pl));
  }
  return out;
}

double textWidth(const Geometry& g) {
  if (g.type != GeomType::Text || g.p.size() < 4) return 0.0;
  return unitOutline(g).advance * g.p[2];
}

}  // namespace fabricad::sketch
