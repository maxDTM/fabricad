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
