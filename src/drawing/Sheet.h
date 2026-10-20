// FabriCAD drawing: sheets (paper format, frame/zones, title block) and their contents.
// A Sheet is a plain value type serialised into Document::extensionData("drawing").
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Types.h"
#include "drawing/Annotations.h"
#include "drawing/Callouts.h"
#include "drawing/Dimension.h"
#include "drawing/DrawingTypes.h"
#include "drawing/DrawingView.h"

#include <QJsonObject>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fabricad::drawing {

/// Paper size. Dimensions are landscape (width > height) unless `landscape` is false.
struct SheetFormat {
  std::string name = "A3";  ///< "A4".."A0", "ANSI A".."ANSI E"
  double width = 420;       ///< mm
  double height = 297;      ///< mm
  bool landscape = true;

  QJsonObject toJson() const;
  static SheetFormat fromJson(const QJsonObject& o);
  bool operator==(const SheetFormat&) const = default;
};
/// All formats in dialog order (A4 A3 A2 A1 A0 ANSI A..E), landscape.
const std::vector<SheetFormat>& sheetFormats();
std::optional<SheetFormat> sheetFormatByName(const std::string& name);
/// "A3 (420 x 297 mm)".
std::string sheetFormatLabel(const SheetFormat& f);

/// Title block (ISO 7200 style; bottom-right corner of the frame). Fields that are empty are
/// auto-filled at render time from the document (Title, Date, Scale, Sheet, Size).
struct TitleBlock {
  static const std::vector<std::string>& fieldNames();  ///< display order
  std::map<std::string, std::string> fields;            ///< name -> value (user overrides)
  double width = 180;   ///< mm
  double height = 56;   ///< mm
  bool visible = true;

  std::string field(const std::string& name) const;
  void setField(const std::string& name, const std::string& value);
  QJsonObject toJson() const;
  static TitleBlock fromJson(const QJsonObject& o);
};

struct Sheet {
  core::ObjectId id = core::kInvalidId;
  std::string name = "Sheet1";
  SheetFormat format;
  ProjectionStandard standard = ProjectionStandard::FirstAngle;
  double scale = 1.0;             ///< default scale for new base views
  double margin = 10.0;           ///< frame inset (mm); left side uses bindingMargin
  double bindingMargin = 20.0;
  bool showZones = true;
  TitleBlock titleBlock;

  std::vector<DrawingView> views;
  std::vector<Dimension> dimensions;
  std::vector<TextNote> notes;
  std::vector<CenterMark> centerMarks;
  std::vector<CenterLine> centerLines;
  std::vector<Balloon> balloons;
  std::vector<PartsList> partsLists;
  CalloutSet callouts;  ///< hole callouts, GD&T, surface texture, ordinates, revision tables

  /// Drawable area inside the frame (sheet coordinates).
  BBox2 frameRect() const;
  /// Title block rectangle (sheet coordinates), bottom-right inside the frame.
  BBox2 titleBlockRect() const;

  DrawingView* view(core::ObjectId id);
  const DrawingView* view(core::ObjectId id) const;
  Dimension* dimension(core::ObjectId id);
  const Dimension* dimension(core::ObjectId id) const;
  TextNote* note(core::ObjectId id);
  Balloon* balloon(core::ObjectId id);
  PartsList* partsList(core::ObjectId id);
  CenterMark* centerMark(core::ObjectId id);
  CenterLine* centerLine(core::ObjectId id);
  /// Direct children (projected/section/detail) of a view.
  std::vector<core::ObjectId> childrenOf(core::ObjectId viewId) const;
  /// Entity ids (dimensions, marks, balloons...) attached to a view.
  std::vector<core::ObjectId> entitiesOf(core::ObjectId viewId) const;
  /// True if any object (view or annotation) has this id.
  bool contains(core::ObjectId id) const;

  /// Next free label for a base/projected view ("VIEW1"), section ("A"), detail ("B").
  std::string nextViewLabel() const;
  std::string nextLetterLabel() const;

  QJsonObject toJson() const;
  static Sheet fromJson(const QJsonObject& o);
};

}  // namespace fabricad::drawing
