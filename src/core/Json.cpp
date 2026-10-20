// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/Json.h"

#include <QJsonDocument>
#include <QJsonParseError>

#include <gp_Ax1.hxx>
#include <gp_Ax3.hxx>
#include <gp_Quaternion.hxx>
#include <gp_Trsf.hxx>

namespace fabricad::core {

QJsonArray toJson(const Vec3& v) { return QJsonArray{v.x, v.y, v.z}; }

Vec3 vec3FromJson(const QJsonValue& v, const Vec3& fallback) {
  if (!v.isArray()) return fallback;
  QJsonArray a = v.toArray();
  if (a.size() != 3) return fallback;
  return {a[0].toDouble(), a[1].toDouble(), a[2].toDouble()};
}

QJsonArray toJson(const std::vector<ObjectId>& ids) {
  QJsonArray a;
  for (ObjectId id : ids) a.append(idToJson(id));
  return a;
}

std::vector<ObjectId> idsFromJson(const QJsonValue& v) {
  std::vector<ObjectId> out;
  for (const auto& x : v.toArray()) out.push_back(idFromJson(x));
  return out;
}

QJsonObject trsfToJson(const gp_Trsf& t) {
  QJsonObject o;
  o["t"] = toJson(Vec3::from(t.TranslationPart()));
  gp_Quaternion q = t.GetRotation();
  o["q"] = QJsonArray{q.X(), q.Y(), q.Z(), q.W()};
  if (t.ScaleFactor() != 1.0) o["s"] = t.ScaleFactor();
  return o;
}

gp_Trsf trsfFromJson(const QJsonValue& v) {
  gp_Trsf t;
  if (!v.isObject()) return t;
  QJsonObject o = v.toObject();
  gp_Quaternion q(0, 0, 0, 1);
  if (o.contains("q")) {
    QJsonArray a = o["q"].toArray();
    if (a.size() == 4) q = gp_Quaternion(a[0].toDouble(), a[1].toDouble(), a[2].toDouble(), a[3].toDouble());
  }
  Vec3 tr = vec3FromJson(o["t"]);
  t.SetTransformation(q, gp_Vec(tr.x, tr.y, tr.z));
  if (o.contains("s")) t.SetScaleFactor(o["s"].toDouble(1.0));
  return t;
}

QJsonObject ax3ToJson(const gp_Ax3& ax) {
  QJsonObject o;
  o["o"] = toJson(Vec3::from(ax.Location()));
  o["n"] = toJson(Vec3::from(ax.Direction()));
  o["x"] = toJson(Vec3::from(ax.XDirection()));
  return o;
}

gp_Ax3 ax3FromJson(const QJsonValue& v) {
  if (!v.isObject()) return gp_Ax3();
  QJsonObject o = v.toObject();
  Vec3 org = vec3FromJson(o["o"]);
  Vec3 n = vec3FromJson(o["n"], {0, 0, 1});
  Vec3 x = vec3FromJson(o["x"], {1, 0, 0});
  try {
    return gp_Ax3(org.toPnt(), n.toDir(), x.toDir());
  } catch (...) {
    return gp_Ax3(org.toPnt(), n.toDir());
  }
}

QJsonObject ax1ToJson(const gp_Ax1& ax) {
  QJsonObject o;
  o["o"] = toJson(Vec3::from(ax.Location()));
  o["d"] = toJson(Vec3::from(ax.Direction()));
  return o;
}

gp_Ax1 ax1FromJson(const QJsonValue& v) {
  if (!v.isObject()) return gp_Ax1();
  QJsonObject o = v.toObject();
  return gp_Ax1(vec3FromJson(o["o"]).toPnt(), vec3FromJson(o["d"], {0, 0, 1}).toDir());
}

std::string jsonToString(const QJsonValue& v, bool compact) {
  QJsonDocument doc;
  if (v.isObject()) doc.setObject(v.toObject());
  else if (v.isArray()) doc.setArray(v.toArray());
  else doc.setArray(QJsonArray{v});
  return doc.toJson(compact ? QJsonDocument::Compact : QJsonDocument::Indented).toStdString();
}

QJsonValue jsonFromString(const std::string& text, std::string* error) {
  QJsonParseError err;
  QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(text), &err);
  if (err.error != QJsonParseError::NoError) {
    if (error) *error = err.errorString().toStdString();
    return QJsonValue();
  }
  if (doc.isObject()) return doc.object();
  if (doc.isArray()) return doc.array();
  return QJsonValue();
}

}  // namespace fabricad::core
