// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/tutorial/TutorialController.h"

#include "app/CommandCard.h"
#include "app/MainWindow.h"
#include "app/TitleBar.h"
#include "app/ThemeManager.h"
#include "app/tutorial/TestProbe.h"
#include "app/tutorial/TutorialBubble.h"
#include "app/tutorial/TutorialEngine.h"
#include "app/tutorial/TutorialOverlay.h"
#include "theme/Tokens.h"
#include "viewer/ViewerWidget.h"

#include <QAccessible>
#include <QApplication>
#include <QDateTime>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

namespace fabricad::app::tutorial {

namespace {

constexpr int kTrackMs = 100;
constexpr int kModelTrackMs = 250;
constexpr int kGap = 14;  ///< bubble to anchor
constexpr int kHiddenHintMs = 4000;  ///< "Can't see it?" only after this long in the step

/// The welcome / finish card: painted like the bubble (tokens), centred in the window.
class Card : public QFrame {
 public:
  explicit Card(QWidget* parent) : QFrame(parent) {
    setFocusPolicy(Qt::NoFocus);
    setFixedWidth(420);
  }

 protected:
  void paintEvent(QPaintEvent* e) override {
    QPainter p(this);
    p.fillRect(rect(), theme::color(theme::Tok::SurfaceOverlay));
    p.setPen(QPen(theme::color(theme::Tok::BorderDefault), 1));
    p.drawRect(rect().adjusted(0, 0, -1, -1));
    p.fillRect(QRect(0, 0, width(), 3), theme::color(theme::Tok::AccentIndicator));
    QFrame::paintEvent(e);
  }
};

QFrame* makeCard(QWidget* window, const QString& name, const QString& title, const QString& text,
                 const std::vector<std::pair<QString, QString>>& buttons, std::vector<QPushButton*>* out) {
  auto* card = new Card(window);
  card->setObjectName(name);
  auto* v = new QVBoxLayout(card);
  v->setContentsMargins(20, 18, 20, 16);
  v->setSpacing(10);
  auto* t = new QLabel(title, card);
  t->setObjectName(name + "Title");
  t->setProperty("textStyle", "title");
  ThemeManager::applyTextStyle(t);
  v->addWidget(t);
  auto* body = new QLabel(TutorialBubble::richText(text), card);
  body->setObjectName(name + "Text");
  body->setWordWrap(true);
  body->setTextFormat(Qt::RichText);
  QFont f = theme::font(theme::TextStyle::Body);
  f.setCapitalization(QFont::MixedCase);
  f.setWeight(QFont::Medium);
  body->setFont(f);
  v->addWidget(body);
  auto* row = new QHBoxLayout;
  row->addStretch(1);
  for (const auto& [label, obj] : buttons) {
    auto* b = new QPushButton(label, card);
    b->setObjectName(obj);
    b->setFocusPolicy(Qt::NoFocus);
    b->setAutoDefault(false);
    row->addWidget(b);
    if (out) out->push_back(b);
  }
  if (out && !out->empty()) out->front()->setDefault(true);  // primary look
  v->addLayout(row);
  card->adjustSize();
  card->move((window->width() - card->width()) / 2, std::max(60, (window->height() - card->height()) / 3));
  card->show();
  card->raise();
  return card;
}

QRect windowRect(const QWidget* w) { return QRect(w->mapToGlobal(QPoint(0, 0)), w->size()); }

}  // namespace

TutorialController* TutorialController::install(MainWindow& w, QMenu* helpMenu) {
  if (TutorialController* c = of(w)) return c;
  return new TutorialController(w, helpMenu);
}

TutorialController* TutorialController::of(const MainWindow& w) { return w.findChild<TutorialController*>("TutorialController", Qt::FindDirectChildrenOnly); }

TutorialController::TutorialController(MainWindow& w, QMenu* helpMenu) : QObject(&w), w_(w), resolver_(w) {
  setObjectName("TutorialController");
  tours_ = discoverTours(&loadErrors_);
  for (const QString& e : loadErrors_) qWarning("tutorial: %s", qPrintable(e));
  engine_ = new TutorialEngine(w, this);
  overlay_ = new TutorialOverlay(&w);
  overlay_->hide();
  overlay_->setDimEnabled(dimSetting());
  bubble_ = new TutorialBubble(&w);
  bubble_->hide();
  connect(overlay_, &TutorialOverlay::raised, this, [this] { raiseOwnWidgets(); });
  track_ = new QTimer(this);
  track_->setInterval(kTrackMs);
  connect(track_, &QTimer::timeout, this, &TutorialController::track);
  connect(engine_, &TutorialEngine::stepChanged, this, [this] { onStep(); });
  connect(engine_, &TutorialEngine::hintChanged, bubble_, [this](const QString& h, bool warning) { bubble_->setHint(h, warning); });
  connect(engine_, &TutorialEngine::finished, this, &TutorialController::onFinished);
  connect(bubble_, &TutorialBubble::backClicked, engine_, &TutorialEngine::back);
  connect(bubble_, &TutorialBubble::nextClicked, engine_, &TutorialEngine::next);
  connect(bubble_, &TutorialBubble::showMeClicked, this, &TutorialController::showMe);
  connect(bubble_, &TutorialBubble::exitClicked, engine_, &TutorialEngine::stop);
  connect(bubble_, &TutorialBubble::undoToStartClicked, this, [this] {
    if (!engine_->undoToStepStart()) w_.showMessage("Nothing to undo in this step");
  });
  connect(bubble_, &TutorialBubble::restoreClicked, this, [this] {
    if (w_.isInteractive() &&
        QMessageBox::question(&w_, "Restore chapter",
                              "Open the start of this chapter in a new tab? Your current tab is kept as it is.") != QMessageBox::Yes)
      return;
    QString err;
    if (!engine_->restoreChapter(&err)) w_.showMessage(("Cannot restore the chapter: " + err).toStdString(), core::MessageLevel::Error);
  });
  connect(overlay_, &TutorialOverlay::ghostFinished, this, [this] { overlay_->setSpotlight(last_.visible ? last_.rect : QRect()); });
  connect(&ThemeManager::instance(), &ThemeManager::themeChanged, overlay_, qOverload<>(&QWidget::update));
  if (helpMenu) {
    menu_ = helpMenu->addMenu("Tutorials");
    menu_->setObjectName("TutorialsMenu");
    // first in the Help menu: tours are what a new user looks for
    if (QAction* first = helpMenu->actions().value(0); first && first != menu_->menuAction()) {
      helpMenu->removeAction(menu_->menuAction());
      helpMenu->insertAction(first, menu_->menuAction());
      helpMenu->insertSeparator(first);
    }
    connect(menu_, &QMenu::aboutToShow, this, &TutorialController::fillMenu);
    fillMenu();
  }
  qApp->installEventFilter(this);
  if (!qEnvironmentVariableIsEmpty("FABRICAD_TEST_PROBE")) probe_ = new TestProbe(w, *this, qEnvironmentVariable("FABRICAD_TEST_PROBE"), this);
  // developer aid (screenshots, docs, the GUI runner's --from-chapter):
  // FABRICAD_TUTORIAL_START=<tour>[:<step id>[:restore]] starts a tour at launch; "restore" then
  // opens that chapter's start state in a new (untitled) tab, as ⋯ > Restore chapter does
  if (const QString start = qEnvironmentVariable("FABRICAD_TUTORIAL_START"); !start.isEmpty())
    QTimer::singleShot(200, this, [this, start] {
      const QString id = start.section(':', 0, 0);
      if (!startTour(id)) return;
      if (const Tour* t = findTour(id); t && start.contains(':'))
        for (int i = t->indexOfStep(start.section(':', 1, 1)); i > engine_->stepIndex() && engine_->running();) engine_->next();
      if (start.section(':', 2, 2) == QLatin1String("restore") && engine_->running()) engine_->restoreChapter();
    });
}

const Tour* TutorialController::findTour(const QString& id) const {
  for (const Tour& t : tours_)
    if (t.id == id) return &t;
  return nullptr;
}

bool TutorialController::dimSetting() { return QSettings().value("tutorial/dim", true).toBool(); }

void TutorialController::setDimEnabled(bool on) {
  QSettings().setValue("tutorial/dim", on);
  overlay_->setDimEnabled(on);
}

void TutorialController::fillMenu() {
  if (!menu_) return;
  menu_->clear();
  if (tours_.empty()) menu_->addAction("No tutorials installed")->setEnabled(false);
  for (const Tour& t : tours_) {
    const QString saved = TutorialEngine::savedStep(t.id);
    const bool canResume = !saved.isEmpty() && t.indexOfStep(saved) > 0;
    QAction* a = menu_->addAction(canResume ? QString("%1 (resume at step %2)").arg(t.title, saved) : t.title);
    a->setObjectName("Tutorial_" + t.id);
    a->setToolTip(t.description);
    const QString id = t.id;
    connect(a, &QAction::triggered, this, [this, id, canResume] { startTour(id, canResume); });
    if (canResume) {
      QAction* over = menu_->addAction(t.title + ": start over");
      connect(over, &QAction::triggered, this, [this, id] { startTour(id, false); });
    }
  }
  menu_->addSeparator();
  if (engine_->running()) connect(menu_->addAction("Exit tour"), &QAction::triggered, engine_, &TutorialEngine::stop);
  QAction* dim = menu_->addAction("Dim around the target");
  dim->setObjectName("TutorialDimAction");
  dim->setCheckable(true);
  dim->setChecked(overlay_->dimEnabled());
  connect(dim, &QAction::toggled, this, &TutorialController::setDimEnabled);
}

bool TutorialController::startTour(const QString& id, bool resume) {
  const Tour* t = findTour(id);
  if (!t) return false;
  if (welcome_) welcome_->deleteLater();
  int step = 0;
  if (resume) step = std::max(0, t->indexOfStep(TutorialEngine::savedStep(id)));
  if (!engine_->start(*t, step)) return false;
  currentTour_ = id;
  overlay_->sync();
  overlay_->show();
  bubble_->show();
  raiseOwnWidgets();
  track_->start();
  onStep();
  return true;
}

bool TutorialController::welcomeSuppressed() {
  if (qEnvironmentVariableIntValue("FABRICAD_TUTORIAL_WELCOME") != 0) return false;  // tests, demos
  if (qEnvironmentVariableIntValue("FABRICAD_NO_TUTORIAL") != 0) return true;
  if (!qEnvironmentVariableIsEmpty("FABRICAD_SCREENSHOT")) return true;
  if (QGuiApplication::platformName() == QLatin1String("offscreen")) return true;
  return false;
}

bool TutorialController::maybeShowWelcome() {
  if (welcomeSuppressed() || !w_.isInteractive() || engine_->running() || welcome_) return false;
  QSettings st;
  QStringList offered = st.value("tutorial/offered").toStringList();
  const Tour* tour = nullptr;
  for (const Tour& t : tours_)
    if (t.welcome && !offered.contains(t.id)) {
      tour = &t;
      break;
    }
  if (!tour) return false;
  offered << tour->id;
  st.setValue("tutorial/offered", offered);  // a new tour is offered once, also after updates
  std::vector<QPushButton*> b;
  const QString text = tour->description.isEmpty() ? "Take a short guided tour?" : tour->description;
  welcome_ = makeCard(&w_, "TutorialWelcome", "Welcome to FabriCAD", TutorialEngine::expand(text),
                      {{"Start tour", "TutorialStart"}, {"Not now", "TutorialNotNow"}}, &b);
  const QString id = tour->id;
  welcome_->setProperty("tourId", id);  // the GUI runner reads it through the probe
  connect(b[0], &QPushButton::clicked, this, [this, id] { startTour(id); });
  connect(b[1], &QPushButton::clicked, welcome_, &QObject::deleteLater);
  return true;
}

void TutorialController::showFinishCard(const QString& text) {
  if (welcome_) welcome_->deleteLater();
  std::vector<QPushButton*> b;
  welcome_ = makeCard(&w_, "TutorialFinished", "Tour complete", text, {{"Close", "TutorialClose"}}, &b);
  connect(b[0], &QPushButton::clicked, welcome_, &QObject::deleteLater);
}

void TutorialController::raiseOwnWidgets() {
  if (bubble_ && !bubble_->isFloating()) bubble_->raise();
  if (welcome_) welcome_->raise();
}

void TutorialController::onFinished(bool completed) {
  track_->stop();
  overlay_->stopGhost();
  overlay_->hide();
  bubble_->setFloating(false);
  bubble_->hide();
  last_ = {};
  if (completed) {
    const Tour* t = findTour(currentTour_);
    showFinishCard(t && !t->finishText.isEmpty() ? TutorialEngine::expand(t->finishText) : QString("Well done. Help > Tutorials has more tours."));
  }
}

void TutorialController::onStep() {
  const Step* s = engine_->step();
  const Tour* t = engine_->tour();
  if (!s || !t) return;
  int chapter = 0;
  t->stepAt(engine_->stepIndex(), &chapter);
  TutorialBubble::Content c;
  c.chapter = s->title.isEmpty() ? t->chapters[chapter].title : s->title;
  c.text = TutorialEngine::expand(s->text);
  c.keys = s->keysShown;
  c.step = engine_->stepIndex() + 1;
  c.total = t->stepCount();
  c.canBack = engine_->stepIndex() > 0;
  c.canShowMe = !s->actions.empty();
  c.nextIsManual = s->until.needsNext();
  c.last = engine_->stepIndex() == t->stepCount() - 1 && c.nextIsManual;
  c.canRestore = true;
  overlay_->stopGhost();
  bubble_->setContent(c);
  lastCameraSeq_ = ~0ull;
  lastVia_.clear();
  track();
  // screen readers: announce the new step
  QAccessibleAnnouncementEvent ev(bubble_, c.chapter + ". " + bubble_->plainText());
  QAccessible::updateAccessibility(&ev);
}

ResolvedAnchor TutorialController::currentAnchor() const {
  const Step* s = engine_->step();
  return s ? resolver_.resolve(s->anchor) : ResolvedAnchor{};
}

void TutorialController::track() {
  const Step* s = engine_->step();
  if (!s) return;
  // model anchors pick-check several points: only after the camera moved, or every 250 ms
  if (s->anchor.isModel()) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (engine_->cameraSeq() == lastCameraSeq_ && now - lastModelResolveMs_ < kModelTrackMs) return;
    lastCameraSeq_ = engine_->cameraSeq();
    lastModelResolveMs_ = now;
  }
  last_ = resolver_.resolve(s->anchor);
  // the step's other click targets (the second edge, the dimension's line): the bubble keeps
  // clear of them, so it never covers what the user is asked to click next
  actionRects_.clear();
  for (const Action& a : s->actions) {
    if (a.type != "click" && a.type != "dclick" && a.type != "rclick" && a.type != "move") continue;
    const QJsonValue t = a.raw.value(a.type);
    if (t.isString() && t.toString() == "@anchor") continue;
    if (t.isObject() && (t.toObject().contains("dx") || t.toObject().contains("dy"))) continue;
    QString err;
    if (auto an = Anchor::parse(t, &err); an && !an->isNone())
      if (const QRect r = resolver_.roughRect(*an); !r.isNull()) actionRects_.push_back(r);
  }
  // popups and modal dialogs cannot be covered: no dim, the bubble floats beside them
  QWidget* popup = QApplication::activePopupWidget();
  if (popup && popup->objectName() == QLatin1String("TutorialMoreMenu")) popup = nullptr;
  QWidget* modal = QApplication::activeModalWidget();
  QWidget* cover = popup ? popup : modal;
  overlay_->setSuppressDim(cover != nullptr);
  bubble_->setFloating(cover != nullptr);
  if (!overlay_->ghostRunning()) overlay_->setSpotlight(last_.visible && !s->anchor.isNone() ? last_.rect : QRect());
  // how to reach a target that is not directly visible (unless the engine shows its own hint)
  // "Can't see it?" waits a few seconds: at step entry the target is often only behind the card
  // for a moment, and an instant hint reads like a reproach
  const bool hiddenVia = last_.via == "offscreen" || last_.via == "occluded";
  if (last_.via != lastVia_ && (!hiddenVia || engine_->stepElapsedMs() >= kHiddenHintMs)) {
    lastVia_ = last_.via;
    if (engine_->hint().isEmpty()) {
      const QString& v = last_.via;
      bubble_->setHint(v == "menu" ? QString("It is in this menu: open it first.")
                       : v == "expand" ? QString("Expand this row first.")
                       : (v == "offscreen" || v == "occluded") ? QString("Can't see it? Orbit, or press [[Home]].")
                                                               : QString());
    }
  }
  placeBubble(last_);
}

void TutorialController::placeBubble(const ResolvedAnchor& r) {
  const QRect win = windowRect(&w_);
  const QSize sz = bubble_->sizeHint().expandedTo(QSize(TutorialBubble::kWidth, 80));
  QRect target = r.visible ? r.rect : QRect();
  // occupied areas the bubble must not cover: the command card it may point into, the
  // orientation control
  std::vector<QRect> avoid;
  if (CommandCard* card = w_.commandCard(); card && card->isVisible()) avoid.push_back(windowRect(card));
  for (const char* name : {"OrientationDial", "ViewCube"})
    if (auto* x = w_.findChild<QWidget*>(name); x && x->isVisible()) avoid.push_back(windowRect(x));
  // the on-canvas value boxes (a rectangle's width / height) the step asks to type into; weighted
  // like a click target, with a margin for the cursor next to them (UI2-TUTORIAL-7)
  std::vector<QRect> mustSee;
  for (QWidget* x : w_.findChildren<QWidget*>("fcValueBox"))
    if (x->isVisible()) mustSee.push_back(windowRect(x).adjusted(-24, -24, 24, 24));
  QRect bounds = win.adjusted(8, 8, -8, -8);
  if (QWidget* cover = bubble_->isFloating() ? (QApplication::activePopupWidget() ? QApplication::activePopupWidget()
                                                                                  : QApplication::activeModalWidget())
                                             : nullptr) {
    // beside the popup / dialog, on the screen
    target = windowRect(cover);
    avoid.clear();
    // inside the main window when it fits there (a rootful / tiled X screen can be larger than
    // what is visible), else on the popup's screen
    if (QScreen* scr = cover->screen()) {
      const QRect screen = scr->availableGeometry().adjusted(8, 8, -8, -8);
      const QRect inWin = bounds.intersected(screen);
      bounds = inWin.height() >= sz.height() && inWin.width() >= sz.width() ? inWin : screen;
    }
  }
  QRect best;
  if (target.isNull()) {
    // no anchor: bottom centre of the window, above the status bar
    best = QRect(QPoint(win.center().x() - sz.width() / 2, win.bottom() - sz.height() - 72), sz);
  } else {
    const QPoint c = target.center();
    std::vector<QRect> cands = {
        QRect(QPoint(c.x() - sz.width() / 2, target.bottom() + kGap), sz),   // below
        QRect(QPoint(target.right() + kGap, c.y() - sz.height() / 2), sz),   // right
        QRect(QPoint(c.x() - sz.width() / 2, target.top() - kGap - sz.height()), sz),  // above
        QRect(QPoint(target.left() - kGap - sz.width(), c.y() - sz.height() / 2), sz),  // left
        // further away (the step's next targets sit around the anchor)
        QRect(QPoint(c.x() - sz.width() / 2, target.bottom() + 6 * kGap), sz),
        QRect(QPoint(target.left() - 6 * kGap - sz.width(), c.y() - sz.height() / 2), sz),
        QRect(QPoint(target.right() + 6 * kGap, c.y() - sz.height() / 2), sz),
        QRect(QPoint(c.x() - sz.width() / 2, target.top() - 6 * kGap - sz.height()), sz),
    };
    int bestScore = INT_MAX;
    for (QRect q : cands) {
      // slide into the bounds along the free axis
      if (q.left() < bounds.left()) q.moveLeft(bounds.left());
      if (q.right() > bounds.right()) q.moveRight(bounds.right());
      if (q.top() < bounds.top()) q.moveTop(bounds.top());
      if (q.bottom() > bounds.bottom()) q.moveBottom(bounds.bottom());
      int score = 0;
      if (q.intersects(target)) score += 100000;
      for (const QRect& a : actionRects_)
        if (q.intersects(a)) score += 50000;
      for (const QRect& a : mustSee)
        if (q.intersects(a)) score += 50000;
      for (const QRect& a : avoid) {
        const QRect i = q.intersected(a);
        score += i.width() * i.height();
      }
      if (score < bestScore) {
        bestScore = score;
        best = q;
      }
    }
  }
  if (bubble_->isFloating()) bubble_->move(best.topLeft());
  else bubble_->move(w_.mapFromGlobal(best.topLeft()));
  bubble_->resize(sz);
  overlay_->setBubble(bubble_->isFloating() ? QRect() : best, target.isNull() ? QPoint() : target.center());
}

void TutorialController::showMe() {
  const Step* s = engine_->step();
  if (!s) return;
  std::vector<TutorialOverlay::GhostStep> steps;
  QPoint at = last_.visible ? last_.point : QPoint();
  auto targetPoint = [&](const QJsonValue& v, bool* ok) -> QPoint {
    *ok = true;
    if (v.isString() && v.toString() == "@anchor") {
      ResolvedAnchor r = currentAnchor();
      *ok = r.visible;
      return r.point;
    }
    QString e;
    if (auto a = Anchor::parse(v, &e)) {
      ResolvedAnchor r = resolver_.resolve(*a);
      *ok = r.visible;
      return r.point;
    }
    *ok = false;
    return {};
  };
  for (const Action& a : s->actions) {
    const QJsonValue v = a.raw.value(a.type);
    using G = TutorialOverlay::GhostStep;
    bool ok = false;
    if (a.type == "click" || a.type == "dclick" || a.type == "rclick" || a.type == "wheel") {
      const QPoint p = targetPoint(v, &ok);
      if (!ok) continue;
      at = p;
      steps.push_back({a.type == "click" ? G::Click : a.type == "dclick" ? G::DoubleClick : a.type == "rclick" ? G::RightClick : G::Move, p, {}});
    } else if (a.type == "move") {
      QPoint p;
      if (v.isObject() && (v.toObject().contains("dx") || v.toObject().contains("dy")))
        p = at + QPoint(v.toObject().value("dx").toInt(), v.toObject().value("dy").toInt()), ok = true;
      else
        p = targetPoint(v, &ok);
      if (!ok) continue;
      at = p;
      steps.push_back({G::Move, p, {}});
    } else if (a.type == "drag") {
      const QJsonObject d = v.toObject();
      bool ok2 = false;
      const QPoint from = targetPoint(d.value("from"), &ok);
      QPoint to;
      if (d.value("to").isObject() && d.value("to").toObject().contains("dx"))
        to = from + QPoint(d.value("to").toObject().value("dx").toInt(), d.value("to").toObject().value("dy").toInt()), ok2 = true;
      else
        to = targetPoint(d.value("to"), &ok2);
      if (!ok || !ok2) continue;
      steps.push_back({G::Move, from, {}});
      steps.push_back({G::Move, to, {}});
      at = to;
    } else if (a.type == "type") {
      steps.push_back({G::Type, at, v.toString()});
    } else if (a.type == "key") {
      steps.push_back({G::Key, at, keycapText(v.toString())});
    }
  }
  if (steps.empty()) {
    bubble_->setHint(s && !s->actions.empty() ? QString("The target is hidden from this view: orbit, or press [[Home]], then try Show me again.")
                                              : QString("This step has nothing to show: follow the text, or press Next."));
    return;
  }
  const QPoint from = bubble_->mapToGlobal(bubble_->rect().center());
  overlay_->setSpotlight(QRect());
  overlay_->playGhost(from, std::move(steps));
}

bool TutorialController::eventFilter(QObject* o, QEvent* e) {
  if (!engine_->running()) return false;
  if (e->type() != QEvent::KeyPress && e->type() != QEvent::ShortcutOverride) return false;
  auto* k = static_cast<QKeyEvent*>(e);
  const Qt::KeyboardModifiers mods = k->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
  const bool f7 = k->key() == Qt::Key_F7 && mods == Qt::NoModifier;
  const bool altLeft = k->key() == Qt::Key_Left && mods == Qt::AltModifier;
  const bool altRight = k->key() == Qt::Key_Right && mods == Qt::AltModifier;
  if (!f7 && !altLeft && !altRight) return false;  // Esc and everything else: the app's
  // only for this window (a dialog of another window keeps its keys)
  if (auto* w = qobject_cast<QWidget*>(o); w && w->window() != &w_ && !w->window()->inherits("QMenu")) return false;
  if (e->type() == QEvent::ShortcutOverride) {
    e->accept();  // no QShortcut / menu shortcut fires; the KeyPress follows
    return true;
  }
  if (k->isAutoRepeat()) return true;
  if (f7) showMe();
  else if (altLeft) engine_->back();
  else engine_->next();
  return true;
}

}  // namespace fabricad::app::tutorial
