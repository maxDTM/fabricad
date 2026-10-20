// FabriCAD app: generic command-dialog body for any Feature's Properties (ARCHITECTURE 4.4),
// and the "app.edit-properties" tool that edits an existing feature with it.
//
// PropertyEditor renders one row per visible Property: Length/Angle/Double as expression
// fields ("width/2 + 3 mm", red border when invalid), Int spin box, Bool check box, Enum
// combo, String line edit, Vec3 triple, Ref/RefList as a selection input (click to make it
// active; viewport picks then go to it, filtered by Property::refFilter()).
//
// EditPropertiesTool follows the edit recipe of ARCHITECTURE section 6: snapshot the
// feature JSON on activation, apply edits live (notifyFeatureChanged + recompute as the
// preview), push ModifyFeatureCommand(before, after) on OK, restore on Cancel. The tool logic
// does not need the widget (headless tests call setNumeric/setRefs directly).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Property.h"
#include "core/Tool.h"

#include <QJsonObject>
#include <QPointer>
#include <QWidget>

#include <map>
#include <string>

class QFormLayout;
class QLabel;
class QLineEdit;
class QToolButton;

namespace fabricad::core {
class Document;
class Feature;
}  // namespace fabricad::core

namespace fabricad::app {

class PropertyEditor : public QWidget {
  Q_OBJECT
 public:
  PropertyEditor(core::Document& doc, core::Feature& feature, QWidget* parent = nullptr);

  /// Re-read every value from the feature.
  void refresh();
  /// Recreate all rows (after PropertyFlags::hidden changed). Safe to call from a row's signal.
  void rebuild();
  /// rebuild() on the next event-loop turn (coalesced).
  void scheduleRebuild();
  const std::string& activeRefInput() const { return activeRef_; }
  void setActiveRefInput(const std::string& prop);
  void setMessage(const QString& text, bool error);

 signals:
  /// A value was changed by the user (already written into the feature's Property).
  void propertyEdited(const std::string& name);
  void activeRefInputChanged(const std::string& name);
  /// Numeric text did not evaluate (dialog shows it, OK should be disabled).
  void validityChanged(bool valid);

 private:
  void build();
  void addRow(core::Property& p);
  /// Feature::updatePropertyFlags(); true when any hidden/readOnly flag changed.
  bool flagsChangeAfter(const std::string& changed);
  bool applyNumeric(core::Property& p, QLineEdit* edit);
  QString refText(const core::Property& p) const;
  void updateValidity();

  core::Document& doc_;
  core::Feature& feature_;
  QFormLayout* form_ = nullptr;
  QLabel* message_ = nullptr;
  std::string activeRef_;
  std::map<std::string, QToolButton*> refButtons_;
  std::map<std::string, QLineEdit*> numericEdits_;
  std::map<std::string, bool> invalid_;
  bool updating_ = false;
  bool rebuildPending_ = false;
};

/// Evaluate `text` for a numeric property in the document scope. On success writes the
/// expression (or plain value) into the property and returns true.
bool setNumericProperty(const core::Document& doc, core::Property& p, const std::string& text, std::string* error);

class EditPropertiesTool : public core::ToolBase {
 public:
  EditPropertiesTool();

  void activate(core::ToolContext& ctx) override;
  void deactivate(core::ToolContext& ctx) override;
  void accept(core::ToolContext& ctx) override;
  void cancel(core::ToolContext& ctx) override;
  void selectionChanged(core::ToolContext& ctx) override;
  bool commandLineInput(core::ToolContext& ctx, const std::string& text) override;
  QWidget* createDialog(core::ToolContext& ctx, QWidget* parent) override;

  core::ObjectId featureId() const { return id_; }
  /// Programmatic edits (dialog rows and tests); each previews immediately.
  bool setNumeric(core::ToolContext& ctx, const std::string& prop, const std::string& text);
  void setActiveRefInput(core::ToolContext& ctx, const std::string& prop);
  void previewNow(core::ToolContext& ctx);

 private:
  core::Feature* feature(core::ToolContext& ctx) const;
  void applyFilter(core::ToolContext& ctx);

  core::ObjectId id_ = core::kInvalidId;
  QJsonObject before_;
  std::string activeRef_;
  QPointer<PropertyEditor> editor_;
  bool applyingSelection_ = false;
};

}  // namespace fabricad::app
