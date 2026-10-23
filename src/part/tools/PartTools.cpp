// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/tools/PartTools.h"

#include "core/AppHost.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Log.h"
#include "core/Selection.h"
#include "core/Tool.h"
#include "core/UndoStack.h"
#include "part/features/Construction.h"
#include "part/features/Extrude.h"
#include "part/features/Fillet.h"
#include "part/features/Hole.h"
#include "part/features/Import.h"
#include "part/io/IgesIO.h"
#include "part/io/MeshExport.h"
#include "part/features/Pattern.h"
#include "part/features/Revolve.h"
#include "part/features/Shell.h"
#include "part/features/SweepLoft.h"
#include "part/tools/ExportMeshTool.h"
#include "part/tools/FeatureTool.h"
#include "part/tools/MeasureTool.h"
#include "part/tools/SectionTool.h"
#include "core/ViewerContext.h"

#include <QFileDialog>
#include <QString>
#include <TopLoc_Location.hxx>

#include <cmath>
#include <cstdlib>
#include <format>
#include <memory>

namespace fabricad::part {

namespace {

core::ToolInfo info(const std::string& id, const std::string& name, const std::string& tooltip, const std::string& tab,
                    const std::string& group, int order, bool promoted, const std::string& shortcut = {},
                    std::vector<std::string> aliases = {}) {
  core::ToolInfo i;
  i.id = id;
  i.name = name;
  i.tooltip = tooltip;
  i.icon = id;
  i.shortcut = shortcut;
  i.aliases = std::move(aliases);
  i.workspace = core::Workspace::Design;
  i.tab = tab;
  i.group = group;
  i.order = order;
  i.promoted = promoted;
  return i;
}

template <class F>
std::unique_ptr<FeatureTool> featureTool(core::ToolInfo i) {
  return std::make_unique<FeatureTool>(std::move(i), F::kType, [] { return std::make_unique<F>(); });
}

/// Move/Copy (ASM-08): it moves bodies. With only components selected it hands off to the assembly
/// Move Component tool (Fusion's Move/Copy takes components too), keeping the selection.
class MoveCopyTool : public FeatureTool {
 public:
  explicit MoveCopyTool(core::ToolInfo i)
      : FeatureTool(std::move(i), MoveBodyFeature::kType, [] { return std::make_unique<MoveBodyFeature>(); }) {}

  void activate(core::ToolContext& ctx) override {
    handedOff_ = false;
    bool components = false, bodies = false;
    for (const core::ShapeRef& r : ctx.preselection) {
      if (r.kind == core::ShapeKind::Component && r.owner != core::kRootComponentId) components = true;
      else if (r.kind == core::ShapeKind::Body || r.isBrep()) bodies = true;
    }
    if (components && !bodies && ctx.manager && ctx.manager->registry().has(kComponentMove)) {
      handedOff_ = true;
      // the selection still holds the components: Move Component takes them as its preselection
      ctx.manager->run(kComponentMove);
      return;
    }
    FeatureTool::activate(ctx);
  }
  void deactivate(core::ToolContext& ctx) override {
    if (handedOff_) {
      handedOff_ = false;
      return;
    }
    FeatureTool::deactivate(ctx);
  }

 private:
  static constexpr const char* kComponentMove = "assembly.move";
  bool handedOff_ = false;
};

std::string chooseFile(core::ToolContext& ctx, bool save, const std::string& filter, const std::string& defaultSuffix) {
  if (!ctx.args.empty()) return ctx.args[0];
  QWidget* parent = ctx.host ? ctx.host->mainWindow() : nullptr;
  if (!parent) return {};
  QString path = save ? QFileDialog::getSaveFileName(parent, "Export", QString(), QString::fromStdString(filter))
                      : QFileDialog::getOpenFileName(parent, "Import", QString(), QString::fromStdString(filter));
  if (path.isEmpty()) return {};
  if (save && !defaultSuffix.empty() && !path.contains('.')) path += QString::fromStdString(defaultSuffix);
  return path.toStdString();
}

}  // namespace

bool collectExportShapes(core::ToolContext& ctx, std::vector<TopoDS_Shape>& shapes, std::vector<std::string>& names) {
  shapes.clear();
  names.clear();
  if (!ctx.document) return false;
  core::Document& doc = *ctx.document;
  std::vector<core::ObjectId> bodies;
  if (ctx.selection)
    for (const auto& r : ctx.selection->itemsOfKind(core::mask(core::ShapeKind::Body))) bodies.push_back(r.owner);
  if (bodies.empty()) bodies = doc.liveBodies(true);
  if (bodies.empty()) {
    ctx.showMessage("nothing to export: the document has no bodies", true);
    return false;
  }
  for (core::ObjectId id : bodies) {
    const TopoDS_Shape& s = doc.bodyShape(id);
    if (s.IsNull()) continue;
    const core::Body* b = doc.body(id);
    // bodies are written where the user sees them: in their component's world placement
    const gp_Trsf world = b ? doc.worldPlacement(b->component) : gp_Trsf();
    shapes.push_back(world.Form() == gp_Identity ? s : s.Moved(TopLoc_Location(world)));
    names.push_back(b ? b->name : "Body");
  }
  return !shapes.empty();
}

bool collectExportItems(core::ToolContext& ctx, std::vector<io::MeshItem>& items,
                        std::map<std::string, io::ProductProperties>* properties) {
  items.clear();
  std::vector<TopoDS_Shape> shapes;
  std::vector<std::string> names;
  if (!collectExportShapes(ctx, shapes, names)) return false;
  core::Document& doc = *ctx.document;
  std::vector<core::ObjectId> bodies;
  if (ctx.selection)
    for (const auto& r : ctx.selection->itemsOfKind(core::mask(core::ShapeKind::Body))) bodies.push_back(r.owner);
  if (bodies.empty()) bodies = doc.liveBodies(true);
  size_t k = 0;
  for (core::ObjectId id : bodies) {
    if (doc.bodyShape(id).IsNull()) continue;
    if (k >= shapes.size()) break;
    io::MeshItem it;
    it.shape = shapes[k];
    it.name = names[k];
    ++k;
    if (const core::Body* b = doc.body(id)) {
      it.color = b->color;
      const core::Component* c = doc.component(b->component);
      if (c && !c->isRoot()) it.component = c->name;
      if (c && properties) {
        io::ProductProperties p{c->partNumber, c->description, c->material.empty() ? b->material : c->material, {}};
        if (!p.empty()) (*properties)[it.name] = p;
      }
    }
    items.push_back(std::move(it));
  }
  return !items.empty();
}

bool exportBodies(core::ToolContext& ctx, const std::string& format) {
  std::vector<TopoDS_Shape> shapes;
  std::vector<std::string> names;
  if (!collectExportShapes(ctx, shapes, names)) return false;
  bool stl = format == "stl";
  if (format == "iges") {
    bool brep = true;
    std::string path;
    for (const std::string& a : ctx.args) {
      if (io::applyIgesOption(a, brep)) continue;
      if (path.empty()) path = a;
    }
    if (path.empty()) path = chooseFile(ctx, true, "IGES files (*.igs *.iges)", ".igs");
    if (path.empty()) return false;
    std::string err;
    if (!io::exportIges(shapes, path, brep, &err)) {
      ctx.showMessage("export failed: " + err, true);
      return false;
    }
    ctx.showMessage(std::format("exported {} bod{} to {} (IGES, {})", shapes.size(), shapes.size() == 1 ? "y" : "ies", path,
                                brep ? "BRep solids" : "trimmed surfaces"));
    return true;
  }
  // command line: EXPORTSTL out.stl [quality=coarse|medium|fine] [deflection=0.05] [angle=11.5|0.2rad] [ascii]
  StlExportSettings st;  // built-in defaults unless options are typed
  std::string path;
  bool pcurves = stepPcurvesDefault();  // EXPORTSTEP out.step [pcurves=off] [schema=ap242]
  std::string schema = io::stepSchemaDefault();
  for (const std::string& a : ctx.args) {
    if (stl ? ExportMeshTool::applyOption(a, st) : applyStepOption(a, pcurves, schema)) continue;
    if (path.empty()) path = a;
  }
  if (path.empty()) path = chooseFile(ctx, true, stl ? "STL files (*.stl)" : "STEP files (*.step *.stp)", stl ? ".stl" : ".step");
  if (path.empty()) return false;
  std::string err;
  bool ok = false;
  if (stl) {
    ok = exportStl(shapes, path, st.binary, st.linearDeflection, st.angularDeflectionDeg, &err);
  } else {
    StepBodyExport so;
    so.schema = schema;
    so.pcurves = pcurves;
    std::vector<io::MeshItem> items;
    collectExportItems(ctx, items, &so.properties);
    for (const auto& it : items) so.colors.push_back(it.color);
    if (so.colors.size() != shapes.size()) so.colors.clear();
    ok = exportStep(shapes, names, path, so, &err);
  }
  if (ok && stl)
    ctx.showMessage(std::format("STL mesh: linear deflection {} mm, angular deflection {} deg ({} rad), {}", st.linearDeflection,
                                st.angularDeflectionDeg, st.angularDeflectionDeg * M_PI / 180.0, st.binary ? "binary" : "ascii"));
  if (!ok) {
    ctx.showMessage("export failed: " + err, true);
    return false;
  }
  ctx.showMessage("exported " + path);
  return true;
}

void registerPartTools(core::ModuleContext& ctx) {
  core::ToolRegistry& t = ctx.tools;
  const std::string SOLID = "SOLID", CREATE = "CREATE", MODIFY = "MODIFY", CONSTRUCT = "CONSTRUCT", INSERT = "INSERT";

  // CREATE (the toolbar follows mockup v6: SKETCH PROJECT | EXTRUDE REVOLVE SWEEP LOFT HOLE |
  // FILLET CHAMFER SHELL PATTERN MIRROR; Pattern and Mirror live in MODIFY there)
  t.registerTool(featureTool<ExtrudeFeature>(info("part.extrude", "Extrude", "Extrude a profile into a solid", SOLID, CREATE, 20, true, "E", {"EXTRUDE", "EXT"})));
  t.registerTool(featureTool<RevolveFeature>(info("part.revolve", "Revolve", "Revolve a profile about an axis", SOLID, CREATE, 30, true, "", {"REVOLVE", "REV"})));
  t.registerTool(featureTool<SweepFeature>(info("part.sweep", "Sweep", "Sweep a profile along a path", SOLID, CREATE, 40, true, "", {"SWEEP"})));
  t.registerTool(featureTool<LoftFeature>(info("part.loft", "Loft", "Loft through profiles", SOLID, CREATE, 50, true, "", {"LOFT"})));
  t.registerTool(featureTool<HoleFeature>(info("part.hole", "Hole", "Simple, counterbore or countersink holes; clearance or tapped (ISO metric)", SOLID, CREATE, 60, true, "H", {"HOLE"})));
  t.registerTool(featureTool<RectangularPatternFeature>(info("part.rectangular-pattern", "Rectangular Pattern", "Repeat features, bodies or components along one or two directions", SOLID, MODIFY, 42, true, "", {"RECTPATTERN", "ARRAY", "AR"})));
  t.registerTool(featureTool<CircularPatternFeature>(info("part.circular-pattern", "Circular Pattern", "Repeat features, bodies or components about an axis", SOLID, MODIFY, 43, false, "", {"CIRCPATTERN", "POLAR"})));
  t.registerTool(featureTool<MirrorFeature>(info("part.mirror", "Mirror", "Mirror features or bodies across a plane", SOLID, MODIFY, 44, true, "", {"MIRROR", "MI"})));

  // MODIFY
  t.registerTool(std::make_unique<PressPullTool>(info("part.press-pull", "Press Pull", "Offset a face, fillet an edge or extrude a profile", SOLID, MODIFY, 10, false, "Q", {"PRESSPULL", "PP"})));
  t.registerTool(featureTool<OffsetFaceFeature>(info("part.offset-face", "Offset Face", "Move planar faces along their normal", SOLID, MODIFY, 15, false, "", {"OFFSETFACE"})));
  t.registerTool(featureTool<FilletFeature>(info("part.fillet", "Fillet", "Round edges with a constant or variable radius", SOLID, MODIFY, 20, true, "F", {"FILLET"})));
  t.registerTool(featureTool<ChamferFeature>(info("part.chamfer", "Chamfer", "Bevel edges", SOLID, MODIFY, 30, true, "", {"CHAMFER", "CHA"})));
  t.registerTool(featureTool<ShellFeature>(info("part.shell", "Shell", "Hollow a body, removing faces", SOLID, MODIFY, 40, true, "", {"SHELL"})));
  t.registerTool(featureTool<DraftFeature>(info("part.draft", "Draft", "Tilt faces by an angle", SOLID, MODIFY, 45, false, "", {"DRAFT"})));
  t.registerTool(featureTool<CombineFeature>(info("part.combine", "Combine", "Join, cut or intersect bodies", SOLID, MODIFY, 50, false, "", {"COMBINE"})));
  t.registerTool(featureTool<SplitBodyFeature>(info("part.split-body", "Split Body", "Split bodies with a plane, face or body", SOLID, MODIFY, 60, false, "", {"SPLIT", "SPLITBODY"})));
  t.registerTool(std::make_unique<MoveCopyTool>(info("part.move", "Move/Copy", "Translate, rotate (free move with typed values) or copy bodies; with components selected, moves the components", SOLID, MODIFY, 70, false, "M", {"MOVE", "M", "COPY", "CO"})));

  // INSPECT
  t.registerTool(std::make_unique<MeasureTool>());
  t.registerTool(std::make_unique<SectionTool>());
  {
    core::ToolInfo i = info("inspect.section-off", "Remove Section", "Turn the section analysis off", SOLID, "INSPECT", 3, false, "",
                            {"SECTIONOFF"});
    i.icon = "inspect-section-off";
    i.repeatable = false;
    i.keepsActiveTool = true;
    t.registerTool(std::make_unique<core::LambdaTool>(
        i,
        [](core::ToolContext& c) {
          if (c.viewer) c.viewer->setSectionPlane(std::nullopt);
          c.showMessage("Section analysis removed");
        },
        [](const core::ToolContext& c) { return c.viewer && c.viewer->sectionPlane().has_value(); }));
  }

  // CONSTRUCT
  t.registerTool(featureTool<OffsetPlaneFeature>(info("construct.offset-plane", "Offset Plane", "Plane parallel to a plane or face", SOLID, CONSTRUCT, 10, true, "", {"OFFSETPLANE", "PLANE"})));
  t.registerTool(featureTool<PlaneAtAngleFeature>(info("construct.plane-at-angle", "Plane at Angle", "Plane through a line at an angle", SOLID, CONSTRUCT, 20, true, "", {"PLANEANGLE"})));
  t.registerTool(featureTool<TangentPlaneFeature>(info("construct.tangent-plane", "Tangent Plane", "Plane tangent to a cylindrical face", SOLID, CONSTRUCT, 25, false, "", {"TANGENTPLANE"})));
  t.registerTool(featureTool<MidplaneFeature>(info("construct.midplane", "Midplane", "Plane halfway between two planes", SOLID, CONSTRUCT, 30, true, "", {"MIDPLANE"})));
  t.registerTool(featureTool<PlaneThreePointsFeature>(info("construct.plane-three-points", "Plane Through Three Points", "Plane through three points", SOLID, CONSTRUCT, 40, false, "", {"PLANE3"})));
  t.registerTool(featureTool<AxisFeature>(info("construct.axis", "Axis", "Construction axis through an edge, cylinder, two points or planes", SOLID, CONSTRUCT, 50, true, "", {"AXIS"})));
  t.registerTool(featureTool<PointFeature>(info("construct.point", "Point", "Construction point", SOLID, CONSTRUCT, 60, false, "", {"POINT"})));

  // INSERT / export
  {
    auto importTool = std::make_unique<FeatureTool>(info("part.import", "Insert STEP/IGES/STL", "Import a STEP, IGES or STL file as bodies", SOLID, INSERT, 10, true, "", {"IMPORT", "INSERT"}),
                                                    ImportFeature::kType, [] { return std::make_unique<ImportFeature>(); });
    t.registerTool(std::move(importTool));
  }
  {
    // Import with a file chooser: picks the file, then opens the Import dialog prefilled.
    core::ToolInfo i = info("part.import-file", "Import File...", "Choose a STEP/IGES/STL file to insert", SOLID, INSERT, 11, false, "", {"IMPORTFILE"});
    t.registerTool(std::make_unique<core::LambdaTool>(i, [](core::ToolContext& c) {
      if (!c.document) return;
      std::string path = chooseFile(c, false, "CAD files (*.step *.stp *.igs *.iges *.stl);;STEP (*.step *.stp);;IGES (*.igs *.iges);;STL (*.stl)", "");
      if (path.empty()) return;
      auto f = std::make_unique<ImportFeature>();
      f->prop("filePath").setString(path);
      f->setName(c.document->uniqueFeatureName(std::string("Import")));
      std::string text = f->name();
      // a STEP assembly keeps its structure: the components are created in the same undo step
      const bool prepare = f->wantsCommitPreparation();
      if (prepare && c.undo) c.undo->beginMacro(text);
      if (prepare) f->prepareCommit(*c.document, c.undo);
      auto cmd = std::make_unique<core::AddFeatureCommand>(*c.document, std::move(f));
      cmd->setText(text);
      if (c.undo) c.undo->push(std::move(cmd));
      else cmd->redo();
      if (prepare && c.undo) c.undo->endMacro();
    }));
  }
  {
    core::ToolInfo i = info("part.export-step", "Export STEP", "Export selected bodies (or all) to STEP", "TOOLS", "EXPORT", 10, true, "", {"EXPORTSTEP", "STEP"});
    i.repeatable = false;
    t.registerTool(std::make_unique<core::LambdaTool>(i, [](core::ToolContext& c) { exportBodies(c, "step"); }));
    core::ToolInfo j = info("part.export-stl", "Export STL", "Export selected bodies (or all) to STL with tessellation settings",
                            "TOOLS", "EXPORT", 20, true, "", {"EXPORTSTL", "STL"});
    j.repeatable = false;
    t.registerTool(std::make_unique<ExportMeshTool>(j));
  }
  {
    // P3 interop: one Export Mesh card for every mesh format (the format from the extension), the
    // per-format verbs on the same card, IGES and the 1:1 DXF of a face or sketch
    core::ToolInfo i = info("part.export-mesh", "Export Mesh", "Export selected bodies (or all) as STL, OBJ, glTF/GLB or 3MF (format from the file name)",
                            "TOOLS", "EXPORT", 21, true, "", {"EXPORTMESH", "MESH"});
    i.repeatable = false;
    t.registerTool(std::make_unique<ExportMeshTool>(i, std::nullopt));
    struct Fixed {
      const char* id;
      const char* name;
      const char* tip;
      const char* verb;
      io::MeshFormat format;
    };
    for (const Fixed& f : {Fixed{"part.export-obj", "Export OBJ", "Export bodies as Wavefront OBJ with an .mtl of the body colours", "EXPORTOBJ", io::MeshFormat::Obj},
                           Fixed{"part.export-gltf", "Export glTF", "Export bodies as glTF 2.0 (binary .glb or .gltf), one node per body and component", "EXPORTGLTF", io::MeshFormat::Glb},
                           Fixed{"part.export-3mf", "Export 3MF", "Export bodies as 3MF for 3D printing (mm, colour and name per object, components kept)", "EXPORT3MF", io::MeshFormat::ThreeMf}}) {
      core::ToolInfo j = info(f.id, f.name, f.tip, "TOOLS", "EXPORT", 22, false, "", {f.verb});
      j.icon = "part.export-mesh";
      j.repeatable = false;
      t.registerTool(std::make_unique<ExportMeshTool>(j, f.format));
    }
    core::ToolInfo g = info("part.export-iges", "Export IGES", "Export selected bodies (or all) to IGES (BRep solids, or trimmed surfaces with 'faces')",
                            "TOOLS", "EXPORT", 15, false, "", {"EXPORTIGES", "IGES"});
    g.repeatable = false;
    t.registerTool(std::make_unique<core::LambdaTool>(g, [](core::ToolContext& c) { exportBodies(c, "iges"); }));
    // part.export-face-dxf (EXPORTDXF in the design workspace) is registered by the drawing module: it writes via DxfWriter
  }
  {
    core::ToolInfo i = info("part.edit-feature", "Edit Feature", "Edit the selected feature", SOLID, MODIFY, 1000, false, "", {"EDITFEATURE"});
    i.hidden = true;
    i.repeatable = false;
    t.registerTool(std::make_unique<EditFeatureTool>(i));
  }
}

}  // namespace fabricad::part
