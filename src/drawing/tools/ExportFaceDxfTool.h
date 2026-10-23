// FabriCAD drawing: Export Face DXF (part.export-face-dxf, Design workspace). 1:1 DXF of the
// selected planar face(s) or sketch: "EXPORTDXF out.dxf [r12|r2000] [units=mm|in] [arcs=0.01]
// [noetch]" with the face or sketch selected (in the drawing workspace EXPORTDXF stays the sheet
// export). From the toolbar: a card with the file, the DXF version, units, the spline-to-arcs
// tolerance and the etch layer. See drawing/FaceDxf.h.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Tool.h"
#include "drawing/FaceDxf.h"

#include <QPointer>

class QComboBox;
class QLineEdit;
class QCheckBox;
class QLabel;

namespace fabricad::drawing {

class ExportFaceDxfTool : public core::ToolBase {
 public:
  ExportFaceDxfTool();
  bool isEnabled(const core::ToolContext& ctx) const override;
  void activate(core::ToolContext& ctx) override;
  void deactivate(core::ToolContext& ctx) override;
  void accept(core::ToolContext& ctx) override;
  bool commandLineInput(core::ToolContext& ctx, const std::string& text) override;
  QWidget* createDialog(core::ToolContext& ctx, QWidget* parent) override;

  /// Build the DXF of the given selection (faces of bodies, or a sketch) without writing.
  static FaceDxfResult build(const core::Document& doc, const std::vector<core::ShapeRef>& refs, const FaceDxfOptions& options);

 private:
  bool exportNow(core::ToolContext& ctx);
  void readDialog();
  FaceDxfOptions options_;
  std::string path_;
  std::vector<core::ShapeRef> refs_;
  QPointer<QLineEdit> pathEdit_;
  QPointer<QComboBox> versionCombo_;
  QPointer<QComboBox> unitsCombo_;
  QPointer<QLineEdit> arcsEdit_;
  QPointer<QCheckBox> etchCheck_;
};

}  // namespace fabricad::drawing
