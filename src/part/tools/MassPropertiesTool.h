// FabriCAD part: Mass Properties command (INSPECT, `part.mass-properties`, verb MASSPROPS).
//
// Scope: the selected bodies / components, else the active component, else the whole design.
// The card lists volume, area, mass, CoG, inertia about the CoG and the origin, principal axes
// and moments and the radii of gyration; the CoG is drawn as a marker with the principal axes
// as a small triad while the card is open. "Copy" puts a tab-separated block on the clipboard,
// "Write report..." saves CSV or Markdown. From a script: `MASSPROPS [report=a.csv|a.md]`
// prints the summary (and writes the report) and finishes at once.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Tool.h"
#include "core/ViewerContext.h"
#include "part/MassProperties.h"

#include <QObject>
#include <QPointer>

#include <string>
#include <vector>

class QLabel;
class QTableWidget;

namespace fabricad::part {

class MassPropertiesTool : public QObject, public core::ToolBase {
  Q_OBJECT
 public:
  explicit MassPropertiesTool(core::ToolInfo info);
  void activate(core::ToolContext& ctx) override;
  void deactivate(core::ToolContext& ctx) override;
  void selectionChanged(core::ToolContext& ctx) override;
  void documentChanged(core::ToolContext& ctx) override;
  bool commandLineInput(core::ToolContext& ctx, const std::string& text) override;
  QWidget* createDialog(core::ToolContext& ctx, QWidget* parent) override;

  const MassProperties& result() const { return result_; }
  /// Write the report (".md"/".markdown" -> Markdown, else CSV). False with `err` on failure.
  bool writeReport(core::ToolContext& ctx, const std::string& path, std::string* err = nullptr) const;

 private:
  void recompute(core::ToolContext& ctx);
  void showResult(core::ToolContext& ctx);
  bool inch(const core::ToolContext& ctx) const;

  std::vector<core::ObjectId> components_, bodies_;
  MassProperties result_;
  std::vector<core::PreviewHandle> previews_;
  core::ToolContext* ctx_ = nullptr;
  QPointer<QTableWidget> table_;
  QPointer<QLabel> scopeLabel_;
};

}  // namespace fabricad::part
