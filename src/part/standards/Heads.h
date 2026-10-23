// FabriCAD part standards: screw head dimensions for sizing counterbores and countersinks,
// and the drill point presets. Metric: ISO 4762 (SHCS), ISO 4014 (hex), DIN 7984 low head,
// ISO 7380 button, ISO 10642 flat countersunk (90 deg), ISO 1207 cheese/pan. Inch: ASME B18.3
// SHCS and flat socket countersunk (82 deg), B18.2.1 hex. Counterbore diameters follow
// DIN 974-1 (socket heads) / DIN 974-2 (hex heads, socket-wrench clearance) and the usual
// inch counterbore drills.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "part/standards/Threads.h"

#include <optional>
#include <string>
#include <vector>

namespace fabricad::part::standards {

enum class HeadType { Custom = 0, SocketCap = 1, HexBolt = 2, LowHead = 3, Button = 4, FlatCountersunk = 5, Pan = 6 };

/// Enum option names in HeadType order (the Hole card's "Fastener" list).
const std::vector<std::string>& headTypeNames();
/// Short script keys in HeadType order: custom shcs hex lowhead button csk pan.
const std::vector<std::string>& headTypeKeys();

struct HeadRow {
  HeadType type;
  const char* size;     ///< "M6", "1/4", "#10"
  bool inch;            ///< values below in inches when set, else mm
  double dk;            ///< head diameter (hex: width across corners)
  double k;             ///< head height
  double socket;        ///< hex socket / wrench size (0 = none)
  double csinkAngle;    ///< countersunk heads: included angle, else 0
  double cboreDia;      ///< counterbore (or countersink) diameter for the head
};

/// Head row for the type and the thread's size (in mm), nullopt when the size is not tabulated.
std::optional<HeadRow> headFor(HeadType type, const ThreadSpec& t);

/// The recess a Hole cuts for a head: counterbore diameter/depth or countersink diameter/angle.
struct HeadRecess {
  bool countersink = false;
  double diameter = 0;  ///< mm
  double depth = 0;     ///< counterbore depth (mm): head height + clearance + offset
  double angle = 0;     ///< countersunk heads: included angle, else 0
};
/// `headClearance` adds to the counterbore depth (and to the countersink diameter as extra
/// radial room); `headOffset` sinks the head below the surface.
std::optional<HeadRecess> headRecess(HeadType type, const ThreadSpec& t, double headClearance, double headOffset);

struct PointAngle {
  const char* label;
  double degrees;
};
/// 118 (HSS), 135 (split point), 140 (carbide).
const std::vector<PointAngle>& drillPointPresets();

}  // namespace fabricad::part::standards
