// FabriCAD assembly tools: Insert Fastener (ASSEMBLE > INSERT, assembly.insert-fastener,
// FASTENER). Pick holes (circular edges or hole walls); the card chooses family, size, length,
// material and the nut / washer stack; OK inserts the parts (FastenerInsert.h).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "assembly/FastenerInsert.h"
#include "core/Tool.h"

#include <QObject>

#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;

namespace fabricad::assembly {

class InsertFastenerTool : public QObject, public core::ToolBase {
  Q_OBJECT
 public:
  InsertFastenerTool();
  void activate(core::ToolContext& ctx) override;
  void deactivate(core::ToolContext& ctx) override;
  void accept(core::ToolContext& ctx) override;
  void selectionChanged(core::ToolContext& ctx) override;
  bool commandLineInput(core::ToolContext& ctx, const std::string& text) override;
  QWidget* createDialog(core::ToolContext& ctx, QWidget* parent) override;

  const InsertOptions& options() const { return opt_; }
  const std::vector<core::ShapeRef>& holes() const { return holes_; }

 private:
  void takeHoles(core::ToolContext& ctx, const std::vector<core::ShapeRef>& refs);
  void updateSummary(core::ToolContext& ctx);
  void fillSizeLists();
  InsertOptions opt_;
  std::vector<core::ShapeRef> holes_;
  core::ToolContext* ctx_ = nullptr;
  QComboBox* family_ = nullptr;
  QComboBox* size_ = nullptr;
  QComboBox* length_ = nullptr;
  QComboBox* grade_ = nullptr;
  QCheckBox* nut_ = nullptr;
  QCheckBox* washer_ = nullptr;
  QCheckBox* rotation_ = nullptr;
  QCheckBox* all_ = nullptr;
  QLabel* summary_ = nullptr;
};

}  // namespace fabricad::assembly
