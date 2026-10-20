// FabriCAD sketch tools: shared base for sketch-mode tools (session access, cursor mapping,
// inference glyphs, temporary previews, AutoCAD-style typed input).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Tool.h"
#include "core/ViewerContext.h"
#include "sketch/SketchInference.h"
#include "sketch/SketchSession.h"

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace fabricad::sketch {

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

/// Arc starting at `p` with tangent direction `t`, ending at `q` (nullopt if q is on the tangent line).
/// `startIsP` tells whether the (ccw) arc's start point is p.
std::optional<Geometry> tangentArc(Pt2 p, Pt2 t, Pt2 q, bool* startIsP);

/// Common behaviour of every tool that runs inside sketch mode.
class SketchTool : public core::ToolBase {
 public:
  using ToolBase::ToolBase;
  /// Enabled only while a sketch session is active.
  bool isEnabled(const core::ToolContext& ctx) const override;
  void activate(core::ToolContext& ctx) override;
  void deactivate(core::ToolContext& ctx) override;
  /// Esc cancels, Return/Enter accepts (tools may override and call the base).
  bool keyPress(core::ToolContext& ctx, const core::ViewerKeyEvent& e) override;

 protected:
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
};

}  // namespace fabricad::sketch
