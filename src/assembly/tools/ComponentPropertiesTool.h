// FabriCAD assembly: Component Properties card (`assembly.component-properties`, verb COMPPROPS)
// and the MATERIAL verb (production-features P1 §2.2).
//
// COMPPROPS edits the BOM data of the selected component (else the active one, which may be
// the root: its part number and revision feed the title block): part number, description,
// material (picker), revision, vendor, cost, with the computed mass shown read-only. From a
// script: `COMPPROPS partNumber=BRK-12 description="Mounting bracket" material=Steel revision=B
// vendor=Acme cost=4.20 [component=<name>]` applies at once in one undo step.
// `MATERIAL "Aluminum 6061"` sets the physical material (density + appearance from the library,
// or `MATERIAL name density=1.3` for a custom one) of the selected bodies or components, else of
// every body of the active component; `MATERIAL none` clears it.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Document.h"
#include "core/Tool.h"
#include "core/UndoStack.h"

#include <QObject>
#include <QPointer>

#include <map>
#include <memory>
#include <string>
#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;

namespace fabricad::assembly {

struct ComponentProps {
  std::string partNumber, description, material, revision, vendor;
  double cost = 0.0;
};

/// Parse key=value words (partNumber, pn, description, desc, material, revision, rev, vendor,
/// cost) over `base`. Unknown keys -> false with `err`. `component=` is returned in `target`.
bool parseComponentProps(const std::vector<std::string>& words, ComponentProps& base, std::string* target,
                         std::string* err);
/// Undo command setting all fields (nullptr when nothing changes).
std::unique_ptr<core::UndoCommand> makeSetComponentProps(core::Document& doc, core::ObjectId comp, const ComponentProps& p);
/// Set the physical material of bodies (library lookup; `density` used when the name is not in
/// the library). Returns the number of bodies changed; pushes one macro on `undo`.
int applyBodyMaterial(core::Document& doc, core::UndoStack* undo, const std::vector<core::ObjectId>& bodies,
                      const std::string& name, double density);

class ComponentPropertiesTool : public QObject, public core::ToolBase {
  Q_OBJECT
 public:
  explicit ComponentPropertiesTool(core::ToolInfo info);
  void activate(core::ToolContext& ctx) override;
  void deactivate(core::ToolContext& ctx) override;
  void accept(core::ToolContext& ctx) override;
  bool commandLineInput(core::ToolContext& ctx, const std::string& text) override;
  QWidget* createDialog(core::ToolContext& ctx, QWidget* parent) override;

  core::ObjectId target() const { return target_; }
  const ComponentProps& values() const { return props_; }

 private:
  void readDialog();
  core::ObjectId target_ = core::kInvalidId;
  ComponentProps props_;
  QPointer<QLineEdit> pn_, desc_, rev_, vendor_, cost_;
  QPointer<QComboBox> material_;
  QPointer<QLabel> mass_;
};

/// Register COMPPROPS and MATERIAL (called from AssemblyModule).
void registerComponentPropertiesTools(core::ToolRegistry& tools);

}  // namespace fabricad::assembly
