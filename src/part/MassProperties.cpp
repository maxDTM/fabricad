// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/MassProperties.h"

#include "core/ShapeUtils.h"

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <GProp_PrincipalProps.hxx>
#include <TopExp_Explorer.hxx>
#include <gp_Mat.hxx>

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>

namespace fabricad::part {

using core::ObjectId;
using core::Vec3;

double tensorAt(const std::array<double, 6>& t, int r, int c) {
  if (r == c) return t[static_cast<size_t>(r)];
  const int a = std::min(r, c), b = std::max(r, c);
  if (a == 0 && b == 1) return t[3];
  if (a == 0 && b == 2) return t[4];
  return t[5];
}

namespace {

std::array<double, 6> pack(const gp_Mat& m) {
  return {m(1, 1), m(2, 2), m(3, 3), 0.5 * (m(1, 2) + m(2, 1)), 0.5 * (m(1, 3) + m(3, 1)), 0.5 * (m(2, 3) + m(3, 2))};
}

bool hasSolid(const TopoDS_Shape& s) {
  TopExp_Explorer ex(s, TopAbs_SOLID);
  return ex.More();
}

}  // namespace

MassProperties computeMassProperties(const std::vector<MassItem>& items) {
  MassProperties p;
  bool anyDensity = false;
  for (const MassItem& it : items)
    if (!it.shape.IsNull() && it.density > 0) anyDensity = true;
  p.massBased = anyDensity;

  GProp_GProps system;  // system location = world origin
  bool any = false;
  for (const MassItem& it : items) {
    if (it.shape.IsNull()) continue;
    ++p.bodies;
    if (it.density <= 0) ++p.bodiesWithoutDensity;
    GProp_GProps vp;
    BRepGProp::VolumeProperties(it.shape, vp);
    const double v = std::abs(vp.Mass());
    p.volume += v;
    p.area += core::area(it.shape);
    Vec3 mn, mx;
    if (core::bbox(it.shape, mn, mx)) {
      if (!p.hasBox) {
        p.bboxMin = mn;
        p.bboxMax = mx;
        p.hasBox = true;
      } else {
        p.bboxMin = {std::min(p.bboxMin.x, mn.x), std::min(p.bboxMin.y, mn.y), std::min(p.bboxMin.z, mn.z)};
        p.bboxMax = {std::max(p.bboxMax.x, mx.x), std::max(p.bboxMax.y, mx.y), std::max(p.bboxMax.z, mx.z)};
      }
    }
    if (!hasSolid(it.shape) || v <= 0) continue;
    // g/cm^3 -> g/mm^3; without any density in the set: unit density (volume properties)
    const double rho = anyDensity ? it.density * 1e-3 : 1.0;
    if (rho <= 0) continue;
    if (anyDensity) p.mass += v * rho;
    system.Add(vp, rho);
    any = true;
  }
  if (!any) return p;
  const double m = system.Mass();
  const gp_Pnt c = system.CentreOfMass();
  p.cog = {c.X(), c.Y(), c.Z()};
  p.inertiaCog = pack(system.MatrixOfInertia());
  // parallel axis theorem: I_O = I_G + m (|c|^2 E - c c^T)
  const double cv[3] = {c.X(), c.Y(), c.Z()};
  const double c2 = cv[0] * cv[0] + cv[1] * cv[1] + cv[2] * cv[2];
  auto shift = [&](int r, int k) { return m * ((r == k ? c2 : 0.0) - cv[r] * cv[k]); };
  p.inertiaOrigin = {p.inertiaCog[0] + shift(0, 0), p.inertiaCog[1] + shift(1, 1), p.inertiaCog[2] + shift(2, 2),
                     p.inertiaCog[3] + shift(0, 1), p.inertiaCog[4] + shift(0, 2), p.inertiaCog[5] + shift(1, 2)};

  const GProp_PrincipalProps pp = system.PrincipalProperties();
  double i1, i2, i3;
  pp.Moments(i1, i2, i3);
  const gp_Vec a1 = pp.FirstAxisOfInertia(), a2 = pp.SecondAxisOfInertia(), a3 = pp.ThirdAxisOfInertia();
  std::array<std::pair<double, Vec3>, 3> axes = {
      std::pair{i1, Vec3{a1.X(), a1.Y(), a1.Z()}}, std::pair{i2, Vec3{a2.X(), a2.Y(), a2.Z()}},
      std::pair{i3, Vec3{a3.X(), a3.Y(), a3.Z()}}};
  std::stable_sort(axes.begin(), axes.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  for (size_t i = 0; i < 3; ++i) {
    Vec3 d = axes[i].second;
    const double len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (len > 0) d = {d.x / len, d.y / len, d.z / len};
    p.principalAxes[i] = d;
    p.principalMoments[i] = axes[i].first;
    p.radiusOfGyration[i] = m > 0 && axes[i].first > 0 ? std::sqrt(axes[i].first / m) : 0.0;
  }
  return p;
}

std::vector<MassItem> massItemsFor(const core::Document& doc, const std::vector<ObjectId>& componentsIn,
                                   const std::vector<ObjectId>& bodies) {
  std::vector<MassItem> out;
  std::vector<ObjectId> comps = componentsIn;
  if (comps.empty() && bodies.empty()) comps.push_back(core::kRootComponentId);
  std::vector<ObjectId> all;
  std::function<void(ObjectId)> walk = [&](ObjectId id) {
    if (std::find(all.begin(), all.end(), id) != all.end()) return;
    const core::Component* c = doc.component(id);
    if (!c) return;
    all.push_back(id);
    for (ObjectId ch : c->children) walk(ch);
  };
  for (ObjectId c : comps) walk(c);
  auto add = [&](ObjectId b, const gp_Trsf& world) {
    const core::Body* body = doc.body(b);
    if (!body || !body->visible) return;
    const TopoDS_Shape& s = doc.bodyShape(b);
    if (s.IsNull()) return;
    MassItem it;
    it.shape = world.Form() == gp_Identity ? s : core::transformed(s, world);
    it.density = body->density;
    it.label = body->name;
    out.push_back(std::move(it));
  };
  for (ObjectId c : all) {
    const core::Component* comp = doc.component(c);
    if (!comp || !comp->visible) continue;
    const gp_Trsf world = doc.worldPlacement(c);
    for (ObjectId b : doc.displayedBodies(c)) add(b, world);
  }
  for (ObjectId b : bodies) {
    const core::Body* body = doc.body(b);
    if (body) add(b, doc.worldPlacement(body->component));
  }
  return out;
}

namespace {

struct Row {
  std::string name, value, unit;
};

std::vector<Row> reportRows(const MassProperties& p, bool inch, int prec) {
  const double L = inch ? 1.0 / 25.4 : 1.0;           // length factor
  const double M = inch ? 1.0 / 453.59237 : 1.0;      // mass factor (g -> lb)
  const std::string lu = inch ? "in" : "mm";
  const std::string mu = inch ? "lb" : "g";
  auto f = [&](double v) { return std::format("{:.{}f}", v, prec); };
  auto g = [&](double v) { return std::format("{:.{}g}", v, std::max(prec + 3, 6)); };
  auto vec = [&](const Vec3& v, double k) { return f(v.x * k) + ", " + f(v.y * k) + ", " + f(v.z * k); };
  std::vector<Row> rows;
  rows.push_back({"Bodies", std::to_string(p.bodies), ""});
  if (p.bodiesWithoutDensity > 0)
    rows.push_back({"Bodies without density", std::to_string(p.bodiesWithoutDensity), ""});
  rows.push_back({"Volume", f(p.volume * L * L * L), lu + "^3"});
  rows.push_back({"Surface area", f(p.area * L * L), lu + "^2"});
  rows.push_back({"Mass", f(p.mass * M), mu});
  rows.push_back({p.massBased ? "Centre of gravity" : "Centroid (volume)", vec(p.cog, L), lu});
  if (p.hasBox) {
    rows.push_back({"Bounding box min", vec(p.bboxMin, L), lu});
    rows.push_back({"Bounding box max", vec(p.bboxMax, L), lu});
  }
  const std::string iu = p.massBased ? mu + "*" + lu + "^2" : lu + "^5";
  const double I = p.massBased ? M * L * L : L * L * L * L * L;
  const char* names[6] = {"Ixx", "Iyy", "Izz", "Ixy", "Ixz", "Iyz"};
  for (int i = 0; i < 6; ++i) rows.push_back({std::string(names[i]) + " (CoG)", g(p.inertiaCog[static_cast<size_t>(i)] * I), iu});
  for (int i = 0; i < 6; ++i)
    rows.push_back({std::string(names[i]) + " (origin)", g(p.inertiaOrigin[static_cast<size_t>(i)] * I), iu});
  for (size_t i = 0; i < 3; ++i) {
    rows.push_back({std::format("Principal moment I{}", i + 1), g(p.principalMoments[i] * I), iu});
    rows.push_back({std::format("Principal axis {}", i + 1), vec(p.principalAxes[i], 1.0), ""});
    if (p.massBased) rows.push_back({std::format("Radius of gyration k{}", i + 1), f(p.radiusOfGyration[i] * L), lu});
  }
  return rows;
}

std::string csvCell(const std::string& s) {
  if (s.find_first_of(",\"\n") == std::string::npos) return s;
  std::string out = "\"";
  for (char c : s) {
    if (c == '"') out += '"';
    out += c;
  }
  return out + "\"";
}

}  // namespace

std::string massPropertiesText(const MassProperties& p, bool inch, int precision) {
  std::string out;
  for (const Row& r : reportRows(p, inch, precision)) out += r.name + "\t" + r.value + (r.unit.empty() ? "" : "\t" + r.unit) + "\n";
  return out;
}

std::string massPropertiesCsv(const MassProperties& p, bool inch, int precision) {
  std::string out = "Property,Value,Unit\n";
  for (const Row& r : reportRows(p, inch, precision)) out += csvCell(r.name) + "," + csvCell(r.value) + "," + csvCell(r.unit) + "\n";
  return out;
}

std::string massPropertiesMarkdown(const MassProperties& p, bool inch, int precision) {
  std::string out = "# Mass properties\n\n| Property | Value | Unit |\n|---|---|---|\n";
  for (const Row& r : reportRows(p, inch, precision)) out += "| " + r.name + " | " + r.value + " | " + r.unit + " |\n";
  if (p.bodiesWithoutDensity > 0)
    out += std::format("\n{} bodies without density: they count for volume and area only.\n", p.bodiesWithoutDensity);
  return out;
}

}  // namespace fabricad::part
