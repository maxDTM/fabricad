// FabriCAD app: "Change Parameters" dialog (user parameters table, Fusion-style).
// Name | Expression | Value | Comment. Every edit is one undoable SetParameterCommand
// (the document recomputes); invalid expressions are shown in red with the error text.
// Renames to an existing or invalid name, and renaming/removing a parameter that features or
// other parameters use, are refused with a red message under the table (APP-01/02/05).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QDialog>

class QLabel;
class QTableWidget;

namespace fabricad::core {
class Document;
}

namespace fabricad::app {

class ParametersDialog : public QDialog {
  Q_OBJECT
 public:
  ParametersDialog(core::Document& doc, QWidget* parent = nullptr);
  void reload();
  /// Last refusal shown under the table ("" when the last edit was accepted). Tests.
  QString errorText() const;
  QTableWidget* table() const { return table_; }
  /// Remove the parameter in `row` (Remove button; refused when it is in use).
  void removeRow(int row);

 private:
  void onCellChanged(int row, int column);
  void addParameter();
  void removeSelected();
  void showError(const QString& text);

  core::Document& doc_;
  QTableWidget* table_ = nullptr;
  QLabel* error_ = nullptr;
  bool loading_ = false;
};

}  // namespace fabricad::app
