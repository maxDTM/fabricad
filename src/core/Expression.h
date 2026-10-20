// FabriCAD core: arithmetic expressions with units and parameters.
//
// Grammar (whitespace insensitive):
//   expr    := term (('+'|'-') term)*
//   term    := factor (('*'|'/') factor)*
//   factor  := unary ('^' factor)?   (right-associative)
//   unary   := ('-'|'+') unary | primary [unit]
//   primary := number | ident '(' expr (',' expr)* ')' | ident | '(' expr ')'
//   unit    := 'mm' | 'cm' | 'm' | 'in' | '"' | 'ft' | 'deg' | '°' | 'rad'
// Functions: sin cos tan (degrees in, scalar out), asin acos atan atan2 (degrees out),
//            sqrt abs min max floor ceil round, pi.
// Identifiers resolve through the ParameterLookup (user parameters, "d1" dimensions).
// Bare numbers are scalars while parsing; a scalar operand of + - min max atan2 is promoted
// to the other operand's dimension in the document's *display* unit, and a scalar final
// result is promoted to the expected dimension the same way: with expected Length and
// units.length == Inch, "2" and "2*1" both mean 2 in = 50.8 mm, "1 + 1 mm" is 26.4 mm.
// Numbers with an explicit unit are converted to base units (mm / deg). Dimension mismatch
// (e.g. "10 mm + 5 deg") is an error.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Units.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fabricad::core {

using ParameterLookup = std::function<std::optional<Quantity>(const std::string& name)>;

struct EvalResult {
  bool ok = false;
  Quantity value{};
  std::string error;
  explicit operator bool() const { return ok; }
};

struct ExpressionContext {
  ParameterLookup lookup;        ///< may be empty: no parameters
  UnitSystem units{};            ///< for bare numbers
};

/// Evaluate `expr`. `expected` = Dimension::Scalar() means "anything goes" and bare
/// numbers are scalars. With an expected Length/Angle, bare numbers get that unit and
/// the final result must have that dimension.
EvalResult evaluateExpression(std::string_view expr, const ExpressionContext& ctx,
                              std::optional<Dimension> expected = std::nullopt);

/// Names referenced by an expression (for dependency ordering of parameters).
std::vector<std::string> referencedParameters(std::string_view expr);

/// True if `name` is a valid parameter identifier: [A-Za-z_][A-Za-z0-9_]* and not a unit/function.
bool isValidParameterName(std::string_view name);

}  // namespace fabricad::core
