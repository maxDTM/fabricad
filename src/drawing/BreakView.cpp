// FabriCAD drawing: break view mapping, curve clipping and break symbols.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "drawing/BreakView.h"

#include "drawing/DrawingView.h"
#include "drawing/SheetRenderer.h"

#include <algorithm>
#include <cmath>

namespace fabricad::drawing {

const char* breakOrientationName(BreakOrientation o) {
  return o == BreakOrientation::Vertical ? "Vertical" : "Horizontal";
}

BreakOrientation breakOrientationFromName(const std::string& s) {
  return (s == "Vertical" || s == "vertical" || s == "v") ? BreakOrientation::Vertical : BreakOrientation::Horizontal;
}

const char* breakStyleName(BreakStyle s) { return s == BreakStyle::Curved ? "Curved" : "Structural"; }

BreakStyle breakStyleFromName(const std::string& s) {
  return (s == "Curved" || s == "curved") ? BreakStyle::Curved : BreakStyle::Structural;
}

QJsonObject ViewBreak::toJson() const {
  QJsonObject o;
  o["orientation"] = breakOrientationName(orientation);
  o["from"] = from;
  o["to"] = to;
  return o;
}

ViewBreak ViewBreak::fromJson(const QJsonObject& o) {
  ViewBreak b;
  b.orientation = breakOrientationFromName(o["orientation"].toString().toStdString());
  b.from = o["from"].toDouble();
  b.to = o["to"].toDouble();
  if (b.to < b.from) std::swap(b.from, b.to);
  return b;
}

std::vector<ViewBreak> normalizedBreaks(const std::vector<ViewBreak>& in) {
  std::vector<ViewBreak> out;
  for (BreakOrientation o : {BreakOrientation::Horizontal, BreakOrientation::Vertical}) {
    std::vector<ViewBreak> v;
    for (ViewBreak b : in) {
      if (b.orientation != o) continue;
      if (b.to < b.from) std::swap(b.from, b.to);
      if (b.to - b.from > 1e-9) v.push_back(b);
    }
    std::sort(v.begin(), v.end(), [](const ViewBreak& a, const ViewBreak& b) { return a.from < b.from; });
    for (const auto& b : v) {
      if (!out.empty() && out.back().orientation == o && b.from <= out.back().to) out.back().to = std::max(out.back().to, b.to);
      else out.push_back(b);
    }
  }
  return out;
}

// Breaks are kept normalised (sorted per orientation, disjoint) by every writer; the mapping
// walks them in order without allocating.
double breakMap(const std::vector<ViewBreak>& breaks, BreakOrientation o, double v, double gap) {
  double offset = 0;
  for (const auto& b : breaks) {
    if (b.orientation != o) continue;
    if (v <= b.from) break;
    double w = b.to - b.from;
    if (v >= b.to) {
      offset += w - gap;
      continue;
    }
    return b.from - offset + (v - b.from) / w * gap;
  }
  return v - offset;
}

double breakUnmap(const std::vector<ViewBreak>& breaks, BreakOrientation o, double u, double gap) {
  double offset = 0;
  for (const auto& b : breaks) {
    if (b.orientation != o) continue;
    double mf = b.from - offset;
    if (u <= mf) break;
    double w = b.to - b.from;
    if (u >= mf + gap) {
      offset += w - gap;
      continue;
    }
    return b.from + (u - mf) / std::max(gap, 1e-12) * w;
  }
  return u + offset;
}

bool insideBreak(const std::vector<ViewBreak>& breaks, BreakOrientation o, double v) {
  for (const auto& b : breaks)
    if (b.orientation == o && v > b.from + 1e-9 && v < b.to - 1e-9) return true;
  return false;
}

namespace {

double coord(const Point2& p, BreakOrientation o) { return o == BreakOrientation::Horizontal ? p.x : p.y; }

/// Parameter intervals of segment a->b that lie outside every band.
std::vector<std::pair<double, double>> keptIntervals(const Point2& a, const Point2& b, const std::vector<ViewBreak>& breaks) {
  std::vector<std::pair<double, double>> kept{{0.0, 1.0}};
  for (const auto& br : breaks) {
    double ca = coord(a, br.orientation), cb = coord(b, br.orientation);
    double t0, t1;
    if (std::abs(cb - ca) < 1e-12) {
      if (ca > br.from && ca < br.to) {
        t0 = 0;
        t1 = 1;
      } else {
        continue;
      }
    } else {
      t0 = (br.from - ca) / (cb - ca);
      t1 = (br.to - ca) / (cb - ca);
      if (t0 > t1) std::swap(t0, t1);
    }
    std::vector<std::pair<double, double>> next;
    for (auto [k0, k1] : kept) {
      if (t1 <= k0 || t0 >= k1) {
        next.push_back({k0, k1});
        continue;
      }
      if (t0 > k0) next.push_back({k0, t0});
      if (t1 < k1) next.push_back({t1, k1});
    }
    kept.swap(next);
  }
  std::vector<std::pair<double, double>> out;
  for (auto k : kept)
    if (k.second - k.first > 1e-9) out.push_back(k);
  return out;
}

bool bboxCrossesBand(const BBox2& b, const std::vector<ViewBreak>& breaks) {
  for (const auto& br : breaks) {
    double lo = br.orientation == BreakOrientation::Horizontal ? b.min.x : b.min.y;
    double hi = br.orientation == BreakOrientation::Horizontal ? b.max.x : b.max.y;
    if (hi > br.from + 1e-9 && lo < br.to - 1e-9) return true;
  }
  return false;
}

}  // namespace

std::vector<Curve2D> brokenCurves(const Curve2D& c, const DrawingView& view) {
  std::vector<Curve2D> out;
  if (view.breaks.empty()) {
    out.push_back(c.transformed(view.position, view.scale, true));
    return out;
  }
  BBox2 bb = c.bbox();
  if (!bboxCrossesBand(bb, view.breaks)) {
    // the curve lies in one piece: the mapping is a pure translation there
    Point2 ref = bb.isValid() ? bb.center() : c.start();
    Point2 mapped = view.toSheet(ref);
    Point2 plain{view.position.x + ref.x * view.scale, view.position.y - ref.y * view.scale};
    out.push_back(c.transformed(view.position + (mapped - plain), view.scale, true));
    return out;
  }
  std::vector<Point2> pts = c.type == CurveType::Segment ? std::vector<Point2>{c.a, c.b} : c.sample(0.02);
  std::vector<Point2> run;
  auto flush = [&]() {
    if (run.size() >= 2) {
      Curve2D piece = run.size() == 2 && c.type == CurveType::Segment ? Curve2D::segment(run[0], run[1], c.cls)
                                                                     : Curve2D::polyline(run, c.cls);
      piece.cls = c.cls;
      piece.sourceBody = c.sourceBody;
      piece.sourceEdge = c.sourceEdge;
      piece.sourceName = c.sourceName;
      piece.sourceSig = c.sourceSig;
      out.push_back(std::move(piece));
    }
    run.clear();
  };
  for (size_t i = 0; i + 1 < pts.size(); ++i) {
    const Point2 a = pts[i], b = pts[i + 1];
    for (auto [t0, t1] : keptIntervals(a, b, view.breaks)) {
      Point2 s = view.toSheet(a + (b - a) * t0), e = view.toSheet(a + (b - a) * t1);
      if (!run.empty() && (t0 > 1e-9 || run.back().distance(s) > 1e-9)) flush();
      if (run.empty()) run.push_back(s);
      run.push_back(e);
      if (t1 < 1 - 1e-9) flush();
    }
    if (keptIntervals(a, b, view.breaks).empty()) flush();
  }
  flush();
  return out;
}

void addBreakLines(DisplayList& out, const DrawingView& view) {
  if (view.breaks.empty() || !view.bbox.isValid()) return;
  const double over = 3.0;  // sheet mm beyond the geometry
  const double a = 2.5;     // zig-zag / wave amplitude
  for (const auto& br : view.breaks) {
    bool horiz = br.orientation == BreakOrientation::Horizontal;
    // extent across the break (sheet), from the mapped bbox
    BBox2 sb = view.sheetBBox();
    double lo = horiz ? sb.min.y - over : sb.min.x - over;
    double hi = horiz ? sb.max.y + over : sb.max.x + over;
    Point2 edge = view.toSheet(horiz ? Point2{br.from, view.bbox.center().y} : Point2{view.bbox.center().x, br.from});
    double e0 = horiz ? edge.x : edge.y;
    // sheet y grows downwards: a vertical break band [from, to] maps to [e0 - gap, e0]
    double e1 = horiz ? e0 + view.breakGap : e0 - view.breakGap;
    for (double e : {e0, e1}) {
      std::vector<Point2> pts;
      auto P = [&](double along, double across) { return horiz ? Point2{e + across, along} : Point2{along, e + across}; };
      double mid = 0.5 * (lo + hi);
      if (view.breakStyle == BreakStyle::Structural) {
        pts = {P(lo, 0), P(mid - a, 0), P(mid - a * 0.5, a * 0.7), P(mid + a * 0.5, -a * 0.7), P(mid + a, 0), P(hi, 0)};
      } else {
        const int n = 48;
        double period = std::max(6.0, (hi - lo) / 2.0);
        for (int i = 0; i <= n; ++i) {
          double t = lo + (hi - lo) * i / n;
          pts.push_back(P(t, a * 0.5 * std::sin(2 * kPi * (t - lo) / period)));
        }
      }
      out.curves.push_back(Curve2D::polyline(pts, LineClass::Annotation));
    }
  }
}

}  // namespace fabricad::drawing
