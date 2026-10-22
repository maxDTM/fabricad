// FabriCAD app: sidebar host.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/SidebarHost.h"

#include "app/BrowserTree.h"
#include "app/FeatureListWidget.h"
#include "app/SelectionPanel.h"
#include "app/SidebarPanel.h"
#include "app/ThemeManager.h"
#include "core/Document.h"
#include "core/Selection.h"
#include "theme/Tokens.h"

#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <numeric>

namespace fabricad::app {

using theme::Tok;

namespace {
const char* kPanelKeys[] = {"features", "model", "selection"};

/// Dashed accent outline over the card drop target (6.4 "Drag to move the card").
class DropOverlay : public QWidget {
 public:
  explicit DropOverlay(QWidget* parent) : QWidget(parent) {
    setObjectName("CardDropHighlight");
    setAttribute(Qt::WA_TransparentForMouseEvents);
    hide();
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    QColor fill = theme::color(Tok::AccentSelFill);
    fill.setAlphaF(0.5);
    p.fillRect(rect(), fill);
    p.setPen(QPen(theme::color(Tok::AccentIndicator), 2, Qt::DashLine));
    p.drawRect(QRectF(rect()).adjusted(2, 2, -2, -2));
  }
};
}  // namespace

SidebarHost::SidebarHost(QWidget* parent) : QWidget(parent) {
  setObjectName("SidebarHost");
  setAttribute(Qt::WA_StyledBackground);
  setMinimumWidth(kMinWidth);
  auto* v = new QVBoxLayout(this);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(0);
  splitter_ = new QSplitter(Qt::Vertical, this);
  splitter_->setObjectName("SidebarSplitter");
  splitter_->setChildrenCollapsible(false);
  splitter_->setHandleWidth(6);
  v->addWidget(splitter_);

  cardSlot_ = new QWidget(splitter_);
  cardSlot_->setObjectName("CardSlot");
  cardSlot_->setAttribute(Qt::WA_StyledBackground);
  auto* slotLayout = new QVBoxLayout(cardSlot_);
  slotLayout->setContentsMargins(0, 0, 0, 0);
  slotLayout->setSpacing(0);
  splitter_->addWidget(cardSlot_);
  cardSlot_->hide();

  // FEATURES: header (search, filter) + optional search field + one list per tab
  auto* features = new SidebarPanel(kPanelKeys[0], "Features", "feature", splitter_);
  searchButton_ = features->header()->addTool("FeatureSearchButton", "search", "Search features");
  searchButton_->setCheckable(true);
  filterButton_ = features->header()->addTool("FeatureFilterButton", "select-filter-all", "Filter features");
  filterButton_->setPopupMode(QToolButton::InstantPopup);
  {
    auto* body = new QWidget(features);
    body->setObjectName("FeaturesBody");
    auto* bv = new QVBoxLayout(body);
    bv->setContentsMargins(0, 0, 0, 0);
    bv->setSpacing(0);
    search_ = new QLineEdit(body);
    search_->setObjectName("FeatureSearch");
    search_->setPlaceholderText("Search features");
    search_->setClearButtonEnabled(true);
    search_->hide();
    auto* searchWrap = new QWidget(body);
    auto* sw = new QHBoxLayout(searchWrap);
    sw->setContentsMargins(12, 6, 12, 0);
    sw->addWidget(search_);
    searchWrap->setVisible(false);
    bv->addWidget(searchWrap);
    featureStack_ = new QStackedWidget(body);
    featureStack_->setObjectName("FeatureStack");
    bv->addWidget(featureStack_, 1);
    features->setBody(body);
    connect(searchButton_, &QToolButton::toggled, this, [this, searchWrap](bool on) {
      searchWrap->setVisible(on);
      search_->setVisible(on);
      if (on) {
        if (isPanelCollapsed(Panel::Features)) setPanelCollapsed(Panel::Features, false);
        search_->setFocus();
      } else {
        search_->clear();
      }
    });
    connect(search_, &QLineEdit::textChanged, this, [this](const QString& t) {
      if (features_) features_->setSearch(t.trimmed());
    });
    auto* menu = new QMenu(filterButton_);
    QAction* hide = menu->addAction("Hide suppressed features");
    hide->setObjectName("FeatureFilterHideSuppressed");
    hide->setCheckable(true);
    QAction* problems = menu->addAction("Only errors and warnings");
    problems->setObjectName("FeatureFilterProblems");
    problems->setCheckable(true);
    connect(hide, &QAction::toggled, this, [this](bool on) {
      hideSuppressed_ = on;
      if (features_) features_->setHideSuppressed(on);
      filterButton_->setChecked(hideSuppressed_ || problemsOnly_);
    });
    connect(problems, &QAction::toggled, this, [this](bool on) {
      problemsOnly_ = on;
      if (features_) features_->setProblemsOnly(on);
      filterButton_->setChecked(hideSuppressed_ || problemsOnly_);
    });
    filterButton_->setCheckable(true);
    filterButton_->setMenu(menu);
  }
  // MODEL: header (master eye) + one tree per tab
  auto* model = new SidebarPanel(kPanelKeys[1], "Model", "component", splitter_);
  eyeButton_ = model->header()->addTool("ModelMasterEye", "eye", "Show or hide all bodies");
  connect(eyeButton_, &QToolButton::clicked, this, &SidebarHost::masterEyeRequested);
  modelStack_ = new QStackedWidget(model);
  modelStack_->setObjectName("ModelStack");
  model->setBody(modelStack_);
  // SELECTION
  auto* sel = new SidebarPanel(kPanelKeys[2], "Selection", "select", splitter_);
  selection_ = new SelectionPanel(sel);
  sel->setBody(selection_);
  connect(selection_, &SelectionPanel::countChanged, this, [this](int) { updateCounts(); });

  panels_ = {features, model, sel};
  for (size_t i = 0; i < panels_.size(); ++i) {
    splitter_->addWidget(panels_[i]);
    splitter_->setStretchFactor(static_cast<int>(i) + 1, i == 1 ? 1 : 0);
    connect(panels_[i]->header(), &PanelHeader::toggled, this, [this, i](bool c) {
      collapsed_[i] = c;
      applyLayout();
      saveState();
      emit panelCollapsedChanged(static_cast<Panel>(i), c);
    });
  }
  splitter_->setStretchFactor(0, 1);
  connect(splitter_, &QSplitter::splitterMoved, this, &SidebarHost::onSplitterMoved);
  dropOverlay_ = new DropOverlay(this);
  connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this] { update(); });
  restoreState();
}

SidebarHost::~SidebarHost() {
  for (const auto& c : docConns_) disconnect(c);
}

void SidebarHost::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.fillRect(rect(), theme::color(Tok::SurfacePanel));
}

// =========================================================================================
// active tab
// =========================================================================================

void SidebarHost::setActive(core::Document* doc, FeatureListWidget* features, BrowserTree* model) {
  for (const auto& c : docConns_) disconnect(c);
  docConns_.clear();
  if (features_ && features_ != features) {
    features_->setCompact(false);
    features_->setSearch({});
    features_->setHideSuppressed(false);
    features_->setProblemsOnly(false);
  }
  doc_ = doc;
  features_ = features;
  model_ = model;
  if (features) {
    featureStack_->setCurrentWidget(features);
    features->setSearch(search_->text().trimmed());
    features->setHideSuppressed(hideSuppressed_);
    features->setProblemsOnly(problemsOnly_);
    docConns_.push_back(connect(features, &FeatureListWidget::rowsChanged, this, [this] {
      updateCounts();
      if (card_) applyLayout();
    }));
  }
  if (model) modelStack_->setCurrentWidget(model);
  selection_->setDocument(doc);
  if (doc) {
    docConns_.push_back(connect(&doc->selection(), &core::SelectionModel::filterChanged, this, [this] { updateFeatureSelection(); }));
  }
  updateFeatureSelection();
  updateCounts();
  applyLayout();
}

void SidebarHost::updateCounts() {
  int n = 0;
  if (doc_)
    for (const auto& f : doc_->features())
      if (f->isTimelineVisible()) ++n;
  panels_[0]->header()->setCount(doc_ ? n : -1);
  panels_[2]->header()->setCount(selection_->selectionCount());
}

// =========================================================================================
// style, collapse, sizes
// =========================================================================================

void SidebarHost::setTimelineStyle(TimelineStyle s) {
  if (style_ == s) return;
  style_ = s;
  applyLayout();
}

void SidebarHost::setPanelCollapsed(Panel p, bool collapsed) {
  const size_t i = static_cast<size_t>(p);
  if (collapsed_[i] == collapsed) return;
  collapsed_[i] = collapsed;
  applyLayout();
  saveState();
  emit panelCollapsedChanged(p, collapsed);
}

bool SidebarHost::isPanelCollapsed(Panel p) const { return panels_[static_cast<size_t>(p)]->isCollapsed(); }

std::array<int, 3> SidebarHost::panelSizes() const {
  const QList<int> s = splitter_->sizes();
  return {s.value(1), s.value(2), s.value(3)};
}

void SidebarHost::setPanelSizes(const std::array<int, 3>& sizes) {
  for (size_t i = 0; i < 3; ++i)
    if (sizes[i] > PanelHeader::kHeight) sizes_[i] = sizes[i];
  applyLayout();
  saveState();
}

void SidebarHost::onSplitterMoved() {
  if (applying_ || card_ || featureSelectionActive()) return;
  const std::array<int, 3> s = panelSizes();
  for (size_t i = 0; i < 3; ++i)
    if (!panels_[i]->isCollapsed() && panels_[i]->isVisible() && s[i] > PanelHeader::kHeight) sizes_[i] = s[i];
  saveState();
}

void SidebarHost::saveState() const {
  QSettings st;
  st.setValue("ui/sidebarSplit", QString("%1,%2,%3").arg(sizes_[0]).arg(sizes_[1]).arg(sizes_[2]));
  QStringList panels;
  for (size_t i = 0; i < 3; ++i) panels << QString("%1:%2").arg(kPanelKeys[i]).arg(collapsed_[i] ? 0 : 1);
  st.setValue("ui/panels", panels.join(','));
}

void SidebarHost::restoreState() {
  QSettings st;
  const QStringList split = st.value("ui/sidebarSplit").toString().split(',', Qt::SkipEmptyParts);
  if (split.size() == 3) {
    std::array<int, 3> s{};
    bool ok = true;
    for (int i = 0; i < 3; ++i) {
      bool good = false;
      s[static_cast<size_t>(i)] = split[i].trimmed().toInt(&good);
      ok = ok && good && s[static_cast<size_t>(i)] > 0 && s[static_cast<size_t>(i)] < 10000;
    }
    if (ok) sizes_ = s;  // garbage keeps the defaults
  }
  const QStringList panels = st.value("ui/panels").toString().split(',', Qt::SkipEmptyParts);
  for (const QString& entry : panels) {
    const QString key = entry.section(':', 0, 0).trimmed();
    const QString val = entry.section(':', 1, 1).trimmed();
    for (size_t i = 0; i < 3; ++i)
      if (key == kPanelKeys[i] && (val == "0" || val == "1")) collapsed_[i] = val == "0";
  }
  applyLayout();
}

bool SidebarHost::temporarilyExpanded() const { return featureSelectionActive() && style_ == TimelineStyle::FeatureList; }

void SidebarHost::applyLayout() {
  applying_ = true;
  const bool showFeatures = style_ == TimelineStyle::FeatureList;
  const bool docked = card_ != nullptr;
  const bool fs = featureSelectionActive();
  const bool compact = docked && !fs && showFeatures;

  panels_[0]->setVisible(showFeatures);
  // FEATURES: the user's collapse, overridden while a feature-selection input is active
  const bool featuresCollapsed = collapsed_[0] && !fs;
  panels_[0]->setCollapsed(featuresCollapsed && !compact);
  if (features_) features_->setCompact(compact);
  if (compact) {
    const int h = PanelHeader::kHeight + (features_ ? features_->compactHeight() : 0);
    panels_[0]->setMinimumHeight(h);
    panels_[0]->setMaximumHeight(h);
  } else {
    panels_[0]->setMinimumHeight(PanelHeader::kHeight);
    panels_[0]->setMaximumHeight(panels_[0]->isCollapsed() ? PanelHeader::kHeight : QWIDGETSIZE_MAX);
  }
  // a docked card plus an expanded FEATURES (feature-selection input) need the room: MODEL and
  // SELECTION fold to their headers meanwhile (the user's state is kept and comes back)
  const bool makeRoom = docked && fs && showFeatures;
  panels_[1]->setCollapsed(collapsed_[1] || makeRoom);
  panels_[2]->setCollapsed(collapsed_[2] || makeRoom);
  cardSlot_->setVisible(docked);

  // sizes: the panels' remembered sizes; a docked card takes what FEATURES gives up
  const int total = std::max(splitter_->height(), 400);
  std::array<int, 3> s{};
  for (size_t i = 0; i < 3; ++i) s[i] = panels_[i]->isCollapsed() ? PanelHeader::kHeight : sizes_[i];
  if (!showFeatures) s[0] = 0;
  int cardH = 0;
  if (docked) {
    const int want = card_ ? std::max(card_->sizeHint().height(), card_->minimumSizeHint().height()) : 300;
    if (compact) {
      s[0] = panels_[0]->maximumHeight();
      cardH = std::max(want, total - s[0] - s[1] - s[2]);
    } else {
      cardH = std::min(want, total / 2);
      if (showFeatures && fs) s[0] = std::max(s[0], (total - cardH) * 3 / 5);
    }
    // MODEL and SELECTION give way to the card (down to their headers), never FEATURES' live row
    const int rest = std::max(0, total - cardH - s[0] - 2 * splitter_->handleWidth());
    if (s[1] + s[2] > rest) {
      const int minimum = 2 * PanelHeader::kHeight;
      const int avail = std::max(rest, minimum);
      const int sum = std::max(1, s[1] + s[2]);
      s[1] = std::max(PanelHeader::kHeight, s[1] * avail / sum);
      s[2] = std::max(PanelHeader::kHeight, avail - s[1]);
    }
  }
  splitter_->setSizes({cardH, s[0], s[1], s[2]});
  applying_ = false;
  if (dropOverlay_->isVisible()) setDropHighlight(true);  // re-place it
}

// =========================================================================================
// command card API
// =========================================================================================

bool SidebarHost::canDockCard() const { return cardAllowed_; }

void SidebarHost::setCardDockAllowed(bool allowed) {
  if (cardAllowed_ == allowed) return;
  cardAllowed_ = allowed;
  emit cardDockAvailabilityChanged(allowed);
}

void SidebarHost::dockCard(QWidget* card) {
  if (!card) return;
  if (card_ && card_ != card) releaseCard(nullptr);
  card_ = card;
  card->setParent(cardSlot_);
  cardSlot_->layout()->addWidget(card);
  card->show();
  applyLayout();
}

void SidebarHost::releaseCard(QWidget* newParent) {
  if (!card_) return;
  QWidget* card = card_;
  card_ = nullptr;
  cardSlot_->layout()->removeWidget(card);
  card->hide();
  card->setParent(newParent);
  applyLayout();
}

void SidebarHost::setFeatureSelectionActive(bool on) {
  if (forcedFeatureSelection_ == on) return;
  const bool before = featureSelectionActive();
  forcedFeatureSelection_ = on;
  if (before != featureSelectionActive()) {
    applyLayout();
    emit featureSelectionChanged(featureSelectionActive());
  }
}

bool SidebarHost::featureSelectionActive() const { return forcedFeatureSelection_ || filterFeatureSelection_; }

void SidebarHost::updateFeatureSelection() {
  const bool before = featureSelectionActive();
  bool on = false;
  if (doc_) {
    const core::ShapeKindMask f = doc_->selection().filter();
    on = f != core::kAllKinds && core::hasKind(f, core::ShapeKind::Feature);
  }
  filterFeatureSelection_ = on;
  if (before != featureSelectionActive()) {
    applyLayout();
    emit featureSelectionChanged(featureSelectionActive());
  }
}

QRect SidebarHost::cardDropRect() const {
  const QWidget* target = card_ ? cardSlot_ : static_cast<QWidget*>(panels_[style_ == TimelineStyle::FeatureList ? 0 : 1]);
  const QRect local(target->mapTo(this, QPoint(0, 0)), target->size());
  return QRect(mapToGlobal(local.topLeft()), local.size());
}

void SidebarHost::setDropHighlight(bool on) {
  dropHighlight_ = on;
  if (!on) {
    dropOverlay_->hide();
    return;
  }
  const QRect g = cardDropRect();
  dropOverlay_->setGeometry(QRect(mapFromGlobal(g.topLeft()), g.size()));
  dropOverlay_->raise();
  dropOverlay_->show();
}

}  // namespace fabricad::app
