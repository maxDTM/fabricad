// FabriCAD part I/O: STEP / IGES reader options, the import report, STEP schema choice and the
// AP242 product properties (part number, description, material, revision) written into and read
// back from the STEP model (production-features.md P3).
//
// Product properties are written in the AP242 / AP214 recommended-practice form the CAx-IF uses
// for user-defined attributes: PRODUCT id = part number, PRODUCT description = description,
// PRODUCT_DEFINITION_FORMATION id = revision, and for each further attribute ("material", plus
// "partNumber" / "revision" / "description" again for readers that only look at properties)
//   PROPERTY_DEFINITION('<key>', '', #product_definition)
//   PROPERTY_DEFINITION_REPRESENTATION(#pd, REPRESENTATION('<key>', (DESCRIPTIVE_REPRESENTATION_ITEM('<key>', '<value>')), #ctx))
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <Standard_Handle.hxx>
#include <TopoDS_Shape.hxx>

#include <map>
#include <string>
#include <vector>

class XSControl_WorkSession;
class StepData_StepModel;

namespace fabricad::part::io {

/// Shape healing applied after a STEP / IGES transfer (the Import feature's sew / fix / unify).
struct ImportOptions {
  bool sew = false;    ///< BRepBuilderAPI_Sewing of loose faces / shells (always on for IGES surfaces)
  bool fix = true;     ///< ShapeFix_Shape
  bool unify = false;  ///< ShapeUpgrade_UnifySameDomain (merge coplanar / co-cylindrical faces)
  double sewTolerance = 1e-3;
};

/// What an import found and did (shown after the import and written to the log / report=file).
struct ImportReport {
  std::string file;
  std::string format;     ///< "STEP", "IGES", "STL"
  std::string schema;     ///< STEP: file schema name
  int entities = 0;       ///< entities in the file model
  int roots = 0;          ///< transferable roots
  int shapes = 0;         ///< shapes transferred
  int solids = 0, shells = 0, faces = 0;
  int failedFaces = 0;    ///< faces BRepCheck_Analyzer reports invalid
  bool valid = true;      ///< BRepCheck_Analyzer on every result shape
  bool sewn = false, fixed = false, unified = false;
  int openShells = 0;     ///< shells that stayed open (surface bodies)
  std::vector<std::string> notes;
  /// Multi-line, human readable ("entities: 1234" ...).
  std::string text() const;
};

/// Apply the healing options to every shape, count solids / shells / faces and run BRepCheck.
/// Closed shells become solids when sewing is on.
void healShapes(std::vector<TopoDS_Shape>& shapes, const ImportOptions& options, ImportReport& report);
/// Count the entities / roots of a reader's work session into the report.
void reportModel(const Handle(XSControl_WorkSession)& ws, ImportReport& report);

/// Option words of IMPORT: sew=on|off, fix=on|off, unify=on|off (bare word = on). False if not one.
bool applyImportOption(const std::string& word, ImportOptions& options);

// ---- STEP schema --------------------------------------------------------------------------

/// "ap203" / "203" / "AP242" ... -> "AP203", "AP214", "AP242"; empty when not a schema.
std::string normalizeStepSchema(const std::string& text);
/// Preference "export/step/schema" (QSettings, default AP214).
std::string stepSchemaDefault();
void setStepSchemaDefault(const std::string& schema);
/// "schema=ap242" (also the bare "ap242"): sets `schema`, true. False if the word is not one.
bool applyStepSchemaOption(const std::string& word, std::string& schema);
/// Set OCCT's write.step.schema static (AP203 = 3, AP214 = 1 (IS), AP242 = 5).
void setStepWriterSchema(const std::string& schema);

// ---- product properties ------------------------------------------------------------------

struct ProductProperties {
  std::string partNumber, description, material, revision;
  bool empty() const { return partNumber.empty() && description.empty() && material.empty() && revision.empty(); }
  bool operator==(const ProductProperties&) const = default;
};

/// Add the properties to the products of a transferred (not yet written) STEP model, matched by
/// PRODUCT name. Returns the number of products updated.
int writeProductProperties(const Handle(StepData_StepModel)& model, const std::map<std::string, ProductProperties>& byProductName);
/// Read the properties of every product of a loaded STEP model, keyed by PRODUCT name.
std::map<std::string, ProductProperties> readProductProperties(const Handle(StepData_StepModel)& model);

}  // namespace fabricad::part::io
