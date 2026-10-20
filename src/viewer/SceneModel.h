// FabriCAD viewer: GL-free "what should be on screen" model.
//
// SceneModel::collect() turns the Document (+ viewer options) into a flat list of display
// items with stable keys. ViewerWidget diffs that list against its AIS objects, so display
// sync is one cheap pass after any Document signal (coalesced), and the decision logic is
// unit-tested headless (src/viewer/tests/test_scene_model.cpp).
//
// Sketch display convention (agreed in docs/integration-notes/ui.md): a sketch feature's
// outputShape() is drawn as under-constrained curves. If the feature also fills
// namedShapes() entries "display:<style>" (free, defined, construction, fixed, projected,
// conflict, points), those compounds are drawn instead, each in its style colour.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/ShapeRef.h"
#include "core/Types.h"

#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>

#include <array>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace fabricad::core {
class Document;
class Feature;
class IProfileSource;
}  // namespace fabricad::core

namespace fabricad::viewer {

enum class SceneStyle {
  Body,
  SketchFree,          ///< under-constrained curves (blue)
  SketchDefined,       ///< fully constrained (black)
  SketchConstruction,  ///< orange dashed
  SketchFixed,         ///< green
  SketchProjected,     ///< purple
  SketchConflict,      ///< red
  SketchPoints,        ///< point markers
  Profile,             ///< pickable closed region (light orange)
  ConstructionPlane,
  ConstructionAxis,
  ConstructionPoint,
  Joint,
  OriginPlane,
  OriginAxis,
  OriginPoint,
};
const char* sceneStyleName(SceneStyle s);
/// "display:construction" -> SketchConstruction; nullopt if not a display key.
std::optional<SceneStyle> sketchStyleFromDisplayKey(const std::string& key);

struct SceneItem {
  std::string key;          ///< stable identity: "body:18", "feat:16", "feat:16/display:fixed", "origin:XY", "profile:16:1,2,3"
  SceneStyle style = SceneStyle::Body;
  core::ShapeRef ref;       ///< what picking the item as a whole means
  TopoDS_Shape shape;       ///< in component-local coordinates
  gp_Trsf placement;        ///< world placement (component)
  std::array<float, 4> color{0.73f, 0.74f, 0.77f, 1.0f};
  bool ghosted = false;     ///< in-context editing: drawn at 30% opacity, not pickable
  std::size_t geomHash = 0; ///< changes when the geometry changes (shape handle + style)
};

struct SceneOptions {
  std::set<core::ObjectId> hiddenFeatures;                        ///< user eye toggles (sketches, construction, joints)
  std::map<core::ObjectId, std::optional<bool>> visibilityOverrides;  ///< from tools (IViewerContext)
  bool showOrigin = false;        ///< origin planes/axes/point (browser eye)
  bool showOriginPlanes = false;  ///< planes only (a tool is picking a plane)
  bool showSketches = true;       ///< folder eyes
  bool showConstruction = true;
  bool showJoints = true;
  bool showProfiles = false;      ///< a tool is picking profiles
  std::vector<core::ObjectId> activeComponents;  ///< non-empty: others are ghosted
  double originSize = 40.0;       ///< half size of origin planes, mm
};

/// Sketch curve lookup for picking (curve ids are probed through IProfileSource::curveEdge
/// because the interface has no id list; see integration notes).
struct SketchCurveInfo {
  int id = -1;
  TopoDS_Edge edge;
  gp_Pnt mid;
};
std::vector<int> probeCurveIds(const core::IProfileSource& src, int maxId = 4096, int maxGap = 256);

class SceneModel {
 public:
  std::vector<SceneItem> collect(const core::Document& doc, const SceneOptions& opt);

  /// Map a picked edge of a sketch item to its curve id (-1 if unknown).
  int sketchCurveForEdge(const core::Document& doc, core::ObjectId sketchId, const TopoDS_Shape& edge);
  /// Candidate sketch points (curve start/end/center + standalone points) in world coords.
  struct PointCandidate {
    core::ShapeRef ref;
    gp_Pnt position;
  };
  const std::vector<PointCandidate>& sketchPoints(const core::Document& doc, core::ObjectId sketchId);
  /// Drop caches (document reset).
  void clear() { sketches_.clear(); profiles_.clear(); }

  /// Whether a feature's display output is currently visible under `opt`.
  static bool featureVisible(const core::Document& doc, const core::Feature& f, const SceneOptions& opt);

 private:
  struct SketchCache {
    std::size_t hash = 0;
    std::vector<SketchCurveInfo> curves;
    std::vector<PointCandidate> points;
    bool pointsBuilt = false;
  };
  SketchCache& sketchCache(const core::Document& doc, core::ObjectId sketchId);
  struct ProfileCache {
    std::size_t hash = 0;
    std::vector<SceneItem> items;
  };
  std::map<core::ObjectId, SketchCache> sketches_;
  std::map<core::ObjectId, ProfileCache> profiles_;
};

/// Style colours (ux.md section 6.1 / 8.12).
std::array<float, 4> styleColor(SceneStyle s);

}  // namespace fabricad::viewer
