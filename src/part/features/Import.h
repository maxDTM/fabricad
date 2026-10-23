// FabriCAD part: Import feature (STEP / STL files become bodies) and export helpers.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "part/PartFeature.h"
#include "part/io/StepIO.h"

#include <TopoDS_Shape.hxx>
#include <gp_Trsf.hxx>

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fabricad::part {

class ImportFeature : public PartFeature {
 public:
  static constexpr const char* kType = "Import";
  ImportFeature();
  void compute(core::RecomputeContext& ctx, Outcome& out) override;
  std::string browserFolder() const override { return {}; }
  void applyDefaultsForNewFeature(const core::Document&) override {}
  std::string primaryText() const override { return "filePath"; }

  /// Read a STEP (.step/.stp) or STL (.stl) file into one shape per body. Errors in `error`.
  struct Imported {
    std::vector<TopoDS_Shape> shapes;
    std::vector<std::string> names;
    io::ImportReport report;  ///< entities, failed faces, healing applied (STEP / IGES)
  };
  static bool readFile(const std::string& path, Imported& out, std::string* error);
  /// Same with the sew / fix / unify options (STEP and IGES, P3).
  static bool readFile(const std::string& path, const io::ImportOptions& options, Imported& out, std::string* error);
  /// Report of the last successful read of this feature's file (empty `file` before).
  const io::ImportReport& lastReport() const { return cache_.report; }
  /// The sew / fix / unify properties.
  io::ImportOptions importOptions() const;

  /// Assembly structure of a STEP file (XCAF): product tree with names, instance placements and
  /// shared part definitions. Parts are keyed by their XCAF label entry ("0:1:1:3"), which is
  /// stable for the same file.
  struct StepPart {
    std::string name;
    std::vector<TopoDS_Shape> shapes;      ///< one per solid (the whole shape when it has none), part-local
    std::vector<std::string> bodyNames;    ///< parallel to `shapes`
  };
  struct StepNode {
    std::string name;
    gp_Trsf placement;                     ///< relative to the parent node
    std::string prototype;                 ///< entry of the referred product (shared by its instances)
    std::string part;                      ///< non-empty: a leaf instance of this part
    std::vector<std::string> ownParts;     ///< parts whose bodies sit directly in this node's component
    std::vector<StepNode> children;
  };
  struct StepAssembly {
    std::string topName;                   ///< name of the single top assembly (or empty)
    StepNode top;                          ///< virtual top: children = top-level components
    std::map<std::string, StepPart> parts;
    /// AP242 / AP214 product properties (part number, description, material, revision) by product name.
    std::map<std::string, io::ProductProperties> properties;
    /// True when the file has at least one assembly (so components are worth creating).
    bool hasStructure() const { return !top.children.empty(); }
  };
  /// Read (cached per path and modification time). Null with `error` set on failure.
  static std::shared_ptr<const StepAssembly> readStepAssembly(const std::string& path, std::string* error);

  /// STEP file with assembly structure and "keepStructure" on: the import creates components.
  bool wantsCommitPreparation() const override;
  /// Create (or, when the file or options changed, rebuild) the component tree of the file.
  void prepareCommit(core::Document& doc, core::UndoStack* undo) override;
  /// Components this import created (from the hidden "structure" property).
  std::vector<core::ObjectId> structureComponents() const;

 private:
  bool usesStructureFile() const;
  std::string cachedPath_;
  long long cachedStamp_ = -1;
  io::ImportOptions cachedOptions_;
  Imported cache_;
};

/// Write shapes to a STEP file (AP214 by default, mm). One product per shape, named, with the colour. `pcurves` = false omits the
/// 2D parameter curves of the edges (OCCT write.surfacecurve.mode 0): a smaller file that most
/// importers rebuild (COMPARISON.md recommendation 8).
bool exportStep(const std::vector<TopoDS_Shape>& shapes, const std::vector<std::string>& names, const std::string& path,
                std::string* error, bool pcurves = true);
/// Full form: schema "AP203" / "AP214" / "AP242", per-shape RGBA colours (may be empty) and
/// product properties keyed by shape name (written as AP242 / AP214 product attributes).
struct StepBodyExport {
  std::string schema = "AP214";
  bool pcurves = true;
  std::vector<std::array<float, 4>> colors;
  std::map<std::string, io::ProductProperties> properties;
};
bool exportStep(const std::vector<TopoDS_Shape>& shapes, const std::vector<std::string>& names, const std::string& path,
                const StepBodyExport& options, std::string* error);
/// Preference "export/step/pcurves" (QSettings, default true): write pcurves into STEP files.
bool stepPcurvesDefault();
void setStepPcurvesDefault(bool on);
/// Command-line STEP option words: "pcurves=off|on|0|1|no|yes", "nopcurves". False if the word
/// is not an option (then it is the path).
bool applyStepOption(const std::string& word, bool& pcurves);
/// Also "schema=ap203|ap214|ap242" (or the bare "ap242").
bool applyStepOption(const std::string& word, bool& pcurves, std::string& schema);
/// Apply the pcurve mode to OCCT's STEP writer statics (both writers call it before Transfer).
void setStepWriterPcurves(bool pcurves);
/// Write shapes to an STL file (binary by default) after meshing with the given deflection.
bool exportStl(const std::vector<TopoDS_Shape>& shapes, const std::string& path, bool binary, double linearDeflection,
               double angularDeflectionDeg, std::string* error);

}  // namespace fabricad::part
