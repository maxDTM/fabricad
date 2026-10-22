// FabriCAD assembly tools: component management implementation.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "assembly/tools/ComponentTools.h"

#include "assembly/JointFeature.h"
#include "assembly/StepExport.h"
#include "assembly/tools/ToolCommon.h"
#include "core/AppHost.h"
#include "core/Commands.h"
#include "core/Json.h"
#include "core/Selection.h"
#include "core/UndoStack.h"
#include "core/ViewerContext.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QWidget>

#include <cctype>
#include <format>

namespace fabricad::assembly {

using core::Document;
using core::kInvalidId;
using core::kRootComponentId;
using core::ObjectId;
using core::ShapeKind;
using core::ShapeRef;
using core::ToolContext;
using core::ToolInfo;

// ---------------------------------------------------------------------------
// NewComponentTool
// ---------------------------------------------------------------------------

NewComponentTool::NewComponentTool()
    : ToolBase(ToolInfo{.id = "assembly.new-component",
                        .name = "New Component",
                        .tooltip = "Create a new component (optionally from selected bodies)",
                        .icon = "assembly-new-component",
                        .shortcut = "N",
                        .aliases = {"NEWCOMPONENT", "COMPONENT", "NC"},
                        .tab = "SOLID",
                        .group = "ASSEMBLE",
                        .order = 1,
                        .promoted = true}) {}

void NewComponentTool::activate(ToolContext& ctx) {
  spec_ = NewComponentSpec{};
  created_ = kInvalidId;
  if (!ctx.args.empty()) spec_.name = ctx.args.front();
  selectedBodies_.clear();
  for (const ShapeRef& r : ctx.preselection)
    if (r.kind == ShapeKind::Body && ctx.document->body(r.owner)) selectedBodies_.push_back(r.owner);
  if (!selectedBodies_.empty()) spec_.bodies = selectedBodies_;
  if (ctx.selection) ctx.selection->setFilter(core::mask(ShapeKind::Body));
  if (ctx.viewer) ctx.viewer->setSelectionFilter(core::mask(ShapeKind::Body));
  ctx.setPrompt(selectedBodies_.empty() ? "New component: enter a name, optionally select bodies to move into it"
                                        : std::format("New component from {} bod{}", selectedBodies_.size(),
                                                      selectedBodies_.size() == 1 ? "y" : "ies"));
}

void NewComponentTool::selectionChanged(ToolContext& ctx) {
  selectedBodies_.clear();
  for (const ShapeRef& r : ctx.selection->items())
    if (r.kind == ShapeKind::Body && ctx.document->body(r.owner)) selectedBodies_.push_back(r.owner);
  if (fromBodies_ && fromBodies_->isChecked()) spec_.bodies = selectedBodies_;
  refreshDialog();
}

bool NewComponentTool::commandLineInput(ToolContext& ctx, const std::string& text) {
  if (text.empty()) return false;
  spec_.name = text;
  if (nameEdit_) nameEdit_->setText(core::qs(text));
  accept(ctx);
  return true;
}

void NewComponentTool::accept(ToolContext& ctx) {
  if (nameEdit_) spec_.name = core::ss(nameEdit_->text());
  if (activate_) spec_.activate = activate_->isChecked();
  if (ground_) spec_.ground = ground_->isChecked();
  if (fromBodies_) spec_.bodies = fromBodies_->isChecked() ? selectedBodies_ : std::vector<ObjectId>{};
  created_ = createComponent(*ctx.document, ctx.undo, spec_);
  if (created_ != kInvalidId) {
    const core::Component* c = ctx.document->component(created_);
    ctx.showMessage(std::format("Created component '{}'", c ? c->name : spec_.name));
    if (spec_.activate && ctx.viewer) {
      std::vector<ObjectId> keep = descendants(*ctx.document, created_);
      keep.push_back(created_);
      ctx.viewer->setGhostedComponents(keep);
    }
  }
  ctx.finish();
}

QWidget* NewComponentTool::createDialog(ToolContext& ctx, QWidget* parent) {
  auto* w = new QWidget(parent);
  auto* form = new QFormLayout(w);
  nameEdit_ = new QLineEdit(core::qs(spec_.name), w);
  nameEdit_->setPlaceholderText("Component name (auto)");
  form->addRow("Name", nameEdit_);
  fromBodies_ = new QCheckBox("From selected bodies", w);
  fromBodies_->setChecked(!selectedBodies_.empty());
  form->addRow(fromBodies_);
  bodiesLabel_ = new QLabel(w);
  form->addRow("Bodies", bodiesLabel_);
  activate_ = new QCheckBox("Activate", w);
  activate_->setChecked(spec_.activate);
  form->addRow(activate_);
  ground_ = new QCheckBox("Ground", w);
  ground_->setChecked(spec_.ground);
  form->addRow(ground_);
  QObject::connect(fromBodies_, &QCheckBox::toggled, this, [this] { refreshDialog(); });
  (void)ctx;
  refreshDialog();
  return w;
}

void NewComponentTool::refreshDialog() {
  if (!bodiesLabel_) return;
  bodiesLabel_->setText(selectedBodies_.empty() ? "none selected" : QString::number(selectedBodies_.size()) + " selected");
  bodiesLabel_->setEnabled(fromBodies_ && fromBodies_->isChecked());
}

// ---------------------------------------------------------------------------
// RigidGroupTool
// ---------------------------------------------------------------------------

RigidGroupTool::RigidGroupTool()
    : ToolBase(ToolInfo{.id = "assembly.rigid-group",
                        .name = "Rigid Group",
                        .tooltip = "Lock the selected components together",
                        .icon = "assembly-rigid-group",
                        .aliases = {"RIGIDGROUP", "RG"},
                        .tab = "SOLID",
                        .group = "ASSEMBLE",
                        .order = 5}) {}

void RigidGroupTool::activate(ToolContext& ctx) {
  members_ = componentsOf(*ctx.document, ctx.preselection);
  ctx.selection->setFilter(ShapeKind::Component | ShapeKind::Body | ShapeKind::Face);
  if (ctx.viewer) ctx.viewer->setSelectionFilter(ctx.selection->filter());
  refresh(ctx);
}

void RigidGroupTool::selectionChanged(ToolContext& ctx) {
  members_ = componentsOf(*ctx.document, ctx.selection->items());
  refresh(ctx);
}

void RigidGroupTool::refresh(ToolContext& ctx) {
  ctx.setPrompt(members_.size() < 2 ? "Rigid group: select two or more components"
                                    : std::format("Rigid group of {} components - OK to create", members_.size()));
  if (ctx.host) ctx.host->setToolAcceptEnabled(members_.size() >= 2);
  if (label_) label_->setText(QString::number(members_.size()) + " components");
  if (ctx.viewer) {
    std::vector<ShapeRef> refs;
    for (ObjectId c : members_) refs.push_back(ShapeRef::component(c));
    ctx.viewer->setHighlighted(refs);
  }
}

void RigidGroupTool::accept(ToolContext& ctx) {
  if (members_.size() < 2) {
    ctx.showMessage("Select at least two components", true);
    return;
  }
  auto f = std::make_unique<RigidGroupFeature>();
  f->setMembers(*ctx.document, members_);
  ctx.undo->push(std::make_unique<core::AddFeatureCommand>(*ctx.document, std::move(f)));
  ctx.finish();
}

QWidget* RigidGroupTool::createDialog(ToolContext& ctx, QWidget* parent) {
  auto* w = new QWidget(parent);
  auto* form = new QFormLayout(w);
  label_ = new QLabel(QString::number(members_.size()) + " components", w);
  form->addRow("Components", label_);
  (void)ctx;
  return w;
}

// ---------------------------------------------------------------------------
// instant tool bodies
// ---------------------------------------------------------------------------

std::vector<ObjectId> setGround(ToolContext& ctx, GroundMode mode) {
  Document& doc = *ctx.document;
  std::vector<ObjectId> comps = componentsOf(doc, ctx.preselection);
  if (comps.empty() && ctx.selection) comps = componentsOf(doc, ctx.selection->items());
  if (comps.empty()) {
    ctx.showMessage("Select a component to ground", true);
    return {};
  }
  // GROUND [on|off|toggle]: explicit words win over the mode of the tool
  for (const std::string& a : ctx.args) {
    std::string w = a;
    for (char& ch : w) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (w == "on" || w == "true" || w == "1" || w == "ground") mode = GroundMode::Ground;
    else if (w == "off" || w == "false" || w == "0" || w == "unground") mode = GroundMode::Unground;
    else if (w == "toggle") mode = GroundMode::Toggle;
  }
  bool ground = true;
  if (mode == GroundMode::Unground) {
    ground = false;
  } else if (mode == GroundMode::Toggle) {
    ground = false;
    for (ObjectId c : comps)
      if (!doc.component(c)->grounded) ground = true;
  }
  std::vector<ObjectId> changed;
  for (ObjectId c : comps)
    if (doc.component(c)->grounded != ground) changed.push_back(c);
  if (changed.empty()) {
    ctx.showMessage(ground ? (comps.size() == 1 ? "Already grounded" : "All already grounded")
                           : (comps.size() == 1 ? "Already ungrounded" : "None grounded"));
    return {};
  }
  if (ctx.undo && changed.size() > 1) ctx.undo->beginMacro(ground ? "Ground" : "Unground");
  for (ObjectId c : changed) {
    auto cmd = makeGroundCommand(doc, c, ground);
    if (ctx.undo) ctx.undo->push(std::move(cmd));
    else cmd->redo();
  }
  if (ctx.undo && changed.size() > 1) ctx.undo->endMacro();
  // placements of jointed components may change now that the ground set changed
  recomputeJoints(doc);
  ctx.showMessage(ground ? "Grounded" : "Ungrounded");
  return changed;
}

std::vector<ObjectId> toggleGround(ToolContext& ctx) { return setGround(ctx, GroundMode::Toggle); }

ObjectId activateSelectedComponent(ToolContext& ctx) {
  Document& doc = *ctx.document;
  std::vector<ObjectId> comps = componentsOf(doc, ctx.preselection);
  ObjectId target = comps.empty() ? kRootComponentId : comps.front();
  if (!ctx.args.empty())
    for (const core::Component* c : doc.components())
      if (c->name == ctx.args.front()) target = c->id;
  doc.setActiveComponent(target);
  if (ctx.viewer) {
    if (target == kRootComponentId) {
      ctx.viewer->setGhostedComponents({});
    } else {
      std::vector<ObjectId> keep = descendants(doc, target);
      keep.push_back(target);
      ctx.viewer->setGhostedComponents(keep);
    }
  }
  return target;
}

std::vector<ObjectId> createComponentsFromSelectedBodies(ToolContext& ctx) {
  Document& doc = *ctx.document;
  std::vector<ObjectId> bodies;
  for (const ShapeRef& r : ctx.preselection)
    if (r.kind == ShapeKind::Body && doc.body(r.owner)) bodies.push_back(r.owner);
  if (bodies.empty()) {
    ctx.showMessage("Select bodies first", true);
    return {};
  }
  std::vector<ObjectId> created;
  if (ctx.undo) ctx.undo->beginMacro("Create components from bodies");
  for (ObjectId b : bodies) {
    const core::Body* body = doc.body(b);
    NewComponentSpec spec;
    spec.name = body->name;
    spec.parent = body->component;
    spec.bodies = {b};
    spec.activate = false;
    created.push_back(createComponent(doc, ctx.undo, spec));
  }
  if (ctx.undo) ctx.undo->endMacro();
  ctx.showMessage(std::format("Created {} component(s)", created.size()));
  return created;
}

namespace {
std::vector<ObjectId>& clipboardStorage() {
  static std::vector<ObjectId> ids;
  return ids;
}
bool isDescendantOf(const Document& doc, ObjectId node, ObjectId ancestor) {
  for (ObjectId p = node; p != kInvalidId;) {
    if (p == ancestor) return true;
    const core::Component* c = doc.component(p);
    p = c ? c->parent : kInvalidId;
  }
  return false;
}
}  // namespace

const std::vector<ObjectId>& componentClipboard() { return clipboardStorage(); }
void setComponentClipboard(std::vector<ObjectId> ids) { clipboardStorage() = std::move(ids); }

int copySelectedComponents(ToolContext& ctx) {
  if (!ctx.document) return 0;
  std::vector<ObjectId> comps = componentsOf(*ctx.document, ctx.preselection);
  if (comps.empty() && ctx.selection) comps = componentsOf(*ctx.document, ctx.selection->items());
  comps.erase(std::remove(comps.begin(), comps.end(), core::kRootComponentId), comps.end());
  if (comps.empty()) {
    ctx.showMessage("Select a component to copy", true);
    return 0;
  }
  setComponentClipboard(comps);
  ctx.showMessage(comps.size() == 1 ? std::format("Copied {}", componentLabel(*ctx.document, comps.front()))
                                    : std::format("Copied {} components", comps.size()),
                  false);
  return static_cast<int>(comps.size());
}

std::vector<ObjectId> pasteComponents(ToolContext& ctx, bool startMove) {
  std::vector<ObjectId> created;
  if (!ctx.document) return created;
  Document& doc = *ctx.document;
  std::vector<ObjectId> sources;
  for (ObjectId id : componentClipboard())
    if (doc.component(id) && id != core::kRootComponentId) sources.push_back(id);
  if (sources.empty()) {
    ctx.showMessage("Nothing to paste: copy a component first (Ctrl+C)", true);
    return created;
  }
  if (ctx.undo && sources.size() > 1) ctx.undo->beginMacro("Paste components");
  for (ObjectId src : sources) {
    const core::Component* c = doc.component(src);
    if (!c) continue;
    ObjectId definition = c->isOccurrence() ? c->definition : src;
    // parent: the active component, unless that is the source or lives inside it
    ObjectId parent = doc.activeComponent();
    if (parent == kInvalidId || !doc.component(parent) || isDescendantOf(doc, parent, definition) || isDescendantOf(doc, parent, src))
      parent = c->parent != kInvalidId ? c->parent : core::kRootComponentId;
    gp_Trsf parentWorld = doc.worldPlacement(parent);
    gp_Trsf local = parentWorld.Inverted().Multiplied(doc.worldPlacement(src));
    ObjectId id = createOccurrence(doc, ctx.undo, definition, parent, local);
    if (id != kInvalidId) created.push_back(id);
  }
  if (ctx.undo && sources.size() > 1) ctx.undo->endMacro();
  if (created.empty()) return created;
  std::vector<core::ShapeRef> refs;
  for (ObjectId id : created) refs.push_back(core::ShapeRef::component(id));
  if (ctx.selection) ctx.selection->set(refs);
  ctx.showMessage(created.size() == 1 ? std::format("Pasted {}", componentLabel(doc, created.front()))
                                      : std::format("Pasted {} occurrences", created.size()),
                  false);
  if (startMove && ctx.manager && ctx.manager->registry().has("assembly.move")) {
    // the pasted copy sits on the original: hand it to Move so the triad places it
    ctx.manager->run("assembly.move");
  }
  return created;
}

int deleteSelectedComponents(ToolContext& ctx) {
  Document& doc = *ctx.document;
  std::vector<ObjectId> comps = componentsOf(doc, ctx.preselection);
  if (comps.empty()) {
    ctx.showMessage("Select a component to delete", true);
    return 0;
  }
  int n = 0;
  std::string report;
  bool warn = false;
  if (ctx.undo && comps.size() > 1) ctx.undo->beginMacro("Delete components");
  for (ObjectId c : comps) {
    const core::Component* comp = doc.component(c);
    if (!comp) continue;
    const std::string name = comp->name;
    const core::Component* parent = doc.component(comp->parent);
    const std::string parentName = parent ? (parent->isRoot() ? "the root" : "'" + parent->name + "'") : "the root";
    DeleteComponentResult r;
    if (!deleteComponent(doc, ctx.undo, c, &r)) continue;
    ++n;
    std::string what;
    if (r.movedBodies) what += std::format("{} bod{}", r.movedBodies, r.movedBodies == 1 ? "y" : "ies");
    if (r.movedChildren) what += std::format("{}{} child component{}", what.empty() ? "" : " and ", r.movedChildren, r.movedChildren == 1 ? "" : "s");
    std::string line = std::format("Deleted component '{}'", name);
    if (r.removedFeatures) line += std::format(", removed {} joint{}", r.removedFeatures, r.removedFeatures == 1 ? "" : "s");
    // ASM-02 option (b): the bodies are kept (their features live in the timeline); say so loudly
    if (!what.empty()) {
      line += std::format(". Kept {}: moved to {}", what, parentName);
      if (r.movedBodies) line += " (delete the bodies separately if they are not needed)";
      warn = warn || r.movedBodies > 0;
    }
    report += (report.empty() ? "" : "\n") + line;
  }
  if (ctx.undo && comps.size() > 1) ctx.undo->endMacro();
  if (ctx.selection) ctx.selection->clear();
  if (!report.empty()) {
    if (warn && ctx.host) ctx.host->showMessage(report, core::MessageLevel::Warning);
    else ctx.showMessage(report);
  }
  return n;
}

ObjectId insertDesign(ToolContext& ctx, const std::string& path) {
  Document& doc = *ctx.document;
  if (path.empty()) {
    ctx.showMessage("No file selected", true);
    return kInvalidId;
  }
  // Fusion grounds the first inserted component
  bool first = doc.rootComponent().children.empty();
  QFileInfo fi(core::qs(path));
  NewComponentSpec spec;
  spec.name = core::ss(fi.completeBaseName());
  spec.parent = doc.activeComponent();
  spec.activate = false;
  spec.ground = first;
  spec.externalPath = path;
  if (ctx.undo) ctx.undo->beginMacro("Insert design");
  ObjectId comp = createComponent(doc, ctx.undo, spec);
  auto f = std::make_unique<InsertDesignFeature>();
  f->setComponent(comp);
  f->setPath(path);
  f->setName(spec.name);
  core::Feature* raw = f.get();
  if (ctx.undo) {
    ctx.undo->push(std::make_unique<core::AddFeatureCommand>(doc, std::move(f)));
    ctx.undo->endMacro();
  } else {
    doc.addFeature(std::move(f));
    doc.recompute();
  }
  if (raw->status() == core::FeatureStatus::Error) ctx.showMessage("Insert failed: " + raw->statusMessage(), true);
  else ctx.showMessage(std::format("Inserted '{}'", spec.name));
  return comp;
}

bool exportStep(ToolContext& ctx, const std::string& path) {
  Document& doc = *ctx.document;
  if (path.empty()) {
    ctx.showMessage("No file selected", true);
    return false;
  }
  StepExportOptions opt;
  std::vector<ObjectId> comps = componentsOf(doc, ctx.preselection);
  if (comps.size() == 1) opt.root = comps.front();
  std::string err;
  StepExportStats stats;
  if (!exportAssemblyStep(doc, path, opt, &err, &stats)) {
    ctx.showMessage("STEP export failed: " + err, true);
    return false;
  }
  ctx.showMessage(std::format("Exported {} part(s), {} instance(s) to {}", stats.parts, stats.instances, path));
  return true;
}

}  // namespace fabricad::assembly
