// FabriCAD sketch tools: shared base for sketch-mode tools (session access, cursor mapping,
// inference glyphs, temporary previews, AutoCAD-style typed input).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Tool.h"
#include "core/ViewerContext.h"
#include "sketch/SketchInference.h"
#include "sketch/SketchSession.h"
#include "sketch/ValueInput.h"

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace fabricad::sketch {

class ValueInputOverlay;

/// Parsed dynamic-input text.
struct TypedInput {
  enum class Kind { None, Number, Pair, Relative, Polar, Text };
  Kind kind = Kind::None;
  double a = 0;  ///< Number: value; Pair/Relative: x; Polar: length
  double b = 0;  ///< Pair/Relative: y; Polar: angle (degrees)
  std::string text;
  bool isNumber() const { return kind == Kind::Number; }
};
/// "12.5" -> Number; "10,20" -> Pair; "@5,-3" -> Relative; "@50<30" -> Polar; anything else Text.
TypedInput parseTypedInput(const std::string& text);

/// Evaluate a typed length/angle expression (units, document parameters and sketch
/// dimension names of the active session). Returns nullopt on error (message in *error).
std::optional<double> evaluateTyped(const core::ToolContext& ctx, const std::string& text, bool angle,
                                    std::string* error = nullptr);

/// The sketch origin (within `tol`) or an origin axis (within 0.75 * `tol`) at sketch point `c`.
/// Tools stand these in with Sketch::originHelper() when the user picks them.
inline std::optional<OriginRef> originRefAt(Pt2 c, double tol) {
  if (c.length() <= tol) return OriginRef::Point;
  if (std::abs(c.y) <= tol * 0.75) return OriginRef::XAxis;
  if (std::abs(c.x) <= tol * 0.75) return OriginRef::YAxis;
  return std::nullopt;
}

/// Arc starting at `p` with tangent direction `t`, ending at `q` (nullopt if q is on the tangent line).
/// `startIsP` tells whether the (ccw) arc's start point is p.
std::optional<Geometry> tangentArc(Pt2 p, Pt2 t, Pt2 q, bool* startIsP);

/// Common behaviour of every tool that runs inside sketch mode.
/// "Typed values create dimensions" (preference sketch/typedDimensions, default on; the
/// session override set by the SKETCH palette toggle / the TYPEDDIMS verb wins).
bool typedDimensionsEnabled();
void setTypedDimensionsOverride(std::optional<bool> on);

class SketchTool : public core::ToolBase {
 public:
  explicit SketchTool(core::ToolInfo info);
  ~SketchTool() override;
  /// Enabled only while a sketch session is active.
  bool isEnabled(const core::ToolContext& ctx) const override;
  void activate(core::ToolContext& ctx) override;
  void deactivate(core::ToolContext& ctx) override;
  /// Keys go to the value boxes first (typing, Tab, Backspace, Esc clears, Enter commits via
  /// commitValues()); otherwise Esc cancels and Return/Enter accepts. Tools may override and
  /// call the base.
  bool keyPress(core::ToolContext& ctx, const core::ViewerKeyEvent& e) override;

  /// The on-screen value boxes of this tool (tests inspect them).
  const ValueInputModel& values() const { return values_; }
  ValueInputModel& values() { return values_; }

 protected:
  // --- on-screen value boxes (Fusion dynamic input) -----------------------------------------
  /// Declare the boxes this tool shows (call from activate(); replaces the previous set).
  void defineValues(std::vector<ValueField> fields);
  /// Feed the measured value for a box and anchor it at a sketch point (both from mouseMove).
  void setValue(const std::string& id, double value, Pt2 anchor);
  void setValueVisible(const std::string& id, bool visible);
  /// Locked (typed) value of a box, nullopt while it follows the cursor.
  std::optional<double> lockedValue(const std::string& id) const { return values_.lockedValue(id); }
  bool isLocked(const std::string& id) const { return values_.isLocked(id); }
  /// Draw the boxes: widgets when the viewer has one, else a preview text label next to `near`.
  void showValueBoxes(Pt2 near);
  /// Hook: a box was typed into / locked / unlocked -> redraw the rubber band from the last cursor.
  virtual void valuesChanged(core::ToolContext& ctx) { (void)ctx; }
  /// Hook: Enter with typed values -> create the geometry from the locked values. Return false
  /// when the tool cannot (e.g. no first point yet); the base then treats Enter as accept().
  virtual bool commitValues(core::ToolContext& ctx) { (void)ctx; return false; }
  /// The value-box part of keyPress(): true when the key was consumed (typing, Tab, Esc clearing
  /// a box, or Enter that commitValues() handled). Tools overriding keyPress() call this first.
  bool handleValueKey(core::ToolContext& ctx, const core::ViewerKeyEvent& e);
  /// Route command-line text into the boxes: "30" -> active box; "30,20" / "50<30" -> consecutive
  /// visible boxes. Locks them and calls commitValues(). False when the text is not a value list.
  bool typedValuesToBoxes(core::ToolContext& ctx, const std::string& text);
  /// Add a driving dimension for a locked box on freshly created geometry (value + expression
  /// from the typed text). If the sketch rejects it (conflict / redundant) the dimension becomes
  /// driven and a message is shown. Returns the constraint id (-1 when not added).
  int addLockedDimension(core::ToolContext& ctx, const ValueField& f, Constraint c);
  /// Typed absolute coordinates (production-features P1 §2.1): each point of the geometry the
  /// tool just created (ids not in `before`) that sits at a typed coordinate gets DistanceX +
  /// DistanceY to the sketch origin, or a Coincident with the origin at 0,0. Points already
  /// coincident with other geometry are skipped; a dimension that would over-constrain is kept
  /// as driven. No-op when typed dimensions are off (typedDimensionsEnabled()).
  void dimensionTypedPoints(core::ToolContext& ctx, const std::vector<Pt2>& typed, const std::vector<int>& before);
  /// Add a dimension created from a typed value: driving, or driven when redundant/conflicting
  /// (without an error message); a redundant geometric constraint is dropped (-1). Returns the id.
  int addTypedDimension(core::ToolContext& ctx, Constraint c);
  /// Ids of the current geometries (snapshot before a tool creates new ones).
  std::vector<int> geometryIds() const;
  /// H/V/parallel/perpendicular constraint for an angle that is a multiple of 90 degrees (so no
  /// redundant Angle dimension is created). `referenceLine` < 0 means the angle is to +X.
  /// Returns true when such a constraint was added instead of an Angle dimension.
  bool addRightAngleInstead(core::ToolContext& ctx, int lineId, double angleDeg, int referenceLine);
  SketchSession* session() const { return SketchSession::current(); }
  Sketch* sketch() const;
  SketchFeature* feature() const;
  core::Document* document() const;

  /// Sketch-plane point for a mouse event.
  std::optional<Pt2> cursor(const core::ViewerMouseEvent& e) const;
  /// Snap tolerance in mm for `px` pixels near `at`.
  double tol(Pt2 at, double px = 8.0) const;
  /// Ctrl held or snapping disabled in the palette.
  bool snapSuppressed(const core::ViewerMouseEvent& e) const;
  /// Run inference with the session's settings applied (tolerance, suppress flag).
  InferenceResult infer(InferenceInput in, bool suppress) const;
  /// Inference for exactly typed coordinates: only exact hits count.
  InferenceResult inferExact(Pt2 p, std::optional<Pt2> from = std::nullopt) const;
  /// Nearest sketch entity under the cursor (point first, then curve).
  std::optional<SketchHit> hitAt(Pt2 p, double px = 8.0) const;

  // --- temporary previews (removed on deactivate and by clearTemp()) ----------------------
  void clearTemp();
  void previewPolyline(const std::vector<Pt2>& pts, const core::PreviewStyle& st);
  void previewLines(const std::vector<Pt2>& segmentPoints, const core::PreviewStyle& st);
  void previewPoints(const std::vector<Pt2>& pts, const core::PreviewStyle& st);
  void previewText(Pt2 at, const std::string& text, const core::PreviewStyle& st);
  void previewGeometry(const Geometry& g, const core::PreviewStyle& st);
  /// Glyph + tracking lines + snapped point marker for an inference result.
  void showInference(const InferenceResult& r);
  /// Dynamic-input style value box near the cursor ("25.0 mm  30°").
  void showDynamicText(Pt2 near, const std::string& text);

  // --- styles ----------------------------------------------------------------------------------
  static core::PreviewStyle rubberStyle();     ///< blue 2 px rubber band
  static core::PreviewStyle guideStyle();      ///< dashed grey tracking line
  static core::PreviewStyle glyphStyle();      ///< inference glyph text
  static core::PreviewStyle markerStyle();     ///< snapped point marker
  static core::PreviewStyle highlightStyle();  ///< hover highlight (orange)
  static core::PreviewStyle removeStyle();     ///< trim preview (red)
  static core::PreviewStyle constructionStyle();

  // --- creation helpers ----------------------------------------------------------------------
  /// Apply the palette's construction mode to a freshly created geometry.
  Geometry asCreated(Geometry g) const;
  std::string formatLength(double mm) const;
  std::string formatAngle(double deg) const;
  /// Add a non-dimensional constraint, verify it solves; remove it and report when it conflicts.
  /// Returns the constraint id (-1 when rejected).
  int addCheckedConstraint(core::ToolContext& ctx, Constraint c);

  core::IViewerContext* viewer_ = nullptr;
  std::set<core::PreviewHandle> temp_;
  ValueInputModel values_;
  std::unique_ptr<ValueInputOverlay> overlay_;
  core::PreviewHandle valueText_ = core::kInvalidPreview;
  core::ToolManager* manager_ = nullptr;
};

/// Evaluator / formatter for ValueInputModel using the document units, expressions and the
/// active sketch's dimension names (shared with the part module's feature value boxes).
ValueInputModel::Evaluator makeSketchEvaluator(core::Document* doc);
ValueInputModel::Formatter makeUnitFormatter(core::Document* doc);

}  // namespace fabricad::sketch
