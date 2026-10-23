// FabriCAD part I/O: OBJ / glTF / 3MF mesh export.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/io/MeshExport.h"

#include "part/features/Import.h"

#include <BRepBuilderAPI_Copy.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Message_ProgressRange.hxx>
#include <Poly_Triangulation.hxx>
#include <Quantity_ColorRGBA.hxx>
#include <RWGltf_CafWriter.hxx>
#include <RWObj_CafWriter.hxx>
#include <Standard_Failure.hxx>
#include <TColStd_IndexedDataMapOfStringString.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <format>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace fabricad::part::io {

namespace {

constexpr double kPi = 3.14159265358979323846;

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

TopoDS_Shape meshedCopy(const TopoDS_Shape& s, double lin, double angDeg) {
  // fresh copy (geometry shared, no triangulation): an existing coarser/finer display mesh is ignored
  BRepBuilderAPI_Copy copier(s, Standard_True, Standard_False);
  TopoDS_Shape fresh = copier.IsDone() ? copier.Shape() : s;
  if (fresh.IsNull()) fresh = s;
  BRepMesh_IncrementalMesh mesh(fresh, lin, Standard_False, angDeg * kPi / 180.0, Standard_True);
  (void)mesh;
  return fresh;
}

std::string xmlEscape(const std::string& s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&apos;"; break;
      default:
        if (static_cast<unsigned char>(c) >= 0x20 || c == '\t' || c == '\n') out += c;
    }
  }
  return out;
}

std::string hexColor(const std::array<float, 4>& c) {
  auto b = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
  return std::format("#{:02X}{:02X}{:02X}{:02X}", b(c[0]), b(c[1]), b(c[2]), b(c[3]));
}

std::string num(double v) {
  char buf[48];
  std::snprintf(buf, sizeof buf, "%.6g", std::abs(v) < 1e-12 ? 0.0 : v);
  return buf;
}

void put16(std::string& o, unsigned v) {
  o += static_cast<char>(v & 0xff);
  o += static_cast<char>((v >> 8) & 0xff);
}
void put32(std::string& o, unsigned v) {
  put16(o, v & 0xffff);
  put16(o, (v >> 16) & 0xffff);
}
unsigned get16(const std::string& s, size_t at) {
  return static_cast<unsigned char>(s[at]) | (static_cast<unsigned char>(s[at + 1]) << 8);
}
unsigned get32(const std::string& s, size_t at) { return get16(s, at) | (get16(s, at + 2) << 16); }

/// OBJ / glTF through XCAF: one label per body (name, surface colour), one assembly per component.
bool exportCaf(const std::vector<MeshItem>& items, const std::string& path, MeshFormat format, const MeshExportOptions& opt,
               std::string* error, MeshExportStats& stats) {
  Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
  Handle(TDocStd_Document) doc;
  app->NewDocument("BinXCAF", doc);
  Handle(XCAFDoc_ShapeTool) shapes = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
  Handle(XCAFDoc_ColorTool) colors = XCAFDoc_DocumentTool::ColorTool(doc->Main());
  std::map<std::string, TDF_Label> assemblies;
  for (const MeshItem& it : items) {
    if (it.shape.IsNull()) continue;
    TopoDS_Shape m = meshedCopy(it.shape, opt.linearDeflection, opt.angularDeflectionDeg);
    for (TopExp_Explorer ex(m, TopAbs_FACE); ex.More(); ex.Next()) {
      TopLoc_Location loc;
      Handle(Poly_Triangulation) t = BRep_Tool::Triangulation(TopoDS::Face(ex.Current()), loc);
      if (!t.IsNull()) {
        stats.triangles += t->NbTriangles();
        stats.vertices += t->NbNodes();
      }
    }
    // a free body is a located top-level shape; a component's body is a part (no location)
    // instanced in the component's assembly with the placement
    TDF_Label part;
    TopLoc_Location loc = m.Location();
    if (it.component.empty()) {
      part = shapes->AddShape(m, Standard_False);
      TDataStd_Name::Set(part, TCollection_ExtendedString(it.name.c_str(), true));
      colors->SetColor(part, Quantity_ColorRGBA(it.color[0], it.color[1], it.color[2], it.color[3]), XCAFDoc_ColorSurf);
    } else {
      part = shapes->AddShape(m.Located(TopLoc_Location()), Standard_False);
      TDataStd_Name::Set(part, TCollection_ExtendedString(it.name.c_str(), true));
      colors->SetColor(part, Quantity_ColorRGBA(it.color[0], it.color[1], it.color[2], it.color[3]), XCAFDoc_ColorSurf);
      auto a = assemblies.find(it.component);
      if (a == assemblies.end()) {
        TDF_Label asmLabel = shapes->NewShape();
        TDataStd_Name::Set(asmLabel, TCollection_ExtendedString(it.component.c_str(), true));
        a = assemblies.emplace(it.component, asmLabel).first;
      }
      TDF_Label inst = shapes->AddComponent(a->second, part, loc);
      TDataStd_Name::Set(inst, TCollection_ExtendedString(it.name.c_str(), true));
    }
    ++stats.objects;
  }
  shapes->UpdateAssemblies();
  TColStd_IndexedDataMapOfStringString info;
  if (!opt.title.empty()) info.Add("Title", opt.title.c_str());
  if (!opt.designer.empty()) info.Add("Author", opt.designer.c_str());
  info.Add("Generator", "FabriCAD");
  bool ok = false;
  if (format == MeshFormat::Obj) {
    RWObj_CafWriter w(path.c_str());
    w.ChangeCoordinateSystemConverter().SetInputLengthUnit(0.001);
    w.ChangeCoordinateSystemConverter().SetOutputLengthUnit(0.001);
    if (opt.yUp) {
      w.ChangeCoordinateSystemConverter().SetInputCoordinateSystem(RWMesh_CoordinateSystem_Zup);
      w.ChangeCoordinateSystemConverter().SetOutputCoordinateSystem(RWMesh_CoordinateSystem_Yup);
    }
    ok = w.Perform(doc, info, Message_ProgressRange());
  } else {
    RWGltf_CafWriter w(path.c_str(), format == MeshFormat::Glb);
    // glTF: metres, Y up; FabriCAD: millimetres, Z up
    w.ChangeCoordinateSystemConverter().SetInputLengthUnit(0.001);
    w.ChangeCoordinateSystemConverter().SetInputCoordinateSystem(RWMesh_CoordinateSystem_Zup);
    w.SetNodeNameFormat(RWMesh_NameFormat_InstanceOrProduct);
    w.SetMeshNameFormat(RWMesh_NameFormat_Product);
    ok = w.Perform(doc, info, Message_ProgressRange());
  }
  if (!ok && error) *error = "could not write " + path;
  return ok;
}

bool export3mf(const std::vector<MeshItem>& items, const std::string& path, const MeshExportOptions& opt, std::string* error,
               MeshExportStats& stats) {
  std::vector<IndexedMesh> meshes;
  for (const MeshItem& it : items) {
    meshes.push_back(triangulate(it.shape, opt.linearDeflection, opt.angularDeflectionDeg));
    stats.triangles += static_cast<long long>(meshes.back().triangles.size());
    stats.vertices += static_cast<long long>(meshes.back().vertices.size());
    ++stats.objects;
  }
  const std::string types =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
      "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
      "<Default Extension=\"model\" ContentType=\"application/vnd.ms-package.3dmanufacturing-3dmodel+xml\"/>"
      "</Types>\n";
  const std::string rels =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
      "<Relationship Target=\"/3D/3dmodel.model\" Id=\"rel0\" "
      "Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\"/>"
      "</Relationships>\n";
  if (!writeZip(path, {{"[Content_Types].xml", types}, {"_rels/.rels", rels}, {"3D/3dmodel.model", threeMfModelXml(items, meshes, opt)}})) {
    if (error) *error = "could not write " + path;
    return false;
  }
  return true;
}

}  // namespace

std::optional<MeshFormat> meshFormatFromPath(const std::string& path) {
  const std::string ext = lower(QFileInfo(QString::fromStdString(path)).suffix().toStdString());
  if (ext == "stl") return MeshFormat::Stl;
  if (ext == "obj") return MeshFormat::Obj;
  if (ext == "gltf") return MeshFormat::Gltf;
  if (ext == "glb") return MeshFormat::Glb;
  if (ext == "3mf") return MeshFormat::ThreeMf;
  return std::nullopt;
}

const char* meshFormatName(MeshFormat f) {
  switch (f) {
    case MeshFormat::Stl: return "STL";
    case MeshFormat::Obj: return "OBJ";
    case MeshFormat::Gltf: return "glTF";
    case MeshFormat::Glb: return "GLB";
    case MeshFormat::ThreeMf: return "3MF";
  }
  return "?";
}

IndexedMesh triangulate(const TopoDS_Shape& shape, double lin, double angDeg) {
  IndexedMesh out;
  if (shape.IsNull()) return out;
  TopoDS_Shape m = meshedCopy(shape, lin, angDeg);
  struct Key {
    long long x, y, z;
    bool operator==(const Key&) const = default;
  };
  struct KeyHash {
    size_t operator()(const Key& k) const {
      return std::hash<long long>()(k.x * 73856093LL ^ k.y * 19349663LL ^ k.z * 83492791LL);
    }
  };
  std::unordered_map<Key, int, KeyHash> index;
  for (TopExp_Explorer ex(m, TopAbs_FACE); ex.More(); ex.Next()) {
    const TopoDS_Face& f = TopoDS::Face(ex.Current());
    TopLoc_Location loc;
    Handle(Poly_Triangulation) t = BRep_Tool::Triangulation(f, loc);
    if (t.IsNull()) continue;
    const gp_Trsf tr = loc.Transformation();
    std::vector<int> map(static_cast<size_t>(t->NbNodes()) + 1, -1);
    for (int i = 1; i <= t->NbNodes(); ++i) {
      gp_Pnt p = t->Node(i).Transformed(tr);
      // merge nodes closer than 1e-6 mm: the edge nodes of neighbouring faces are the same points
      Key k{std::llround(p.X() * 1e6), std::llround(p.Y() * 1e6), std::llround(p.Z() * 1e6)};
      auto [it, inserted] = index.emplace(k, static_cast<int>(out.vertices.size()));
      if (inserted) out.vertices.push_back({p.X(), p.Y(), p.Z()});
      map[static_cast<size_t>(i)] = it->second;
    }
    const bool rev = f.Orientation() == TopAbs_REVERSED;
    for (int i = 1; i <= t->NbTriangles(); ++i) {
      int a, b, c;
      t->Triangle(i).Get(a, b, c);
      if (rev) std::swap(b, c);
      std::array<int, 3> tri{map[static_cast<size_t>(a)], map[static_cast<size_t>(b)], map[static_cast<size_t>(c)]};
      if (tri[0] == tri[1] || tri[1] == tri[2] || tri[0] == tri[2]) continue;  // collapsed by the merge
      out.triangles.push_back(tri);
    }
  }
  return out;
}

std::string threeMfModelXml(const std::vector<MeshItem>& items, const std::vector<IndexedMesh>& meshes, const MeshExportOptions& opt) {
  std::ostringstream x;
  x << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
       "<model unit=\"millimeter\" xml:lang=\"en-US\" xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\">\n";
  if (!opt.title.empty()) x << " <metadata name=\"Title\">" << xmlEscape(opt.title) << "</metadata>\n";
  if (!opt.designer.empty()) x << " <metadata name=\"Designer\">" << xmlEscape(opt.designer) << "</metadata>\n";
  x << " <metadata name=\"Application\">FabriCAD</metadata>\n";
  x << " <resources>\n  <basematerials id=\"1\">\n";
  for (const MeshItem& it : items) x << "   <base name=\"" << xmlEscape(it.name) << "\" displaycolor=\"" << hexColor(it.color) << "\"/>\n";
  x << "  </basematerials>\n";
  // mesh objects 2..n+1; component objects after them
  std::vector<std::string> order;  // component names in first-seen order
  std::map<std::string, std::vector<int>> members;
  for (size_t i = 0; i < items.size(); ++i) {
    const int id = static_cast<int>(i) + 2;
    const IndexedMesh& m = meshes[i];
    x << "  <object id=\"" << id << "\" type=\"model\" name=\"" << xmlEscape(items[i].name) << "\" pid=\"1\" pindex=\"" << i << "\">\n"
      << "   <mesh>\n    <vertices>\n";
    for (const auto& v : m.vertices) x << "     <vertex x=\"" << num(v[0]) << "\" y=\"" << num(v[1]) << "\" z=\"" << num(v[2]) << "\"/>\n";
    x << "    </vertices>\n    <triangles>\n";
    for (const auto& t : m.triangles) x << "     <triangle v1=\"" << t[0] << "\" v2=\"" << t[1] << "\" v3=\"" << t[2] << "\"/>\n";
    x << "    </triangles>\n   </mesh>\n  </object>\n";
    if (!items[i].component.empty()) {
      if (!members.count(items[i].component)) order.push_back(items[i].component);
      members[items[i].component].push_back(id);
    }
  }
  std::vector<int> buildItems;
  int nextId = static_cast<int>(items.size()) + 2;
  std::map<std::string, int> compIds;
  for (const std::string& c : order) {
    const int id = nextId++;
    compIds[c] = id;
    x << "  <object id=\"" << id << "\" type=\"model\" name=\"" << xmlEscape(c) << "\">\n   <components>\n";
    for (int m : members[c]) x << "    <component objectid=\"" << m << "\"/>\n";
    x << "   </components>\n  </object>\n";
  }
  x << " </resources>\n <build>\n";
  std::map<std::string, bool> emitted;
  for (size_t i = 0; i < items.size(); ++i) {
    if (items[i].component.empty()) {
      x << "  <item objectid=\"" << (i + 2) << "\"/>\n";
    } else if (!emitted[items[i].component]) {
      emitted[items[i].component] = true;
      x << "  <item objectid=\"" << compIds[items[i].component] << "\"/>\n";
    }
  }
  x << " </build>\n</model>\n";
  return x.str();
}

unsigned int crc32(const std::string& data) {
  static unsigned int table[256] = {0};
  static bool init = false;
  if (!init) {
    for (unsigned int i = 0; i < 256; ++i) {
      unsigned int c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
    init = true;
  }
  unsigned int c = 0xFFFFFFFFu;
  for (unsigned char b : data) c = table[(c ^ b) & 0xff] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

bool writeZip(const std::string& path, const std::vector<std::pair<std::string, std::string>>& members) {
  std::string out, central;
  constexpr unsigned kDosTime = 0, kDosDate = (46u << 9) | (1u << 5) | 1u;  // 2026-01-01: deterministic output
  for (const auto& [name, data] : members) {
    if (data.size() > 0xFFFFFFF0ull) return false;  // no ZIP64
    const unsigned crc = crc32(data);
    const unsigned offset = static_cast<unsigned>(out.size());
    const unsigned size = static_cast<unsigned>(data.size());
    put32(out, 0x04034b50); put16(out, 20); put16(out, 0); put16(out, 0); put16(out, kDosTime); put16(out, kDosDate);
    put32(out, crc); put32(out, size); put32(out, size); put16(out, static_cast<unsigned>(name.size())); put16(out, 0);
    out += name;
    out += data;
    put32(central, 0x02014b50); put16(central, 20); put16(central, 20); put16(central, 0); put16(central, 0);
    put16(central, kDosTime); put16(central, kDosDate); put32(central, crc); put32(central, size); put32(central, size);
    put16(central, static_cast<unsigned>(name.size())); put16(central, 0); put16(central, 0); put16(central, 0); put16(central, 0);
    put32(central, 0); put32(central, offset);
    central += name;
  }
  const unsigned cdOffset = static_cast<unsigned>(out.size());
  out += central;
  put32(out, 0x06054b50); put16(out, 0); put16(out, 0); put16(out, static_cast<unsigned>(members.size()));
  put16(out, static_cast<unsigned>(members.size())); put32(out, static_cast<unsigned>(central.size())); put32(out, cdOffset); put16(out, 0);
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(out.data(), static_cast<std::streamsize>(out.size()));
  return static_cast<bool>(f);
}

std::map<std::string, std::string> readZipStored(const std::string& path) {
  std::map<std::string, std::string> out;
  std::ifstream f(path, std::ios::binary);
  if (!f) return out;
  std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  size_t at = 0;
  while (at + 30 <= s.size() && get32(s, at) == 0x04034b50) {
    const unsigned method = get16(s, at + 8), crc = get32(s, at + 14), csize = get32(s, at + 18);
    const unsigned nlen = get16(s, at + 26), xlen = get16(s, at + 28);
    if (at + 30 + nlen + xlen + csize > s.size() || method != 0) return {};
    std::string name = s.substr(at + 30, nlen);
    std::string data = s.substr(at + 30 + nlen + xlen, csize);
    if (crc32(data) != crc) return {};
    out[name] = std::move(data);
    at += 30 + nlen + xlen + csize;
  }
  return out;
}

bool exportMesh(const std::vector<MeshItem>& items, const std::string& path, MeshFormat format, const MeshExportOptions& opt,
                std::string* error, MeshExportStats* statsOut) {
  if (items.empty()) {
    if (error) *error = "nothing to export";
    return false;
  }
  MeshExportStats stats;
  try {
    bool ok = false;
    if (format == MeshFormat::Stl) {
      std::vector<TopoDS_Shape> shapes;
      for (const auto& it : items) shapes.push_back(it.shape);
      ok = exportStl(shapes, path, opt.binaryStl, opt.linearDeflection, opt.angularDeflectionDeg, error);
      stats.objects = static_cast<int>(items.size());
    } else if (format == MeshFormat::ThreeMf) {
      ok = export3mf(items, path, opt, error, stats);
    } else {
      ok = exportCaf(items, path, format, opt, error, stats);
    }
    if (ok && statsOut) *statsOut = stats;
    return ok;
  } catch (const Standard_Failure& e) {
    if (error) *error = std::format("{} export failed: {}", meshFormatName(format), e.GetMessageString() ? e.GetMessageString() : "OCCT error");
    return false;
  }
}

}  // namespace fabricad::part::io
