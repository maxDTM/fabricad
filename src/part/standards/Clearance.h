// FabriCAD part standards: clearance holes for bolts and screws. Metric: ISO 273 fine / medium /
// coarse series (shown as Close / Normal / Loose). Inch: ASME B18.2.8 close / normal / loose
// fits for #0 .. 1 1/2" screws, rounded to drill sizes.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "part/standards/Threads.h"

namespace fabricad::part::standards {

enum class Fit { Close = 0, Normal = 1, Loose = 2 };

struct ClearanceRow {
  const char* size;  ///< "M6" or "1/4" / "#10"
  double close, normal, loose;  ///< mm for metric rows, inches for inch rows
};

/// Clearance hole diameter in mm for the thread's nominal size; sizes outside the tables use
/// 1.07 / 1.1 / 1.2 x d.
double clearanceDiameter(const ThreadSpec& t, Fit fit);

}  // namespace fabricad::part::standards
