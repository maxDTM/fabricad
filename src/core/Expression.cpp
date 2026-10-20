// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/Expression.h"

#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <numbers>
#include <set>
#include <stdexcept>

namespace fabricad::core {

namespace {

const std::set<std::string> kFunctions = {"sin", "cos", "tan", "asin", "acos", "atan", "atan2", "sqrt",
                                          "abs", "min", "max", "floor", "ceil", "round"};
const std::set<std::string> kUnits = {"mm", "cm", "m", "in", "inch", "ft", "deg", "rad"};

struct ParseError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

class Parser {
 public:
  Parser(std::string_view s, const ExpressionContext& ctx, std::optional<Dimension> expected)
      : s_(s), ctx_(ctx), expected_(expected) { (void)expected_; }

  Quantity parse() {
    Quantity q = expr();
    skipWs();
    if (pos_ != s_.size()) throw ParseError(std::format("unexpected '{}' at {}", s_[pos_], pos_));
    return q;
  }

 private:
  std::string_view s_;
  const ExpressionContext& ctx_;
  std::optional<Dimension> expected_;
  size_t pos_ = 0;

  void skipWs() {
    while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_]))) ++pos_;
  }
  bool peek(char c) {
    skipWs();
    return pos_ < s_.size() && s_[pos_] == c;
  }
  bool accept(char c) {
    if (peek(c)) { ++pos_; return true; }
    return false;
  }

  /// Promote a bare scalar operand to the dimension of the other operand (display units).
  void promote(Quantity& a, Quantity& b) const {
    if (a.dim.isScalar() && !b.dim.isScalar()) a = fromDisplayValue(a.value, b.dim, ctx_.units);
    else if (b.dim.isScalar() && !a.dim.isScalar()) b = fromDisplayValue(b.value, a.dim, ctx_.units);
  }

  static void requireSame(const Quantity& a, const Quantity& b, const char* op) {
    if (a.dim != b.dim)
      throw ParseError(std::format("cannot {} {} and {}", op, a.dim.toString(), b.dim.toString()));
  }

  Quantity expr() {
    Quantity v = term();
    for (;;) {
      if (accept('+')) { Quantity r = term(); promote(v, r); requireSame(v, r, "add"); v.value += r.value; }
      else if (accept('-')) { Quantity r = term(); promote(v, r); requireSame(v, r, "subtract"); v.value -= r.value; }
      else return v;
    }
  }

  Quantity term() {
    Quantity v = factor();
    for (;;) {
      if (accept('*')) {
        Quantity r = factor();
        v.value *= r.value;
        v.dim.length += r.dim.length;
        v.dim.angle += r.dim.angle;
      } else if (accept('/')) {
        Quantity r = factor();
        if (r.value == 0.0) throw ParseError("division by zero");
        v.value /= r.value;
        v.dim.length -= r.dim.length;
        v.dim.angle -= r.dim.angle;
      } else {
        return v;
      }
    }
  }

  Quantity factor() {
    Quantity base = unary();
    if (accept('^')) {
      Quantity e = factor();  // right-associative: 2^3^2 == 2^(3^2)
      if (!e.dim.isScalar()) throw ParseError("exponent must be a scalar");
      double ev = e.value;
      if (!base.dim.isScalar()) {
        if (std::floor(ev) != ev) throw ParseError("non-integer power of a dimensioned value");
        base.dim.length *= static_cast<int>(ev);
        base.dim.angle *= static_cast<int>(ev);
      }
      base.value = std::pow(base.value, ev);
    }
    return base;
  }

  Quantity unary() {
    if (accept('-')) { Quantity v = unary(); v.value = -v.value; return v; }
    if (accept('+')) return unary();
    Quantity v = primary();
    // optional unit suffix
    skipWs();
    size_t save = pos_;
    std::string unit = readUnitToken();
    if (!unit.empty()) {
      auto info = parseUnitSymbol(unit);
      if (info) {
        if (!v.dim.isScalar()) throw ParseError(std::format("unit '{}' applied to a dimensioned value", unit));
        v.value *= info->toBase;
        v.dim = info->dim;
      } else {
        pos_ = save;
      }
    }
    return v;
  }

  std::string readUnitToken() {
    if (pos_ >= s_.size()) return {};
    unsigned char c = static_cast<unsigned char>(s_[pos_]);
    if (c == '"' || c == '\'') { ++pos_; return std::string(1, static_cast<char>(c)); }
    // UTF-8 degree sign U+00B0 = C2 B0
    if (c == 0xC2 && pos_ + 1 < s_.size() && static_cast<unsigned char>(s_[pos_ + 1]) == 0xB0) {
      pos_ += 2;
      return "°";
    }
    if (!std::isalpha(c)) return {};
    size_t start = pos_;
    while (pos_ < s_.size() && std::isalpha(static_cast<unsigned char>(s_[pos_]))) ++pos_;
    std::string tok(s_.substr(start, pos_ - start));
    if (kUnits.count(tok)) return tok;
    pos_ = start;
    return {};
  }

  Quantity primary() {
    skipWs();
    if (pos_ >= s_.size()) throw ParseError("unexpected end of expression");
    char c = s_[pos_];
    if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') return number();
    if (c == '(') {
      ++pos_;
      Quantity v = expr();
      if (!accept(')')) throw ParseError("expected ')'");
      return v;
    }
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') return identifier();
    throw ParseError(std::format("unexpected '{}' at {}", c, pos_));
  }

  Quantity number() {
    size_t start = pos_;
    while (pos_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '.')) ++pos_;
    if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
      size_t p = pos_ + 1;
      if (p < s_.size() && (s_[p] == '+' || s_[p] == '-')) ++p;
      if (p < s_.size() && std::isdigit(static_cast<unsigned char>(s_[p]))) {
        pos_ = p;
        while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
      }
    }
    std::string_view tok = s_.substr(start, pos_ - start);
    double v = 0;
    auto res = std::from_chars(tok.data(), tok.data() + tok.size(), v);
    if (res.ec != std::errc{} || res.ptr != tok.data() + tok.size())
      throw ParseError(std::format("bad number '{}'", tok));
    // Bare numbers are scalars while parsing. Scalars are promoted to the dimension
    // of the other operand on +/-/min/max (display units), and a scalar final result
    // is promoted to the expected dimension in evaluateExpression().
    return Quantity::scalar(v);
  }

  Quantity identifier() {
    size_t start = pos_;
    while (pos_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '_')) ++pos_;
    std::string name(s_.substr(start, pos_ - start));
    if (name == "pi" || name == "PI") return Quantity::scalar(std::numbers::pi);
    if (kFunctions.count(name)) {
      if (!accept('(')) throw ParseError(std::format("expected '(' after {}", name));
      std::vector<Quantity> args;
      if (!peek(')')) {
        args.push_back(expr());
        while (accept(',')) args.push_back(expr());
      }
      if (!accept(')')) throw ParseError("expected ')'");
      return call(name, args);
    }
    if (!ctx_.lookup) throw ParseError(std::format("unknown name '{}'", name));
    auto v = ctx_.lookup(name);
    if (!v) throw ParseError(std::format("unknown parameter '{}'", name));
    return *v;
  }

  static double toRad(const Quantity& q) {
    if (q.dim == Dimension::Angle()) return q.value * std::numbers::pi / 180.0;
    if (q.dim.isScalar()) return q.value * std::numbers::pi / 180.0;  // bare numbers are degrees
    throw ParseError("trigonometric function needs an angle");
  }

  Quantity call(const std::string& fn, const std::vector<Quantity>& a) const {
    auto need = [&](size_t n) {
      if (a.size() != n) throw ParseError(std::format("{} expects {} argument(s)", fn, n));
    };
    if (fn == "sin") { need(1); return Quantity::scalar(std::sin(toRad(a[0]))); }
    if (fn == "cos") { need(1); return Quantity::scalar(std::cos(toRad(a[0]))); }
    if (fn == "tan") { need(1); return Quantity::scalar(std::tan(toRad(a[0]))); }
    if (fn == "asin") { need(1); return Quantity::deg(std::asin(a[0].value) * 180.0 / std::numbers::pi); }
    if (fn == "acos") { need(1); return Quantity::deg(std::acos(a[0].value) * 180.0 / std::numbers::pi); }
    if (fn == "atan") { need(1); return Quantity::deg(std::atan(a[0].value) * 180.0 / std::numbers::pi); }
    if (fn == "atan2") {
      need(2);
      Quantity p = a[0], q = a[1];
      promote(p, q);
      requireSame(p, q, "atan2");
      return Quantity::deg(std::atan2(p.value, q.value) * 180.0 / std::numbers::pi);
    }
    if (fn == "sqrt") {
      need(1);
      Quantity r = a[0];
      if (r.dim.length % 2 || r.dim.angle % 2) throw ParseError("sqrt of odd dimension");
      r.dim.length /= 2; r.dim.angle /= 2;
      r.value = std::sqrt(r.value);
      return r;
    }
    if (fn == "abs") { need(1); Quantity r = a[0]; r.value = std::fabs(r.value); return r; }
    if (fn == "floor") { need(1); Quantity r = a[0]; r.value = std::floor(r.value); return r; }
    if (fn == "ceil") { need(1); Quantity r = a[0]; r.value = std::ceil(r.value); return r; }
    if (fn == "round") { need(1); Quantity r = a[0]; r.value = std::round(r.value); return r; }
    if (fn == "min" || fn == "max") {
      if (a.empty()) throw ParseError(fn + " needs arguments");
      Quantity r = a[0];
      for (size_t i = 1; i < a.size(); ++i) {
        Quantity b = a[i];
        promote(r, b);
        requireSame(r, b, fn.c_str());
        r.value = fn == "min" ? std::min(r.value, b.value) : std::max(r.value, b.value);
      }
      return r;
    }
    throw ParseError("unknown function " + fn);
  }
};

}  // namespace

EvalResult evaluateExpression(std::string_view expr, const ExpressionContext& ctx,
                              std::optional<Dimension> expected) {
  EvalResult r;
  try {
    Parser p(expr, ctx, expected);
    r.value = p.parse();
    if (expected && *expected != r.value.dim) {
      // Allow scalar result where a dimension is expected only if the expression was
      // dimensionless throughout (e.g. "width/height * 10" when width,height are lengths).
      if (r.value.dim.isScalar() && !expected->isScalar()) {
        r.value = fromDisplayValue(r.value.value, *expected, ctx.units);
      } else {
        r.error = std::format("expected {} but got {}", expected->toString(), r.value.dim.toString());
        return r;
      }
    }
    if (!std::isfinite(r.value.value)) {
      r.error = "result is not finite";
      return r;
    }
    r.ok = true;
  } catch (const ParseError& e) {
    r.error = e.what();
  }
  return r;
}

std::vector<std::string> referencedParameters(std::string_view expr) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < expr.size()) {
    unsigned char c = static_cast<unsigned char>(expr[i]);
    if (std::isalpha(c) || c == '_') {
      size_t start = i;
      while (i < expr.size() && (std::isalnum(static_cast<unsigned char>(expr[i])) || expr[i] == '_')) ++i;
      std::string name(expr.substr(start, i - start));
      if (kFunctions.count(name) || kUnits.count(name) || name == "pi" || name == "PI") continue;
      bool dup = false;
      for (const auto& n : out) dup |= (n == name);
      if (!dup) out.push_back(name);
    } else {
      ++i;
    }
  }
  return out;
}

bool isValidParameterName(std::string_view name) {
  if (name.empty()) return false;
  if (!(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_')) return false;
  for (char ch : name)
    if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_')) return false;
  std::string s(name);
  return !kFunctions.count(s) && !kUnits.count(s) && s != "pi" && s != "PI";
}

}  // namespace fabricad::core
