#include "core/Json.h"
#include "core/Property.h"

#include <QtTest/QtTest>

using namespace fabricad::core;

class PropertyTest : public QObject {
  Q_OBJECT
 private slots:
  void numericAndExpression() {
    Property p("distance", PropertyType::Length);
    p.setDouble(10.0);
    QCOMPARE(p.toDouble(), 10.0);
    QVERIFY(!p.hasExpression());
    QVERIFY(p.dimension() == Dimension::Length());
    p.setExpression("width/2");
    QVERIFY(p.hasExpression());
    QCOMPARE(p.toDouble(), 10.0);  // cached until evaluated
    p.setEvaluatedValue(20.0);
    QCOMPARE(p.toDouble(), 20.0);
    QCOMPARE(p.expression(), std::string("width/2"));
    p.setDouble(5.0);
    QVERIFY(!p.hasExpression());
    UnitSystem u;
    QCOMPARE(p.displayText(u), std::string("5 mm"));
  }

  void enumByName() {
    Property p("op", PropertyType::Enum);
    p.setOptions({"Join", "Cut", "Intersect", "New Body"});
    p.setEnum("Cut");
    QCOMPARE(p.enumIndex(), 1);
    QCOMPARE(p.enumName(), std::string("Cut"));
    QJsonValue j = p.toJson();
    Property q("op", PropertyType::Enum);
    q.setOptions({"New Body", "Join", "Cut"});  // reordered options
    q.fromJson(j);
    QCOMPARE(q.enumName(), std::string("Cut"));
    QCOMPARE(q.enumIndex(), 2);
  }

  // damaged / hand-edited files (fuzzing, docs/TESTING.md): no UB, no out-of-range enum index
  void damagedJsonValues() {
    Property e("op", PropertyType::Enum);
    e.setOptions({"Join", "Cut"});
    e.setEnum("Cut");
    e.fromJson(QJsonObject{{"value", 1e300}});
    QCOMPARE(e.enumIndex(), 1);  // out of range: default kept
    e.fromJson(QJsonObject{{"value", -5}});
    QCOMPARE(e.enumIndex(), 1);
    e.fromJson(QJsonObject{{"value", 0}});
    QCOMPARE(e.enumIndex(), 0);
    Property i("n", PropertyType::Int);
    i.fromJson(QJsonObject{{"value", 1e300}});
    QVERIFY(i.toInt() > 0);  // saturates
    i.fromJson(QJsonObject{{"value", -1e300}});
    QVERIFY(i.toInt() < 0);
    Property d("d", PropertyType::Length);
    d.setDouble(1e300);
    QVERIFY(d.toInt() > 0);
    QCOMPARE(idFromJson(QJsonValue(-1.0), 7), ObjectId(7));
    QCOMPARE(idFromJson(QJsonValue(1e300), 7), ObjectId(7));
    QCOMPARE(idFromJson(QJsonValue(2.5), 7), ObjectId(7));
    QCOMPARE(idFromJson(QJsonValue(42.0), 7), ObjectId(42));
  }

  void refsRoundTrip() {
    Property p("profiles", PropertyType::RefList);
    p.addRef(ShapeRef::profile(7, Vec3(1, 2, 0), {3, 1, 2}));
    p.addRef(ShapeRef::face(9, 3));
    p.addRef(ShapeRef::face(9, 3));  // duplicate ignored
    QCOMPARE(p.refs().size(), size_t(2));
    QCOMPARE(p.refs()[0].name, std::string("1,2,3"));
    auto owners = p.referencedOwners();
    QCOMPARE(owners.size(), size_t(2));
    Property q("profiles", PropertyType::RefList);
    q.fromJson(p.toJson());
    QVERIFY(q.valueEquals(p));
    QVERIFY(q.refs()[0].hasPoint);
    QCOMPARE(q.refs()[0].point, Vec3(1, 2, 0));
    QVERIFY(q.removeRef(ShapeRef::face(9, 3)));
    QCOMPARE(q.refs().size(), size_t(1));
  }

  void otherTypes() {
    Property b("flag", PropertyType::Bool);
    b.setBool(true);
    Property i("count", PropertyType::Int);
    i.setInt(42);
    Property s("name", PropertyType::String);
    s.setString("hello");
    Property v("dir", PropertyType::Vec3);
    v.setVec3({0, 0, 1});
    Property j("extra", PropertyType::Json);
    QJsonObject o;
    o["k"] = 1;
    j.setJson(o);
    for (Property* p : {&b, &i, &s, &v, &j}) {
      Property copy(p->name(), p->type());
      copy.fromJson(p->toJson());
      QVERIFY(copy.valueEquals(*p));
    }
    QCOMPARE(i.toDouble(), 42.0);
    QCOMPARE(propertyTypeFromName("RefList"), PropertyType::RefList);
  }
};

QTEST_GUILESS_MAIN(PropertyTest)
#include "test_property.moc"
