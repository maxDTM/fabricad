// FabriCAD assembly tools: component management (New Component, Ground, Activate, Create
// Components from Bodies, Delete, Insert Design, Rigid Group, Export STEP).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "assembly/ComponentOps.h"
#include "core/Tool.h"

#include <QObject>

#include <memory>
#include <string>
#include <vector>

class QCheckBox;
class QLineEdit;
class QComboBox;
class QLabel;

namespace fabricad::assembly {

/// New Component (N): name, optional bodies to move in, activate, ground.
class NewComponentTool : public QObject, public core::ToolBase {
  Q_OBJECT
 public:
  NewComponentTool();
  void activate(core::ToolContext& ctx) override;
  void accept(core::ToolContext& ctx) override;
  void selectionChanged(core::ToolContext& ctx) override;
  bool commandLineInput(core::ToolContext& ctx, const std::string& text) override;
  QWidget* createDialog(core::ToolContext& ctx, QWidget* parent) override;

  NewComponentSpec& spec() { return spec_; }
  core::ObjectId createdComponent() const { return created_; }

 private:
  void refreshDialog();
  NewComponentSpec spec_;
  core::ObjectId created_ = core::kInvalidId;
  QLineEdit* nameEdit_ = nullptr;
  QCheckBox* fromBodies_ = nullptr;
  QCheckBox* activate_ = nullptr;
  QCheckBox* ground_ = nullptr;
  QLabel* bodiesLabel_ = nullptr;
  std::vector<core::ObjectId> selectedBodies_;
};

/// Instant tools built from lambdas live in AssemblyModule.cpp; these need a bit of state.

/// Rigid Group: select two or more components, OK locks them together.
class RigidGroupTool : public core::ToolBase {
 public:
  RigidGroupTool();
  void activate(core::ToolContext& ctx) override;
  void accept(core::ToolContext& ctx) override;
  void selectionChanged(core::ToolContext& ctx) override;
  QWidget* createDialog(core::ToolContext& ctx, QWidget* parent) override;
  const std::vector<core::ObjectId>& members() const { return members_; }

 private:
  void refresh(core::ToolContext& ctx);
  std::vector<core::ObjectId> members_;
  QLabel* label_ = nullptr;
};

// --- instant tool bodies (shared by the lambda registrations and the tests) --------------
enum class GroundMode { Ground, Unground, Toggle };
/// Ground / unground the selected components (bodies select their component). An argument
/// `on`, `off` or `toggle` overrides `mode`. Returns the ids whose state changed (a message says
/// "Already grounded" when nothing did).
std::vector<core::ObjectId> setGround(core::ToolContext& ctx, GroundMode mode);
/// setGround(ctx, Toggle).
std::vector<core::ObjectId> toggleGround(core::ToolContext& ctx);
/// Activate the selected component (root when nothing selected). Returns the activated id.
core::ObjectId activateSelectedComponent(core::ToolContext& ctx);
/// One component per selected body. Returns the new ids.
std::vector<core::ObjectId> createComponentsFromSelectedBodies(core::ToolContext& ctx);
/// Delete the selected components. Returns how many were removed.
int deleteSelectedComponents(core::ToolContext& ctx);
/// Insert an external .fcad as a new (grounded when first) component. Returns the component id.
core::ObjectId insertDesign(core::ToolContext& ctx, const std::string& path);
/// Export the assembly (or selected components' sub-trees) to STEP. Returns success.
bool exportStep(core::ToolContext& ctx, const std::string& path);

// --- copy / paste (Ctrl+C / Ctrl+V) --------------------------------------------------------
/// Components copied last (ids in this document; stale ids are skipped on paste).
const std::vector<core::ObjectId>& componentClipboard();
void setComponentClipboard(std::vector<core::ObjectId> ids);
/// Copy the selected components (bodies select their component). Returns how many.
int copySelectedComponents(core::ToolContext& ctx);
/// Paste: one new occurrence per copied component (of its definition), under the active
/// component, at the source's world placement. The new occurrences become the selection and,
/// when `startMove` is set and a tool manager is available, the Move tool starts on them so the
/// copy can be dragged into place with the triad. Returns the new component ids.
std::vector<core::ObjectId> pasteComponents(core::ToolContext& ctx, bool startMove = true);

/// Bodies copied last (G3 PART-05): Ctrl+C on bodies of the active component copies the bodies,
/// not their component. Copying components clears it and vice versa.
const std::vector<core::ObjectId>& bodyClipboard();
void setBodyClipboard(std::vector<core::ObjectId> ids);
/// True when either clipboard holds something that still exists in `doc`.
bool clipboardHasContent(const core::Document& doc);
/// Paste / Paste New of copied bodies: one frozen copy (BaseBody feature) per body in the active
/// component, at the source's world position, in one undo step; the copies become the
/// selection and Move starts on them (`startMove`). Returns the new body ids.
std::vector<core::ObjectId> pasteBodies(core::ToolContext& ctx, bool startMove = true);

}  // namespace fabricad::assembly
