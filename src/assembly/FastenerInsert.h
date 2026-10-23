// FabriCAD assembly: Insert Fastener (assembly.insert-fastener, FASTENER). Recognises a hole on
// a body (circular edge or cylindrical face of any origin: Hole, Pocket Hole, imported) and
// places a generated standard part (standards/Fasteners.h) in it as a component: seated on the
// counterbore bottom or the start face, coaxial, with a rigid (or, for pins, revolute) as-built
// joint to the holed component; optional washer under the head and washer + nut on the far
// face. Identical parts share one definition (one BOM row, quantity counted).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Document.h"
#include "core/ShapeRef.h"
#include "core/UndoStack.h"
#include "part/standards/Fasteners.h"

#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <optional>
#include <string>
#include <vector>

namespace fabricad::assembly {

/// A recognised hole in world coordinates.
struct HoleSite {
  core::ObjectId body = core::kInvalidId;
  core::ObjectId component = core::kRootComponentId;  ///< component owning the body
  gp_Pnt axisPoint;       ///< on the axis (world)
  gp_Dir axisIn;          ///< from the entry face into the material
  gp_Pnt entry;           ///< axis meets the entry face (top of the counterbore / hole)
  gp_Pnt seat;            ///< where a head seats: counterbore bottom, else the entry
  double diameter = 0;    ///< smallest (shank) diameter
  double length = 0;      ///< entry -> far end of the hole
  bool through = false;   ///< the hole leaves the body at the far end
  double seatToFar = 0;   ///< grip: seat -> far end
};

/// Analyse a picked circular edge or cylindrical face. nullopt + error when it is not a hole.
std::optional<HoleSite> analyseHole(const core::Document& doc, const core::ShapeRef& ref, std::string* error = nullptr);
/// Every hole of the body with the same diameter, length and direction as `site` (incl. itself).
std::vector<HoleSite> equalHoles(const core::Document& doc, const HoleSite& site);

enum class JointMode { Auto = 0, Rigid, Group, None };

struct InsertOptions {
  part::standards::Family family = part::standards::Family::SocketCap;
  std::string size;          ///< empty = from the hole diameter
  double length = 0;         ///< 0 = auto from the grip
  std::string grade = "8.8";
  bool nut = false;          ///< hex nut (with washer) on the far face of through holes
  bool nyloc = false;        ///< the nut is a nyloc nut
  bool washer = false;       ///< washer under the head (and under the nut when `nut`)
  bool allowRotation = false;  ///< revolute instead of rigid joint
  bool allEqual = false;     ///< every equal hole of the body
  JointMode joint = JointMode::Auto;  ///< Auto: rigid joints up to 8 parts, else one rigid group
};

struct InsertResult {
  std::vector<core::ObjectId> components;  ///< created instances (definitions and occurrences)
  int holes = 0;
  std::string partNumber;  ///< of the main part
  std::vector<std::string> warnings;
  std::string error;
  bool ok() const { return error.empty(); }
};

/// Resolve the size and length a hole gets with the options.
part::standards::FastenerSpec specForHole(const HoleSite& site, const InsertOptions& opt, std::string* error);

/// Insert into the holes (one undo step when `undo` is given). Recomputes the document.
InsertResult insertFasteners(core::Document& doc, core::UndoStack* undo, const std::vector<core::ShapeRef>& holes,
                             const InsertOptions& opt);

/// The standard-part definition component with this part number, created on first use.
core::ObjectId standardPartDefinition(core::Document& doc, const part::standards::FastenerSpec& spec);

/// Apply a script word (family=shcs size=M6 length=auto material=8.8 nut=yes washer=yes
/// rotation=yes all=yes joint=rigid|group|none). False for an unknown word or value.
bool applyInsertOption(const std::string& word, InsertOptions& opt);

}  // namespace fabricad::assembly
