// FabriCAD app: title-bar pieces of the redesign (docs/plans/ui-redesign.md 5.1, 7.1).
//
//   [F FABRICAD v] [PART  ALT 1 v] /bracket-v3 *\ /chair\ [+]       [>_ SEARCH COMMANDS  S] [undo][redo][save][?]
//
// - DocumentTabBar (#DocumentTabs): QTabBar with painted angled tabs (12 deg slant, 8 px run),
//   2 px accent bar on the active tab, a neutral dot for unsaved changes (decision 11). Tab
//   texts keep their " *" suffix (tests, findTab); the paint shows the dot instead.
// - WorkspaceSelector (#WorkspaceSwitcher): PART / ASSEMBLY / DRAWING. Assembly is a toolbar
//   filter of the Design workspace (decision 2). Its keycap shows the shortcut of the current
//   choice (kWorkspaceShortcuts, tentative Alt+1/2/3; Ctrl+1 stays Front view).
// - Keycap (#Keycap): small boxed key label ("ALT 1", "ESC").
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QLabel>
#include <QTabBar>
#include <QToolButton>

class QMenu;

namespace fabricad::app {

/// Workspace choices of the selector.
enum class WorkspaceChoice { Part, Assembly, Drawing };

struct WorkspaceShortcut {
  WorkspaceChoice choice;
  const char* toolId;    ///< app tool that switches ("app.workspace-part")
  const char* name;      ///< "Part"
  const char* shortcut;  ///< Qt key sequence ("Alt+1"); the one place to change them
  const char* icon;
};
/// Tentative (the user has not confirmed): Alt+1 Part, Alt+2 Assembly, Alt+3 Drawing.
inline constexpr WorkspaceShortcut kWorkspaceShortcuts[] = {
    {WorkspaceChoice::Part, "app.workspace-part", "Part", "Alt+1", "workspace-design"},
    {WorkspaceChoice::Assembly, "app.workspace-assembly", "Assembly", "Alt+2", "component"},
    {WorkspaceChoice::Drawing, "app.workspace-drawing", "Drawing", "Alt+3", "workspace-drawing"},
};
const WorkspaceShortcut& workspaceShortcut(WorkspaceChoice c);
/// "Alt+1" -> "ALT 1" (keycap text; the font adds the caps).
QString keycapText(const QString& keySequence);

class Keycap : public QLabel {
  Q_OBJECT
 public:
  explicit Keycap(const QString& keys, QWidget* parent = nullptr);
  void setKeys(const QString& keySequence) { setText(keycapText(keySequence)); }
};

class DocumentTabBar : public QTabBar {
  Q_OBJECT
 public:
  explicit DocumentTabBar(QWidget* parent = nullptr);
  /// Horizontal run of each slanted side (12 deg over the tab height, about 8 px).
  static constexpr int kSlant = 8;
  static constexpr int kHeight = 36;
  /// The tab's outline (trapezoid) in bar coordinates.
  QPainterPath tabPath(int index) const;
  /// Tab under `pos` using the slanted outlines (the front tab wins on the slant).
  int tabAtPath(const QPoint& pos) const;
  /// Label without the unsaved marker, and whether the tab has one.
  static QString plainText(const QString& tabText, bool* modified = nullptr);
  /// The close glyph of tab `index` in bar coordinates: the close button's geometry while it is
  /// shown, else where it sits at the tab's right end (the tutorial's `tabclose:` anchor). Null
  /// for an invalid index.
  QRect closeRect(int index) const;

 protected:
  QSize tabSizeHint(int index) const override;
  QSize minimumTabSizeHint(int index) const override;
  void paintEvent(QPaintEvent* e) override;
  void mousePressEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void mouseReleaseEvent(QMouseEvent* e) override;
  void leaveEvent(QEvent* e) override;

 private:
  int hover_ = -1;
  int pressed_ = -1;
  QPoint pressPos_;
  bool dragging_ = false;
};

class WorkspaceSelector : public QToolButton {
  Q_OBJECT
 public:
  explicit WorkspaceSelector(QWidget* parent = nullptr);
  void setChoice(WorkspaceChoice c);
  WorkspaceChoice choice() const { return choice_; }
  QMenu* choicesMenu() const { return menu_; }
  QSize sizeHint() const override;

 signals:
  void requested(fabricad::app::WorkspaceChoice choice);

 private:
  WorkspaceChoice choice_ = WorkspaceChoice::Part;
  QMenu* menu_ = nullptr;
  QLabel* icon_ = nullptr;
  QLabel* name_ = nullptr;
  Keycap* keycap_ = nullptr;
  QLabel* arrow_ = nullptr;
};

}  // namespace fabricad::app
