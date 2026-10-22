// FabriCAD assembly tools: Interference check dialog (INSPECT > Interference).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "assembly/Interference.h"
#include "core/Tool.h"
#include "core/ViewerContext.h"

#include <QObject>

#include <vector>

class QLabel;
class QTableWidget;
class QCheckBox;

namespace fabricad::assembly {

class InterferenceTool : public QObject, public core::ToolBase {
  Q_OBJECT
 public:
  InterferenceTool();
  void activate(core::ToolContext& ctx) override;
  void deactivate(core::ToolContext& ctx) override;
  void accept(core::ToolContext& ctx) override;
  void selectionChanged(core::ToolContext& ctx) override;
  bool commandLineInput(core::ToolContext& ctx, const std::string& text) override;
  QWidget* createDialog(core::ToolContext& ctx, QWidget* parent) override;

  /// Run the check on the current component set (empty = all). Returns the report.
  const InterferenceReport& compute(core::ToolContext& ctx);
  const InterferenceReport& report() const { return report_; }
  const std::vector<core::ObjectId>& components() const { return components_; }

 private:
  void showResults(core::ToolContext& ctx);
  void highlightRow(core::ToolContext& ctx, int row);
  /// Common-volume previews, drawn on top; `selectedRow` (>= 0) is emphasised.
  void showPreviews(core::ToolContext& ctx, int selectedRow);
  std::vector<core::ObjectId> components_;
  InterferenceReport report_;
  std::vector<core::PreviewHandle> previews_;
  core::ToolContext* ctx_ = nullptr;
  QLabel* scopeLabel_ = nullptr;
  QLabel* summaryLabel_ = nullptr;
  QTableWidget* table_ = nullptr;
};

}  // namespace fabricad::assembly
