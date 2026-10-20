// FabriCAD app: parameters table.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/ParametersDialog.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/Json.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace fabricad::app {

ParametersDialog::ParametersDialog(core::Document& doc, QWidget* parent) : QDialog(parent), doc_(doc) {
  setWindowTitle("Parameters");
  resize(620, 360);
  auto* v = new QVBoxLayout(this);
  table_ = new QTableWidget(0, 4, this);
  table_->setHorizontalHeaderLabels({"Name", "Expression", "Value", "Comment"});
  table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
  table_->verticalHeader()->hide();
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  v->addWidget(table_);
  auto* buttons = new QDialogButtonBox(this);
  QPushButton* add = buttons->addButton("Add", QDialogButtonBox::ActionRole);
  QPushButton* rem = buttons->addButton("Remove", QDialogButtonBox::ActionRole);
  buttons->addButton(QDialogButtonBox::Close);
  v->addWidget(buttons);
  connect(add, &QPushButton::clicked, this, &ParametersDialog::addParameter);
  connect(rem, &QPushButton::clicked, this, &ParametersDialog::removeSelected);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(table_, &QTableWidget::cellChanged, this, &ParametersDialog::onCellChanged);
  connect(&doc_, &core::Document::parametersChanged, this, &ParametersDialog::reload);
  connect(&doc_, &core::Document::documentReset, this, &ParametersDialog::reload);
  reload();
}

void ParametersDialog::reload() {
  loading_ = true;
  table_->setRowCount(0);
  for (const core::Document::Parameter& p : doc_.parameters()) {
    const int r = table_->rowCount();
    table_->insertRow(r);
    auto* name = new QTableWidgetItem(core::qs(p.name));
    name->setData(Qt::UserRole, core::qs(p.name));
    table_->setItem(r, 0, name);
    table_->setItem(r, 1, new QTableWidgetItem(core::qs(p.expression)));
    auto* val = new QTableWidgetItem(p.ok ? core::qs(core::formatQuantity(p.value, doc_.units())) : core::qs(p.error));
    val->setFlags(val->flags() & ~Qt::ItemIsEditable);
    if (!p.ok) val->setForeground(QColor("#D93025"));
    table_->setItem(r, 2, val);
    table_->setItem(r, 3, new QTableWidgetItem(core::qs(p.comment)));
  }
  loading_ = false;
}

void ParametersDialog::onCellChanged(int row, int column) {
  if (loading_ || column == 2) return;
  QTableWidgetItem* nameItem = table_->item(row, 0);
  if (!nameItem) return;
  const std::string oldName = nameItem->data(Qt::UserRole).toString().toStdString();
  const std::string name = nameItem->text().trimmed().toStdString();
  const std::string expr = table_->item(row, 1) ? table_->item(row, 1)->text().trimmed().toStdString() : "";
  const std::string comment = table_->item(row, 3) ? table_->item(row, 3)->text().toStdString() : "";
  if (name.empty() || expr.empty()) return;
  if (!oldName.empty() && oldName != name) {
    // rename = remove + add in one undo step
    auto macro = std::make_unique<core::MacroCommand>("Rename parameter");
    macro->add(std::make_unique<core::SetParameterCommand>(doc_, name, expr, comment));
    macro->add(std::make_unique<core::SetParameterCommand>(doc_, oldName, "", ""));
    doc_.undoStack().push(std::move(macro));
  } else {
    doc_.undoStack().push(std::make_unique<core::SetParameterCommand>(doc_, name, expr, comment));
  }
  QMetaObject::invokeMethod(this, &ParametersDialog::reload, Qt::QueuedConnection);
}

void ParametersDialog::addParameter() {
  int n = 1;
  std::string name;
  do name = "p" + std::to_string(n++);
  while (doc_.parameter(name));
  doc_.undoStack().push(std::make_unique<core::SetParameterCommand>(doc_, name, "10 mm", ""));
  reload();
  table_->editItem(table_->item(table_->rowCount() - 1, 1));
}

void ParametersDialog::removeSelected() {
  const int r = table_->currentRow();
  if (r < 0 || !table_->item(r, 0)) return;
  doc_.undoStack().push(
      std::make_unique<core::SetParameterCommand>(doc_, table_->item(r, 0)->data(Qt::UserRole).toString().toStdString(), ""));
  reload();
}

}  // namespace fabricad::app
