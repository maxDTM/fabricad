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
#include "part/features/Pattern.h"
#include "part/features/Revolve.h"
#include "part/features/Shell.h"
#include "part/features/SweepLoft.h"
#include "part/tools/FeatureTool.h"
#include "part/tools/MeasureTool.h"

#include <QFileDialog>
#include <QString>

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

bool exportBodies(core::ToolContext& ctx, const std::string& format) {
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
  std::vector<TopoDS_Shape> shapes;
  std::vector<std::string> names;
  for (core::ObjectId id : bodies) {
    const TopoDS_Shape& s = doc.bodyShape(id);
    if (s.IsNull()) continue;
    shapes.push_back(s);
    const core::Body* b = doc.body(id);
    names.push_back(b ? b->name : "Body");
  }
  bool stl = format == "stl";
  std::string path = chooseFile(ctx, true, stl ? "STL files (*.stl)" : "STEP files (*.step *.stp)", stl ? ".stl" : ".step");
  if (path.empty()) return false;
  std::string err;
  bool ok = stl ? exportStl(shapes, path, true, 0.05, 20.0, &err) : exportStep(shapes, names, path, &err);
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

  // CREATE
  t.registerTool(featureTool<ExtrudeFeature>(info("part.extrude", "Extrude", "Extrude a profile into a solid", SOLID, CREATE, 20, true, "E", {"EXTRUDE", "EXT"})));
  t.registerTool(featureTool<RevolveFeature>(info("part.revolve", "Revolve", "Revolve a profile about an axis", SOLID, CREATE, 30, true, "", {"REVOLVE", "REV"})));
  t.registerTool(featureTool<SweepFeature>(info("part.sweep", "Sweep", "Sweep a profile along a path", SOLID, CREATE, 40, false, "", {"SWEEP"})));
  t.registerTool(featureTool<LoftFeature>(info("part.loft", "Loft", "Loft through profiles", SOLID, CREATE, 50, false, "", {"LOFT"})));
  t.registerTool(featureTool<HoleFeature>(info("part.hole", "Hole", "Simple, counterbore or countersink holes; clearance or tapped (ISO metric)", SOLID, CREATE, 60, true, "H", {"HOLE"})));
  t.registerTool(featureTool<RectangularPatternFeature>(info("part.rectangular-pattern", "Rectangular Pattern", "Repeat features, bodies or components along one or two directions", SOLID, CREATE, 70, false, "", {"RECTPATTERN", "ARRAY", "AR"})));
  t.registerTool(featureTool<CircularPatternFeature>(info("part.circular-pattern", "Circular Pattern", "Repeat features, bodies or components about an axis", SOLID, CREATE, 71, false, "", {"CIRCPATTERN", "POLAR"})));
  t.registerTool(featureTool<MirrorFeature>(info("part.mirror", "Mirror", "Mirror features or bodies across a plane", SOLID, CREATE, 72, false, "", {"MIRROR", "MI"})));

  // MODIFY
  t.registerTool(std::make_unique<PressPullTool>(info("part.press-pull", "Press Pull", "Offset a face, fillet an edge or extrude a profile", SOLID, MODIFY, 10, true, "Q", {"PRESSPULL", "PP"})));
  t.registerTool(featureTool<OffsetFaceFeature>(info("part.offset-face", "Offset Face", "Move planar faces along their normal", SOLID, MODIFY, 15, false, "", {"OFFSETFACE"})));
  t.registerTool(featureTool<FilletFeature>(info("part.fillet", "Fillet", "Round edges with a constant or variable radius", SOLID, MODIFY, 20, true, "F", {"FILLET"})));
  t.registerTool(featureTool<ChamferFeature>(info("part.chamfer", "Chamfer", "Bevel edges", SOLID, MODIFY, 30, true, "", {"CHAMFER", "CHA"})));
  t.registerTool(featureTool<ShellFeature>(info("part.shell", "Shell", "Hollow a body, removing faces", SOLID, MODIFY, 40, true, "", {"SHELL"})));
  t.registerTool(featureTool<DraftFeature>(info("part.draft", "Draft", "Tilt faces by an angle", SOLID, MODIFY, 45, false, "", {"DRAFT"})));
  t.registerTool(featureTool<CombineFeature>(info("part.combine", "Combine", "Join, cut or intersect bodies", SOLID, MODIFY, 50, true, "", {"COMBINE"})));
  t.registerTool(featureTool<SplitBodyFeature>(info("part.split-body", "Split Body", "Split bodies with a plane, face or body", SOLID, MODIFY, 60, false, "", {"SPLIT", "SPLITBODY"})));
  t.registerTool(featureTool<MoveBodyFeature>(info("part.move", "Move/Copy", "Translate, rotate (free move with typed values) or copy bodies", SOLID, MODIFY, 70, false, "M", {"MOVE", "M", "COPY", "CO"})));

  // INSPECT
  t.registerTool(std::make_unique<MeasureTool>());

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
    auto importTool = std::make_unique<FeatureTool>(info("part.import", "Insert STEP/STL", "Import a STEP or STL file as bodies", SOLID, INSERT, 10, true, "", {"IMPORT", "INSERT"}),
                                                    ImportFeature::kType, [] { return std::make_unique<ImportFeature>(); });
    t.registerTool(std::move(importTool));
  }
  {
    // Import with a file chooser: picks the file, then opens the Import dialog prefilled.
    core::ToolInfo i = info("part.import-file", "Import File...", "Choose a STEP/STL file to insert", SOLID, INSERT, 11, false, "", {"IMPORTFILE"});
    t.registerTool(std::make_unique<core::LambdaTool>(i, [](core::ToolContext& c) {
      if (!c.document) return;
      std::string path = chooseFile(c, false, "CAD files (*.step *.stp *.stl);;STEP (*.step *.stp);;STL (*.stl)", "");
      if (path.empty()) return;
      auto f = std::make_unique<ImportFeature>();
      f->prop("filePath").setString(path);
      f->setName(c.document->uniqueFeatureName(std::string("Import")));
      std::string text = f->name();
      auto cmd = std::make_unique<core::AddFeatureCommand>(*c.document, std::move(f));
      cmd->setText(text);
      if (c.undo) c.undo->push(std::move(cmd));
      else cmd->redo();
    }));
  }
  {
    core::ToolInfo i = info("part.export-step", "Export STEP", "Export selected bodies (or all) to STEP", "TOOLS", "EXPORT", 10, true, "", {"EXPORTSTEP", "STEP"});
    i.repeatable = false;
    t.registerTool(std::make_unique<core::LambdaTool>(i, [](core::ToolContext& c) { exportBodies(c, "step"); }));
    core::ToolInfo j = info("part.export-stl", "Export STL", "Export selected bodies (or all) to STL", "TOOLS", "EXPORT", 20, true, "", {"EXPORTSTL", "STL"});
    j.repeatable = false;
    t.registerTool(std::make_unique<core::LambdaTool>(j, [](core::ToolContext& c) { exportBodies(c, "stl"); }));
  }
  {
    core::ToolInfo i = info("part.edit-feature", "Edit Feature", "Edit the selected feature", SOLID, MODIFY, 1000, false, "", {"EDITFEATURE"});
    i.hidden = true;
    i.repeatable = false;
    t.registerTool(std::make_unique<EditFeatureTool>(i));
  }
}

}  // namespace fabricad::part
