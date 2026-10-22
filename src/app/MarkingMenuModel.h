// FabriCAD app: content and hit-testing of the right-click marking menu (ux.md 6.3),
// widget-free so the context rules are unit-tested (tests/test_app_logic.cpp).
//
// Eight radial slots (N, NE, E, SE, S, SW, W, NW) plus a linear overflow list. Entries are
// tool ids, or one of the pseudo commands below that the shell handles itself.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/ShapeRef.h"
#include "core/Types.h"

#include <array>
#include <string>
#include <vector>

namespace fabricad::core {
class ToolRegistry;
}

namespace fabricad::app {

inline constexpr const char* kCmdOk = "#ok";
inline constexpr const char* kCmdCancel = "#cancel";
inline constexpr const char* kCmdRepeat = "#repeat";
inline constexpr const char* kCmdSeparator = "#sep";

enum Slot : int { N = 0, NE, E, SE, S, SW, W, NW, kSlotCount };

struct MarkingEntry {
  std::string command;  ///< tool id or pseudo command; empty = unused slot
  std::string label;    ///< display text
};

struct MarkingMenuModel {
  std::array<MarkingEntry, kSlotCount> radial;
  std::vector<MarkingEntry> overflow;
  bool isEmpty() const;
};

struct MarkingContext {
  bool toolActive = false;
  std::string activeToolName;  ///< for "OK"/"Cancel" labels
  std::string lastToolId;      ///< "Repeat <name>" (the #repeat pseudo command repeats this one)
  /// Last command outside the SKETCH tab. Outside a sketch, Repeat names this one (Fusion repeats
  /// the last solid command); when it differs from lastToolId the entry runs it by id.
  std::string lastModelToolId;
  bool inSketch = false;
  std::vector<core::ShapeKind> selectionKinds;  ///< kinds of the selected items
};

/// Fill the menu for a context; tools not registered (or disabled by `isAvailable`) are skipped.
MarkingMenuModel buildMarkingMenu(const core::ToolRegistry& registry, const MarkingContext& ctx);

/// Slot for a cursor offset from the menu centre (Qt screen coords, y down).
/// -1 inside the dead zone (radius `deadZone` px).
int slotForOffset(double dx, double dy, double deadZone = 15.0);
/// Unit direction of a slot (y down), for laying out the labels.
void slotDirection(int slot, double& dx, double& dy);

}  // namespace fabricad::app
