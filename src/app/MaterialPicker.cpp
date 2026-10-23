// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/MaterialPicker.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTabBar>
#include <QUrl>
#include <QVBoxLayout>

#include <format>

namespace fabricad::app {

MaterialPicker::MaterialPicker(const std::string& current, QWidget* parent) : QDialog(parent), current_(current) {
  setWindowTitle("Physical Material");
  setObjectName("materialPicker");
  auto* v = new QVBoxLayout(this);
  tabs_ = new QTabBar(this);
  tabs_->setObjectName("materialCategories");
  tabs_->addTab("All");
  for (const std::string& c : core::materialCategories()) tabs_->addTab(QString::fromStdString(c));
  tabs_->setExpanding(false);
  v->addWidget(tabs_);
  filter_ = new QLineEdit(this);
  filter_->setObjectName("materialFilter");
  filter_->setPlaceholderText("Filter: name, category or standard (6061, ASTM, PLA...)");
  filter_->setClearButtonEnabled(true);
  v->addWidget(filter_);
  list_ = new QListWidget(this);
  list_->setObjectName("materialList");
  list_->setMinimumSize(360, 260);
  v->addWidget(list_, 1);
  detail_ = new QLabel(this);
  detail_->setObjectName("materialDetail");
  detail_->setWordWrap(true);
  v->addWidget(detail_);
  auto* row = new QHBoxLayout();
  auto* custom = new QPushButton("Custom...", this);
  custom->setObjectName("materialCustom");
  auto* library = new QPushButton("Library...", this);
  library->setObjectName("materialLibrary");
  library->setToolTip(QString::fromStdString("Open the folder of the user library " + core::userMaterialLibraryPath()));
  row->addWidget(custom);
  row->addWidget(library);
  row->addStretch(1);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  row->addWidget(buttons);
  v->addLayout(row);
  connect(filter_, &QLineEdit::textChanged, this, [this] { refill(); });
  connect(tabs_, &QTabBar::currentChanged, this, [this] { refill(); });
  connect(list_, &QListWidget::currentRowChanged, this, [this](int) {
    QListWidgetItem* it = list_->currentItem();
    const core::Material* m = it ? core::findMaterial(it->data(Qt::UserRole).toString().toStdString()) : nullptr;
    if (!m) {
      detail_->clear();
      return;
    }
    std::string d = std::format("{}: {:.3f} g/cm3", m->name, m->density);
    if (!m->standard.empty()) d += ", " + m->standard;
    if (m->yieldMPa > 0) d += std::format(", yield {:.0f} MPa", m->yieldMPa);
    if (m->costPerKg > 0) d += std::format(", {:.2f} / kg", m->costPerKg);
    if (m->source == "user") d += " (user library)";
    detail_->setText(QString::fromStdString(d));
  });
  connect(list_, &QListWidget::itemDoubleClicked, this, [this] { accept(); });
  connect(buttons, &QDialogButtonBox::accepted, this, [this] {
    QListWidgetItem* it = list_->currentItem();
    if (const core::Material* m = it ? core::findMaterial(it->data(Qt::UserRole).toString().toStdString()) : nullptr) {
      chosen_ = *m;
      accept();
    }
  });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(custom, &QPushButton::clicked, this, [this] { defineCustom(); });
  connect(library, &QPushButton::clicked, this, [] {
    const QString dir = QFileInfo(QString::fromStdString(core::userMaterialLibraryPath())).absolutePath();
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
  });
  refill();
  filter_->setFocus();
}

void MaterialPicker::refill() {
  const QString cat = tabs_->currentIndex() <= 0 ? QString() : tabs_->tabText(tabs_->currentIndex());
  list_->clear();
  int select = -1;
  for (const core::Material* m : core::searchMaterials(filter_->text().trimmed().toStdString(), cat.toStdString())) {
    std::string label = std::format("{}    {:.2f} g/cm3", m->name, m->density);
    if (!m->standard.empty()) label += "    " + m->standard;
    auto* it = new QListWidgetItem(QString::fromStdString(label), list_);
    it->setData(Qt::UserRole, QString::fromStdString(m->name));
    if (m->name == current_) select = list_->count() - 1;
  }
  if (list_->count() > 0) list_->setCurrentRow(select >= 0 ? select : 0);
}

QStringList MaterialPicker::visibleNames() const {
  QStringList out;
  for (int i = 0; i < list_->count(); ++i) out << list_->item(i)->data(Qt::UserRole).toString();
  return out;
}

void MaterialPicker::setFilter(const QString& text) { filter_->setText(text); }

void MaterialPicker::setCategory(const QString& category) {
  for (int i = 0; i < tabs_->count(); ++i)
    if (tabs_->tabText(i).compare(category.isEmpty() ? QString("All") : category, Qt::CaseInsensitive) == 0) tabs_->setCurrentIndex(i);
}

void MaterialPicker::defineCustom() {
  QDialog dlg(this);
  dlg.setWindowTitle("Custom material");
  auto* form = new QFormLayout(&dlg);
  auto* name = new QLineEdit(&dlg);
  name->setObjectName("customName");
  auto* category = new QComboBox(&dlg);
  category->setEditable(true);
  for (const std::string& c : core::materialCategories()) category->addItem(QString::fromStdString(c));
  category->setCurrentText("Other");
  auto* density = new QLineEdit(&dlg);
  density->setObjectName("customDensity");
  density->setPlaceholderText("g/cm3");
  auto* standard = new QLineEdit(&dlg);
  auto* yield = new QLineEdit(&dlg);
  yield->setPlaceholderText("MPa (optional)");
  auto* cost = new QLineEdit(&dlg);
  cost->setPlaceholderText("per kg (optional)");
  auto* save = new QCheckBox("Save to the user library", &dlg);
  save->setChecked(true);
  form->addRow("Name", name);
  form->addRow("Category", category);
  form->addRow("Density (g/cm3)", density);
  form->addRow("Standard", standard);
  form->addRow("Yield", yield);
  form->addRow("Cost", cost);
  form->addRow(save);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  form->addRow(buttons);
  if (dlg.exec() != QDialog::Accepted) return;
  core::Material m;
  m.name = name->text().trimmed().toStdString();
  m.category = category->currentText().trimmed().toStdString();
  m.density = density->text().trimmed().toDouble();
  m.standard = standard->text().trimmed().toStdString();
  m.yieldMPa = yield->text().trimmed().toDouble();
  m.costPerKg = cost->text().trimmed().toDouble();
  m.source = "user";
  if (m.name.empty() || !(m.density > 0)) {
    QMessageBox::warning(this, "Custom material", "A custom material needs a name and a density > 0.");
    return;
  }
  if (save->isChecked()) {
    std::string err;
    if (!core::saveUserMaterial(m, &err)) QMessageBox::warning(this, "Custom material", QString::fromStdString(err));
  }
  chosen_ = m;
  accept();
}

std::optional<core::Material> pickMaterial(QWidget* parent, const std::string& current) {
  MaterialPicker p(current, parent);
  if (p.exec() != QDialog::Accepted) return std::nullopt;
  return p.chosen();
}

}  // namespace fabricad::app
