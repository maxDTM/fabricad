// FabriCAD part: command-dialog body generated from a feature's properties (Fusion-style
// property panel). Length/angle fields accept expressions with units and parameters,
// enums are combos, bools checkboxes, Ref/RefList rows are selection inputs (the active one
// is highlighted; clicking makes it active; "x" clears). The shell adds title + OK/Cancel.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QTimer>
#include <QWidget>

#include <functional>
#include <map>
#include <string>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;

namespace fabricad::core {
class Document;
class Property;
}  // namespace fabricad::core

namespace fabricad::part {

class PartFeature;

class PropertyPanel : public QWidget {
  Q_OBJECT
 public:
  PropertyPanel(core::Document& doc, PartFeature& feature, QWidget* parent = nullptr);

  /// Recreate all rows (after PropertyFlags::hidden changed).
  void rebuild();
  /// Refresh displayed values from the feature (after refs, enums or bools changed programmatically).
  void refresh();
  const std::string& activeInput() const { return active_; }
  void setActiveInput(const std::string& propName);
  void setMessage(const std::string& text, bool error);
  /// Debounce typed numeric values: valueChanged is emitted `ms` after typing stops instead of
  /// on every keystroke (editingFinished still flushes immediately). 0 disables.
  void setValueChangeDelay(int ms);
  int valueChangeDelay() const { return delayMs_; }

  /// Remove every row of `form` safely: widgets are hidden, unparented and deleteLater()ed so
  /// a widget that is still emitting the signal that triggered the rebuild is not destroyed
  /// under its own feet (shared with app::PropertyEditor).
  static void retireFormRows(QFormLayout* form);

 signals:
  void valueChanged(const std::string& property);
  void activeInputChanged(const std::string& property);
  void refsCleared(const std::string& property);

 private:
  void addRow(core::Property& p);
  void applyNumericText(core::Property& p, QLineEdit* edit, bool immediate);
  void flushPending();
  QString refText(const core::Property& p) const;

  core::Document& doc_;
  PartFeature& feature_;
  QFormLayout* form_ = nullptr;
  QLabel* message_ = nullptr;
  std::string active_;
  std::map<std::string, QPushButton*> refButtons_;
  std::map<std::string, QLineEdit*> numericEdits_;
  std::map<std::string, QComboBox*> enumCombos_;  ///< synced by refresh() (automatic operation)
  std::map<std::string, QCheckBox*> boolChecks_;  ///< synced by refresh() (automatic flip)
  bool rebuilding_ = false;
  int delayMs_ = 0;
  QTimer debounce_;
  std::string pendingProp_;
};

}  // namespace fabricad::part
