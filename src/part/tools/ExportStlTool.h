// FabriCAD part: Export STL command with tessellation settings.
//
// From the command line it behaves like an instant export ("EXPORTSTL out.stl [deflection=0.05]
// [angle=20|0.2rad] [ascii]"). From the ribbon (no arguments) it opens a dialog with the file,
// the linear and angular deflection and the format, remembers the last settings (QSettings) and
// exports on OK. The selected bodies (or every visible body) are written in their world
// placement. Everything works headlessly: a script gives the path as argument or types
// `deflection=` / `angle=` / the path while the command is active, then OK.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Tool.h"

#include <QPointer>

#include <string>

class QLineEdit;
class QComboBox;
class QLabel;

namespace fabricad::part {

struct StlExportSettings {
  double linearDeflection = 0.05;  ///< mm, absolute
  double angularDeflectionDeg = 20.0;
  bool binary = true;
};

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
  /// Parse one option word: "deflection=0.05", "linear=0.05", "angle=20", "angle=0.2rad",
  /// "angular=...", "ascii", "binary". False if the word is not an option (then it is a path).
  static bool applyOption(const std::string& word, StlExportSettings& s);
  static StlExportSettings loadDefaults();
  static void saveDefaults(const StlExportSettings& s);

 private:
  bool exportNow(core::ToolContext& ctx);
  void readDialog();

  StlExportSettings settings_;
  std::string path_;
  QPointer<QLineEdit> pathEdit_;
  QPointer<QLineEdit> linearEdit_;
  QPointer<QLineEdit> angularEdit_;
  QPointer<QComboBox> formatCombo_;
  QPointer<QLabel> bodiesLabel_;
};

}  // namespace fabricad::part
