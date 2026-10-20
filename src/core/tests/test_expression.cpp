#include "core/Expression.h"
#include "core/Units.h"

#include <QtTest/QtTest>

using namespace fabricad::core;

class ExpressionTest : public QObject {
  Q_OBJECT
 private slots:
  void arithmetic() {
    ExpressionContext ctx;
    auto r = evaluateExpression("1 + 2 * 3 - 4 / 2", ctx);
    QVERIFY(r.ok);
    QCOMPARE(r.value.value, 5.0);
    QVERIFY(r.value.dim.isScalar());
    r = evaluateExpression("2^3^1 + (1+1)^2", ctx);
    QVERIFY(r.ok);
    QCOMPARE(r.value.value, 12.0);
    r = evaluateExpression("-(3) + +2", ctx);
    QVERIFY(r.ok);
    QCOMPARE(r.value.value, -1.0);
  }

  void unitsAndDimensions() {
    ExpressionContext ctx;
    auto r = evaluateExpression("10 mm + 1 cm", ctx);
    QVERIFY(r.ok);
    QCOMPARE(r.value.value, 20.0);
    QVERIFY(r.value.dim == Dimension::Length());
    r = evaluateExpression("1 in", ctx);
    QVERIFY(r.ok);
    QCOMPARE(r.value.value, 25.4);
    r = evaluateExpression("90 deg + 0.5 rad", ctx);
    QVERIFY(r.ok);
    QVERIFY(std::abs(r.value.value - (90.0 + 0.5 * 180.0 / 3.14159265358979)) < 1e-9);
    r = evaluateExpression("10 mm * 2 mm", ctx);
    QVERIFY(r.ok);
    QVERIFY(r.value.dim == Dimension::Area());
    r = evaluateExpression("10 mm + 5 deg", ctx);
    QVERIFY(!r.ok);  // mismatch
  }

  void bareNumbersUseExpectedDimension() {
    ExpressionContext ctx;
    ctx.units.length = LengthUnit::Inch;
    auto r = evaluateExpression("2", ctx, Dimension::Length());
    QVERIFY(r.ok);
    QCOMPARE(r.value.value, 50.8);
    r = evaluateExpression("2 * 5", ctx, Dimension::Length());  // scalar product promoted at the end
    QVERIFY(r.ok);
    QCOMPARE(r.value.value, 254.0);
    r = evaluateExpression("1 + 1 mm", ctx, Dimension::Length());  // bare 1 -> 1 in
    QVERIFY(r.ok);
    QCOMPARE(r.value.value, 26.4);
    r = evaluateExpression("5 mm", ctx, Dimension::Angle());
    QVERIFY(!r.ok);
    ctx.units.length = LengthUnit::Millimeter;
    r = evaluateExpression("45", ctx, Dimension::Angle());
    QVERIFY(r.ok);
    QCOMPARE(r.value.value, 45.0);
  }

  void functions() {
    ExpressionContext ctx;
    auto r = evaluateExpression("sin(30)", ctx);
    QVERIFY(r.ok);
    QVERIFY(std::abs(r.value.value - 0.5) < 1e-12);
    r = evaluateExpression("sqrt(16 mm * 4 mm)", ctx);
    QVERIFY(r.ok);
    QCOMPARE(r.value.value, 8.0);
    QVERIFY(r.value.dim == Dimension::Length());
    r = evaluateExpression("max(1 mm, 2 cm, 3)", ctx);
    QVERIFY(r.ok);
    QCOMPARE(r.value.value, 20.0);
    r = evaluateExpression("atan2(1, 1)", ctx);
    QVERIFY(r.ok);
    QVERIFY(std::abs(r.value.value - 45.0) < 1e-9);
    r = evaluateExpression("2 * pi", ctx);
    QVERIFY(r.ok);
  }

  void parameters() {
    ExpressionContext ctx;
    ctx.lookup = [](const std::string& n) -> std::optional<Quantity> {
      if (n == "width") return Quantity::mm(40.0);
      if (n == "count") return Quantity::scalar(4);
      return std::nullopt;
    };
    auto r = evaluateExpression("width / count + 3", ctx, Dimension::Length());
    QVERIFY(r.ok);
    QCOMPARE(r.value.value, 13.0);
    r = evaluateExpression("height * 2", ctx);
    QVERIFY(!r.ok);
    QVERIFY(r.error.find("height") != std::string::npos);
    auto refs = referencedParameters("width / count + sin(3) + mm");
    QCOMPARE(refs.size(), size_t(2));
    QCOMPARE(refs[0], std::string("width"));
    QVERIFY(isValidParameterName("d1"));
    QVERIFY(!isValidParameterName("mm"));
    QVERIFY(!isValidParameterName("1x"));
  }

  void errors() {
    ExpressionContext ctx;
    QVERIFY(!evaluateExpression("", ctx).ok);
    QVERIFY(!evaluateExpression("1 +", ctx).ok);
    QVERIFY(!evaluateExpression("(1 + 2", ctx).ok);
    QVERIFY(!evaluateExpression("1 / 0", ctx).ok);
    QVERIFY(!evaluateExpression("foo(1)", ctx).ok);
  }

  void formatting() {
    UnitSystem u;
    QCOMPARE(formatQuantity(Quantity::mm(12.5), u), std::string("12.5 mm"));
    QCOMPARE(formatQuantity(Quantity::mm(10), u), std::string("10 mm"));
    QCOMPARE(formatQuantity(Quantity::deg(90), u), std::string("90 deg"));
    QCOMPARE(formatQuantity(Quantity::scalar(3.14159), u), std::string("3.142"));
    u.length = LengthUnit::Inch;
    QCOMPARE(formatQuantity(Quantity::mm(25.4), u), std::string("1 in"));
  }
};

QTEST_GUILESS_MAIN(ExpressionTest)
#include "test_expression.moc"
