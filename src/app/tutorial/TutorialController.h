// FabriCAD tutorial engine: the shell side (docs/plans/tutorial.md 4, TutorialLauncher).
// One per MainWindow (install()). Owns the engine, the overlay, the bubble, the first-launch welcome
// card and the read-only TestProbe; fills Help > Tutorials (every discovered tour, resume, light dim
// on/off); routes the tour shortcuts F7 (show me), Alt+Left (back), Alt+Right (skip / next) while a
// tour runs. Esc is never taken: it cancels the current command and never exits the tour.
// The welcome card is not shown for --script, offscreen runs, FABRICAD_NO_TUTORIAL=1 or
// FABRICAD_SCREENSHOT, and only once per tour id (QSettings tutorial/offered); FABRICAD_TUTORIAL_WELCOME=1
// forces it (tests, demos). FABRICAD_TUTORIAL_START=<tour>[:<step>] starts a tour at launch (screenshots).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "app/tutorial/AnchorResolver.h"
#include "app/tutorial/TutorialDefinition.h"

#include <QObject>
#include <QPointer>

class QMenu;
class QTimer;
class QFrame;

namespace fabricad::app {
class MainWindow;
}

namespace fabricad::app::tutorial {

class TutorialEngine;
class TutorialOverlay;
class TutorialBubble;
class TestProbe;

class TutorialController : public QObject {
  Q_OBJECT
 public:
  /// Creates the controller (child of `w`) and adds the Tutorials submenu to `helpMenu`.
  static TutorialController* install(MainWindow& w, QMenu* helpMenu);
  static TutorialController* of(const MainWindow& w);

  TutorialEngine& engine() { return *engine_; }
  TutorialOverlay* overlay() const { return overlay_; }
  TutorialBubble* bubble() const { return bubble_; }
  QFrame* welcomeCard() const { return welcome_; }
  const std::vector<Tour>& tours() const { return tours_; }
  const Tour* findTour(const QString& id) const;
  QStringList loadErrors() const { return loadErrors_; }

  /// Start a tour (from its saved step when `resume`).
  bool startTour(const QString& id, bool resume = false);
  /// First launch: the welcome card for the first tour not offered yet. False when not shown.
  bool maybeShowWelcome();
  static bool welcomeSuppressed();  ///< --script / offscreen / FABRICAD_NO_TUTORIAL / FABRICAD_SCREENSHOT
  /// "Show me" for the current step (F7): the ghost cursor plays the step's `do` path.
  void showMe();
  void setDimEnabled(bool on);  ///< QSettings tutorial/dim (default on)
  static bool dimSetting();

  /// The current step's anchor, resolved now.
  ResolvedAnchor currentAnchor() const;
  /// Re-resolve the anchor and move the spotlight and the bubble (normally on a timer).
  void track();

 protected:
  bool eventFilter(QObject* o, QEvent* e) override;

 private:
  TutorialController(MainWindow& w, QMenu* helpMenu);
  void fillMenu();
  void onStep();
  void onFinished(bool completed);
  void placeBubble(const ResolvedAnchor& r);
  void showFinishCard(const QString& text);
  void raiseOwnWidgets();

  MainWindow& w_;
  std::vector<Tour> tours_;
  QStringList loadErrors_;
  TutorialEngine* engine_ = nullptr;
  TutorialOverlay* overlay_ = nullptr;
  TutorialBubble* bubble_ = nullptr;
  QPointer<QFrame> welcome_;
  QPointer<QMenu> menu_;
  QString currentTour_;
  TestProbe* probe_ = nullptr;
  QTimer* track_ = nullptr;
  AnchorResolver resolver_;
  ResolvedAnchor last_;
  QString lastVia_;
  quint64 lastCameraSeq_ = ~0ull;
  qint64 lastModelResolveMs_ = 0;
};

}  // namespace fabricad::app::tutorial
