// FabriCAD part standards: screw thread tables. ISO 261 metric threads live in
// part/ExtraUtils (isoMetricThreads(), kept for the Thread feature); this adds the unified inch
// series (ASME B1.1 UNC / UNF, #0 to 1 1/2") with the tap drill sizes for ~75 % thread, and one
// lookup over both standards. ThreadSpec is the plan's name for the shared row type.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "part/ExtraUtils.h"

#include <string>
#include <vector>

namespace fabricad::part::standards {

using ThreadSpec = IsoThread;

constexpr double kInch = 25.4;

/// Hole / fastener standard: ISO metric (mm) or ANSI unified inch.
enum class Standard { Iso = 0, Ansi = 1 };

/// UNC and UNF threads ordered by size, UNC first for each size. Values in mm.
const std::vector<ThreadSpec>& inchThreads();
/// Designations of inchThreads() in order ("#10-24", "#10-32", "1/4-20", ...).
const std::vector<std::string>& inchDesignations();
/// "1/4-20", "#10-32", also "1/4" (= UNC) and "1/4-20 UNC". nullptr if unknown.
const ThreadSpec* findInchThread(const std::string& designation);
/// Any standard: metric designations through findIsoThread(), else inch.
const ThreadSpec* findThread(const std::string& designation);
/// Size part of a designation: "M6x1" -> "M6", "1/4-20" -> "1/4", "#10-32" -> "#10".
std::string sizeOf(const std::string& designation);
/// Default thread class: 6H / 6g (ISO 965), 2B / 2A (ASME B1.1).
std::string defaultThreadClass(const ThreadSpec& t, bool internal);
/// Inch value as the closest common fraction when exact to 1/64 ("0.25" -> "1/4"), else decimals.
std::string inchText(double inches);

}  // namespace fabricad::part::standards
