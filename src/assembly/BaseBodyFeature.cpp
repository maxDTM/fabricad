// SPDX-License-Identifier: LGPL-2.1-or-later
#include "assembly/BaseBodyFeature.h"

#include "core/Feature.h"

#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>

#include <format>
#include <sstream>

namespace fabricad::assembly {

BaseBodyFeature::BaseBodyFeature() : PartFeature(kType) {
  addString("bodyName", "").setLabel("Body name");
  core::Property& b = addString("brep", "");
  b.setLabel("Shape");
  b.flags().hidden = true;
}

std::string BaseBodyFeature::shapeToText(const TopoDS_Shape& shape) {
  if (shape.IsNull()) return {};
  std::ostringstream os;
  try {
    BRepTools::Write(shape, os);
  } catch (const Standard_Failure&) {
    return {};
  }
  return os.str();
}

TopoDS_Shape BaseBodyFeature::shapeFromText(const std::string& text) {
  TopoDS_Shape s;
  if (text.empty()) return s;
  std::istringstream is(text);
  try {
    BRep_Builder builder;
    BRepTools::Read(s, is, builder);
  } catch (const Standard_Failure&) {
    return {};
  }
  return s;
}

void BaseBodyFeature::setShape(const TopoDS_Shape& shape, const std::string& bodyName) {
  prop("brep").setString(shapeToText(shape));
  prop("bodyName").setString(bodyName);
}

TopoDS_Shape BaseBodyFeature::storedShape() const {
  const std::string text = prop("brep").toString();
  if (text != cachedText_ || cached_.IsNull()) {
    cachedText_ = text;
    cached_ = shapeFromText(text);
  }
  return cached_;
}

void BaseBodyFeature::compute(core::RecomputeContext& ctx, part::Outcome& out) {
  TopoDS_Shape s = storedShape();
  if (s.IsNull()) {
    ctx.setError("the stored body shape is empty or unreadable");
    return;
  }
  out.addNew(s, prop("bodyName").toString());
  const std::string base = name().empty() ? type() : name();
  int fi = 0;
  for (TopExp_Explorer ex(s, TopAbs_FACE); ex.More(); ex.Next()) out.names[std::format("{}/F{}", base, ++fi)] = ex.Current();
  out.addPreview(s, part::Operation::NewBody);
}

}  // namespace fabricad::assembly
