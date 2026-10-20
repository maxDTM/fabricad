// FabriCAD assembly tools: shared helpers (selection -> components, expression field).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Document.h"
#include "core/ShapeRef.h"
#include "core/Tool.h"
#include "core/Types.h"
#include "core/Units.h"

#include <QLineEdit>

#include <string>
#include <vector>

namespace fabricad::assembly {

/// Distinct non-root component ids referenced by `refs` (Component refs, bodies' components).
std::vector<core::ObjectId> componentsOf(const core::Document& doc, const std::vector<core::ShapeRef>& refs);
/// Joint feature ids referenced by `refs` (ShapeKind::Joint/Feature refs that are joints).
std::vector<core::ObjectId> jointsOf(const core::Document& doc, const std::vector<core::ShapeRef>& refs);
/// Case-insensitive prefix match helper for command-line words.
bool wordIs(const std::string& text, const char* keyword);
/// Parse a plain number from command-line text (false when not numeric).
bool parseNumber(const std::string& text, double& out);

/// Line edit that evaluates an expression in the document's parameter scope and reports the
/// value in base units (mm / deg). Shows invalid input with a red frame.
class ExpressionEdit : public QLineEdit {
  Q_OBJECT
 public:
  ExpressionEdit(const core::Document* doc, core::Dimension dim, QWidget* parent = nullptr);
  double value() const { return value_; }
  bool isValid() const { return valid_; }
  /// Set a base-unit value; the text shows it in display units.
  void setValue(double base);
  const std::string& expression() const { return expr_; }

 signals:
  void valueChanged(double baseValue);

 private:
  void evaluateText(bool emitSignal);
  const core::Document* doc_;
  core::Dimension dim_;
  double value_ = 0.0;
  bool valid_ = true;
  std::string expr_;
};

}  // namespace fabricad::assembly
