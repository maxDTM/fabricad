// FabriCAD app: searchable physical material picker (production-features P1 §2.2).
//
// Category tabs (All, Metal, Plastic, Wood, Other, user categories), a filter box matching name,
// category and standard, and a list showing density and standard. "Custom..." defines a name +
// density (+ category, yield, cost) and can save it to the user library
// (~/.config/FabriCAD/materials.json); "Library..." opens that file's folder. The picker works on
// core::materialLibrary() and does not touch the document: the caller applies the choice.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Materials.h"

#include <QDialog>

#include <optional>
#include <string>

class QLineEdit;
class QListWidget;
class QTabBar;
class QLabel;

namespace fabricad::app {

class MaterialPicker : public QDialog {
  Q_OBJECT
 public:
  explicit MaterialPicker(const std::string& current, QWidget* parent = nullptr);

  /// The chosen material (library entry or custom), after exec() == Accepted.
  std::optional<core::Material> chosen() const { return chosen_; }
  /// Rows currently listed (names), for tests.
  QStringList visibleNames() const;
  void setFilter(const QString& text);
  void setCategory(const QString& category);  ///< "" or "All" = every category

 private:
  void refill();
  void defineCustom();

  QLineEdit* filter_ = nullptr;
  QTabBar* tabs_ = nullptr;
  QListWidget* list_ = nullptr;
  QLabel* detail_ = nullptr;
  std::optional<core::Material> chosen_;
  std::string current_;
};

/// Run the picker; nullopt when cancelled.
std::optional<core::Material> pickMaterial(QWidget* parent, const std::string& current);

}  // namespace fabricad::app
