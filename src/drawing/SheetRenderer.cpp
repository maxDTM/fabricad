// FabriCAD drawing: display-list construction and QPainter rendering.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "drawing/SheetRenderer.h"

#include "core/Document.h"
#include "core/ShapeUtils.h"
#include "core/Json.h"
#include "drawing/DrawingModel.h"
#include "drawing/CalloutRenderer.h"
#include "drawing/Symbols.h"
#include "theme/Tokens.h"

#include <QDate>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fabricad::drawing {

// ---------------------------------------------------------------------------------------
// DisplayList
// ---------------------------------------------------------------------------------------

void DisplayList::append(const DisplayList& o) {
  curves.insert(curves.end(), o.curves.begin(), o.curves.end());
  texts.insert(texts.end(), o.texts.begin(), o.texts.end());
  fills.insert(fills.end(), o.fills.begin(), o.fills.end());
}

BBox2 DisplayList::bbox() const {
  BBox2 b;
  for (const auto& c : curves) b.add(c.bbox());
  for (const auto& t : texts) {
    double w = SheetRenderer::textWidth(t.text, t.height);
    b.add(t.pos - Point2{w * 0.5, t.height});
    b.add(t.pos + Point2{w * 0.5, t.height});
  }
  for (const auto& f : fills)
    for (const auto& p : f.polygon) b.add(p);
  return b;
}

// ---------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------

std::string viewLabelText(const DrawingView& v) {
  std::string s = v.label;
  if (v.type == ViewType::Section || v.type == ViewType::Detail) s += " (" + scaleLabel(v.scale) + ")";
  return s;
}

double SheetRenderer::textWidth(const std::string& text, double height) {
  // count UTF-8 code points
  size_t n = 0;
  for (unsigned char c : text)
    if ((c & 0xC0) != 0x80) ++n;
  return 0.62 * height * static_cast<double>(n);
}

void SheetRenderer::addArrow(DisplayList& out, const Point2& tip, const Point2& dir, LineClass cls) const {
  auto tri = arrowHead(tip, dir, style_);
  out.fills.push_back({{tri[0], tri[1], tri[2]}, cls});
}

// ---------------------------------------------------------------------------------------
// Frame / title block
// ---------------------------------------------------------------------------------------

void SheetRenderer::addFrame(DisplayList& out, const Sheet& sheet) const {
  BBox2 f = sheet.frameRect();
  auto seg = [&](Point2 a, Point2 b, LineClass cls = LineClass::Border) { out.curves.push_back(Curve2D::segment(a, b, cls)); };
  seg({f.min.x, f.min.y}, {f.max.x, f.min.y});
  seg({f.max.x, f.min.y}, {f.max.x, f.max.y});
  seg({f.max.x, f.max.y}, {f.min.x, f.max.y});
  seg({f.min.x, f.max.y}, {f.min.x, f.min.y});
  if (!sheet.showZones) return;
  // Zones: 50 mm pitch from the frame centre; numbers along X, letters along Y (ISO 5457).
  const double pitch = 50.0;
  const double tick = 5.0;
  const double cx = f.center().x, cy = f.center().y;
  int nx = static_cast<int>(std::floor(f.width() / pitch / 2.0));
  int ny = static_cast<int>(std::floor(f.height() / pitch / 2.0));
  // Vertical tick marks (top and bottom) with numbers in each zone
  std::vector<double> xs;
  for (int i = -nx; i <= nx; ++i) xs.push_back(cx + i * pitch);
  for (double x : xs) {
    if (x <= f.min.x + 1e-6 || x >= f.max.x - 1e-6) continue;
    seg({x, f.min.y}, {x, f.min.y - tick}, LineClass::Annotation);
    seg({x, f.max.y}, {x, f.max.y + tick}, LineClass::Annotation);
  }
  std::vector<double> bounds = {f.min.x};
  for (double x : xs)
    if (x > f.min.x + 1e-6 && x < f.max.x - 1e-6) bounds.push_back(x);
  bounds.push_back(f.max.x);
  for (size_t i = 0; i + 1 < bounds.size(); ++i) {
    double mx = 0.5 * (bounds[i] + bounds[i + 1]);
    std::string num = std::to_string(i + 1);
    out.texts.push_back({{mx, f.min.y - tick * 0.5}, num, 2.5, 0, HAlign::Center, VAlign::Middle, LineClass::Annotation});
    out.texts.push_back({{mx, f.max.y + tick * 0.5}, num, 2.5, 0, HAlign::Center, VAlign::Middle, LineClass::Annotation});
  }
  std::vector<double> ys;
  for (int i = -ny; i <= ny; ++i) ys.push_back(cy + i * pitch);
  for (double y : ys) {
    if (y <= f.min.y + 1e-6 || y >= f.max.y - 1e-6) continue;
    seg({f.min.x, y}, {f.min.x - tick, y}, LineClass::Annotation);
    seg({f.max.x, y}, {f.max.x + tick, y}, LineClass::Annotation);
  }
  std::vector<double> ybounds = {f.min.y};
  for (double y : ys)
    if (y > f.min.y + 1e-6 && y < f.max.y - 1e-6) ybounds.push_back(y);
  ybounds.push_back(f.max.y);
  for (size_t i = 0; i + 1 < ybounds.size(); ++i) {
    double my = 0.5 * (ybounds[i] + ybounds[i + 1]);
    std::string letter(1, static_cast<char>('A' + i));
    out.texts.push_back({{f.min.x - tick * 0.5, my}, letter, 2.5, 0, HAlign::Center, VAlign::Middle, LineClass::Annotation});
    out.texts.push_back({{f.max.x + tick * 0.5, my}, letter, 2.5, 0, HAlign::Center, VAlign::Middle, LineClass::Annotation});
  }
  // centring marks
  seg({cx, 0}, {cx, f.min.y}, LineClass::Annotation);
  seg({cx, sheet.format.height}, {cx, f.max.y}, LineClass::Annotation);
  seg({0, cy}, {f.min.x, cy}, LineClass::Annotation);
  seg({sheet.format.width, cy}, {f.max.x, cy}, LineClass::Annotation);
}

void SheetRenderer::addTitleBlock(DisplayList& out, const Sheet& sheet,
                                  const std::map<std::string, std::string>& values) const {
  if (!sheet.titleBlock.visible) return;
  BBox2 r = sheet.titleBlockRect();
  auto seg = [&](Point2 a, Point2 b) { out.curves.push_back(Curve2D::segment(a, b, LineClass::Border)); };
  auto val = [&](const std::string& k) {
    auto it = values.find(k);
    return it == values.end() ? std::string() : it->second;
  };
  // Rows of equal height; cells given as (label, key, relative width)
  struct Cell {
    const char* label;
    const char* key;
    double w;
  };
  const std::vector<std::vector<Cell>> rows = {
      {{"Company", "Company", 0.35}, {"Title", "Title", 0.65}},
      {{"Drawn by", "DrawnBy", 0.25}, {"Checked", "Checked", 0.25}, {"Approved", "Approved", 0.25}, {"Date", "Date", 0.25}},
      {{"Material", "Material", 0.35}, {"Mass", "Mass", 0.2}, {"Scale", "Scale", 0.15}, {"Size", "Size", 0.1}, {"Sheet", "Sheet", 0.1}, {"Rev", "Rev", 0.1}},
      {{"Part number", "PartNumber", 1.0}},
  };
  const double rowH = r.height() / static_cast<double>(rows.size());
  // outline
  seg({r.min.x, r.min.y}, {r.max.x, r.min.y});
  seg({r.min.x, r.min.y}, {r.min.x, r.max.y});
  for (size_t ri = 0; ri < rows.size(); ++ri) {
    double y0 = r.min.y + rowH * static_cast<double>(ri);
    double y1 = y0 + rowH;
    if (ri > 0) seg({r.min.x, y0}, {r.max.x, y0});
    double x = r.min.x;
    for (size_t ci = 0; ci < rows[ri].size(); ++ci) {
      const Cell& c = rows[ri][ci];
      double w = r.width() * c.w;
      if (ci > 0) seg({x, y0}, {x, y1});
      out.texts.push_back({{x + 1.0, y0 + 0.8}, c.label, 1.8, 0, HAlign::Left, VAlign::Top, LineClass::Annotation});
      std::string v = val(c.key);
      if (!v.empty()) {
        bool title = std::string(c.key) == "Title";
        double h = title ? style_.titleTextHeight : 3.5;
        // shrink to fit
        double maxW = w - 2.0;
        double est = textWidth(v, h);
        if (est > maxW && est > 0) h = std::max(1.8, h * maxW / est);
        out.texts.push_back({{x + w * 0.5, y0 + rowH * 0.62}, v, h, 0, HAlign::Center, VAlign::Middle, LineClass::Annotation, title});
      }
      x += w;
    }
  }
}

// ---------------------------------------------------------------------------------------
// Views
// ---------------------------------------------------------------------------------------

void SheetRenderer::addViewCurves(DisplayList& out, const DrawingView& view) const {
  if (!view.breaks.empty()) {
    for (const auto& c : view.curves)
      for (auto& piece : brokenCurves(c, view)) out.curves.push_back(std::move(piece));
    return;
  }
  for (const auto& c : view.curves) out.curves.push_back(c.transformed(view.position, view.scale, true));
}

void SheetRenderer::addSectionLine(DisplayList& out, const DrawingView& parent, const DrawingView& sec) const {
  const SectionLine& L = sec.section;
  std::vector<Point2> pts;
  for (const Point2& p : L.path()) pts.push_back(parent.toSheet(p));
  if (pts.size() < 2) return;
  Point2 dFirst = (pts[1] - pts[0]).normalized();
  Point2 dLast = (pts.back() - pts[pts.size() - 2]).normalized();
  if (dFirst.length() < 0.5 || dLast.length() < 0.5) return;
  // arrow direction in sheet space (Y flipped); aligned cuts: the last arm has its own arrow,
  // the first arm's arrow revolved with the arm
  Point2 arrow = Point2{L.arrow.x, -L.arrow.y}.normalized();
  Point2 arrowLast = arrow;
  if (L.aligned && pts.size() >= 3) {
    double rot = std::atan2(dFirst.cross(dLast), dFirst.dot(dLast));
    arrowLast = {arrow.x * std::cos(rot) - arrow.y * std::sin(rot), arrow.x * std::sin(rot) + arrow.y * std::cos(rot)};
  }
  const double ext = 4.0;        // line beyond the geometry
  const double thick = 6.0;      // thick end segments
  Point2 a0 = pts.front() - dFirst * ext, b0 = pts.back() + dLast * ext;
  // thin chain line, thick ends and (ISO 128-44) thick strokes at every change of direction
  std::vector<Point2> chain = pts;
  chain.front() = a0;
  chain.back() = b0;
  for (size_t i = 0; i + 1 < chain.size(); ++i) out.curves.push_back(Curve2D::segment(chain[i], chain[i + 1], LineClass::Center));
  out.curves.push_back(Curve2D::segment(a0, a0 + dFirst * thick, LineClass::Visible));
  out.curves.push_back(Curve2D::segment(b0 - dLast * thick, b0, LineClass::Visible));
  for (size_t i = 1; i + 1 < pts.size(); ++i) {
    Point2 din = (pts[i] - pts[i - 1]).normalized(), dout = (pts[i + 1] - pts[i]).normalized();
    double lin = std::min(thick * 0.5, pts[i].distance(pts[i - 1]) * 0.5);
    double lout = std::min(thick * 0.5, pts[i].distance(pts[i + 1]) * 0.5);
    out.curves.push_back(Curve2D::polyline({pts[i] - din * lin, pts[i], pts[i] + dout * lout}, LineClass::Visible));
  }
  // arrows perpendicular to the end segments, 7 mm shafts, pointing in the viewing direction
  const double shaft = 7.0;
  for (int k = 0; k < 2; ++k) {
    Point2 end = k == 0 ? a0 : b0;
    Point2 dir = k == 0 ? arrow : arrowLast;
    Point2 base = end - dir * shaft;
    Point2 tip = end - dir * 1.0;
    out.curves.push_back(Curve2D::segment(base, tip, LineClass::Dimension));
    addArrow(out, tip, dir, LineClass::Dimension);
    Point2 textPos = base - dir * (style_.dimTextHeight * 1.2);
    out.texts.push_back({textPos, L.label, style_.dimTextHeight * 1.4, 0, HAlign::Center, VAlign::Middle, LineClass::Annotation, true});
  }
}

void SheetRenderer::addDetailCircle(DisplayList& out, const DrawingView& parent, const DrawingView& det) const {
  Point2 c = parent.toSheet(det.detail.center);
  double r = det.detail.radius * parent.scale;
  out.curves.push_back(Curve2D::circle(c, r, LineClass::Detail));
  // DRW-VIEWS-ANNOT-10: the label sits right next to the circle, on the diagonal that points
  // away from the middle of the parent view (out of the geometry, not into it)
  Point2 away = c - parent.sheetBBox().center();
  Point2 dir = Point2::polar(-45.0, 1.0);  // upper right in sheet space (y down)
  if (away.length() > 1e-6) dir = Point2{away.x >= 0 ? 1.0 : -1.0, away.y > 0 ? 1.0 : -1.0}.normalized();
  Point2 start = c + dir * r;
  Point2 end = start + dir * 2.0;
  out.curves.push_back(Curve2D::segment(start, end, LineClass::Dimension));
  const double h = style_.dimTextHeight * 1.4;
  out.texts.push_back({end + dir * (h * 0.75), det.detail.label, h, 0, HAlign::Center, VAlign::Middle, LineClass::Annotation, true});
}

void SheetRenderer::addViewDecorations(DisplayList& out, const Sheet& sheet, const DrawingView& view) const {
  for (core::ObjectId childId : sheet.childrenOf(view.id)) {
    const DrawingView* child = sheet.view(childId);
    if (!child) continue;
    if (child->type == ViewType::Section) addSectionLine(out, view, *child);
    else if (child->type == ViewType::Detail) addDetailCircle(out, view, *child);
  }
  addBreakLines(out, view);
  if (view.showLabel) {
    out.texts.push_back({view.labelPosition(), viewLabelText(view), style_.dimTextHeight * 1.2, 0, HAlign::Center, VAlign::Top, LineClass::Annotation, true});
  }
}

void SheetRenderer::addView(DisplayList& out, const Sheet& sheet, const DrawingView& view) const {
  addViewCurves(out, view);
  addViewDecorations(out, sheet, view);
}

// ---------------------------------------------------------------------------------------
// Annotations
// ---------------------------------------------------------------------------------------

void SheetRenderer::addDimension(DisplayList& out, const DrawingView& view, const Dimension& dim) const {
  DimLayout L = layoutDimension(dim, view, style_, units_);
  if (!L.ok) {
    // orphaned (a reference no longer resolves) or not measurable: red "?" at the text anchor
    DisplayText t{dim.textPos, "?", style_.dimTextHeight * 1.4, 0, HAlign::Center, VAlign::Middle, LineClass::Dimension, true};
    t.error = true;
    out.texts.push_back(t);
    return;
  }
  for (const auto& c : L.lines) out.curves.push_back(c);
  for (const auto& a : L.arrows) out.fills.push_back({{a[0], a[1], a[2]}, LineClass::Dimension});
  if (!dim.tolerance.isSet()) {
    out.texts.push_back({L.textPos, L.text, style_.dimTextHeight, L.textAngle, HAlign::Center, VAlign::Middle, LineClass::Dimension});
    return;
  }
  // toleranced text block (drawing/Symbols.h)
  Dimension measured = dim;
  DimGeometry g;
  if (measureDimension(dim, view, g)) {
    measured.valid = true;
    measured.value = g.value;
  }
  addToleranceText(out, measured, measured.value, L.textPos, L.textAngle, style_, units_);
}

void SheetRenderer::addNote(DisplayList& out, const TextNote& note) const {
  // multi-line, anchored top-left
  std::vector<std::string> lines;
  size_t start = 0;
  while (true) {
    size_t nl = note.text.find('\n', start);
    lines.push_back(note.text.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
    if (nl == std::string::npos) break;
    start = nl + 1;
  }
  Point2 down = Point2::polar(-note.angle, 1.0);  // visual CCW angle -> sheet-space direction
  Point2 right = Point2::polar(-note.angle, 1.0);
  down = Point2{-right.y, right.x};  // perpendicular, downward in sheet space for angle 0
  for (size_t i = 0; i < lines.size(); ++i) {
    Point2 p = note.position + down * (note.height * 1.6 * static_cast<double>(i));
    out.texts.push_back({p, lines[i], note.height, note.angle, HAlign::Left, VAlign::Top, LineClass::Annotation});
  }
  if (note.leader) {
    // the leader starts just outside the text box, measured with the real font (the estimate
    // was narrower than the text and the line struck through it: DRW2-VIEWS-ANNOT-6)
    double w = 0;
    for (const auto& l : lines) w = std::max(w, measureText(l, note.height, style_.fontFamily));
    const double gap = 0.3 * note.height;
    Point2 anchor = note.position + Point2{-gap, note.height * 0.6};
    if (note.leaderTo.x > note.position.x + w * 0.5) anchor = note.position + Point2{w + gap, note.height * 0.6};
    Point2 shoulder = anchor + Point2{note.leaderTo.x > anchor.x ? 2.0 : -2.0, 0};
    out.curves.push_back(Curve2D::segment(anchor, shoulder, LineClass::Dimension));
    out.curves.push_back(Curve2D::segment(shoulder, note.leaderTo, LineClass::Dimension));
    addArrow(out, note.leaderTo, (note.leaderTo - shoulder).normalized(), LineClass::Dimension);
  }
}

void SheetRenderer::addCenterMark(DisplayList& out, const DrawingView& view, const CenterMark& mark) const {
  for (const auto& c : layoutCenterMark(mark, view)) out.curves.push_back(c);
}

void SheetRenderer::addCenterLine(DisplayList& out, const DrawingView& view, const CenterLine& line) const {
  for (const auto& c : layoutCenterLine(line, view)) out.curves.push_back(c);
}

void SheetRenderer::addBalloon(DisplayList& out, const DrawingView& view, const Balloon& b) const {
  Point2 tip = view.toSheet(b.attach);
  Point2 c = b.position;
  double r = style_.balloonRadius;
  Point2 dir = (tip - c).normalized();
  if (dir.length() < 0.5) dir = {0, 1};
  Point2 start = c + dir * r;
  out.curves.push_back(Curve2D::circle(c, r, LineClass::Annotation));
  if (start.distance(tip) > 0.5) {
    out.curves.push_back(Curve2D::segment(start, tip, LineClass::Dimension));
    addArrow(out, tip, dir, LineClass::Dimension);
  }
  std::string txt = b.itemNumber > 0 ? std::to_string(b.itemNumber) : "?";
  out.texts.push_back({c, txt, style_.dimTextHeight, 0, HAlign::Center, VAlign::Middle, LineClass::Annotation});
}

void SheetRenderer::addPartsList(DisplayList& out, const PartsList& list) const {
  const double w = list.width();
  const double h = list.height();
  const Point2 o = list.position;
  auto seg = [&](Point2 a, Point2 b) { out.curves.push_back(Curve2D::segment(a, b, LineClass::Border)); };
  // outer + horizontal lines
  seg(o, o + Point2{w, 0});
  for (size_t r = 0; r <= list.rows.size(); ++r) {
    double y = o.y + list.rowHeight * static_cast<double>(r + 1);
    seg({o.x, y}, {o.x + w, y});
  }
  // vertical lines
  double x = o.x;
  seg({x, o.y}, {x, o.y + h});
  for (double cw : list.columnWidths) {
    x += cw;
    seg({x, o.y}, {x, o.y + h});
  }
  // header
  x = o.x;
  for (size_t c = 0; c < list.columns.size() && c < list.columnWidths.size(); ++c) {
    double cw = list.columnWidths[c];
    out.texts.push_back({{x + cw * 0.5, o.y + list.rowHeight * 0.5}, list.columns[c], list.textHeight, 0, HAlign::Center, VAlign::Middle, LineClass::Annotation, true});
    x += cw;
  }
  // rows
  for (size_t r = 0; r < list.rows.size(); ++r) {
    const BomRow& row = list.rows[r];
    double y = o.y + list.rowHeight * (static_cast<double>(r) + 1.5);
    x = o.x;
    for (size_t c = 0; c < list.columns.size() && c < list.columnWidths.size(); ++c) {
      double cw = list.columnWidths[c];
      const std::string& col = list.columns[c];
      std::string v;
      HAlign al = HAlign::Left;
      if (col == "Item") { v = std::to_string(row.item); al = HAlign::Center; }
      else if (col == "Qty") { v = std::to_string(row.quantity); al = HAlign::Center; }
      else if (col == "Part Number") v = row.partNumber;
      else if (col == "Description") v = row.description;
      else if (col == "Material") v = row.material;
      else if (col == "Mass") {
        if (row.mass > 0) {
          char buf[32];
          std::snprintf(buf, sizeof buf, "%.3f kg", row.mass);
          v = buf;
        }
        al = HAlign::Right;
      }
      double tx = al == HAlign::Left ? x + 1.5 : (al == HAlign::Right ? x + cw - 1.5 : x + cw * 0.5);
      double th = list.textHeight;
      double est = textWidth(v, th);
      if (est > cw - 3.0 && est > 0) th = std::max(1.5, th * (cw - 3.0) / est);
      out.texts.push_back({{tx, y}, v, th, 0, al, VAlign::Middle, LineClass::Annotation});
      x += cw;
    }
  }
}

void SheetRenderer::addSheet(DisplayList& out, const Sheet& sheet, const std::map<std::string, std::string>& titleValues,
                             bool frame) const {
  if (frame) {
    addFrame(out, sheet);
    addTitleBlock(out, sheet, titleValues);
  }
  for (const auto& v : sheet.views) addView(out, sheet, v);
  for (const auto& d : sheet.dimensions)
    if (const DrawingView* v = sheet.view(d.viewId)) addDimension(out, *v, d);
  for (const auto& m : sheet.centerMarks)
    if (const DrawingView* v = sheet.view(m.viewId)) addCenterMark(out, *v, m);
  for (const auto& l : sheet.centerLines)
    if (const DrawingView* v = sheet.view(l.viewId)) addCenterLine(out, *v, l);
  for (const auto& b : sheet.balloons)
    if (const DrawingView* v = sheet.view(b.viewId)) addBalloon(out, *v, b);
  for (const auto& n : sheet.notes) addNote(out, n);
  for (const auto& p : sheet.partsLists) addPartsList(out, p);
  CalloutRenderer(style_, units_).addAll(out, sheet);
}

// ---------------------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------------------

QColor SheetRenderer::ink() { return theme::color(theme::Tok::DrawingInk, theme::Theme::Light); }
QColor SheetRenderer::errorInk() { return theme::color(theme::Tok::StatusError, theme::Theme::Light); }

QPen SheetRenderer::pen(LineClass cls, const QColor& color, double widthScale) {
  const double w = lineClassWidth(cls) * widthScale;
  QPen p(color, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  p.setCosmetic(false);
  QList<qreal> pattern;  // in units of pen width
  switch (cls) {
    case LineClass::Hidden:
      pattern = {3.0 / w, 1.5 / w};
      break;
    case LineClass::Center:
      pattern = {8.0 / w, 1.5 / w, 1.0 / w, 1.5 / w};
      break;
    case LineClass::Tangent:
      p.setColor(QColor(color).lighter(160));
      break;
    default: break;
  }
  if (!pattern.isEmpty()) {
    p.setDashPattern(pattern);
    p.setCapStyle(Qt::FlatCap);
  }
  return p;
}

void SheetRenderer::paintCurve(QPainter& p, const Curve2D& c) {
  switch (c.type) {
    case CurveType::Segment:
      p.drawLine(QPointF(c.a.x, c.a.y), QPointF(c.b.x, c.b.y));
      break;
    case CurveType::Arc: {
      QRectF rect(c.center.x - c.radius, c.center.y - c.radius, 2 * c.radius, 2 * c.radius);
      if (c.isCircle()) {
        p.drawEllipse(rect);
      } else {
        // Qt angles are CCW visually (y up) -> negate our y-down angles.
        QPainterPath path;
        path.arcMoveTo(rect, -c.startAngle);
        path.arcTo(rect, -c.startAngle, -c.sweep);
        p.drawPath(path);
      }
      break;
    }
    case CurveType::Polyline: {
      QPolygonF poly;
      poly.reserve(static_cast<int>(c.pts.size()));
      for (const auto& q : c.pts) poly << QPointF(q.x, q.y);
      p.drawPolyline(poly);
      break;
    }
  }
}

void SheetRenderer::paintText(QPainter& p, const DisplayText& t, const std::string& fontFamily) {
  if (t.text.empty() || t.height <= 0) return;
  QFont font(QString::fromStdString(fontFamily));
  font.setCapitalization(QFont::MixedCase);  // typed case on every device (DRW2-ASM-OUTPUT-5, DRW2-VIEWS-ANNOT-5)
  font.setPixelSize(100);
  font.setBold(t.bold);
  QFontMetricsF fm(font);
  double cap = fm.capHeight() > 0 ? fm.capHeight() : fm.ascent() * 0.72;
  double s = t.height / cap;  // mm per font pixel
  QString text = QString::fromUtf8(t.text.c_str());
  double w = fm.horizontalAdvance(text);
  p.save();
  p.translate(t.pos.x, t.pos.y);
  p.rotate(-t.angle);
  p.scale(s, s);
  p.setFont(font);
  double x = 0;
  if (t.halign == HAlign::Center) x = -w * 0.5;
  else if (t.halign == HAlign::Right) x = -w;
  double y = 0;  // baseline
  if (t.valign == VAlign::Middle) y = cap * 0.5;
  else if (t.valign == VAlign::Top) y = cap;
  p.setPen(QPen(t.error ? errorInk() : p.pen().color()));
  p.drawText(QPointF(x, y), text);
  p.restore();
}

void SheetRenderer::paint(QPainter& p, const DisplayList& list, const QColor& color, const std::string& fontFamily) {
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setRenderHint(QPainter::TextAntialiasing, true);
  LineClass current = LineClass::Visible;
  bool penSet = false;
  p.setBrush(Qt::NoBrush);
  for (const auto& c : list.curves) {
    if (!penSet || c.cls != current) {
      p.setPen(pen(c.cls, color));
      current = c.cls;
      penSet = true;
    }
    paintCurve(p, c);
  }
  p.setPen(Qt::NoPen);
  p.setBrush(color);
  for (const auto& f : list.fills) {
    QPolygonF poly;
    for (const auto& q : f.polygon) poly << QPointF(q.x, q.y);
    p.drawPolygon(poly);
  }
  p.setBrush(Qt::NoBrush);
  p.setPen(QPen(color));
  for (const auto& t : list.texts) paintText(p, t, fontFamily);
}

// ---------------------------------------------------------------------------------------
// Title block values
// ---------------------------------------------------------------------------------------

std::map<std::string, std::string> titleBlockValues(const DrawingModel& model, const Sheet& sheet) {
  std::map<std::string, std::string> v = sheet.titleBlock.fields;
  const core::Document& doc = model.document();
  auto def = [&](const std::string& k, const std::string& value) {
    if (v[k].empty()) v[k] = value;
  };
  def("Title", doc.name());
  def("Date", QDate::currentDate().toString(Qt::ISODate).toStdString());
  // the scale of the main (first base) view, which is what the drawing is read at; the sheet's
  // default scale only until a view exists (DRW-ASM-OUTPUT-7)
  double mainScale = sheet.scale;
  for (const auto& view : sheet.views)
    if (view.type == ViewType::Base) {
      mainScale = view.scale;
      break;
    }
  def("Scale", scaleLabel(mainScale));
  def("Size", sheet.format.name);
  int index = 0, count = static_cast<int>(model.sheets().size());
  for (size_t i = 0; i < model.sheets().size(); ++i)
    if (model.sheets()[i].id == sheet.id) index = static_cast<int>(i) + 1;
  def("Sheet", std::to_string(std::max(index, 1)) + " / " + std::to_string(std::max(count, 1)));
  const core::Component& root = doc.rootComponent();
  def("PartNumber", root.partNumber);
  // revision: latest row of the sheet's revision table, else the root component's revision
  std::string revision = root.revision;
  for (const auto& t : sheet.callouts.revisionTables)
    if (!t.rows.empty()) revision = t.rows.back().rev;
  def("Rev", revision);
  // material / mass from bodies
  std::string material;
  double mass = 0;
  bool mixed = false;
  for (core::ObjectId b : doc.liveBodies(false)) {
    const core::Body* body = doc.body(b);
    if (!body) continue;
    if (!body->material.empty()) {
      if (material.empty()) material = body->material;
      else if (material != body->material) mixed = true;
    }
    if (body->density > 0) {
      const TopoDS_Shape& s = doc.bodyShape(b);
      if (!s.IsNull()) mass += core::volume(s) * 1e-6 * body->density;
    }
  }
  if (!root.material.empty()) material = root.material, mixed = false;  // the BOM material wins
  def("Material", mixed ? "VARIOUS" : material);
  if (mass > 0) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.3f kg", mass);
    def("Mass", buf);
  }
  // ${token} fields: values from the model, refreshed on every render (production-features P1)
  TitleBlockModel tm;
  tm.text["material"] = mixed ? "VARIOUS" : material;
  tm.text["partnumber"] = root.partNumber;
  tm.text["revision"] = revision;
  tm.text["description"] = root.description;
  tm.text["vendor"] = root.vendor;
  tm.text["title"] = doc.name();
  tm.massGrams = mass * 1000.0;
  for (auto& [k, value] : v)
    if (value.find("${") != std::string::npos) value = expandTitleBlockTokens(value, tm);
  for (auto it = v.begin(); it != v.end();)
    it = it->second.empty() ? v.erase(it) : std::next(it);
  return v;
}

}  // namespace fabricad::drawing
