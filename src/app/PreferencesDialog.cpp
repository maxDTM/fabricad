// FabriCAD app: Preferences dialog.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/PreferencesDialog.h"

#include "app/AppActions.h"
#include "core/Document.h"
#include "viewer/ViewerWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace fabricad::app {

namespace {
constexpr core::LengthUnit kLengths[] = {core::LengthUnit::Millimeter, core::LengthUnit::Centimeter,
                                         core::LengthUnit::Meter, core::LengthUnit::Inch, core::LengthUnit::Foot};
constexpr core::AngleUnit kAngles[] = {core::AngleUnit::Degree, core::AngleUnit::Radian};

QComboBox* lengthBox(QWidget* parent) {
  auto* b = new QComboBox(parent);
  for (core::LengthUnit u : kLengths) b->addItem(core::lengthUnitSymbol(u), static_cast<int>(u));
  return b;
}
QComboBox* angleBox(QWidget* parent) {
  auto* b = new QComboBox(parent);
  for (core::AngleUnit u : kAngles) b->addItem(core::angleUnitSymbol(u), static_cast<int>(u));
  return b;
}
void select(QComboBox* b, int value) {
  const int i = b->findData(value);
  if (i >= 0) b->setCurrentIndex(i);
}
class ClickFilter : public QObject {
 public:
  ClickFilter(QObject* parent, std::function<void()> fn) : QObject(parent), fn_(std::move(fn)) {}
  bool eventFilter(QObject* o, QEvent* e) override {
    if (e->type() == QEvent::MouseButtonRelease && static_cast<QMouseEvent*>(e)->button() == Qt::LeftButton) {
      if (auto* w = qobject_cast<QWidget*>(o); w && w->rect().contains(static_cast<QMouseEvent*>(e)->position().toPoint())) {
        fn_();
        return true;
      }
    }
    return QObject::eventFilter(o, e);
  }

 private:
  std::function<void()> fn_;
};
}  // namespace

void installClickHandler(QWidget* w, std::function<void()> fn) {
  if (w) w->installEventFilter(new ClickFilter(w, std::move(fn)));
}

std::optional<core::UnitSystem> defaultUnitsPreference() {
  QSettings s;
  if (!s.contains("prefs/defaultLength")) return std::nullopt;
  core::UnitSystem u;
  if (auto l = core::lengthUnitFromSymbol(s.value("prefs/defaultLength").toString().toStdString())) u.length = *l;
  if (auto a = core::angleUnitFromSymbol(s.value("prefs/defaultAngle", "deg").toString().toStdString())) u.angle = *a;
  return u;
}

PreferencesDialog::PreferencesDialog(core::Document& doc, viewer::ViewerWidget* viewer, QWidget* parent)
    : QDialog(parent), doc_(&doc), viewer_(viewer) {
  setObjectName("PreferencesDialog");
  setWindowTitle("Preferences");
  setModal(false);
  auto* v = new QVBoxLayout(this);
  tabs_ = new QTabWidget(this);
  v->addWidget(tabs_);

  auto* general = new QWidget(tabs_);
  auto* gf = new QFormLayout(general);
  defLength_ = lengthBox(general);
  defAngle_ = angleBox(general);
  reverseZoom_ = new QCheckBox("Reverse zoom direction", general);
  gf->addRow("Default length unit (new designs)", defLength_);
  gf->addRow("Default angle unit (new designs)", defAngle_);
  gf->addRow(reverseZoom_);
  tabs_->addTab(general, "General");

  auto* document = new QWidget(tabs_);
  auto* df = new QFormLayout(document);
  docLength_ = lengthBox(document);
  docLength_->setObjectName("DocumentLengthUnit");
  docAngle_ = angleBox(document);
  docAngle_->setObjectName("DocumentAngleUnit");
  precision_ = new QSpinBox(document);
  precision_->setRange(0, 8);
  df->addRow("Length unit", docLength_);
  df->addRow("Angle unit", docAngle_);
  df->addRow("Display precision (decimals)", precision_);
  auto* note = new QLabel("Applies to the active design and can be undone. Plain-number parameters keep their size.",
                          document);
  note->setWordWrap(true);
  note->setObjectName("StatusDim");
  df->addRow(note);
  tabs_->addTab(document, "Document");

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
  v->addWidget(buttons);

  for (QComboBox* b : {defLength_, defAngle_})
    connect(b, &QComboBox::currentIndexChanged, this, &PreferencesDialog::applyGeneral);
  connect(reverseZoom_, &QCheckBox::toggled, this, &PreferencesDialog::applyGeneral);
  for (QComboBox* b : {docLength_, docAngle_})
    connect(b, &QComboBox::currentIndexChanged, this, &PreferencesDialog::applyDocumentUnits);
  connect(precision_, &QSpinBox::valueChanged, this, &PreferencesDialog::applyDocumentUnits);
  setTarget(doc, viewer);
}

void PreferencesDialog::setTarget(core::Document& doc, viewer::ViewerWidget* viewer) {
  doc_ = &doc;
  viewer_ = viewer;
  disconnect(unitsConn_);
  unitsConn_ = connect(doc_, &core::Document::unitsChanged, this, &PreferencesDialog::reload);
  reload();
}

void PreferencesDialog::reload() {
  loading_ = true;
  const core::UnitSystem def = defaultUnitsPreference().value_or(core::UnitSystem{});
  select(defLength_, static_cast<int>(def.length));
  select(defAngle_, static_cast<int>(def.angle));
  reverseZoom_->setChecked(viewer_ ? viewer_->reverseZoom() : QSettings().value("view/reverseZoom", false).toBool());
  const core::UnitSystem& u = doc_->units();
  select(docLength_, static_cast<int>(u.length));
  select(docAngle_, static_cast<int>(u.angle));
  precision_->setValue(u.precision);
  loading_ = false;
}

void PreferencesDialog::showPage(Page p) { tabs_->setCurrentIndex(static_cast<int>(p)); }

PreferencesDialog::Page PreferencesDialog::currentPage() const { return static_cast<Page>(tabs_->currentIndex()); }

void PreferencesDialog::applyGeneral() {
  if (loading_) return;
  QSettings s;
  s.setValue("prefs/defaultLength", defLength_->currentText());
  s.setValue("prefs/defaultAngle", defAngle_->currentText());
  s.setValue("view/reverseZoom", reverseZoom_->isChecked());
  if (viewer_) viewer_->setReverseZoom(reverseZoom_->isChecked());
}

void PreferencesDialog::applyDocumentUnits() {
  if (loading_) return;
  core::UnitSystem u = doc_->units();
  u.length = static_cast<core::LengthUnit>(docLength_->currentData().toInt());
  u.angle = static_cast<core::AngleUnit>(docAngle_->currentData().toInt());
  u.precision = precision_->value();
  if (auto cmd = makeSetUnits(*doc_, u)) doc_->undoStack().push(std::move(cmd));
}

}  // namespace fabricad::app
