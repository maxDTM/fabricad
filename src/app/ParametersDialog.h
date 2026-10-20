// FabriCAD app: "Change Parameters" dialog (user parameters table, Fusion-style).
// Name | Expression | Value | Comment. Every edit is one undoable SetParameterCommand
// (the document recomputes); invalid expressions are shown in red with the error text.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QDialog>

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

 private:
  void onCellChanged(int row, int column);
  void addParameter();
  void removeSelected();

  core::Document& doc_;
  QTableWidget* table_ = nullptr;
  bool loading_ = false;
};

}  // namespace fabricad::app
