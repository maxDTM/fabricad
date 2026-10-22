// FabriCAD app: parameters table.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/ParametersDialog.h"

#include "theme/Tokens.h"

#include "app/AppActions.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/Expression.h"
#include "core/Json.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace fabricad::app {

using theme::Tok;

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
  // refusals are shown inline, not in a modal box (scripted/headless use must not block)
  error_ = new QLabel(this);
  error_->setObjectName("ParametersError");
  error_->setWordWrap(true);
  error_->hide();
  v->addWidget(error_);
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
    if (!p.ok) val->setForeground(theme::color(Tok::StatusError));
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
  if (const std::string why = checkParameterEdit(doc_, oldName, name); !why.empty()) {
    // keep the old name: rebuild the table after the item's own edit has finished
    showError(core::qs(why));
    QMetaObject::invokeMethod(this, &ParametersDialog::reload, Qt::QueuedConnection);
    return;
  }
  if (oldName.empty() && !core::isValidParameterName(name)) {
    showError(core::qs("Invalid name '" + name + "'"));
    QMetaObject::invokeMethod(this, &ParametersDialog::reload, Qt::QueuedConnection);
    return;
  }
  showError({});
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

void ParametersDialog::removeSelected() { removeRow(table_->currentRow()); }

void ParametersDialog::removeRow(int r) {
  if (r < 0 || !table_->item(r, 0)) return;
  const std::string name = table_->item(r, 0)->data(Qt::UserRole).toString().toStdString();
  if (const std::string why = checkParameterEdit(doc_, name, ""); !why.empty()) {
    showError(core::qs(why));
    return;
  }
  showError({});
  doc_.undoStack().push(std::make_unique<core::SetParameterCommand>(doc_, name, ""));
  reload();
}

void ParametersDialog::showError(const QString& text) {
  error_->setText(text);
  error_->setVisible(!text.isEmpty());
}

QString ParametersDialog::errorText() const { return error_->text(); }

}  // namespace fabricad::app
