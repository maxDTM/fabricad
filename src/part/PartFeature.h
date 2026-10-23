// FabriCAD part module: base class for all part features (solid, construction, import).
//
// Design: recompute() is split into compute() (pure geometry, reads inputs through the
// RecomputeContext, never creates bodies) and commit() (writes bodies / output shape /
// named shapes). Command dialogs run compute() alone on a working feature to produce a
// live preview headlessly, so the whole part module is testable without OpenGL.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Feature.h"
#include "core/ShapeRef.h"
#include "core/Types.h"

#include <TopoDS_Shape.hxx>
#include <gp_Trsf.hxx>

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fabricad::core {
class UndoStack;
}

namespace fabricad::part {

/// Boolean operation of an additive/subtractive feature (Fusion vocabulary).
enum class Operation { Join = 0, Cut = 1, Intersect = 2, NewBody = 3 };
const std::vector<std::string>& operationNames();
Operation operationFromIndex(int i);

/// History names -> sub-shapes (topological naming layer 1).
using NameMap = std::map<std::string, TopoDS_Shape>;

/// One item a command dialog shows while the feature is being defined.
struct PreviewShape {
  TopoDS_Shape shape;
  Operation operation = Operation::NewBody;  ///< picks the preview colour
};

/// Everything compute() produces; commit() writes it into the document.
struct Outcome {
  struct BodyChange {
    core::ObjectId body = core::kInvalidId;  ///< existing body to replace; kInvalidId = create a new body
    TopoDS_Shape shape;                      ///< new shape (null = body consumed/merged away)
    std::string newName;                     ///< name for a new body (empty = "BodyN")
    core::ObjectId component = core::kInvalidId;
  };
  std::vector<BodyChange> bodies;
  TopoDS_Shape tool;        ///< tool solid (stored in Feature::outputShape for Pattern/Mirror)
  TopoDS_Shape display;     ///< display geometry for construction features (plane face, axis edge, point)
  NameMap names;            ///< named sub-shapes of the result (cumulative per body)
  std::vector<PreviewShape> previews;
  Operation operation = Operation::NewBody;
  /// Local placements commit() writes into components (component patterns).
  std::vector<std::pair<core::ObjectId, gp_Trsf>> placements;

  void addNew(const TopoDS_Shape& shape, const std::string& name = {}, core::ObjectId component = core::kInvalidId) {
    bodies.push_back({core::kInvalidId, shape, name, component});
  }
  void addModified(core::ObjectId body, const TopoDS_Shape& shape) { bodies.push_back({body, shape, {}, core::kInvalidId}); }
  void addPreview(const TopoDS_Shape& s, Operation op) {
    if (!s.IsNull()) previews.push_back({s, op});
  }
};

class PartFeature : public core::Feature {
 public:
  explicit PartFeature(std::string type) : core::Feature(std::move(type)) {}

  /// compute() + commit(). Final: subclasses implement compute().
  void recompute(core::RecomputeContext& ctx) final;

  /// Batch mode (FeatureTool, script runs): the Outcome of the preview that was computed from
  /// `inputState` just before the feature is committed. The next recompute commits it instead of
  /// computing again when its input state is the same (one shot; any other input recomputes).
  void adoptPrecomputed(Outcome out, std::vector<std::string> warnings, std::map<core::ObjectId, TopoDS_Shape> inputState);
  bool hasPrecomputed() const { return precomputed_.has_value(); }
  /// Status-line notes of the last computation about bodies of other components (COMPARISON.md
  /// recommendation 6): a Join/Cut/Intersect/Hole that changes a body of another component, or
  /// whose tool touches one it leaves alone. FeatureTool shows them after OK.
  const std::vector<std::string>& crossComponentNotes() const { return crossComponentNotes_; }
  void addCrossComponentNote(std::string note) { crossComponentNotes_.push_back(std::move(note)); }

  /// Only an Outcome with component placements (component patterns) moves components.
  bool affectsPlacements() const override { return placementsWritten_; }
  /// Body features work in component-local coordinates (TransformFeature overrides: component patterns).
  bool readsPlacements() const override { return false; }

  /// Geometry only. Must call ctx.setError() (and return) on failure. Must not call
  /// ctx.createBody/setBodyShape. Reference indices may be written back (ctx.resolve).
  virtual void compute(core::RecomputeContext& ctx, Outcome& out) = 0;

  /// Write an Outcome into the document state (bodies, output shape, named shapes).
  void commit(core::RecomputeContext& ctx, const Outcome& out);

  /// Called by dialogs after an input changed: adjust PropertyFlags::hidden of properties
  /// that only make sense for some enum choices.
  void updatePropertyFlags() override {}

  /// Property that receives the preselection when the tool starts ("profiles", "edges"...).
  virtual std::string primaryInput() const;
  /// Numeric property a bare typed value goes to ("EXTRUDE 25", dynamic input): "distance",
  /// "radius", "thickness"... Default: a well-known name, else the first visible Length/Angle.
  virtual std::string primaryNumeric() const;
  /// String property a bare typed value (or the first command-line argument) goes to when the
  /// feature has no primary numeric: "filePath" for Import. Empty by default.
  virtual std::string primaryText() const { return {}; }
  /// Inputs a preselected reference goes to when a new feature starts (empty = the default:
  /// the primary input, else the first input accepting the kind). Hole: a preselected face is the
  /// start face (and, with a pick point, also a placement point), COMPARISON.md recommendation 7.
  virtual std::vector<std::string> preselectionInputs(const core::ShapeRef& ref) const {
    (void)ref;
    return {};
  }

  /// The feature's boolean operation (from an "operation" enum if it has one).
  virtual Operation operation() const;

  /// True when picks on the same B-rep entity at different points are different inputs of
  /// `propName` (Hole on a face: every click is a hole). The pick point is stored in the
  /// body's local coordinates.
  virtual bool refsDistinguishedByPoint(const std::string& propName) const {
    (void)propName;
    return false;
  }

  /// Solid used by Pattern/Mirror when patterning this feature (default: outputShape()).
  virtual TopoDS_Shape toolShape() const { return outputShape(); }

  /// Features that own document objects besides bodies (component pattern occurrences) create
  /// or remove them here, right before the tool pushes the Add/Modify feature command (both
  /// inside one undo macro). `undo` may be null (direct edit).
  virtual bool wantsCommitPreparation() const { return false; }
  virtual void prepareCommit(core::Document& doc, core::UndoStack* undo) {
    (void)doc;
    (void)undo;
  }

  /// Default: the "operation" enum, if present, is set to New Body when the document has
  /// no live body yet and to Join otherwise (Fusion behaviour on dialog open).
  virtual void applyDefaultsForNewFeature(const core::Document& doc);

 private:
  bool placementsWritten_ = false;  ///< the last commit() wrote component placements
  struct Precomputed {
    Outcome out;
    std::vector<std::string> warnings;
    std::map<core::ObjectId, TopoDS_Shape> input;
    std::vector<TopoDS_Shape> scopeProbes;
    bool scopeOpaque = false;
  };
  std::optional<Precomputed> precomputed_;
  std::vector<std::string> crossComponentNotes_;
};

/// Convenience for features with an "operation" enum: adds the enum and the optional
/// participant list ("targets", bodies to join/cut/intersect; empty = automatic).
void addOperationProperties(core::Feature& f, Operation def = Operation::Join);

}  // namespace fabricad::part
