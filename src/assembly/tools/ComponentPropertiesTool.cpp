// SPDX-License-Identifier: LGPL-2.1-or-later
#include "assembly/tools/ComponentPropertiesTool.h"

#include "assembly/Bom.h"
#include "assembly/ComponentOps.h"
#include "assembly/tools/ToolCommon.h"
#include "core/AppHost.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Materials.h"
#include "core/Selection.h"
#include "core/UndoStack.h"

#include <QComboBox>
#include <QCompleter>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QWidget>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <format>

namespace fabricad::assembly {

using core::ObjectId;
using core::ToolContext;

namespace {

std::string lowerAlnum(const std::string& s) {
  std::string out;
  for (char c : s)
    if (std::isalnum(static_cast<unsigned char>(c))) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

ComponentProps propsOf(const core::Component& c) {
  return {c.partNumber, c.description, c.material, c.revision, c.vendor, c.cost};
}

ObjectId componentByName(const core::Document& doc, const std::string& name) {
  const std::string key = lowerAlnum(name);
  for (const core::Component* c : doc.components())
    if (lowerAlnum(c->name) == key || lowerAlnum(componentLabel(doc, c->id)) == key) return c->id;
  return core::kInvalidId;
}

/// Selected component, else the active component.
ObjectId targetOf(const ToolContext& ctx) {
  std::vector<ObjectId> comps = componentsOf(*ctx.document, ctx.preselection);
  if (!comps.empty()) return comps.front();
  return ctx.document->activeComponent();
}

double massOf(const core::Document& doc, ObjectId comp) {
  BomOptions opt;
  opt.root = comp;
  opt.structured = true;
  double m = 0;
  const BomTable t = buildBom(doc, opt);
  for (const BomRow& r : t.rows)
    if (r.level == 0) m += r.mass * r.quantity;
  return m;
}

}  // namespace

bool parseComponentProps(const std::vector<std::string>& words, ComponentProps& p, std::string* target, std::string* err) {
  for (const std::string& w : words) {
    const size_t eq = w.find('=');
    if (eq == std::string::npos) {
      if (err) *err = "expected key=value, got '" + w + "' (partNumber description material revision vendor cost component)";
      return false;
    }
    const std::string key = lowerAlnum(w.substr(0, eq));
    const std::string value = w.substr(eq + 1);
    if (key == "partnumber" || key == "pn" || key == "partno") p.partNumber = value;
    else if (key == "description" || key == "desc") p.description = value;
    else if (key == "material" || key == "mat") p.material = value;
    else if (key == "revision" || key == "rev") p.revision = value;
    else if (key == "vendor" || key == "supplier") p.vendor = value;
    else if (key == "cost" || key == "price") {
      char* end = nullptr;
      const double v = std::strtod(value.c_str(), &end);
      if (value.empty() || !end || *end != '\0' || v < 0) {
        if (err) *err = "cost must be a number >= 0, got '" + value + "'";
        return false;
      }
      p.cost = v;
    } else if (key == "component" || key == "comp") {
      if (target) *target = value;
    } else {
      if (err) *err = "unknown property '" + w.substr(0, eq) + "' (partNumber description material revision vendor cost component)";
      return false;
    }
  }
  return true;
}

std::unique_ptr<core::UndoCommand> makeSetComponentProps(core::Document& doc, ObjectId comp, const ComponentProps& p) {
  const core::Component* c = doc.component(comp);
  if (!c) return nullptr;
  const ComponentProps old = propsOf(*c);
  if (old.partNumber == p.partNumber && old.description == p.description && old.material == p.material &&
      old.revision == p.revision && old.vendor == p.vendor && old.cost == p.cost)
    return nullptr;
  return core::makeComponentEditCommand(
      doc, comp,
      [p](core::Component& x) {
        x.partNumber = p.partNumber;
        x.description = p.description;
        x.material = p.material;
        x.revision = p.revision;
        x.vendor = p.vendor;
        x.cost = p.cost;
      },
      "Properties of " + c->name);
}

int applyBodyMaterial(core::Document& doc, core::UndoStack* undo, const std::vector<ObjectId>& bodies, const std::string& nameIn,
                      double density) {
  const bool clear = lowerAlnum(nameIn) == "none" || nameIn.empty();
  const core::Material* lib = clear ? nullptr : core::findMaterial(nameIn);
  const std::string name = clear ? std::string() : lib ? lib->name : nameIn;
  const double rho = clear ? 0.0 : lib ? lib->density : density;
  const std::optional<std::array<float, 4>> color = lib ? std::optional(lib->color) : std::nullopt;
  std::vector<std::unique_ptr<core::UndoCommand>> cmds;
  for (ObjectId b : bodies) {
    const core::Body* body = doc.body(b);
    if (!body || (body->material == name && body->density == rho && (!color || body->color == *color))) continue;
    cmds.push_back(core::makeBodyEditCommand(
        doc, b,
        [name, rho, color](core::Body& x) {
          x.material = name;
          x.density = rho;
          if (color) x.color = *color;
        },
        name.empty() ? "Clear material of " + body->name : std::format("Material {} for {}", name, body->name)));
  }
  if (cmds.empty()) return 0;
  const int n = static_cast<int>(cmds.size());
  if (undo && n > 1) undo->beginMacro(name.empty() ? "Clear material" : "Material " + name);
  for (auto& c : cmds) {
    if (undo) undo->push(std::move(c));
    else c->redo();
  }
  if (undo && n > 1) undo->endMacro();
  return n;
}

ComponentPropertiesTool::ComponentPropertiesTool(core::ToolInfo info) : ToolBase(std::move(info)) {}

void ComponentPropertiesTool::activate(ToolContext& ctx) {
  if (!ctx.document) {
    ctx.cancel();
    return;
  }
  target_ = targetOf(ctx);
  std::vector<std::string> words;
  for (const std::string& a : ctx.args)
    if (lowerAlnum(a) != "edit") words.push_back(a);
  // "COMPPROPS <id>" from the browser menu: the component by id
  if (!words.empty() && words.front().find('=') == std::string::npos) {
    char* end = nullptr;
    const unsigned long long id = std::strtoull(words.front().c_str(), &end, 10);
    if (end && *end == '\0' && ctx.document->component(id)) {
      target_ = id;
      words.erase(words.begin());
    }
  }
  std::string named;
  ComponentProps scratch;
  std::string err;
  if (!parseComponentProps(words, scratch, &named, &err)) {
    ctx.showMessage(err, true);
    ctx.cancel();
    return;
  }
  if (!named.empty()) {
    target_ = componentByName(*ctx.document, named);
    if (target_ == core::kInvalidId) {
      ctx.showMessage("no component '" + named + "'", true);
      ctx.cancel();
      return;
    }
  }
  const core::Component* c = ctx.document->component(target_);
  if (!c) {
    ctx.cancel();
    return;
  }
  props_ = propsOf(*c);
  parseComponentProps(words, props_, nullptr, nullptr);
  if (!words.empty()) {
    // script: apply at once
    if (auto cmd = makeSetComponentProps(*ctx.document, target_, props_)) {
      if (ctx.undo) ctx.undo->push(std::move(cmd));
      else cmd->redo();
    }
    ctx.showMessage(std::format("Properties of {}: part number '{}', revision '{}', material '{}'", componentLabel(*ctx.document, target_),
                                props_.partNumber, props_.revision, props_.material));
    ctx.finish();
    return;
  }
  if (!(ctx.host && ctx.host->mainWindow())) {
    const double m = massOf(*ctx.document, target_);
    ctx.showMessage(std::format("{}: part number '{}', description '{}', material '{}', revision '{}', vendor '{}', cost {}, mass {:.3f} g",
                                componentLabel(*ctx.document, target_), props_.partNumber, props_.description, props_.material,
                                props_.revision, props_.vendor, props_.cost, m));
    ctx.finish();
    return;
  }
  ctx.setPrompt("Component Properties of " + componentLabel(*ctx.document, target_) + ": edit the fields, then OK");
  if (ctx.host) ctx.host->setToolAcceptEnabled(true);
}

void ComponentPropertiesTool::deactivate(ToolContext&) {
  pn_ = desc_ = rev_ = vendor_ = cost_ = nullptr;
  material_ = nullptr;
  mass_ = nullptr;
}

void ComponentPropertiesTool::readDialog() {
  if (pn_) props_.partNumber = pn_->text().trimmed().toStdString();
  if (desc_) props_.description = desc_->text().trimmed().toStdString();
  if (material_) props_.material = material_->currentText().trimmed().toStdString();
  if (rev_) props_.revision = rev_->text().trimmed().toStdString();
  if (vendor_) props_.vendor = vendor_->text().trimmed().toStdString();
  if (cost_) {
    bool ok = false;
    const double v = cost_->text().trimmed().toDouble(&ok);
    props_.cost = ok && v >= 0 ? v : 0.0;
  }
}

void ComponentPropertiesTool::accept(ToolContext& ctx) {
  readDialog();
  if (ctx.document)
    if (auto cmd = makeSetComponentProps(*ctx.document, target_, props_)) {
      if (ctx.undo) ctx.undo->push(std::move(cmd));
      else cmd->redo();
    }
  ctx.finish();
}

bool ComponentPropertiesTool::commandLineInput(ToolContext& ctx, const std::string& text) {
  std::string err;
  std::vector<std::string> words = core::splitCommandLine(text);
  if (words.empty()) return false;
  if (!parseComponentProps(words, props_, nullptr, &err)) {
    ctx.showMessage(err, true);
    return true;
  }
  if (pn_) pn_->setText(QString::fromStdString(props_.partNumber));
  if (desc_) desc_->setText(QString::fromStdString(props_.description));
  if (material_) material_->setCurrentText(QString::fromStdString(props_.material));
  if (rev_) rev_->setText(QString::fromStdString(props_.revision));
  if (vendor_) vendor_->setText(QString::fromStdString(props_.vendor));
  if (cost_) cost_->setText(props_.cost > 0 ? QString::number(props_.cost) : QString());
  return true;
}

QWidget* ComponentPropertiesTool::createDialog(ToolContext& ctx, QWidget* parent) {
  auto* w = new QWidget(parent);
  auto* form = new QFormLayout(w);
  auto line = [&](const std::string& value, const char* object, const char* placeholder) {
    auto* e = new QLineEdit(QString::fromStdString(value), w);
    e->setObjectName(object);
    e->setPlaceholderText(placeholder);
    return e;
  };
  pn_ = line(props_.partNumber, "compPartNumber", "e.g. BRK-0012");
  desc_ = line(props_.description, "compDescription", "e.g. Mounting bracket, left");
  material_ = new QComboBox(w);
  material_->setObjectName("compMaterial");
  material_->setEditable(true);
  material_->addItem("");
  for (const core::Material& m : core::materialLibrary()) material_->addItem(QString::fromStdString(m.name));
  material_->setCurrentText(QString::fromStdString(props_.material));
  if (material_->completer()) {
    material_->completer()->setFilterMode(Qt::MatchContains);
    material_->completer()->setCaseSensitivity(Qt::CaseInsensitive);
  }
  material_->setToolTip("BOM material text; empty: the bodies' physical materials");
  rev_ = line(props_.revision, "compRevision", "e.g. B");
  vendor_ = line(props_.vendor, "compVendor", "supplier");
  cost_ = line(props_.cost > 0 ? std::format("{}", props_.cost) : std::string(), "compCost", "unit cost");
  mass_ = new QLabel(w);
  mass_->setObjectName("compMass");
  if (ctx.document) {
    const double m = massOf(*ctx.document, target_);
    mass_->setText(m > 0 ? QString::fromStdString(std::format("{:.3f} g", m)) : QString("no density (set a Physical Material)"));
  }
  form->addRow("Part number", pn_);
  form->addRow("Description", desc_);
  form->addRow("Material", material_);
  form->addRow("Revision", rev_);
  form->addRow("Vendor", vendor_);
  form->addRow("Cost", cost_);
  form->addRow("Mass", mass_);
  return w;
}

void registerComponentPropertiesTools(core::ToolRegistry& tools) {
  {
    core::ToolInfo i;
    i.id = "assembly.component-properties";
    i.name = "Component Properties";
    i.tooltip = "Part number, description, material, revision, vendor and cost of a component (BOM and title block data)";
    i.icon = "assembly-component-properties";
    i.aliases = {"COMPPROPS", "COMPONENTPROPERTIES"};
    i.workspace = core::Workspace::Design;
    i.tab = "SOLID";
    i.group = "ASSEMBLE";
    i.order = 60;
    i.repeatable = false;
    tools.registerTool(std::make_unique<ComponentPropertiesTool>(i));
  }
  {
    core::ToolInfo i;
    i.id = "assembly.material";
    i.name = "Physical Material";
    i.tooltip = "Set the physical material (density, appearance) of the selected bodies or components";
    i.icon = "assembly-component-properties";
    i.aliases = {"MATERIAL", "MAT"};
    i.workspace = core::Workspace::Design;
    i.hidden = true;
    i.repeatable = false;
    tools.registerTool(std::make_unique<core::LambdaTool>(i, [](ToolContext& c) {
      if (!c.document) return;
      if (c.args.empty()) {
        c.showMessage("usage: MATERIAL \"Aluminum 6061\" | MATERIAL <name> density=<g/cm3> | MATERIAL none", true);
        return;
      }
      std::string name;
      double density = 0;
      for (const std::string& a : c.args) {
        if (a.rfind("density=", 0) == 0) density = std::strtod(a.c_str() + 8, nullptr);
        else name += (name.empty() ? "" : " ") + a;
      }
      if (lowerAlnum(name) != "none" && !core::findMaterial(name) && density <= 0) {
        c.showMessage("unknown material '" + name + "': give density=<g/cm3> for a custom one", true);
        return;
      }
      std::vector<ObjectId> bodies;
      auto addBodiesOf = [&](ObjectId comp) {
        for (ObjectId b : displayedBodies(*c.document, comp))
          if (std::find(bodies.begin(), bodies.end(), b) == bodies.end()) bodies.push_back(b);
      };
      for (const core::ShapeRef& r : c.preselection) {
        if (r.kind == core::ShapeKind::Component) addBodiesOf(r.owner);
        else if (c.document->body(r.owner) && std::find(bodies.begin(), bodies.end(), r.owner) == bodies.end())
          bodies.push_back(r.owner);
      }
      if (bodies.empty()) addBodiesOf(c.document->activeComponent());
      const int n = applyBodyMaterial(*c.document, c.undo, bodies, name, density);
      c.showMessage(std::format("Material {} on {} bod{}", lowerAlnum(name) == "none" ? "cleared" : "'" + name + "'", n, n == 1 ? "y" : "ies"));
    }));
  }
}

}  // namespace fabricad::assembly
