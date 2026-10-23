// FabriCAD part standards: generated standard parts (fastener library). Each part is built on
// demand from compact tables (no stored mesh or B-rep): socket head cap screws (ISO 4762 /
// ASME B18.3), hex bolts (ISO 4017 / B18.2.1), hex nuts (ISO 4032 / B18.2.2), nyloc nuts
// (ISO 7040), plain washers (ISO 7089 / ISO 7092 / ASME B18.22.1), heat-set inserts (common
// 3D-print sizes), dowel pins (ISO 2338 / ASME B18.8.2) and set screws (ISO 4026 flat point).
// Frame: the seating plane is z = 0 with its outward normal +Z (the face the part sits on is
// below). Screw heads, nuts and washers lie on z >= 0; screw shanks, inserts and set screws on
// z <= 0; dowel pins are centred on z = 0. Threads are cosmetic (shank at the major diameter).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "part/standards/Threads.h"

#include <TopoDS_Shape.hxx>

#include <optional>
#include <string>
#include <vector>

namespace fabricad::part::standards {

enum class Family { SocketCap = 0, HexBolt, HexNut, NylocNut, Washer, SmallWasher, HeatSetInsert, DowelPin, SetScrew };
constexpr int kFamilyCount = 9;

/// Card names ("Socket head cap screw (ISO 4762 / B18.3)") and script keys
/// (shcs hexbolt hexnut nyloc washer smallwasher insert dowel setscrew), in Family order.
const std::vector<std::string>& familyNames();
const std::vector<std::string>& familyKeys();
std::optional<Family> familyFromKey(const std::string& key);
/// Screws, bolts, set screws and pins have a length; nuts and washers do not.
bool familyHasLength(Family f);
/// Nuts, washers, inserts and set screws are threaded/bored for a screw of the size.
bool familyIsScrew(Family f);

/// Material grades in card order ("8.8", "10.9", "12.9", "A2", "A4", "Brass", "Nylon") with
/// density (g/cm3) and the material library name (core::findMaterial).
struct Grade {
  const char* key;
  const char* materialName;
  double density;
};
const std::vector<Grade>& grades();
const Grade* findGrade(const std::string& key);

/// Sizes the family's tables hold ("M6", "1/4" ...), metric first then inch.
std::vector<std::string> familySizes(Family f);
/// Standard lengths (mm) for the family and size (empty when the family has no length).
std::vector<double> familyLengths(Family f, const std::string& size);

struct FastenerSpec {
  Family family = Family::SocketCap;
  std::string size = "M6";
  double length = 20;   ///< mm, ignored for nuts and washers
  std::string grade = "8.8";
  bool shortInsert = false;  ///< heat-set inserts: short variant
};

/// Main dimensions resolved from the tables (mm).
struct FastenerDims {
  double d = 0;            ///< nominal (thread major) or pin diameter
  double headDia = 0;      ///< screws: dk; hex: across corners; washers: outer; inserts: outer
  double headHeight = 0;   ///< k (screws), m / h (nuts), h (washers)
  double wrench = 0;       ///< hex across flats or socket size
  double socketDepth = 0;
  double bore = 0;         ///< nuts / washers / inserts: hole diameter
  double length = 0;       ///< shank / pin / insert / set screw length
  double hexHeight = 0;    ///< nyloc: height of the hex part
  double recommendedHole = 0;  ///< inserts: recommended hole diameter
};
std::optional<FastenerDims> fastenerDims(const FastenerSpec& s, std::string* error = nullptr);

/// Build the solid in the frame above. Null + error for unknown sizes.
TopoDS_Shape makeFastener(const FastenerSpec& s, std::string* error = nullptr);
/// Closed-form volume (mm3) from the catalogue dimensions: the reference the generated solid is
/// checked against.
double nominalVolume(const FastenerSpec& s);
/// Height of the part above the seating plane (head, nut or washer thickness).
double stackHeight(const FastenerSpec& s);

/// BOM data: "ISO 4762 M6x20 8.8", "Socket head cap screw", "ISO 4762".
std::string partNumber(const FastenerSpec& s);
std::string familyDescription(Family f);
std::string standardName(Family f, bool inch);

/// Screw size whose clearance hole (any fit, +-0.05 mm), tap drill or nominal diameter matches a
/// hole diameter (mm); inch sizes when `inch`. Empty when nothing matches.
std::string sizeForHoleDiameter(double holeDia, bool inch);
/// The shortest standard length >= `minLength` (else the longest).
double nextStandardLength(Family f, const std::string& size, double minLength);
/// Cosmetic thread length of a screw (ISO 4762 b = 2d + 12 rounded; full thread when short).
double threadLength(const FastenerSpec& s);
/// Thread designation for the size ("M6" -> "M6x1", "1/4" -> "1/4-20").
std::string threadDesignationFor(const std::string& size);

}  // namespace fabricad::part::standards
