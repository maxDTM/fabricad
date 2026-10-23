// FabriCAD part: Section Analysis (INSPECT > Section).
//
// A live clipping plane through the model (Fusion's Section Analysis). Pick a plane or planar
// face (or type XY / XZ / YZ); the viewer cuts the bodies away on the side the plane normal
// points to and caps the cut. Drag the arrow manipulator (or type a number) to move the cut
// along the normal, FLIP to keep the other side. OK keeps the section on after the tool ends;
// running Section again resumes it, and SECTION OFF / "Remove Section" clears it. Esc restores
// what was there before the tool started.
//
// Command line: SECTION [XY|XZ|YZ] [offset] [flip] | SECTION OFF
// The plane state lives in IViewerContext::setSectionPlane() (headless contexts keep it too).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Tool.h"
#include "core/ViewerContext.h"

#include <QObject>
#include <QPointer>
#include <gp_Ax3.hxx>
#include <gp_Pln.hxx>

#include <optional>
#include <string>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;

namespace fabricad::part {

/// The clipping plane of a section: `base` moved by `offset` along its normal, normal reversed
/// when `flip` (the removed side is the one the returned plane's normal points into).
gp_Pln sectionPlaneFor(const gp_Ax3& base, double offset, bool flip);

class SectionTool : public QObject, public core::ToolBase {
  Q_OBJECT
 public:
  SectionTool();
  void activate(core::ToolContext& ctx) override;
  void deactivate(core::ToolContext& ctx) override;
  void accept(core::ToolContext& ctx) override;
  void cancel(core::ToolContext& ctx) override;
  void selectionChanged(core::ToolContext& ctx) override;
  bool mousePress(core::ToolContext& ctx, const core::ViewerMouseEvent& e) override;
  bool commandLineInput(core::ToolContext& ctx, const std::string& text) override;
  QWidget* createDialog(core::ToolContext& ctx, QWidget* parent) override;

  bool hasPlane() const { return base_.has_value(); }
  double offset() const { return offset_; }
  bool flipped() const { return flip_; }
  /// Programmatic edits (what the manipulator, the card and the command line do).
  void setBase(core::ToolContext& ctx, const gp_Ax3& base);
  void setOffset(core::ToolContext& ctx, double mm);
  void setFlip(core::ToolContext& ctx, bool flip);

 private:
  bool pickPlane(core::ToolContext& ctx, const core::ShapeRef& ref);
  bool applyWord(core::ToolContext& ctx, const std::string& word);
  void apply(core::ToolContext& ctx);
  void syncDialog();

  std::optional<gp_Ax3> base_;
  double offset_ = 0.0;
  bool flip_ = false;
  std::optional<gp_Pln> before_;  ///< the viewer's section when the tool started (Esc restores it)
  core::ManipulatorId manip_ = core::kInvalidManipulator;
  core::ToolContext* ctx_ = nullptr;
  bool syncing_ = false;
  QPointer<QLabel> planeLabel_;
  QPointer<QDoubleSpinBox> offsetBox_;
  QPointer<QCheckBox> flipBox_;

  /// The last section the tool applied, so a second run resumes it (base, offset, flip).
  static inline std::optional<gp_Ax3> lastBase_;
  static inline double lastOffset_ = 0.0;
  static inline bool lastFlip_ = false;
};

}  // namespace fabricad::part
