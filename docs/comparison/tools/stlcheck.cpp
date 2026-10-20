// SPDX-License-Identifier: LGPL-2.1-or-later
// stlcheck: measures an STL mesh against a STEP B-rep (docs/comparison/COMPARISON.md, section 3).
//
// usage: stlcheck <file.stl> <reference.step> [subdiv=4] [threads=4]   -> one JSON object on stdout
//
// Mesh: vertices are welded by exact float coordinates (the way STL writers emit shared corners).
// Reported: triangles, unique vertices, file size, binary/ASCII, degenerate triangles (area < 1e-9 mm2
// or repeated corner), edge use (open = used once, non-manifold = used > 2 times), winding consistency
// (every interior edge used once in each direction), stored facet normals that disagree with the
// winding, connected shells with signed volume (> 0 = outward normals), Euler genus, bbox.
// Deviation: every triangle is sampled on a barycentric grid with `subdiv` steps per edge (4 ->
// 15 points: corners, quarter/mid edge points, interior), and each sample's distance to the boundary
// faces of the matching STEP solid is computed with BRepExtrema_DistShapeShape. The maximum over the
// non-corner samples is the chordal deviation; corners give the vertex placement error.
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <GeomAdaptor_Surface.hxx>
#include <STEPControl_Reader.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using V3 = std::array<double, 3>;
static V3 sub(const V3& a, const V3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
static V3 cross(const V3& a, const V3& b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
static double dot(const V3& a, const V3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

struct Mesh {
  bool binary = false;
  long bytes = 0;
  std::vector<V3> verts;
  std::vector<std::array<int, 3>> tris;
  std::vector<V3> normals;
};

static bool readStl(const char* path, Mesh& m) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::string data((std::istreambuf_iterator<char>(f)), {});
  m.bytes = static_cast<long>(data.size());
  std::map<std::array<float, 3>, int> index;
  auto vid = [&](std::array<float, 3> p) {
    auto it = index.find(p);
    if (it != index.end()) return it->second;
    int id = static_cast<int>(m.verts.size());
    index.emplace(p, id);
    m.verts.push_back({p[0], p[1], p[2]});
    return id;
  };
  if (data.size() >= 84) {
    uint32_t n;
    std::memcpy(&n, data.data() + 80, 4);
    if (84 + 50ull * n == data.size()) {
      m.binary = true;
      for (uint32_t i = 0; i < n; ++i) {
        const char* r = data.data() + 84 + 50ull * i;
        float v[12];
        std::memcpy(v, r, 48);
        m.normals.push_back({v[0], v[1], v[2]});
        m.tris.push_back({vid({v[3], v[4], v[5]}), vid({v[6], v[7], v[8]}), vid({v[9], v[10], v[11]})});
      }
      return true;
    }
  }
  std::istringstream in(data);
  std::string w;
  std::vector<int> cur;
  V3 nrm{0, 0, 0};
  while (in >> w) {
    if (w == "normal") in >> nrm[0] >> nrm[1] >> nrm[2];
    else if (w == "vertex") {
      std::array<float, 3> p;
      in >> p[0] >> p[1] >> p[2];
      cur.push_back(vid(p));
      if (cur.size() == 3) {
        m.tris.push_back({cur[0], cur[1], cur[2]});
        m.normals.push_back(nrm);
        cur.clear();
      }
    }
  }
  return !m.tris.empty();
}

struct Ref {
  TopoDS_Shape boundary;  // compound of the solid's faces: distance to the surface, not 0 inside
  double volume;
  double box[6];
};

static int findp(std::vector<int>& p, int x) {
  while (p[x] != x) x = p[x] = p[p[x]];
  return x;
}

static const char* surfName(GeomAbs_SurfaceType t) {
  switch (t) {
    case GeomAbs_Plane: return "plane";
    case GeomAbs_Cylinder: return "cylinder";
    case GeomAbs_Cone: return "cone";
    case GeomAbs_Sphere: return "sphere";
    case GeomAbs_Torus: return "torus";
    case GeomAbs_BSplineSurface: return "bspline";
    case GeomAbs_BezierSurface: return "bezier";
    case GeomAbs_SurfaceOfRevolution: return "revolution";
    case GeomAbs_SurfaceOfExtrusion: return "extrusion";
    case GeomAbs_OffsetSurface: return "offset";
    default: return "other";
  }
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: stlcheck <file.stl> <reference.step> [subdiv=4] [threads=4]\n");
    return 2;
  }
  const int subdiv = argc > 3 ? std::atoi(argv[3]) : 4;
  const int nthreads = argc > 4 ? std::atoi(argv[4]) : 4;
  Mesh m;
  if (!readStl(argv[1], m)) {
    std::fprintf(stderr, "cannot read %s\n", argv[1]);
    return 1;
  }
  STEPControl_Reader reader;
  if (reader.ReadFile(argv[2]) != IFSelect_RetDone) {
    std::fprintf(stderr, "cannot read %s\n", argv[2]);
    return 1;
  }
  reader.TransferRoots();
  std::vector<Ref> refs;
  for (int i = 1; i <= reader.NbShapes(); ++i)
    for (TopExp_Explorer ex(reader.Shape(i), TopAbs_SOLID); ex.More(); ex.Next()) {
      Ref r;
      BRep_Builder b;
      TopoDS_Compound c;
      b.MakeCompound(c);
      for (TopExp_Explorer fx(ex.Current(), TopAbs_FACE); fx.More(); fx.Next()) b.Add(c, fx.Current());
      r.boundary = c;
      GProp_GProps g;
      BRepGProp::VolumeProperties(ex.Current(), g);
      r.volume = g.Mass();
      Bnd_Box bb;
      BRepBndLib::AddOptimal(ex.Current(), bb, false, false);
      bb.Get(r.box[0], r.box[1], r.box[2], r.box[3], r.box[4], r.box[5]);
      refs.push_back(r);
    }

  // ---- topology
  const size_t nt = m.tris.size(), nv = m.verts.size();
  int degenerate = 0, flippedNormals = 0, zeroNormals = 0;
  std::vector<double> area(nt);
  for (size_t i = 0; i < nt; ++i) {
    const auto& t = m.tris[i];
    V3 c = cross(sub(m.verts[t[1]], m.verts[t[0]]), sub(m.verts[t[2]], m.verts[t[0]]));
    area[i] = 0.5 * std::sqrt(dot(c, c));
    if (t[0] == t[1] || t[1] == t[2] || t[0] == t[2] || area[i] < 1e-9) ++degenerate;
    const V3& n = m.normals[i];
    if (dot(n, n) < 1e-12) ++zeroNormals;
    else if (dot(n, c) < 0) ++flippedNormals;
  }
  std::map<std::pair<int, int>, std::array<int, 2>> edges;  // (lo,hi) -> uses lo->hi, hi->lo
  for (const auto& t : m.tris)
    for (int k = 0; k < 3; ++k) {
      int a = t[k], b = t[(k + 1) % 3];
      if (a < b) edges[{a, b}][0]++;
      else edges[{b, a}][1]++;
    }
  long openE = 0, nonManifoldE = 0, inconsistentE = 0;
  for (auto& [e, u] : edges) {
    int n = u[0] + u[1];
    if (n == 1) ++openE;
    else if (n > 2) ++nonManifoldE;
    else if (u[0] != 1 || u[1] != 1) ++inconsistentE;
  }
  std::vector<int> par(nv);
  std::iota(par.begin(), par.end(), 0);
  for (const auto& t : m.tris) {
    par[findp(par, t[1])] = findp(par, t[0]);
    par[findp(par, t[2])] = findp(par, t[0]);
  }
  std::map<int, int> shellOf;
  std::vector<int> triShell(nt);
  for (size_t i = 0; i < nt; ++i) {
    int r = findp(par, m.tris[i][0]);
    auto it = shellOf.emplace(r, static_cast<int>(shellOf.size())).first;
    triShell[i] = it->second;
  }
  const int ns = static_cast<int>(shellOf.size());
  struct Shell { double vol = 0, area = 0; long f = 0; std::map<int, int> v; long e = 0; double box[6] = {1e300, 1e300, 1e300, -1e300, -1e300, -1e300}; int ref = -1; };
  std::vector<Shell> sh(ns);
  for (size_t i = 0; i < nt; ++i) {
    Shell& s = sh[triShell[i]];
    const auto& t = m.tris[i];
    s.vol += dot(m.verts[t[0]], cross(m.verts[t[1]], m.verts[t[2]])) / 6.0;
    s.area += area[i];
    s.f++;
    for (int k = 0; k < 3; ++k) {
      s.v[t[k]] = 1;
      for (int d = 0; d < 3; ++d) {
        s.box[d] = std::min(s.box[d], m.verts[t[k]][d]);
        s.box[d + 3] = std::max(s.box[d + 3], m.verts[t[k]][d]);
      }
    }
  }
  {
    std::vector<long> ecount(ns, 0);
    std::map<int, int> vShell;
    for (size_t i = 0; i < nt; ++i)
      for (int k = 0; k < 3; ++k) vShell[m.tris[i][k]] = triShell[i];
    for (auto& [e, u] : edges) ecount[vShell[e.first]]++;
    for (int s = 0; s < ns; ++s) sh[s].e = ecount[s];
  }
  // match each shell to the STEP solid with the closest box
  for (auto& s : sh) {
    double best = 1e300;
    for (size_t r = 0; r < refs.size(); ++r) {
      double d = 0;
      for (int k = 0; k < 6; ++k) d += std::fabs(s.box[k] - refs[r].box[k]);
      if (d < best) best = d, s.ref = static_cast<int>(r);
    }
  }

  // ---- deviation samples
  struct Sample { V3 p; int ref; bool corner; };
  std::vector<Sample> samples;
  for (size_t i = 0; i < nt; ++i) {
    const auto& t = m.tris[i];
    const V3 &a = m.verts[t[0]], &b = m.verts[t[1]], &c = m.verts[t[2]];
    for (int u = 0; u <= subdiv; ++u)
      for (int v = 0; u + v <= subdiv; ++v) {
        int w = subdiv - u - v;
        bool corner = (u == subdiv || v == subdiv || w == subdiv);
        if (corner) continue;  // corners are measured once per vertex below
        V3 p;
        for (int d = 0; d < 3; ++d) p[d] = (u * a[d] + v * b[d] + w * c[d]) / subdiv;
        samples.push_back({p, sh[triShell[i]].ref, false});
      }
  }
  {
    std::vector<int> vref(nv, -1);
    for (size_t i = 0; i < nt; ++i)
      for (int k = 0; k < 3; ++k) vref[m.tris[i][k]] = sh[triShell[i]].ref;
    for (size_t i = 0; i < nv; ++i) samples.push_back({m.verts[i], vref[i], true});
  }
  std::vector<double> dist(samples.size(), 0);
  std::vector<std::string> surf(samples.size());
  std::atomic<size_t> next{0};
  auto work = [&] {
    BRepExtrema_DistShapeShape ds;
    int loaded = -1;
    for (size_t i; (i = next++) < samples.size();) {
      const Sample& s = samples[i];
      if (s.ref < 0) continue;
      if (loaded != s.ref) ds.LoadS2(refs[s.ref].boundary), loaded = s.ref;
      ds.LoadS1(BRepBuilderAPI_MakeVertex(gp_Pnt(s.p[0], s.p[1], s.p[2])));
      ds.Perform();
      if (!ds.IsDone() || ds.NbSolution() == 0) { dist[i] = -1; continue; }
      dist[i] = ds.Value();
      if (!s.corner) {
        TopoDS_Shape sup = ds.SupportOnShape2(1);
        if (sup.ShapeType() == TopAbs_FACE) surf[i] = surfName(GeomAdaptor_Surface(BRep_Tool::Surface(TopoDS::Face(sup))).GetType());
        else surf[i] = sup.ShapeType() == TopAbs_EDGE ? "edge" : "vertex";
      }
    }
  };
  std::vector<std::thread> th;
  for (int i = 0; i < nthreads; ++i) th.emplace_back(work);
  for (auto& t : th) t.join();
  double maxIn = 0, sumIn = 0, sq = 0, maxCorner = 0;
  long nIn = 0, failed = 0;
  std::string maxSurf;
  V3 maxP{0, 0, 0};
  std::map<std::string, double> maxBySurf;
  for (size_t i = 0; i < samples.size(); ++i) {
    if (dist[i] < 0) { ++failed; continue; }
    if (samples[i].corner) { maxCorner = std::max(maxCorner, dist[i]); continue; }
    ++nIn;
    sumIn += dist[i];
    sq += dist[i] * dist[i];
    maxBySurf[surf[i]] = std::max(maxBySurf[surf[i]], dist[i]);
    if (dist[i] > maxIn) maxIn = dist[i], maxSurf = surf[i], maxP = samples[i].p;
  }

  // ---- JSON
  double vol = 0, box[6] = {1e300, 1e300, 1e300, -1e300, -1e300, -1e300};
  for (auto& s : sh) {
    vol += s.vol;
    for (int d = 0; d < 3; ++d) box[d] = std::min(box[d], s.box[d]), box[d + 3] = std::max(box[d + 3], s.box[d + 3]);
  }
  std::printf("{\n \"file\": \"%s\", \"reference\": \"%s\", \"binary\": %s, \"bytes\": %ld,\n", argv[1], argv[2], m.binary ? "true" : "false", m.bytes);
  std::printf(" \"triangles\": %zu, \"vertices\": %zu, \"edges\": %zu, \"degenerate\": %d,\n", nt, nv, edges.size(), degenerate);
  std::printf(" \"open_edges\": %ld, \"nonmanifold_edges\": %ld, \"inconsistent_winding_edges\": %ld,\n", openE, nonManifoldE, inconsistentE);
  std::printf(" \"watertight\": %s, \"stored_normals_flipped\": %d, \"stored_normals_zero\": %d,\n",
              (openE == 0 && nonManifoldE == 0 && inconsistentE == 0) ? "true" : "false", flippedNormals, zeroNormals);
  std::printf(" \"volume\": %.4f, \"bbox\": [%.5f, %.5f, %.5f, %.5f, %.5f, %.5f],\n", vol, box[0], box[1], box[2], box[3], box[4], box[5]);
  std::printf(" \"shells\": [");
  for (int i = 0; i < ns; ++i) {
    const Shell& s = sh[i];
    long chi = static_cast<long>(s.v.size()) - s.e + s.f;
    const Ref& r = refs[s.ref];
    std::printf("%s\n  {\"triangles\": %ld, \"vertices\": %zu, \"volume\": %.4f, \"outward\": %s, \"euler\": %ld, \"genus\": %ld, \"area\": %.3f,"
                " \"bbox\": [%.5f, %.5f, %.5f, %.5f, %.5f, %.5f], \"ref_solid\": %d, \"ref_volume\": %.4f,"
                " \"ref_bbox\": [%.5f, %.5f, %.5f, %.5f, %.5f, %.5f]}",
                i ? "," : "", s.f, s.v.size(), s.vol, s.vol > 0 ? "true" : "false", chi, (2 - chi) / 2, s.area,
                s.box[0], s.box[1], s.box[2], s.box[3], s.box[4], s.box[5], s.ref, r.volume, r.box[0], r.box[1], r.box[2], r.box[3], r.box[4], r.box[5]);
  }
  std::printf("\n ],\n \"deviation\": {\"subdiv\": %d, \"samples\": %ld, \"failed\": %ld, \"max\": %.6f, \"mean\": %.6f, \"rms\": %.6f,"
              " \"max_at\": [%.3f, %.3f, %.3f], \"max_surface\": \"%s\", \"max_vertex\": %.2e, \"max_by_surface\": {",
              subdiv, nIn, failed, maxIn, nIn ? sumIn / nIn : 0, nIn ? std::sqrt(sq / nIn) : 0, maxP[0], maxP[1], maxP[2], maxSurf.c_str(), maxCorner);
  bool first = true;
  for (auto& [k, v] : maxBySurf) std::printf("%s\"%s\": %.6f", first ? "" : ", ", k.c_str(), v), first = false;
  std::printf("}}\n}\n");
  return 0;
}
