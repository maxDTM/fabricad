// FabriCAD assembly tools: shared helpers.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "assembly/tools/ToolCommon.h"

#include "assembly/ComponentOps.h"
#include "assembly/JointFeature.h"
#include "core/Json.h"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace fabricad::assembly {

using core::Document;
using core::ObjectId;
using core::ShapeRef;

std::vector<ObjectId> componentsOf(const Document& doc, const std::vector<ShapeRef>& refs) {
  std::vector<ObjectId> out;
  for (const ShapeRef& r : refs) {
    ObjectId c = componentOfRef(doc, r);
    if (r.kind != core::ShapeKind::Component && r.kind != core::ShapeKind::Body && !r.isBrep()) continue;
    if (c == core::kInvalidId || c == core::kRootComponentId) continue;
    if (std::find(out.begin(), out.end(), c) == out.end()) out.push_back(c);
  }
  return out;
}

std::vector<ObjectId> jointsOf(const Document& doc, const std::vector<ShapeRef>& refs) {
  std::vector<ObjectId> out;
  for (const ShapeRef& r : refs) {
    if (r.kind != core::ShapeKind::Joint && r.kind != core::ShapeKind::Feature) continue;
    if (doc.featureAs<JointFeature>(r.owner) && std::find(out.begin(), out.end(), r.owner) == out.end()) out.push_back(r.owner);
  }
  return out;
}

bool wordIs(const std::string& text, const char* keyword) {
  std::string a, b(keyword);
  for (char c : text) a += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  for (char& c : b) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return !a.empty() && b.rfind(a, 0) == 0;
}

bool parseNumber(const std::string& text, double& out) {
  std::string t = text;
  t.erase(std::remove_if(t.begin(), t.end(), [](unsigned char c) { return std::isspace(c); }), t.end());
  if (t.empty()) return false;
  char* end = nullptr;
  out = std::strtod(t.c_str(), &end);
  return end && *end == '\0';
}

ExpressionEdit::ExpressionEdit(const Document* doc, core::Dimension dim, QWidget* parent)
    : QLineEdit(parent), doc_(doc), dim_(dim) {
  connect(this, &QLineEdit::editingFinished, this, [this] { evaluateText(true); });
  connect(this, &QLineEdit::textEdited, this, [this] { evaluateText(true); });
}

void ExpressionEdit::setValue(double base) {
  value_ = base;
  valid_ = true;
  core::UnitSystem units = doc_ ? doc_->units() : core::UnitSystem{};
  expr_ = core::formatQuantity(core::Quantity{base, dim_}, units, false);
  setText(core::qs(expr_));
  setStyleSheet({});
}

void ExpressionEdit::evaluateText(bool emitSignal) {
  std::string text = core::ss(this->text());
  core::EvalResult r;
  if (doc_) {
    r = doc_->evaluate(text, dim_);
  } else {
    core::ExpressionContext ctx;
    r = core::evaluateExpression(text, ctx, dim_);
  }
  valid_ = r.ok;
  setStyleSheet(valid_ ? QString() : QString("QLineEdit { border: 1px solid #d33; }"));
  if (valid_) {
    expr_ = text;
    if (r.value.value != value_) {
      value_ = r.value.value;
      if (emitSignal) emit valueChanged(value_);
    }
  }
}

}  // namespace fabricad::assembly
