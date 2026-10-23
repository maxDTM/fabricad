// SPDX-License-Identifier: LGPL-2.1-or-later
// OCC_CATCH_SIGNALS below turns a crash inside OCCT's STEP/STL readers into a Standard_Failure
// (see ScopedOcctSignals); it must be defined before any OCCT header is included.
#ifndef OCC_CONVERT_SIGNALS
#define OCC_CONVERT_SIGNALS
#endif
#include "part/features/Import.h"

#include "core/Document.h"
#include "core/Feature.h"
#include "core/Json.h"
#include "core/Log.h"
#include "core/ShapeUtils.h"
#include "core/UndoStack.h"
#include "part/ComponentCommands.h"
#include "part/PartUtils.h"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Interface_Static.hxx>
#include <QSettings>
#include <STEPCAFControl_Reader.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <OSD.hxx>
#include <OSD_Signal.hxx>
#include <Standard_ErrorHandler.hxx>
#include <Standard_Failure.hxx>
#include <BRepBuilderAPI_MakeShapeOnMesh.hxx>
#include <Poly_Triangulation.hxx>
#include <RWStl.hxx>
#include <StlAPI_Writer.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>
#include <gp_Quaternion.hxx>
#include <TDF_Label.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDF_Tool.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <functional>
#include <mutex>

#include <csignal>
#include <pthread.h>
#include <set>

namespace fabricad::part {

namespace {

/// Damaged STEP files make OCCT's translators dereference null handles (an entity reference of the
/// wrong type: StepToGeom::MakeDirection, StepGeom_Vector::Orientation ... found by fuzzing). While
/// a reader runs, a fault signal (SIGSEGV/SIGBUS/SIGFPE/SIGILL) raised on a thread that is inside an
/// OCCT try block (OCC_CATCH_SIGNALS here or in OCCT itself) goes to OCCT's handler, which turns it
/// into a Standard_Failure: a clean "import failed" error, or OCCT skips the broken entity. Any
/// other fault (another thread, our own code) goes to the previous handler (the default, Qt's or a
/// sanitizer's) exactly as without the guard. Everything is restored afterwards; imports are
/// serialised, the handlers being process-wide. FABRICAD_OCCT_SIGNALS=0 disables the guard.
class ScopedOcctSignals {
 public:
  ScopedOcctSignals() : lock_(mutex()) {
    if (depth()++ > 0 || qEnvironmentVariable("FABRICAD_OCCT_SIGNALS") == "0") return;
    active_ = true;
    pthread_sigmask(SIG_SETMASK, nullptr, &oldMask_);
    for (int s = 1; s < kSignals; ++s) saved_[s] = sigaction(s, nullptr, &old_[s]) == 0;
    OSD::SetSignal(OSD_SignalMode_Set, false);
    for (int s = 1; s < kSignals; ++s) {
      if (!saved_[s]) continue;
      if (isFault(s)) {
        sigaction(s, nullptr, &occt()[s]);
        previous()[s] = old_[s];
        // SA_NODEFER: OCCT leaves its handler by longjmp, which does not restore the signal mask;
        // a still-blocked SIGSEGV would make the next fault kill the process
        struct sigaction a = {};
        a.sa_sigaction = &trampoline;
        sigemptyset(&a.sa_mask);
        a.sa_flags = SA_SIGINFO | SA_NODEFER | SA_ONSTACK;
        sigaction(s, &a, nullptr);
      } else {
        sigaction(s, &old_[s], nullptr);  // OSD also grabs SIGINT, SIGHUP, SIGALRM...: keep them
      }
    }
  }
  ~ScopedOcctSignals() {
    --depth();
    if (!active_) return;
    for (int s = 1; s < kSignals; ++s)
      if (saved_[s]) sigaction(s, &old_[s], nullptr);
    pthread_sigmask(SIG_SETMASK, &oldMask_, nullptr);
  }
  ScopedOcctSignals(const ScopedOcctSignals&) = delete;
  ScopedOcctSignals& operator=(const ScopedOcctSignals&) = delete;

 private:
  static constexpr int kSignals = 32;
  static bool isFault(int s) { return s == SIGSEGV || s == SIGBUS || s == SIGFPE || s == SIGILL; }
  static std::recursive_mutex& mutex() {
    static std::recursive_mutex m;
    return m;
  }
  static int& depth() {
    static int d = 0;
    return d;
  }
  static struct sigaction* occt() {
    static struct sigaction a[kSignals];
    return a;
  }
  static struct sigaction* previous() {
    static struct sigaction a[kSignals];
    return a;
  }
  static void trampoline(int sig, siginfo_t* info, void* context) {
    const struct sigaction& a = Standard_ErrorHandler::IsInTryBlock() ? occt()[sig] : previous()[sig];
    if (a.sa_flags & SA_SIGINFO) {
      a.sa_sigaction(sig, info, context);
    } else if (a.sa_handler != SIG_DFL && a.sa_handler != SIG_IGN) {
      a.sa_handler(sig);
    } else {
      signal(sig, SIG_DFL);  // the default action: returning re-executes the fault and dies
    }
  }

  std::lock_guard<std::recursive_mutex> lock_;
  struct sigaction old_[kSignals] = {};
  sigset_t oldMask_ = {};
  bool active_ = false;
  bool saved_[kSignals] = {};
};

std::string failureText(const Standard_Failure& e) {
  if (e.IsKind(STANDARD_TYPE(OSD_Signal))) return "the file is damaged (the reader crashed)";
  return e.GetMessageString() && *e.GetMessageString() ? e.GetMessageString() : "OCCT error";
}

std::string lowerExt(const std::string& path) {
  std::string ext = QFileInfo(QString::fromStdString(path)).suffix().toStdString();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext;
}

/// Closed shells read from STL become solids so booleans can work on them.
TopoDS_Shape solidify(const TopoDS_Shape& s) {
  if (s.IsNull() || hasSolid(s)) return s;
  try {
    BRepBuilderAPI_Sewing sew(1e-3);
    sew.Add(s);
    sew.Perform();
    TopoDS_Shape sewn = sew.SewedShape();
    if (sewn.IsNull()) return s;
    std::vector<TopoDS_Shape> solids;
    for (TopExp_Explorer ex(sewn, TopAbs_SHELL); ex.More(); ex.Next()) {
      TopoDS_Shell shell = TopoDS::Shell(ex.Current());
      if (!shell.Closed()) continue;
      BRepBuilderAPI_MakeSolid ms(shell);
      if (ms.IsDone()) solids.push_back(ms.Solid());
    }
    if (solids.empty()) return sewn;
    return solidsToShape(solids);
  } catch (const Standard_Failure&) {
    return s;
  }
}

bool isStepPath(const std::string& path) {
  const std::string ext = lowerExt(path);
  return ext == "step" || ext == "stp";
}

long long fileStamp(const std::string& path) {
  QFileInfo fi(QString::fromStdString(path));
  // modification time and size: a file rewritten within the timestamp granularity still changes
  return fi.exists() ? fi.lastModified().toMSecsSinceEpoch() * 1000003LL + fi.size() : -2;
}

std::string labelName(const TDF_Label& label) {
  Handle(TDataStd_Name) n;
  if (label.IsNull() || !label.FindAttribute(TDataStd_Name::GetID(), n)) return {};
  return TCollection_AsciiString(n->Get()).ToCString();  // UTF-8
}

std::string labelEntry(const TDF_Label& label) {
  TCollection_AsciiString e;
  TDF_Tool::Entry(label, e);
  return e.ToCString();
}

using StepAssembly = ImportFeature::StepAssembly;
using StepNode = ImportFeature::StepNode;

struct StepAssemblyReader {
  Handle(XCAFDoc_ShapeTool) tool;
  StepAssembly& out;

  std::string addPart(const TDF_Label& ref) {
    const std::string entry = labelEntry(ref);
    if (out.parts.count(entry)) return entry;
    ImportFeature::StepPart part;
    part.name = labelName(ref);
    TopoDS_Shape shape = XCAFDoc_ShapeTool::GetShape(ref);
    std::vector<TopoDS_Shape> solids = solidsOf(shape);
    if (solids.empty() && !shape.IsNull()) solids.push_back(shape);
    for (size_t i = 0; i < solids.size(); ++i) {
      std::string bodyName;
      TDF_Label sub;
      if (solids.size() > 1 && tool->FindSubShape(ref, solids[i], sub)) bodyName = labelName(sub);
      if (bodyName.empty())
        bodyName = solids.size() == 1 ? part.name : std::format("{} {}", part.name.empty() ? "Body" : part.name, i + 1);
      part.shapes.push_back(solids[i]);
      part.bodyNames.push_back(bodyName);
    }
    out.parts[entry] = std::move(part);
    return entry;
  }

  /// Children of an assembly label into `node` (recursively).
  void build(const TDF_Label& asmLabel, StepNode& node, int depth = 0) {
    if (depth > 64) return;
    TDF_LabelSequence comps;
    XCAFDoc_ShapeTool::GetComponents(asmLabel, comps, false);
    for (int i = 1; i <= comps.Length(); ++i) {
      const TDF_Label& c = comps.Value(i);
      TDF_Label ref;
      if (!XCAFDoc_ShapeTool::GetReferredShape(c, ref)) continue;
      const TopLoc_Location loc = XCAFDoc_ShapeTool::GetLocation(c);
      const std::string protoName = labelName(ref), instName = labelName(c);
      StepNode child;
      child.name = !protoName.empty() ? protoName : !instName.empty() ? instName : "Component";
      child.placement = loc.Transformation();
      child.prototype = labelEntry(ref);
      if (XCAFDoc_ShapeTool::IsAssembly(ref)) {
        build(ref, child, depth + 1);
        if (!child.children.empty() || !child.ownParts.empty()) node.children.push_back(std::move(child));
        continue;
      }
      const std::string entry = addPart(ref);
      // FabriCAD's own export writes the bodies of a component that also has child components as
      // an extra part instance "<name> bodies" at the identity: they belong to the component itself
      const std::string own = node.name + " bodies";
      const gp_Trsf& tr = child.placement;
      const bool identity = tr.TranslationPart().Modulus() < 1e-9 && tr.GetRotation().GetRotationAngle() < 1e-12 &&
                            std::abs(tr.ScaleFactor() - 1.0) < 1e-12;  // a STEP location is never IsIdentity()
      if (identity && !node.name.empty() && (protoName == own || instName == own)) {
        ImportFeature::StepPart& p = out.parts[entry];
        if (p.bodyNames.size() == 1 && p.bodyNames.front() == own) p.bodyNames.front() = node.name;
        node.ownParts.push_back(entry);
        continue;
      }
      child.part = entry;
      node.children.push_back(std::move(child));
    }
  }
};

}  // namespace

std::shared_ptr<const ImportFeature::StepAssembly> ImportFeature::readStepAssembly(const std::string& path, std::string* error) {
  static std::mutex mutex;
  static std::map<std::string, std::pair<long long, std::shared_ptr<const StepAssembly>>> cache;
  const long long stamp = fileStamp(path);
  if (stamp == -2) {
    if (error) *error = "file not found: " + path;
    return nullptr;
  }
  {
    std::lock_guard lock(mutex);
    auto it = cache.find(path);
    if (it != cache.end() && it->second.first == stamp) return it->second.second;
  }
  auto result = std::make_shared<StepAssembly>();
  Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
  Handle(TDocStd_Document) xdoc;
  ScopedOcctSignals signalGuard;
  try {
    OCC_CATCH_SIGNALS
    app->NewDocument("BinXCAF", xdoc);
    STEPCAFControl_Reader reader;
    reader.SetNameMode(true);
    reader.SetColorMode(false);
    reader.SetLayerMode(false);
    if (reader.ReadFile(path.c_str()) != IFSelect_RetDone) {
      if (error) *error = "could not read STEP file";
      app->Close(xdoc);
      return nullptr;
    }
    if (!reader.Transfer(xdoc)) {
      if (error) *error = "could not transfer the STEP file";
      app->Close(xdoc);
      return nullptr;
    }
    StepAssemblyReader r{XCAFDoc_DocumentTool::ShapeTool(xdoc->Main()), *result};
    TDF_LabelSequence free;
    r.tool->GetFreeShapes(free);
    if (free.Length() == 1 && XCAFDoc_ShapeTool::IsAssembly(free.First())) {
      // one top product: it is the design itself, its children become the components
      result->topName = labelName(free.First());
      result->top.name = result->topName;
      r.build(free.First(), result->top);
    } else {
      for (int i = 1; i <= free.Length(); ++i) {
        const TDF_Label& l = free.Value(i);
        if (XCAFDoc_ShapeTool::IsAssembly(l)) {
          StepNode child;
          child.name = labelName(l).empty() ? "Component" : labelName(l);
          child.prototype = labelEntry(l);
          r.build(l, child);
          if (!child.children.empty() || !child.ownParts.empty()) result->top.children.push_back(std::move(child));
        } else {
          result->top.ownParts.push_back(r.addPart(l));
        }
      }
    }
    app->Close(xdoc);
  } catch (const Standard_Failure& e) {
    if (error) *error = std::format("import failed: {}", failureText(e));
    // after a crash inside the reader the document may be inconsistent: leak it rather than close
    if (!e.IsKind(STANDARD_TYPE(OSD_Signal)) && !xdoc.IsNull()) app->Close(xdoc);
    return nullptr;
  }
  std::lock_guard lock(mutex);
  if (cache.size() > 8) cache.clear();
  cache[path] = {stamp, result};
  return result;
}

ImportFeature::ImportFeature() : PartFeature(kType) {
  addString("filePath", "").setLabel("File");
  addEnum("format", {"Auto", "STEP", "STL"}, 0).setLabel("Format");
  addBool("oneBodyPerSolid", true).setLabel("One body per solid");
  addDouble("scale", 1.0).setLabel("Scale");
  addBool("keepStructure", true).setLabel("Keep assembly structure");
  // STEP assembly import: the components created by prepareCommit and which part goes where
  core::Property structure("structure", core::PropertyType::Json);
  structure.setJson(QJsonObject());
  structure.setLabel("Assembly structure");
  structure.flags().hidden = true;
  addProperty(std::move(structure));
}

bool ImportFeature::usesStructureFile() const {
  return isStepPath(prop("filePath").toString()) && flag("keepStructure");
}

std::vector<core::ObjectId> ImportFeature::structureComponents() const {
  std::vector<core::ObjectId> out;
  if (const core::Property* p = property("structure"))
    for (const auto& v : p->json().toObject().value("components").toArray()) out.push_back(core::idFromJson(v.toObject().value("component")));
  return out;
}

bool ImportFeature::wantsCommitPreparation() const {
  if (!structureComponents().empty()) return true;  // may need a rebuild or removal
  if (!usesStructureFile()) return false;
  auto a = readStepAssembly(prop("filePath").toString(), nullptr);
  return a && a->hasStructure();
}

namespace {

gp_Trsf scaledPlacement(gp_Trsf t, double scale) {
  if (std::abs(scale - 1.0) > 1e-12 && scale > 0) t.SetTranslationPart(t.TranslationPart() * scale);
  return t;
}

}  // namespace

void ImportFeature::prepareCommit(core::Document& doc, core::UndoStack* undo) {
  const std::string path = prop("filePath").toString();
  const double scale = prop("scale").toDouble();
  const QJsonObject old = prop("structure").json().toObject();
  std::shared_ptr<const StepAssembly> a = usesStructureFile() ? readStepAssembly(path, nullptr) : nullptr;
  const bool want = a && a->hasStructure();
  const std::vector<core::ObjectId> oldComps = structureComponents();
  if (want && !oldComps.empty() && old.value("file").toString().toStdString() == path &&
      std::abs(old.value("scale").toDouble() - scale) < 1e-12 &&
      std::all_of(oldComps.begin(), oldComps.end(), [&](core::ObjectId id) { return doc.component(id) != nullptr; }))
    return;  // same file and placements: keep the components (and whatever the user did with them)
  // created parent first: remove in reverse so each removal is of a leaf
  for (auto it = oldComps.rbegin(); it != oldComps.rend(); ++it)
    if (doc.component(*it)) runOrPush(undo, std::make_unique<RemoveComponentCommand>(doc, *it, "Remove imported component"));
  if (!want) {
    prop("structure").setJson(QJsonObject());
    return;
  }
  const core::ObjectId target = component() != core::kInvalidId && doc.component(component()) ? component() : doc.activeComponent();
  QJsonArray groups, comps;
  auto add = [&](core::Component c) {
    auto cmd = std::make_unique<AddComponentCommand>(doc, std::move(c), "Add imported component");
    AddComponentCommand* raw = cmd.get();
    runOrPush(undo, std::move(cmd));
    QJsonObject o;
    o["component"] = core::idToJson(raw->id());
    comps.append(o);
    return raw->id();
  };
  auto group = [&](const std::string& part, core::ObjectId comp) {
    QJsonObject o;
    o["part"] = QString::fromStdString(part);
    o["component"] = core::idToJson(comp);
    groups.append(o);
  };
  // Inserted into a design that already has content: the file becomes one component (Fusion);
  // opened into an empty design, the file's top product is the design itself.
  core::ObjectId top = target;
  const core::Component* tc = doc.component(target);
  if (tc && (!tc->children.empty() || !doc.bodiesOf(target).empty())) {
    core::Component wrap;
    wrap.name = !a->topName.empty() ? a->topName : QFileInfo(QString::fromStdString(path)).completeBaseName().toStdString();
    wrap.parent = target;
    top = add(std::move(wrap));
  }
  std::map<std::string, core::ObjectId> leafDefinitions;  // part entry -> component holding its bodies
  std::function<void(const StepNode&, core::ObjectId)> create = [&](const StepNode& node, core::ObjectId parent) {
    for (const std::string& own : node.ownParts) group(own, parent);
    for (const StepNode& child : node.children) {
      core::Component c;
      c.name = child.name;
      c.parent = parent;
      c.placement = scaledPlacement(child.placement, scale);
      auto def = child.part.empty() ? leafDefinitions.end() : leafDefinitions.find(child.part);
      if (def != leafDefinitions.end()) c.definition = def->second;  // another instance of a part: occurrence
      const core::ObjectId id = add(std::move(c));
      if (!child.part.empty()) {
        if (def == leafDefinitions.end()) {
          leafDefinitions[child.part] = id;
          group(child.part, id);
        }
      } else {
        // sub-assemblies are copied per instance: an occurrence shows only its definition's own bodies
        create(child, id);
      }
    }
  };
  create(a->top, top);
  QJsonObject next;
  next["file"] = QString::fromStdString(path);
  next["scale"] = scale;
  next["groups"] = groups;
  next["components"] = comps;
  prop("structure").setJson(next);
}

bool ImportFeature::readFile(const std::string& path, Imported& out, std::string* error) {
  out = {};
  std::string ext = lowerExt(path);
  QFileInfo fi(QString::fromStdString(path));
  if (!fi.exists()) {
    if (error) *error = "file not found: " + path;
    return false;
  }
  std::string baseName = fi.completeBaseName().toStdString();
  ScopedOcctSignals signalGuard;
  try {
    OCC_CATCH_SIGNALS
    if (ext == "stl") {
      // StlAPI_Reader::Read without its blind trust in the numbers: OCCT loops forever (or
      // crashes) building faces from NaN / infinite / absurdly large vertex coordinates, which a
      // damaged binary STL easily contains. Check the mesh first, then build the same shape.
      Handle(Poly_Triangulation) mesh = RWStl::ReadFile(path.c_str());
      if (mesh.IsNull() || mesh->NbTriangles() == 0) {
        if (error) *error = "could not read STL file";
        return false;
      }
      constexpr double kMaxCoord = 1e8;  // mm: 100 km
      for (int i = 1; i <= mesh->NbNodes(); ++i) {
        const gp_Pnt p = mesh->Node(i);
        if (!std::isfinite(p.X()) || !std::isfinite(p.Y()) || !std::isfinite(p.Z()) || std::abs(p.X()) > kMaxCoord ||
            std::abs(p.Y()) > kMaxCoord || std::abs(p.Z()) > kMaxCoord) {
          if (error) *error = "could not read STL file: invalid vertex coordinates (damaged file?)";
          return false;
        }
      }
      BRepBuilderAPI_MakeShapeOnMesh converter(mesh);
      converter.Build();
      TopoDS_Shape s = converter.IsDone() ? converter.Shape() : TopoDS_Shape();
      if (s.IsNull()) {
        if (error) *error = "could not read STL file";
        return false;
      }
      out.shapes.push_back(solidify(s));
      out.names.push_back(baseName);
      return true;
    }
    if (ext == "step" || ext == "stp") {
      STEPControl_Reader reader;
      if (reader.ReadFile(path.c_str()) != IFSelect_RetDone) {
        if (error) *error = "could not read STEP file";
        return false;
      }
      reader.TransferRoots();
      int n = reader.NbShapes();
      if (n == 0) {
        if (error) *error = "STEP file contains no shapes";
        return false;
      }
      for (int i = 1; i <= n; ++i) {
        TopoDS_Shape s = reader.Shape(i);
        if (s.IsNull()) continue;
        std::vector<TopoDS_Shape> solids = solidsOf(s);
        if (solids.empty()) {
          out.shapes.push_back(s);
          out.names.push_back(n == 1 ? baseName : std::format("{} {}", baseName, i));
          continue;
        }
        for (size_t k = 0; k < solids.size(); ++k) {
          out.shapes.push_back(solids[k]);
          out.names.push_back(solids.size() == 1 && n == 1 ? baseName : std::format("{} {}", baseName, out.shapes.size()));
        }
      }
      return !out.shapes.empty();
    }
  } catch (const Standard_Failure& e) {
    out = {};
    if (error) *error = std::format("import failed: {}", failureText(e));
    return false;
  }
  if (error) *error = "unsupported file type: " + ext;
  return false;
}

void ImportFeature::compute(core::RecomputeContext& ctx, Outcome& out) {
  std::string path = prop("filePath").toString();
  if (path.empty()) {
    ctx.setError("choose a file to import");
    return;
  }
  const double scale = prop("scale").toDouble();
  const bool perSolid = flag("oneBodyPerSolid");
  const std::string base = name().empty() ? type() : name();
  const QJsonObject structure = prop("structure").json().toObject();
  const QJsonArray groups = structure.value("groups").toArray();
  if (usesStructureFile() && !groups.isEmpty() && structure.value("file").toString().toStdString() == path) {
    std::string err;
    std::shared_ptr<const StepAssembly> a = readStepAssembly(path, &err);
    if (!a) {
      ctx.setError(err);
      return;
    }
    const core::Document& doc = ctx.document();
    const bool scaled = std::abs(scale - 1.0) > 1e-12 && scale > 0;
    gp_Trsf st;
    if (scaled) st.SetScale(gp_Pnt(0, 0, 0), scale);
    int bodyIndex = 0;
    for (const auto& g : groups) {
      const QJsonObject o = g.toObject();
      auto it = a->parts.find(o.value("part").toString().toStdString());
      if (it == a->parts.end()) {
        ctx.setError("the STEP file changed: its assembly structure no longer matches (edit the import to rebuild it)");
        return;
      }
      core::ObjectId comp = core::idFromJson(o.value("component"));
      if (!doc.component(comp)) comp = core::kInvalidId;  // component deleted: the feature's own
      const gp_Trsf world = doc.worldPlacement(comp != core::kInvalidId ? comp : component());
      std::vector<TopoDS_Shape> parts;
      for (const TopoDS_Shape& p : it->second.shapes) parts.push_back(scaled ? copyTransformed(p, st) : p);
      std::vector<std::string> names = it->second.bodyNames;
      if (!perSolid && parts.size() > 1) {
        parts = {compoundOf(parts)};
        names = {it->second.name};
      }
      for (size_t i = 0; i < parts.size(); ++i) {
        out.addNew(parts[i], names[i], comp);
        ++bodyIndex;
        int fi2 = 0;
        for (const auto& f : facesOf(parts[i])) out.names[std::format("{}/B{}/F{}", base, bodyIndex, ++fi2)] = f;
        out.addPreview(core::transformed(parts[i], world), Operation::NewBody);
      }
    }
    return;
  }
  const long long stamp = fileStamp(path);
  if (path != cachedPath_ || stamp != cachedStamp_ || cache_.shapes.empty()) {
    std::string err;
    Imported imported;
    if (!readFile(path, imported, &err)) {
      ctx.setError(err);
      return;
    }
    cache_ = std::move(imported);
    cachedPath_ = path;
    cachedStamp_ = stamp;
  }
  std::vector<TopoDS_Shape> shapes = cache_.shapes;
  if (std::abs(scale - 1.0) > 1e-12 && scale > 0) {
    gp_Trsf t;
    t.SetScale(gp_Pnt(0, 0, 0), scale);
    for (auto& s : shapes) s = copyTransformed(s, t);
  }
  if (!perSolid && shapes.size() > 1) {
    TopoDS_Shape all = compoundOf(shapes);
    out.addNew(all, cache_.names.front());
    int fi2 = 0;
    for (const auto& f : facesOf(all)) out.names[std::format("{}/F{}", base, ++fi2)] = f;
    out.addPreview(all, Operation::NewBody);
    return;
  }
  for (size_t i = 0; i < shapes.size(); ++i) {
    out.addNew(shapes[i], cache_.names[i]);
    int fi2 = 0;
    for (const auto& f : facesOf(shapes[i])) out.names[std::format("{}/B{}/F{}", base, i + 1, ++fi2)] = f;
    out.addPreview(shapes[i], Operation::NewBody);
  }
}

// ---------------------------------------------------------------------------
// export
// ---------------------------------------------------------------------------

bool stepPcurvesDefault() { return QSettings().value("export/step/pcurves", true).toBool(); }
void setStepPcurvesDefault(bool on) { QSettings().setValue("export/step/pcurves", on); }

bool applyStepOption(const std::string& word, bool& pcurves) {
  std::string w = word;
  for (char& ch : w) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  if (w == "nopcurves" || w == "no-pcurves") {
    pcurves = false;
    return true;
  }
  if (w == "pcurves") {
    pcurves = true;
    return true;
  }
  if (!w.starts_with("pcurves=")) return false;
  const std::string v = w.substr(8);
  pcurves = !(v == "off" || v == "0" || v == "no" || v == "false");
  return true;
}

void setStepWriterPcurves(bool pcurves) { Interface_Static::SetIVal("write.surfacecurve.mode", pcurves ? 1 : 0); }

bool exportStep(const std::vector<TopoDS_Shape>& shapes, const std::vector<std::string>& names, const std::string& path,
                std::string* error, bool pcurves) {
  (void)names;  // product names need XCAF; plain writer for now
  if (shapes.empty()) {
    if (error) *error = "nothing to export";
    return false;
  }
  try {
    Interface_Static::SetCVal("write.step.schema", "AP214");
    Interface_Static::SetCVal("write.step.unit", "MM");
    setStepWriterPcurves(pcurves);
    STEPControl_Writer writer;
    for (const auto& s : shapes) {
      if (s.IsNull()) continue;
      if (writer.Transfer(s, STEPControl_AsIs) != IFSelect_RetDone) {
        if (error) *error = "STEP transfer failed";
        return false;
      }
    }
    if (writer.Write(path.c_str()) != IFSelect_RetDone) {
      if (error) *error = "could not write " + path;
      return false;
    }
    return true;
  } catch (const Standard_Failure& e) {
    if (error) *error = std::format("STEP export failed: {}", e.GetMessageString() ? e.GetMessageString() : "OCCT error");
    return false;
  }
}

bool exportStl(const std::vector<TopoDS_Shape>& shapes, const std::string& path, bool binary, double linearDeflection,
               double angularDeflectionDeg, std::string* error) {
  if (shapes.empty()) {
    if (error) *error = "nothing to export";
    return false;
  }
  try {
    TopoDS_Shape all = shapes.size() == 1 ? shapes.front() : compoundOf(shapes);
    if (all.IsNull()) {
      if (error) *error = "nothing to export";
      return false;
    }
    // Mesh a fresh copy (geometry shared, no triangulation): BRepMesh keeps an existing mesh when the
    // linear deflection is not coarser and ignores the angle then, so a body that was already
    // meshed (viewer display, an earlier export) would be written with the old tessellation.
    BRepBuilderAPI_Copy copier(all, Standard_True, Standard_False);
    TopoDS_Shape fresh = copier.IsDone() ? copier.Shape() : all;
    if (fresh.IsNull()) fresh = all;
    BRepMesh_IncrementalMesh mesh(fresh, linearDeflection, Standard_False, angularDeflectionDeg * M_PI / 180.0, Standard_True);
    (void)mesh;
    StlAPI_Writer writer;
    writer.ASCIIMode() = binary ? Standard_False : Standard_True;
    if (!writer.Write(fresh, path.c_str())) {
      if (error) *error = "could not write " + path;
      return false;
    }
    return true;
  } catch (const Standard_Failure& e) {
    if (error) *error = std::format("STL export failed: {}", e.GetMessageString() ? e.GetMessageString() : "OCCT error");
    return false;
  }
}

}  // namespace fabricad::part
