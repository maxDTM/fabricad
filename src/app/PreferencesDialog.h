// FabriCAD app: Preferences (TOOLS > UTILITIES > Preferences, PREFS / UNITS commands, the
// status-bar units label). Modeless, changes apply immediately (ux.md 13, APP-03):
//   General  - default units for new designs ("prefs/defaultLength", "prefs/defaultAngle"),
//              reverse zoom direction ("view/reverseZoom", shared with the nav-bar menu).
//   Document - length / angle units and display precision of the active design: one undoable
//              SetUnitsCommand per change (plain-number parameters keep their size).
//   Appearance - theme Dark / Light / Follow system (live), UI scale, glow and scanline effects
//                (redesign 6.1; ui/* keys through UiSettings).
//   Layout   - toolbar position, sidebar side, command card placement, quick toolbar, orientation
//              widget, timeline style, with a live preview. Phase 1 applies the toolbar at the
//              top; the other values are stored and applied by the later redesign phases.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Units.h"

#include <QDialog>
#include <QPointer>

#include <functional>
#include <optional>
#include <utility>
#include <vector>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QSpinBox;
class QTabWidget;

namespace fabricad::core {
class Document;
}
namespace fabricad::viewer {
class ViewerWidget;
}

namespace fabricad::app {

/// Default units for new designs from QSettings (nullopt: not set, keep the built-in mm/deg).
std::optional<core::UnitSystem> defaultUnitsPreference();

/// Make a plain widget (the status-bar units label) clickable: `fn` runs on a left click.
void installClickHandler(QWidget* w, std::function<void()> fn);

class PreferencesDialog : public QDialog {
  Q_OBJECT
 public:
  enum class Page { General = 0, Document = 1, Appearance = 2, Layout = 3 };
  PreferencesDialog(core::Document& doc, viewer::ViewerWidget* viewer, QWidget* parent = nullptr);
  /// Rebind to the active design (multi-document tabs).
  void setTarget(core::Document& doc, viewer::ViewerWidget* viewer);
  void showPage(Page p);
  Page currentPage() const;
  /// Reload the widgets from the document / settings.
  void reload();

  QComboBox* documentLengthBox() const { return docLength_; }
  QComboBox* documentAngleBox() const { return docAngle_; }
  /// The control bound to a ui/* key (objectName "pref_<name>", e.g. pref_toolbarArea).
  QWidget* uiControl(const QString& key) const;

 private:
  void applyDocumentUnits();
  void applyGeneral();
  QWidget* buildAppearancePage();
  QWidget* buildLayoutPage();
  QComboBox* uiCombo(const QString& key, const std::vector<std::pair<QString, QString>>& items, QWidget* parent);
  void reloadUi(const QString& key);

  core::Document* doc_;
  QPointer<viewer::ViewerWidget> viewer_;
  QTabWidget* tabs_ = nullptr;
  QComboBox* defLength_ = nullptr;
  QComboBox* defAngle_ = nullptr;
  QCheckBox* reverseZoom_ = nullptr;
  QComboBox* docLength_ = nullptr;
  QComboBox* docAngle_ = nullptr;
  QSpinBox* precision_ = nullptr;
  QMetaObject::Connection unitsConn_;
  QButtonGroup* theme_ = nullptr;
  QSpinBox* fontScale_ = nullptr;
  QWidget* layoutPreview_ = nullptr;
  std::vector<std::pair<QString, QWidget*>> uiControls_;
  bool loading_ = false;
};

}  // namespace fabricad::app
