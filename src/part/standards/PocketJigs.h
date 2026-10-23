// FabriCAD part standards: pocket-hole jigs. A jig fixes the screw angle to the board face,
// the step (counterbore) and pilot diameters; the board thickness selects the screw length
// (the jig makers' charts). The step depth follows from the geometry: the screw head sits at
// the step bottom with half the screw in each board (the collar setting of the jig).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <string>
#include <vector>

namespace fabricad::part::standards {

struct PocketJig {
  const char* key;    ///< "kreg15", "castle6", "custom"
  const char* name;   ///< "Kreg 15°"
  double angle;       ///< degrees between the screw axis and the entry face
  double pilot;       ///< pilot diameter (mm)
  double step;        ///< step / counterbore diameter (mm)
};
const std::vector<PocketJig>& pocketJigs();
const PocketJig* findPocketJig(const std::string& keyOrName);

/// Pocket screw length (mm) for a board thickness (Kreg chart: 1/2" -> 1", 5/8" -> 1",
/// 3/4" -> 1 1/4", 1" -> 1 1/2", 1 1/2" -> 2 1/2").
double pocketScrewLength(double thickness);
/// Standard pocket screw lengths (mm): 25, 32, 38, 64.
const std::vector<double>& pocketScrewLengths();
/// Board thickness presets (mm): 12, 15, 18, 19, 25, 38.
const std::vector<double>& pocketThicknessPresets();

/// Geometry of one pocket hole in the board frame: distances along the screw axis from the
/// entry point (where the axis meets the entry face).
struct PocketGeometry {
  double axisToExit = 0;   ///< entry point -> exit point (on the edge face)
  double stepDepth = 0;    ///< entry point -> bottom of the step (screw head seat)
  double entryInset = 0;   ///< entry point distance from the edge, in the entry face
};
/// `exitOffset`: exit point offset from mid-thickness towards the entry face.
PocketGeometry pocketGeometry(double thickness, double angleDeg, double screwLength, double exitOffset = 0);

}  // namespace fabricad::part::standards
