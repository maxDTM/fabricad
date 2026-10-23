// FabriCAD app: Preferences dialog.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/PreferencesDialog.h"
#include "part/features/Import.h"

#include "app/AppActions.h"
#include "app/ThemeManager.h"
#include "app/UiSettings.h"
#include "theme/Tokens.h"
#include "core/Document.h"
#include "viewer/ViewerWidget.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QPainter>
#include <QRadioButton>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <cmath>

namespace fabricad::app {

namespace {
constexpr core::LengthUnit kLengths[] = {core::LengthUnit::Millimeter, core::LengthUnit::Centimeter,
                                         core::LengthUnit::Meter, core::LengthUnit::Inch, core::LengthUnit::Foot};
constexpr core::AngleUnit kAngles[] = {core::AngleUnit::Degree, core::AngleUnit::Radian};

QComboBox* lengthBox(QWidget* parent) {
  auto* b = new QComboBox(parent);
  b->setProperty("textCase", "as-is");  // units are lowercase (redesign 3.3)
  for (core::LengthUnit u : kLengths) b->addItem(core::lengthUnitSymbol(u), static_cast<int>(u));
  return b;
}
QComboBox* angleBox(QWidget* parent) {
  auto* b = new QComboBox(parent);
  b->setProperty("textCase", "as-is");
  for (core::AngleUnit u : kAngles) b->addItem(core::angleUnitSymbol(u), static_cast<int>(u));
  return b;
}
void select(QComboBox* b, int value) {
  const int i = b->findData(value);
  if (i >= 0) b->setCurrentIndex(i);
}
/// Thumbnail of the window layout from the ui/* settings (6.1 "live preview"), painted with
/// tokens: title bar, toolbar (top / left / right), sidebar side, card placement, quick toolbar,
/// timeline strip.
class LayoutPreview : public QWidget {
 public:
  explicit LayoutPreview(QWidget* parent) : QWidget(parent) {
    setObjectName("LayoutPreview");
    setFixedSize(240, 150);
    connect(&UiSettings::instance(), &UiSettings::changed, this, [this] { update(); });
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this] { update(); });
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    using theme::Tok;
    UiSettings& ui = UiSettings::instance();
    QPainter p(this);
    QRect r = rect().adjusted(0, 0, -1, -1);
    p.fillRect(r, theme::color(Tok::VpBgBottom));
    p.setPen(theme::color(Tok::BorderDefault));
    p.drawRect(r);
    r.adjust(1, 1, 0, 0);
    p.fillRect(QRect(r.left(), r.top(), r.width(), 10), theme::color(Tok::BgApp));  // title bar
    r.setTop(r.top() + 10);
    p.fillRect(QRect(r.left(), r.bottom() - 6, r.width(), 7), theme::color(Tok::BgApp));  // status bar
    r.setBottom(r.bottom() - 7);
    const QString tb = ui.string("ui/toolbarArea");
    const QColor bar = theme::color(Tok::SurfaceHeader);
    if (tb == "left") {
      p.fillRect(QRect(r.left(), r.top(), 12, r.height()), bar);
      r.setLeft(r.left() + 12);
    } else if (tb == "right") {
      p.fillRect(QRect(r.right() - 11, r.top(), 12, r.height()), bar);
      r.setRight(r.right() - 12);
    } else {
      p.fillRect(QRect(r.left(), r.top(), r.width(), 16), bar);
      for (int x = r.left() + 6; x < r.right() - 10; x += 14)
        p.fillRect(QRect(x, r.top() + 4, 8, 6), theme::color(Tok::TextTertiary));
      r.setTop(r.top() + 16);
    }
    // p2p3-check 10: the strip and the panels are drawn so that every choice shows
    const bool strip = ui.string("ui/timelineStyle") == "strip";
    if (strip) {
      const QRect s(r.left(), r.bottom() - 11, r.width(), 12);
      p.fillRect(s, theme::color(Tok::SurfaceHeader));
      p.fillRect(QRect(s.left(), s.top(), s.width(), 1), theme::color(Tok::BorderDefault));
      for (int x = s.left() + 8; x < s.right() - 8; x += 11) p.fillRect(QRect(x, s.top() + 4, 7, 5), theme::color(Tok::TextTertiary));
      r.setBottom(r.bottom() - 12);
    }
    const bool sideRight = ui.string("ui/sidebarSide") == "right";
    const QRect side(sideRight ? r.right() - 53 : r.left(), r.top(), 54, r.height());
    p.fillRect(side, theme::color(Tok::SurfacePanel));
    p.fillRect(QRect(sideRight ? side.left() : side.right(), side.top(), 1, side.height()), theme::color(Tok::BorderDefault));
    // panel headers: FEATURES (feature-list style), MODEL, SELECTION; a docked card sits on top
    const bool docked = ui.string("ui/cardPlacement") == "sidebar";
    int y = side.top() + (docked ? 46 : 3);
    for (int i = strip ? 1 : 0; i < 3; ++i) {
      p.fillRect(QRect(side.left() + 2, y, side.width() - 4, 5), theme::color(Tok::SurfaceHeader));
      p.fillRect(QRect(side.left() + 2, y, 2, 5), theme::color(Tok::AccentIndicator));
      y += docked ? 9 : 22;
    }
    QRect vp = sideRight ? r.adjusted(0, 0, -54, 0) : r.adjusted(54, 0, 0, 0);
    const QString quick = ui.string("ui/quickToolbar");
    if (quick == "left") p.fillRect(QRect(vp.left() + 3, vp.top() + 14, 7, 34), theme::color(Tok::SurfaceRaised));
    if (quick == "right") p.fillRect(QRect(vp.right() - 9, vp.top() + 14, 7, 34), theme::color(Tok::SurfaceRaised));
    const QString card = ui.string("ui/cardPlacement");
    QRect c;
    if (card == "sidebar") c = QRect(side.left() + 3, side.top() + 3, side.width() - 6, 40);
    else if (card == "floatLeft") c = QRect(vp.left() + (quick == "left" ? 13 : 4), vp.top() + 6, 44, 50);
    else c = QRect(vp.right() - (quick == "right" ? 56 : 47), vp.top() + 6, 44, 50);
    p.fillRect(c, theme::color(Tok::SurfaceRaised));
    p.setPen(theme::color(Tok::AccentIndicator));
    p.drawRect(c.adjusted(0, 0, -1, -1));
    // orientation widget: hexagon dial or cube at the viewport's top right
    p.setPen(theme::color(Tok::TextSecondary));
    const QPoint o(vp.right() - 12, vp.top() + 12);
    if (ui.string("ui/orientation") == "viewCube") p.drawRect(QRect(o - QPoint(6, 6), QSize(12, 12)));
    else {
      QPolygon hex;
      for (int i = 0; i < 6; ++i) hex << o + QPoint(int(7 * std::cos(i * M_PI / 3 + M_PI / 6)), int(7 * std::sin(i * M_PI / 3 + M_PI / 6)));
      p.drawPolygon(hex);
    }
  }
};

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
  // COMPARISON.md recommendation 8: STEP files without pcurves are smaller
  auto* pcurves = new QCheckBox("Write pcurves into STEP files (off: smaller files)", general);
  pcurves->setObjectName("stepPcurves");
  pcurves->setToolTip("Edge parameter curves (write.surfacecurve.mode). Most importers rebuild them; EXPORTSTEP pcurves=on|off overrides");
  pcurves->setChecked(part::stepPcurvesDefault());
  connect(pcurves, &QCheckBox::toggled, this, [](bool on) { part::setStepPcurvesDefault(on); });
  gf->addRow(pcurves);
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
  tabs_->addTab(buildAppearancePage(), "Appearance");
  tabs_->addTab(buildLayoutPage(), "Layout");
  connect(&UiSettings::instance(), &UiSettings::changed, this, &PreferencesDialog::reloadUi);

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

QComboBox* PreferencesDialog::uiCombo(const QString& key, const std::vector<std::pair<QString, QString>>& items,
                                     QWidget* parent) {
  auto* b = new QComboBox(parent);
  b->setObjectName("pref_" + key.section('/', 1));
  for (const auto& [label, value] : items) b->addItem(label, value);
  connect(b, &QComboBox::currentIndexChanged, this, [this, b, key] {
    if (!loading_) UiSettings::instance().set(key, b->currentData());
  });
  uiControls_.emplace_back(key, b);
  return b;
}

QWidget* PreferencesDialog::uiControl(const QString& key) const {
  for (const auto& [k, w] : uiControls_)
    if (k == key) return w;
  return nullptr;
}

QWidget* PreferencesDialog::buildAppearancePage() {
  auto* page = new QWidget(tabs_);
  page->setObjectName("PreferencesAppearance");
  auto* f = new QFormLayout(page);
  auto* row = new QWidget(page);
  row->setObjectName("pref_theme");
  auto* h = new QHBoxLayout(row);
  h->setContentsMargins(0, 0, 0, 0);
  theme_ = new QButtonGroup(row);
  const std::pair<const char*, const char*> modes[] = {{"Dark", "dark"}, {"Light", "light"}, {"Follow system", "system"}};
  for (const auto& [label, value] : modes) {
    auto* rb = new QRadioButton(label, row);
    rb->setObjectName(QString("pref_theme_%1").arg(value));
    rb->setProperty("value", QString(value));
    theme_->addButton(rb);
    h->addWidget(rb);
  }
  h->addStretch(1);
  connect(theme_, &QButtonGroup::buttonToggled, this, [this](QAbstractButton* b, bool on) {
    if (on && !loading_) UiSettings::instance().set("ui/theme", b->property("value"));
  });
  uiControls_.emplace_back("ui/theme", row);
  f->addRow("Theme", row);
  fontScale_ = new QSpinBox(page);
  fontScale_->setObjectName("pref_fontScale");
  fontScale_->setRange(90, 130);
  fontScale_->setSingleStep(5);
  fontScale_->setSuffix(" %");
  connect(fontScale_, &QSpinBox::valueChanged, this, [this](int v) {
    if (!loading_) UiSettings::instance().set("ui/fontScale", v);
  });
  uiControls_.emplace_back("ui/fontScale", fontScale_);
  f->addRow("UI scale", fontScale_);
  auto check = [&](const QString& key, const QString& text) {
    auto* c = new QCheckBox(text, page);
    c->setObjectName("pref_" + key.section('/', 1));
    connect(c, &QCheckBox::toggled, this, [this, key](bool on) {
      if (!loading_) UiSettings::instance().set(key, on);
    });
    uiControls_.emplace_back(key, c);
    f->addRow(c);
  };
  check("ui/glow", "Glow on live handles and the active card");
  check("ui/scanlines", "Viewport scanline texture");
  return page;
}

QWidget* PreferencesDialog::buildLayoutPage() {
  auto* page = new QWidget(tabs_);
  page->setObjectName("PreferencesLayout");
  auto* h = new QHBoxLayout(page);
  auto* form = new QWidget(page);
  auto* f = new QFormLayout(form);
  f->setContentsMargins(0, 0, 0, 0);
  f->addRow("Toolbar", uiCombo("ui/toolbarArea", {{"Top", "top"}, {"Left", "left"}, {"Right", "right"}}, form));
  f->addRow("Sidebar", uiCombo("ui/sidebarSide", {{"Left", "left"}, {"Right", "right"}}, form));
  f->addRow("Command card",
            uiCombo("ui/cardPlacement",
                    {{"Float right", "floatRight"}, {"Float left", "floatLeft"}, {"In the sidebar", "sidebar"}, {"Where dragged", "free"}},
                    form));
  f->addRow("Quick toolbar", uiCombo("ui/quickToolbar", {{"Off", "off"}, {"Left", "left"}, {"Right", "right"}}, form));
  f->addRow("Orientation", uiCombo("ui/orientation", {{"Hex dial", "hexDial"}, {"View cube", "viewCube"}}, form));
  f->addRow("Timeline", uiCombo("ui/timelineStyle", {{"Feature list", "featureList"}, {"Bottom strip", "strip"}}, form));
  auto* remember = new QCheckBox("Remember the dragged card position", form);
  remember->setObjectName("pref_cardRememberDrag");
  connect(remember, &QCheckBox::toggled, this, [this](bool on) {
    if (!loading_) UiSettings::instance().set("ui/cardRememberDrag", on);
  });
  uiControls_.emplace_back("ui/cardRememberDrag", remember);
  f->addRow(remember);
  auto* rulers = new QCheckBox("Edge rulers in sketch mode", form);
  rulers->setObjectName("pref_edgeRuler");
  connect(rulers, &QCheckBox::toggled, this, [this](bool on) {
    if (!loading_) UiSettings::instance().set("ui/edgeRuler", on);
  });
  uiControls_.emplace_back("ui/edgeRuler", rulers);
  f->addRow(rulers);
  auto* note = new QLabel("Every choice applies right away.", form);
  note->setObjectName("StatusDim");
  note->setWordWrap(true);
  // p2p3-check 10: room for the wrapped lines (a form row does not grow for a wrapped label)
  note->setMinimumHeight(3 * note->fontMetrics().lineSpacing());
  f->addRow(note);
  h->addWidget(form, 1);
  layoutPreview_ = new LayoutPreview(page);
  h->addWidget(layoutPreview_, 0, Qt::AlignTop);
  return page;
}

void PreferencesDialog::reloadUi(const QString& key) {
  const bool wasLoading = loading_;
  loading_ = true;
  UiSettings& ui = UiSettings::instance();
  for (const auto& [k, w] : uiControls_) {
    if (!key.isEmpty() && k != key) continue;
    if (k == "ui/theme") {
      for (QAbstractButton* b : theme_->buttons()) b->setChecked(b->property("value").toString() == ui.string(k));
    } else if (auto* c = qobject_cast<QComboBox*>(w)) {
      const int i = c->findData(ui.string(k));
      if (i >= 0) c->setCurrentIndex(i);
    } else if (auto* cb = qobject_cast<QCheckBox*>(w)) {
      cb->setChecked(ui.flag(k));
    } else if (auto* sp = qobject_cast<QSpinBox*>(w)) {
      sp->setValue(ui.number(k));
    }
  }
  loading_ = wasLoading;
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
  reloadUi({});
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
