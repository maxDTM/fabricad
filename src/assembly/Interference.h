// FabriCAD assembly: interference (clash) check between body instances.
// Pairs are pre-filtered with axis-aligned bounding boxes, then intersected with
// BRepAlgoAPI_Common; pairs whose common volume exceeds `minVolume` are reported with the
// intersection solid in world coordinates (for red highlighting / "create bodies").
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "assembly/ComponentOps.h"
#include "core/Document.h"
#include "core/Types.h"

#include <TopoDS_Shape.hxx>

#include <functional>
#include <string>
#include <vector>

namespace fabricad::assembly {

struct InterferenceOptions {
  double minVolume = 1e-3;             ///< mm^3; smaller intersections are noise (touching faces)
  bool sameComponentPairs = false;     ///< also test bodies that share a component
  double bboxGap = 1e-6;               ///< boxes enlarged by this before the overlap test
  std::function<bool()> cancelled;     ///< polled between pairs
};

struct InterferencePair {
  BodyInstance a, b;
  double volume = 0.0;    ///< mm^3 of the intersection
  TopoDS_Shape shape;     ///< intersection solid(s), world coordinates
  std::string label() const;  ///< "Body1 (Bracket:1) x Body2 (Bolt:1)"
};

struct InterferenceReport {
  std::vector<InterferencePair> pairs;
  int instances = 0;
  int pairsTested = 0;      ///< pairs that reached the boolean
  int pairsSkipped = 0;     ///< rejected by the bounding-box prefilter
  bool cancelled = false;
  std::vector<std::string> errors;
  double totalVolume() const;
};

/// Check the given components (empty = whole document, children included).
InterferenceReport checkInterference(const core::Document& doc, const std::vector<core::ObjectId>& components = {},
                                     const InterferenceOptions& options = {});
/// Check explicit body instances (world shapes already set).
InterferenceReport checkInterference(const std::vector<BodyInstance>& instances, const core::Document* doc,
                                     const InterferenceOptions& options = {});

}  // namespace fabricad::assembly
