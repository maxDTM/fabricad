// Test-only stand-in for the sketch module: a feature that implements core::IProfileSource
// from a list of closed rectangles / circles given in sketch-plane coordinates. Lets the
// part tests run without the real SketchFeature.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Document.h"
#include "core/Feature.h"
#include "core/Registry.h"
#include "core/ShapeUtils.h"

#include <QJsonArray>
#include <QJsonObject>

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRep_Builder.hxx>
#include <GC_MakeCircle.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>

#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <vector>

namespace fabricad::part::test {

class TestSketch : public core::Feature, public core::IProfileSource {
 public:
  struct Rect {
    double x0, y0, x1, y1;
  };
  struct Circle {
    double cx, cy, r;
  };
  struct Point {
    double x, y;
  };

  TestSketch() : Feature("TestSketch") { addRef("plane", core::kPlaneKinds); }

  /// Geometry in plane coordinates. Curve ids: rect i -> 4 ids starting at 10*i+1, circle -> 100+i.
  std::vector<Rect> rects;
  std::vector<Circle> circles;
  std::vector<Point> points;  ///< standalone sketch points (ids 1000+i)
  std::vector<Point> lines;   ///< optional open polyline (ids 2000+i for segments), construction

  void recompute(core::RecomputeContext& ctx) override {
    auto pl = ctx.plane(ref("plane"));
    if (!pl) {
      ctx.setError("no plane");
      return;
    }
    plane_ = *pl;
    setOutputShape(allEdges(false));
  }
  const core::IProfileSource* profileSource() const override { return this; }
  std::string browserFolder() const override { return "Sketches"; }

  QJsonObject toJson() const override {
    QJsonObject o = Feature::toJson();
    QJsonArray r, c, p, l;
    for (const auto& x : rects) r.append(QJsonArray{x.x0, x.y0, x.x1, x.y1});
    for (const auto& x : circles) c.append(QJsonArray{x.cx, x.cy, x.r});
    for (const auto& x : points) p.append(QJsonArray{x.x, x.y});
    for (const auto& x : lines) l.append(QJsonArray{x.x, x.y});
    o["rects"] = r;
    o["circles"] = c;
    o["points"] = p;
    o["lines"] = l;
    return o;
  }
  void fromJson(const QJsonObject& o) override {
    Feature::fromJson(o);
    rects.clear();
    circles.clear();
    points.clear();
    lines.clear();
    for (const auto& v : o["rects"].toArray()) {
      QJsonArray a = v.toArray();
      rects.push_back({a[0].toDouble(), a[1].toDouble(), a[2].toDouble(), a[3].toDouble()});
    }
    for (const auto& v : o["circles"].toArray()) {
      QJsonArray a = v.toArray();
      circles.push_back({a[0].toDouble(), a[1].toDouble(), a[2].toDouble()});
    }
    for (const auto& v : o["points"].toArray()) {
      QJsonArray a = v.toArray();
      points.push_back({a[0].toDouble(), a[1].toDouble()});
    }
    for (const auto& v : o["lines"].toArray()) {
      QJsonArray a = v.toArray();
      lines.push_back({a[0].toDouble(), a[1].toDouble()});
    }
  }

  gp_Pnt to3d(double x, double y) const {
    gp_Vec o(plane_.Location().XYZ());
    return gp_Pnt((o + gp_Vec(plane_.XDirection()) * x + gp_Vec(plane_.YDirection()) * y).XYZ());
  }

  // --- IProfileSource ---------------------------------------------------------
  gp_Ax3 sketchPlane() const override { return plane_; }

  std::vector<core::ProfileInfo> profiles() const override {
    std::vector<core::ProfileInfo> out;
    for (size_t i = 0; i < rects.size(); ++i) {
      core::ProfileInfo p;
      p.curveIds = rectIds(i);
      core::Vec3 seed = core::Vec3::from(to3d((rects[i].x0 + rects[i].x1) / 2, (rects[i].y0 + rects[i].y1) / 2));
      p.ref = core::ShapeRef::profile(id(), seed, p.curveIds);
      p.area = std::abs((rects[i].x1 - rects[i].x0) * (rects[i].y1 - rects[i].y0));
      out.push_back(p);
    }
    for (size_t i = 0; i < circles.size(); ++i) {
      core::ProfileInfo p;
      p.curveIds = {100 + static_cast<int>(i)};
      core::Vec3 seed = core::Vec3::from(to3d(circles[i].cx, circles[i].cy));
      p.ref = core::ShapeRef::profile(id(), seed, p.curveIds);
      p.area = M_PI * circles[i].r * circles[i].r;
      out.push_back(p);
    }
    return out;
  }

  TopoDS_Face profileFace(const core::ShapeRef& ref) const override {
    // match by curve ids (name) first, then by seed
    for (size_t i = 0; i < rects.size(); ++i) {
      auto ids = rectIds(i);
      if (core::ShapeRef::profile(id(), {}, ids).name == ref.name) return rectFace(i);
    }
    for (size_t i = 0; i < circles.size(); ++i) {
      if (core::ShapeRef::profile(id(), {}, {100 + static_cast<int>(i)}).name == ref.name) return circleFace(i);
    }
    if (ref.hasPoint) {
      gp_Pnt s = ref.point.toPnt();
      for (size_t i = 0; i < rects.size(); ++i) {
        gp_Pnt a = to3d(rects[i].x0, rects[i].y0), b = to3d(rects[i].x1, rects[i].y1);
        core::Vec3 mn{std::min(a.X(), b.X()), std::min(a.Y(), b.Y()), std::min(a.Z(), b.Z())};
        core::Vec3 mx{std::max(a.X(), b.X()), std::max(a.Y(), b.Y()), std::max(a.Z(), b.Z())};
        if (s.X() >= mn.x - 1e-6 && s.X() <= mx.x + 1e-6 && s.Y() >= mn.y - 1e-6 && s.Y() <= mx.y + 1e-6 &&
            s.Z() >= mn.z - 1e-6 && s.Z() <= mx.z + 1e-6)
          return rectFace(i);
      }
      for (size_t i = 0; i < circles.size(); ++i)
        if (s.Distance(to3d(circles[i].cx, circles[i].cy)) <= circles[i].r) return circleFace(i);
    }
    return {};
  }

  TopoDS_Shape curveEdge(int curveId) const override {
    if (curveId >= 100 && curveId < 1000) {
      size_t i = static_cast<size_t>(curveId - 100);
      if (i >= circles.size()) return {};
      return BRepBuilderAPI_MakeEdge(circ(i)).Edge();
    }
    if (curveId >= 2000) {
      size_t i = static_cast<size_t>(curveId - 2000);
      if (i + 1 >= lines.size()) return {};
      return BRepBuilderAPI_MakeEdge(to3d(lines[i].x, lines[i].y), to3d(lines[i + 1].x, lines[i + 1].y)).Edge();
    }
    if (curveId >= 1) {
      size_t ri = static_cast<size_t>((curveId - 1) / 10);
      int side = (curveId - 1) % 10;
      if (ri >= rects.size() || side > 3) return {};
      auto c = corners(ri);
      return BRepBuilderAPI_MakeEdge(c[side], c[(side + 1) % 4]).Edge();
    }
    return {};
  }

  TopoDS_Shape allEdges(bool includeConstruction) const override {
    std::vector<TopoDS_Shape> edges;
    for (size_t i = 0; i < rects.size(); ++i)
      for (int k = 0; k < 4; ++k) edges.push_back(curveEdge(static_cast<int>(10 * i) + 1 + k));
    for (size_t i = 0; i < circles.size(); ++i) edges.push_back(curveEdge(100 + static_cast<int>(i)));
    if (includeConstruction)
      for (size_t i = 0; i + 1 < lines.size(); ++i) edges.push_back(curveEdge(2000 + static_cast<int>(i)));
    return core::makeCompound(edges);
  }

  std::optional<gp_Pnt> sketchPoint(const core::ShapeRef& ref) const override {
    if (ref.kind != core::ShapeKind::SketchPoint) return std::nullopt;
    if (ref.index >= 1000 && ref.index < 2000) {
      size_t i = static_cast<size_t>(ref.index - 1000);
      if (i < points.size()) return to3d(points[i].x, points[i].y);
    }
    if (ref.index >= 100 && ref.index < 1000 && ref.subIndex == static_cast<int>(core::SketchPointRole::Center)) {
      size_t i = static_cast<size_t>(ref.index - 100);
      if (i < circles.size()) return to3d(circles[i].cx, circles[i].cy);
    }
    return std::nullopt;
  }

  // helpers for tests
  core::ShapeRef rectProfile(size_t i) const { return profiles()[i].ref; }
  core::ShapeRef circleProfile(size_t i) const { return profiles()[rects.size() + i].ref; }
  core::ShapeRef pointRef(size_t i) const {
    return core::ShapeRef::sketchPoint(id(), 1000 + static_cast<int>(i), core::SketchPointRole::Standalone);
  }
  core::ShapeRef lineRef(size_t segment) const { return core::ShapeRef::sketchCurve(id(), 2000 + static_cast<int>(segment)); }

 private:
  std::vector<int> rectIds(size_t i) const {
    int b = static_cast<int>(10 * i);
    return {b + 1, b + 2, b + 3, b + 4};
  }
  std::array<gp_Pnt, 4> corners(size_t i) const {
    const Rect& r = rects[i];
    return {to3d(r.x0, r.y0), to3d(r.x1, r.y0), to3d(r.x1, r.y1), to3d(r.x0, r.y1)};
  }
  gp_Circ circ(size_t i) const {
    gp_Ax2 ax(to3d(circles[i].cx, circles[i].cy), plane_.Direction(), plane_.XDirection());
    return gp_Circ(ax, circles[i].r);
  }
  TopoDS_Face rectFace(size_t i) const {
    auto c = corners(i);
    BRepBuilderAPI_MakeWire w;
    for (int k = 0; k < 4; ++k) w.Add(BRepBuilderAPI_MakeEdge(c[k], c[(k + 1) % 4]).Edge());
    return BRepBuilderAPI_MakeFace(gp_Pln(plane_), w.Wire(), Standard_True).Face();
  }
  TopoDS_Face circleFace(size_t i) const {
    BRepBuilderAPI_MakeWire w(BRepBuilderAPI_MakeEdge(circ(i)).Edge());
    return BRepBuilderAPI_MakeFace(gp_Pln(plane_), w.Wire(), Standard_True).Face();
  }

  gp_Ax3 plane_{gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0)};
};

inline void registerTestSketch() {
  core::FeatureFactory::instance().registerType("TestSketch", [] { return std::make_unique<TestSketch>(); });
}

/// Adds a TestSketch on the given origin plane with one rectangle and recomputes.
inline TestSketch* addRectSketch(core::Document& doc, const std::string& plane, double x0, double y0, double x1, double y1) {
  auto sk = std::make_unique<TestSketch>();
  sk->prop("plane").setRef(core::ShapeRef::originPlane(plane));
  sk->rects.push_back({x0, y0, x1, y1});
  auto* p = static_cast<TestSketch*>(doc.addFeature(std::move(sk)));
  doc.recompute();
  return p;
}

}  // namespace fabricad::part::test
