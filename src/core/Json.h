// FabriCAD core: JSON helpers (Qt JSON <-> core/OCCT value types).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Types.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <string>
#include <vector>

class gp_Trsf;
class gp_Ax3;
class gp_Ax1;

namespace fabricad::core {

inline QString qs(const std::string& s) { return QString::fromStdString(s); }
inline std::string ss(const QString& s) { return s.toStdString(); }
inline QString qs(std::string_view s) { return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size())); }

QJsonArray toJson(const Vec3& v);
Vec3 vec3FromJson(const QJsonValue& v, const Vec3& fallback = {});

QJsonArray toJson(const std::vector<ObjectId>& ids);
std::vector<ObjectId> idsFromJson(const QJsonValue& v);
/// ObjectId round-trips through JSON as a double; this is exact below 2^53.
inline QJsonValue idToJson(ObjectId id) { return static_cast<double>(id); }
inline ObjectId idFromJson(const QJsonValue& v, ObjectId fallback = kInvalidId) {
  return v.isDouble() ? static_cast<ObjectId>(v.toDouble()) : fallback;
}

/// gp_Trsf as {"t":[x,y,z],"q":[x,y,z,w]} (translation + unit quaternion).
QJsonObject trsfToJson(const gp_Trsf& t);
gp_Trsf trsfFromJson(const QJsonValue& v);
/// gp_Ax3 as {"o":[..],"n":[..],"x":[..]}.
QJsonObject ax3ToJson(const gp_Ax3& ax);
gp_Ax3 ax3FromJson(const QJsonValue& v);
QJsonObject ax1ToJson(const gp_Ax1& ax);
gp_Ax1 ax1FromJson(const QJsonValue& v);

std::string jsonToString(const QJsonValue& v, bool compact = true);
QJsonValue jsonFromString(const std::string& text, std::string* error = nullptr);

}  // namespace fabricad::core
