// FabriCAD sketch: the sketch-mode edit session. One sketch feature is edited at a time;
// the session owns the Fusion-style rendering (geometry coloured by constraint status,
// constraint glyphs, dimension labels) through the viewer preview API, converts between
// world and sketch coordinates, snapshots the feature for undoable commits, and keeps the
// idle "sketch.select" tool running between commands.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Tool.h"
#include "core/ViewerContext.h"
#include "sketch/SketchFeature.h"

#include <QJsonObject>
#include <QObject>

#include <optional>
#include <map>
#include <set>
#include <string>
#include <vector>

class QTimer;

namespace fabricad::core {
class Document;
class SelectionModel;
class UndoStack;
class IAppHost;
class ToolManager;
}  // namespace fabricad::core

namespace fabricad::sketch {

/// Fusion 360 light-theme sketch colours (RGBA 0..1).
namespace colors {
constexpr std::array<float, 4> kFree{0.118f, 0.435f, 0.851f, 1.0f};         // #1E6FD9
constexpr std::array<float, 4> kConstrained{0.0f, 0.0f, 0.0f, 1.0f};        // #000000
constexpr std::array<float, 4> kConstruction{0.898f, 0.541f, 0.0f, 1.0f};   // #E58A00
constexpr std::array<float, 4> kProjected{0.608f, 0.188f, 0.851f, 1.0f};    // #9B30D9
constexpr std::array<float, 4> kFixed{0.180f, 0.620f, 0.180f, 1.0f};        // #2E9E2E
constexpr std::array<float, 4> kConflict{0.878f, 0.125f, 0.125f, 1.0f};     // #E02020
constexpr std::array<float, 4> kSelected{0.043f, 0.424f, 1.0f, 1.0f};       // #0B6CFF
constexpr std::array<float, 4> kDimension{0.30f, 0.30f, 0.30f, 1.0f};
constexpr std::array<float, 4> kGlyph{0.35f, 0.35f, 0.35f, 1.0f};
constexpr std::array<float, 4> kProfile{1.0f, 0.788f, 0.541f, 0.4f};        // #FFC98A
constexpr std::array<float, 4> kTrim{0.878f, 0.125f, 0.125f, 1.0f};
}  // namespace colors

class SketchSession : public QObject {
  Q_OBJECT
 public:
  ~SketchSession() override;

  /// The active session (nullptr when not in sketch mode).
  static SketchSession* current();
  /// Enter sketch mode on an existing sketch feature. Returns nullptr if the id is not a sketch.
  static SketchSession* begin(core::ToolContext& ctx, core::ObjectId sketchId);
  /// Leave sketch mode (Finish Sketch). Keeps the camera (Fusion behaviour).
  static void end();
  /// True while end() is tearing the session down (tools must then really finish on cancel).
  bool isEnding() const { return ending_; }

  core::Document* document() const { return doc_; }
  core::ObjectId sketchId() const { return sketchId_; }
  SketchFeature* feature() const;
  Sketch* sketch() const;
  core::IViewerContext* viewer() const { return viewer_; }
  core::IAppHost* host() const { return host_; }
  core::ToolManager* manager() const { return manager_; }
  core::UndoStack* undo() const { return undo_; }
  core::SelectionModel* selection() const { return selection_; }

  // --- coordinates -----------------------------------------------------------------
  Pt2 toSketch(const core::Vec3& world) const;
  core::Vec3 toWorld(Pt2 p) const;
  /// Sketch-plane point for a mouse event (planePoint, or a ray/plane intersection).
  std::optional<Pt2> cursor(const core::ViewerMouseEvent& e) const;
  /// Snap tolerance in mm for `pixels` screen pixels around the sketch point.
  double tolerance(Pt2 near, double pixels = 8.0) const;

  // --- editing -------------------------------------------------------------------------
  /// Snapshot the feature (call before mutating the sketch).
  void beginEdit();
  /// Push a ModifyFeatureCommand(before, after) with `text`, recompute, re-render. Returns false
  /// if nothing changed. Re-solves the sketch first so the stored state is consistent.
  bool commitEdit(const std::string& text);
  /// Throw away changes since beginEdit().
  void cancelEdit();
  bool isEditing() const { return editing_; }
  /// Re-solve + rebuild output without a command (live preview during drags). Re-renders.
  void refresh();
  /// Delete the selected geometry/constraints (Delete key).
  bool deleteSelection();

  // --- options (Sketch Palette) -----------------------------------------------------------
  bool constructionMode() const { return constructionMode_; }
  void setConstructionMode(bool on) { constructionMode_ = on; }
  bool snapEnabled() const { return snapEnabled_; }
  void setSnapEnabled(bool on) { snapEnabled_ = on; }
  bool showConstraints() const { return showConstraints_; }
  void setShowConstraints(bool on) { showConstraints_ = on; render(); }
  bool showDimensions() const { return showDimensions_; }
  void setShowDimensions(bool on) { showDimensions_ = on; render(); }
  bool showPoints() const { return showPoints_; }
  void setShowPoints(bool on) { showPoints_ = on; render(); }
  bool showProfiles() const { return showProfiles_; }
  void setShowProfiles(bool on) { showProfiles_ = on; render(); }

  // --- selection -----------------------------------------------------------------------
  core::ShapeRef toShapeRef(const GeoRef& r) const;
  std::optional<GeoRef> fromShapeRef(const core::ShapeRef& ref) const;
  /// Sketch entities in the document selection (this sketch only).
  std::vector<GeoRef> selectedRefs() const;
  std::vector<int> selectedCurves() const;
  const std::set<int>& selectedConstraints() const { return selectedConstraints_; }
  void selectConstraint(int id, bool add);
  void clearConstraintSelection();
  /// Nearest geometry/point under the cursor (tool-independent hit test).
  std::optional<SketchHit> hitAt(Pt2 p, double tol) const;
  /// Dimension whose label is under the cursor.
  std::optional<int> dimensionAt(Pt2 p, double tol) const;

  // --- rendering ---------------------------------------------------------------------------
  /// Redraw the whole sketch as previews (called after every change and after tools finish).
  void render();
  /// Preview handle set owned by the session (so tools never remove them).
  bool ownsPreview(core::PreviewHandle h) const { return handles_.count(h) > 0; }
  /// Formats a dimension value for display ("40.00", "R5", "90°", "(12)" for driven).
  std::string dimensionText(const Constraint& c) const;
  /// "Sketch1: 3 DOF" for the status bar.
  std::string statusText() const;

 signals:
  void changed();

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  SketchSession(core::ToolContext& ctx, core::ObjectId sketchId);
  void enter();
  void leave();
  void onToolFinished(const std::string& id, bool accepted);
  void ensureIdleTool();
  void renderGeometry();
  void renderConstraints();
  void renderDimensions();
  void renderProfiles();
  void drawDimension(const Constraint& c);
  void addLines(const std::vector<core::Vec3>& pts, const core::PreviewStyle& st);
  void addPolyline(const std::vector<core::Vec3>& pts, const core::PreviewStyle& st);
  void addPoints(const std::vector<core::Vec3>& pts, const core::PreviewStyle& st);
  void addText(const core::Vec3& at, const std::string& text, const core::PreviewStyle& st);

  core::Document* doc_ = nullptr;
  core::SelectionModel* selection_ = nullptr;
  core::UndoStack* undo_ = nullptr;
  core::IViewerContext* viewer_ = nullptr;
  core::IAppHost* host_ = nullptr;
  core::ToolManager* manager_ = nullptr;
  core::ObjectId sketchId_ = core::kInvalidId;
  gp_Ax3 plane_;

  bool editing_ = false;
  QJsonObject before_;
  bool constructionMode_ = false;
  bool snapEnabled_ = true;
  bool showConstraints_ = true;
  bool showDimensions_ = true;
  bool showPoints_ = true;
  bool showProfiles_ = true;  // Fusion shows profiles by default (UX-26)
  std::set<int> selectedConstraints_;
  std::map<int, Pt2> textPos_;  ///< where each dimension's text was drawn (beside its line), for picking
  std::set<core::PreviewHandle> handles_;
  std::vector<QMetaObject::Connection> connections_;
  bool rendering_ = false;
  double renderedPixelSize_ = 0;  ///< pixel size (mm) of the last render, to re-render after a zoom
  bool zoomCheckPending_ = false;
  bool redrawPosted_ = false;   ///< a deferred full redraw is queued (SK-02)
  QTimer* zoomPoll_ = nullptr;  ///< catches camera animations (F6 Fit, ViewCube) the events miss (SK-11)
  /// Re-render when the pixel size changed since the last render (glyphs keep their size).
  void checkPixelSize();
  bool ending_ = false;
};

}  // namespace fabricad::sketch
