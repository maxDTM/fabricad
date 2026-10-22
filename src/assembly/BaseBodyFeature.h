// FabriCAD assembly: BaseBody feature, a frozen (non-parametric) copy of one body.
// Paste New uses it when a copied component holds a body whose creating feature lives outside
// the copied tree (e.g. "Create Components from Bodies" moved a root body into the component):
// that feature cannot be copied, so the body's current shape is stored instead (Fusion "base
// feature"). The shape is kept as BRep text in a hidden string property, in the owning
// component's local coordinates.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "part/PartFeature.h"

#include <TopoDS_Shape.hxx>

#include <string>

namespace fabricad::assembly {

class BaseBodyFeature : public part::PartFeature {
 public:
  static constexpr const char* kType = "BaseBody";
  BaseBodyFeature();
  void compute(core::RecomputeContext& ctx, part::Outcome& out) override;
  std::string browserFolder() const override { return {}; }
  std::string iconName() const override { return "body"; }
  void applyDefaultsForNewFeature(const core::Document&) override {}

  /// Store a shape (and the body name the feature creates).
  void setShape(const TopoDS_Shape& shape, const std::string& bodyName);
  TopoDS_Shape storedShape() const;

  static std::string shapeToText(const TopoDS_Shape& shape);
  static TopoDS_Shape shapeFromText(const std::string& text);

 private:
  mutable std::string cachedText_;
  mutable TopoDS_Shape cached_;
};

}  // namespace fabricad::assembly
