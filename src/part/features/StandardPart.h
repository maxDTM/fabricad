// FabriCAD part: StandardPart feature, the body of a generated standard part (fastener
// library, standards/Fasteners.h). Only the family, size, length and grade are stored; the
// solid is regenerated from the tables on every recompute (no mesh or B-rep in the file).
// Insert Fastener (assembly.insert-fastener) puts one into its own component.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "part/PartFeature.h"
#include "part/standards/Fasteners.h"

namespace fabricad::part {

class StandardPartFeature : public PartFeature {
 public:
  static constexpr const char* kType = "StandardPart";
  StandardPartFeature();
  void compute(core::RecomputeContext& ctx, Outcome& out) override;
  void applyDefaultsForNewFeature(const core::Document&) override {}
  std::string primaryNumeric() const override { return "length"; }
  /// Double-click opens the standard part card (size / length / material edits regenerate it).
  std::string editToolId() const override { return "part.standard-part"; }
  /// An edit keeps the owning component's BOM data (part number, description, material) in step.
  bool wantsCommitPreparation() const override { return true; }
  void prepareCommit(core::Document& doc, core::UndoStack* undo) override;

  standards::FastenerSpec spec() const;
  void setSpec(const standards::FastenerSpec& s);
};

}  // namespace fabricad::part
