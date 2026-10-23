// FabriCAD part: Export STL command with tessellation settings.
//
// From the command line it behaves like an instant export ("EXPORTSTL out.stl [quality=fine]
// [deflection=0.05] [angle=11.5|0.2rad] [ascii]"). Quality presets (Fusion's refinement levels,
// COMPARISON.md recommendation 3): coarse 0.1 mm / 20 deg, medium 0.05 mm / 0.2 rad (11.46 deg,
// the default), fine 0.01 mm / 5 deg; explicit deflection/angle words override a preset. From the ribbon (no arguments) it opens a dialog with the file,
// the linear and angular deflection and the format, remembers the last settings (QSettings) and
// exports on OK. The selected bodies (or every visible body) are written in their world
// placement. Everything works headlessly: a script gives the path as argument or types
// `deflection=` / `angle=` / the path while the command is active, then OK.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Tool.h"

#include <QPointer>

#include <optional>
#include <string>

class QLineEdit;
class QComboBox;
class QLabel;

namespace fabricad::part {

/// 0.2 rad: the default angular deflection (Medium), small fillets stay within 0.05 mm.
inline constexpr double kStlDefaultAngleDeg = 0.2 * 180.0 / 3.14159265358979323846;

enum class StlQuality { Coarse, Medium, Fine, Custom };

struct StlExportSettings {
  double linearDeflection = 0.05;  ///< mm, absolute
  double angularDeflectionDeg = kStlDefaultAngleDeg;
  bool binary = true;
};

/// Settings of a preset (Custom: the defaults).
StlExportSettings stlPreset(StlQuality q);
/// The preset whose deflections these are, else Custom.
StlQuality stlQualityOf(const StlExportSettings& s);
const char* stlQualityName(StlQuality q);  ///< "coarse", "medium", "fine", "custom"
std::optional<StlQuality> parseStlQuality(const std::string& name);

class ExportStlTool : public core::ToolBase {
 public:
  explicit ExportStlTool(core::ToolInfo info);
  bool isEnabled(const core::ToolContext& ctx) const override;
  void activate(core::ToolContext& ctx) override;
  void deactivate(core::ToolContext& ctx) override;
  void accept(core::ToolContext& ctx) override;
  bool commandLineInput(core::ToolContext& ctx, const std::string& text) override;
  QWidget* createDialog(core::ToolContext& ctx, QWidget* parent) override;

  /// Settings of the running (or last) command; the defaults come from QSettings ("export/stl/*").
  const StlExportSettings& settings() const { return settings_; }
  const std::string& path() const { return path_; }
  /// Parse one option word: "quality=coarse|medium|fine" (also "preset=", or the bare word),
  /// "deflection=0.05", "linear=0.05", "angle=20", "angle=0.2rad", "angular=...", "ascii",
  /// "binary". False if the word is not an option (then it is a path).
  static bool applyOption(const std::string& word, StlExportSettings& s);
  static StlExportSettings loadDefaults();
  static void saveDefaults(const StlExportSettings& s);

 private:
  bool exportNow(core::ToolContext& ctx);
  void readDialog();

  StlExportSettings settings_;
  std::string path_;
  void syncQualityCombo();
  QPointer<QComboBox> qualityCombo_;
  QPointer<QLineEdit> pathEdit_;
  QPointer<QLineEdit> linearEdit_;
  QPointer<QLineEdit> angularEdit_;
  QPointer<QComboBox> formatCombo_;
  QPointer<QLabel> bodiesLabel_;
};

}  // namespace fabricad::part
