// FabriCAD app: the floating quick toolbar in the model view (docs/plans/ui-redesign.md 5.3,
// 6.1 toggle 2 `ui/quickToolbar` off | left | right, 6.3 priority 6, 6.4).
//
// A 40 px vertical strip of 32 x 32 buttons inside the viewport:
//   [repeat last]  [6 most-used tools of the context]  [Finish Sketch while sketching]
// "Most used" counts every started command (QSettings `quickToolbar/usage`, per tool id); a
// context is the model (every tab but SKETCH) or the sketch (the SKETCH tab). The defaults fill
// the strip until enough commands have been used. MainWindow places the strip with the
// viewport's OverlayLayout (Slot::QuickToolbar): between the card and the viewport edge on the
// card's side, left of the view-tool column on the right (6.3, 6.4).
//
// Test hooks: #QuickToolbar (this frame), #QuickRepeat, #FinishSketchButton, the dynamic
// property `toolId` on every tool button (the tutorial anchor `tool:<id>` finds the first
// visible one).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Tool.h"

#include <QFrame>
#include <QMap>
#include <QString>

#include <string>
#include <vector>

class QToolButton;
class QVBoxLayout;

namespace fabricad::app {

/// Usage counts per tool id (QSettings `quickToolbar/usage`).
using ToolUsage = QMap<QString, int>;
ToolUsage loadToolUsage();
void saveToolUsage(const ToolUsage& usage);

/// The tool ids of the strip: the `count` most-used enabled-in-workspace, visible tools of the
/// context (sketch: tab SKETCH; model: any other tab of `ws`), ties in registry order, padded
/// with the defaults of the context. Pure (unit-tested).
std::vector<std::string> pickQuickTools(core::ToolRegistry& registry, core::Workspace ws, bool sketching,
                                        const ToolUsage& usage, int count = 6);

class QuickToolbar : public QFrame {
  Q_OBJECT
 public:
  static constexpr int kWidth = 40;
  static constexpr int kButton = 32;
  static constexpr int kGap = 4;

  explicit QuickToolbar(core::ToolManager& manager, QWidget* parent = nullptr);

  void setToolManager(core::ToolManager* m);
  core::ToolManager& manager() const { return *manager_; }
  /// Rebuild the buttons for a workspace / sketch context (only when the ids change).
  void rebuild(core::Workspace ws, bool sketching);
  /// A command started: count it in QSettings (the strip changes on the next rebuild, not
  /// under the pointer). Static: counted while the strip is off too.
  static void noteUsed(const core::ToolInfo& info);
  /// Enabled / checked states, the repeat button's tooltip.
  void refreshStates();
  const std::vector<std::string>& toolIds() const { return ids_; }
  bool isSketching() const { return sketching_; }
  QSize sizeHint() const override;

 signals:
  void toolTriggered(const std::string& id);
  void repeatRequested();

 private:
  QToolButton* makeButton(const QString& objectName);

  core::ToolManager* manager_;
  QVBoxLayout* layout_ = nullptr;
  QToolButton* repeat_ = nullptr;
  QToolButton* finish_ = nullptr;
  std::vector<QToolButton*> buttons_;
  std::vector<std::string> ids_;
  core::Workspace ws_ = core::Workspace::Design;
  bool sketching_ = false;
  bool built_ = false;
};

}  // namespace fabricad::app
