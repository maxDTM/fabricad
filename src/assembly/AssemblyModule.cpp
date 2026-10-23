// FabriCAD assembly module: feature type + tool registration.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "assembly/AssemblyModule.h"
#include "part/features/Import.h"

#include "assembly/BaseBodyFeature.h"

#include "assembly/Bom.h"
#include "assembly/ComponentOps.h"
#include "assembly/JointFeature.h"
#include "assembly/PasteNew.h"
#include "assembly/tools/ComponentPropertiesTool.h"
#include "assembly/tools/ComponentTools.h"
#include "assembly/tools/DriveJointTool.h"
#include "assembly/tools/InsertFastenerTool.h"
#include "assembly/tools/InterferenceTool.h"
#include "assembly/tools/JointTool.h"
#include "assembly/tools/MoveComponentTool.h"
#include "assembly/tools/ToolCommon.h"
#include "core/AppHost.h"
#include "core/Document.h"
#include "core/Json.h"
#include "core/Log.h"
#include "core/Selection.h"
#include "core/Tool.h"

#include <QFileDialog>
#include <QSaveFile>
#include <QWidget>

#include <algorithm>
#include <cctype>
#include <format>
#include <memory>

namespace fabricad::assembly {

using core::LambdaTool;
using core::ToolContext;
using core::ToolInfo;

namespace {

/// First command-line argument, or a file dialog when the host has a window (headless: empty).
std::string pathArgument(ToolContext& ctx, bool save, const char* title, const char* filter, const char* suffix) {
  // the first word that is not an export option (EXPORTSTEP out.step pcurves=off)
  for (const std::string& a : ctx.args) {
    bool dummy = true;
    if (!part::applyStepOption(a, dummy)) return a;
  }
  QWidget* parent = ctx.host ? ctx.host->mainWindow() : nullptr;
  if (!parent) return {};
  QString start = ctx.document && !ctx.document->filePath().empty() ? core::qs(ctx.document->filePath()) : QString();
  if (!start.isEmpty() && suffix) start = start.left(start.lastIndexOf('.')) + suffix;
  QString file = save ? QFileDialog::getSaveFileName(parent, title, start, filter)
                      : QFileDialog::getOpenFileName(parent, title, start, filter);
  return core::ss(file);
}

bool hasComponentSelection(const ToolContext& ctx) {
  if (!ctx.document || !ctx.selection) return false;
  return !componentsOf(*ctx.document, ctx.selection->items()).empty();
}

ToolInfo assembleInfo(std::string id, std::string name, std::string tooltip, int order) {
  ToolInfo i;
  i.id = std::move(id);
  i.name = std::move(name);
  i.tooltip = std::move(tooltip);
  i.icon = "assembly-" + i.id.substr(i.id.find('.') + 1);
  i.tab = "SOLID";
  i.group = "ASSEMBLE";
  i.order = order;
  i.repeatable = false;
  return i;
}

}  // namespace

void registerModule(fabricad::core::ModuleContext& ctx) {
  // --- feature types (needed to load .fcad files) ----------------------------------
  ctx.features.registerType(JointFeature::kType, [] { return std::make_unique<JointFeature>(); });
  ctx.features.registerType(RigidGroupFeature::kType, [] { return std::make_unique<RigidGroupFeature>(); });
  ctx.features.registerType(NewComponentFeature::kType, [] { return std::make_unique<NewComponentFeature>(); });
  ctx.features.registerType(PositionFeature::kType, [] { return std::make_unique<PositionFeature>(); });
  ctx.features.registerType(InsertDesignFeature::kType, [] { return std::make_unique<InsertDesignFeature>(); });
  ctx.features.registerType(BaseBodyFeature::kType, [] { return std::make_unique<BaseBodyFeature>(); });

  // --- interactive tools -------------------------------------------------------------
  ctx.tools.registerTool(std::make_unique<NewComponentTool>());
  ctx.tools.registerTool(std::make_unique<JointTool>(false));
  ctx.tools.registerTool(std::make_unique<JointTool>(true));
  ctx.tools.registerTool(std::make_unique<RigidGroupTool>());
  ctx.tools.registerTool(std::make_unique<MoveComponentTool>());
  ctx.tools.registerTool(std::make_unique<DriveJointTool>());
  ctx.tools.registerTool(std::make_unique<InterferenceTool>());
  ctx.tools.registerTool(std::make_unique<InsertFastenerTool>());  // production features P2

  // --- instant tools -------------------------------------------------------------------
  {
    ToolInfo i = assembleInfo("assembly.ground", "Ground", "Fix the selected components in space (GROUND [on|off|toggle])", 4);
    i.aliases = {"GROUND"};
    i.hidden = true;  // the command line (idempotent); the toolbar shows the toggle below
    ctx.tools.registerTool(std::make_unique<LambdaTool>(i, [](ToolContext& c) { setGround(c, GroundMode::Ground); }, hasComponentSelection));
    // ASM-09: the toolbar button is a toggle: with every selected component already grounded it
    // ungrounds them (Fusion's Ground / Unground)
    ToolInfo g = assembleInfo("assembly.ground-toggle", "Ground",
                              "Fix the selected components in space; when all of them are grounded, unground them", 4);
    g.dynamicName = [](const ToolContext& c) -> std::string {
      if (!c.document || !c.selection) return {};
      const std::vector<core::ObjectId> comps = componentsOf(*c.document, c.selection->items());
      if (comps.empty()) return {};
      for (core::ObjectId id : comps)
        if (const core::Component* comp = c.document->component(id); !comp || !comp->grounded) return {};
      return "Unground";  // every selected component is grounded: the toggle frees them
    };
    ctx.tools.registerTool(std::make_unique<LambdaTool>(g, [](ToolContext& c) { setGround(c, GroundMode::Toggle); }, hasComponentSelection));
    ToolInfo u = assembleInfo("assembly.unground", "Unground", "Free the selected components (undo Ground)", 5);
    u.aliases = {"UNGROUND"};
    u.hidden = true;  // the browser context menu and the command line
    ctx.tools.registerTool(std::make_unique<LambdaTool>(u, [](ToolContext& c) { setGround(c, GroundMode::Unground); }, hasComponentSelection));
  }
  {
    ToolInfo i = assembleInfo("assembly.activate", "Activate Component",
                              "Make the selected component the edit context (none selected: the root)", 8);
    i.aliases = {"ACTIVATE"};
    ctx.tools.registerTool(std::make_unique<LambdaTool>(i, [](ToolContext& c) { activateSelectedComponent(c); }));
  }
  {
    ToolInfo i = assembleInfo("assembly.components-from-bodies", "Create Components from Bodies",
                              "One new component per selected body", 9);
    i.aliases = {"COMPONENTSFROMBODIES", "CFB"};
    ctx.tools.registerTool(std::make_unique<LambdaTool>(
        i, [](ToolContext& c) { createComponentsFromSelectedBodies(c); },
        [](const ToolContext& c) {
          return c.document && c.selection && !c.selection->itemsOfKind(core::mask(core::ShapeKind::Body)).empty();
        }));
  }
  {
    ToolInfo i = assembleInfo("assembly.delete-component", "Delete Component",
                              "Delete the selected components (bodies and child components move to the parent, joints are removed)", 10);
    i.aliases = {"DELETECOMPONENT"};
    i.hidden = true;  // browser context menu / search only
    ctx.tools.registerTool(std::make_unique<LambdaTool>(i, [](ToolContext& c) { deleteSelectedComponents(c); }, hasComponentSelection));
  }
  {
    ToolInfo i = assembleInfo("assembly.copy", "Copy", "Copy the selected bodies or components (paste creates copies / occurrences)", 14);
    i.aliases = {"COPYCOMPONENT", "CC"};
    i.shortcut = "Ctrl+C";
    i.hidden = true;  // shortcut, browser context menu and search
    // G3 PART-05: enabled with any selection, so Ctrl+C on a body (copied as a body) or on
    // something that cannot be copied (a face: reported) is never silently ignored
    ctx.tools.registerTool(std::make_unique<LambdaTool>(
        i, [](ToolContext& c) { copySelectedComponents(c); },
        [](const ToolContext& c) { return c.document && c.selection && !c.selection->isEmpty(); }));
  }
  {
    ToolInfo i = assembleInfo("assembly.paste", "Paste Component",
                              "Insert an occurrence of each copied component and place it with the move triad", 15);
    i.aliases = {"PASTE", "PASTECOMPONENT"};
    i.shortcut = "Ctrl+V";
    i.hidden = true;
    ctx.tools.registerTool(std::make_unique<LambdaTool>(
        i, [](ToolContext& c) { pasteComponents(c, true); },
        [](const ToolContext& c) { return c.document != nullptr; }));  // empty clipboard: "Nothing to paste"
  }
  {
    ToolInfo i = assembleInfo("assembly.paste-new", "Paste New",
                              "Insert an independent copy of each copied component (own bodies and feature history)", 16);
    i.aliases = {"PASTENEW"};
    i.shortcut = "Ctrl+Shift+V";
    i.hidden = true;
    ctx.tools.registerTool(std::make_unique<LambdaTool>(
        i, [](ToolContext& c) { pasteNewComponents(c, true); },
        [](const ToolContext& c) { return c.document != nullptr; }));
  }
  {
    ToolInfo i = assembleInfo("assembly.insert-design", "Insert Design", "Insert an external .fcad part as a linked component", 11);
    i.aliases = {"INSERTDESIGN", "INSERT"};
    i.group = "INSERT";
    i.promoted = true;
    i.icon = "insert-design";
    ctx.tools.registerTool(std::make_unique<LambdaTool>(i, [](ToolContext& c) {
      std::string path = pathArgument(c, false, "Insert design", "FabriCAD designs (*.fcad)", nullptr);
      if (path.empty()) return;
      insertDesign(c, path);
    }));
  }
  {
    ToolInfo i = assembleInfo("assembly.export-step", "Export STEP (assembly)",
                              "Write the component tree (names, colours, occurrences) as a STEP file", 12);
    i.aliases = {"EXPORTSTEP", "STEP"};
    i.tab = "TOOLS";
    i.group = "EXPORT";
    i.icon = "export-step";
    ctx.tools.registerTool(std::make_unique<LambdaTool>(i, [](ToolContext& c) {
      std::string path = pathArgument(c, true, "Export STEP", "STEP files (*.step *.stp)", ".step");
      if (path.empty()) return;
      exportStep(c, path);
    }));
  }
  {
    ToolInfo i = assembleInfo("assembly.export-bom", "Export BOM (CSV)", "Write the bill of materials as a CSV file", 13);
    i.aliases = {"EXPORTBOM", "BOM"};
    i.tab = "TOOLS";
    i.group = "EXPORT";
    i.icon = "export-bom";
    ctx.tools.registerTool(std::make_unique<LambdaTool>(i, [](ToolContext& c) {
      // EXPORTBOM a.csv [flat|structured] [columns=item,name,partNumber,qty,mass,massTotal]
      BomOptions opt;
      std::vector<BomColumn> columns = defaultBomColumns();
      std::vector<std::string> rest;
      for (const std::string& a : c.args) {
        std::string low = a;
        for (char& ch : low) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (low == "flat" || low == "flattened") opt.structured = false;
        else if (low == "structured" || low == "indented") opt.structured = true;
        else if (low.rfind("columns=", 0) == 0 || low.rfind("cols=", 0) == 0) {
          std::string err;
          if (!parseBomColumns(a.substr(a.find('=') + 1), columns, &err)) {
            c.showMessage(err, true);
            return;
          }
        } else rest.push_back(a);
      }
      c.args = rest;
      std::string path = pathArgument(c, true, "Export BOM", "CSV files (*.csv)", ".csv");
      if (path.empty() || !c.document) return;
      std::vector<core::ObjectId> comps = componentsOf(*c.document, c.preselection);
      if (comps.size() == 1) opt.root = comps.front();
      const BomTable table = buildBom(*c.document, opt);
      // default flat seven-column output stays byte-identical to the original exporter
      std::string csv = !opt.structured && columns == defaultBomColumns() ? bomToCsv(table) : bomToCsv(table, columns, opt.structured);
      QSaveFile f(core::qs(path));
      if (!f.open(QIODevice::WriteOnly | QIODevice::Text) || f.write(csv.data(), static_cast<qint64>(csv.size())) < 0 ||
          !f.commit()) {
        c.showMessage("Cannot write " + path, true);
        return;
      }
      // PRODUCTION-8: a cost column with unpriced rows (standard fasteners have no price) would
      // understate the total silently: say so
      const bool costs = std::find(columns.begin(), columns.end(), BomColumn::Cost) != columns.end() ||
                         std::find(columns.begin(), columns.end(), BomColumn::CostTotal) != columns.end();
      std::string note;
      if (costs) {
        double total = 0;
        int priced = 0, unpriced = 0;
        BomOptions flat = opt;
        flat.structured = false;  // the total over the whole tree (structured quantities are per parent)
        for (const BomRow& r : (opt.structured ? buildBom(*c.document, flat) : table).rows) {
          if (r.cost > 0) {
            total += r.cost * r.quantity;
            ++priced;
          } else {
            ++unpriced;
          }
        }
        note = std::format(" (cost total {:.2f}", total);
        if (unpriced > 0) note += std::format("; {} of {} rows have no cost and are not in the total", unpriced, priced + unpriced);
        note += ")";
      }
      c.showMessage("BOM written to " + path + note);
    }));
  }
  registerComponentPropertiesTools(ctx.tools);  // production-features P1: COMPPROPS, MATERIAL
  FC_DEBUG("assembly module registered");
}

}  // namespace fabricad::assembly
