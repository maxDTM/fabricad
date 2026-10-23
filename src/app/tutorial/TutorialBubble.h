// FabriCAD tutorial engine: the coach mark (docs/plans/tutorial.md 4).
// Chapter title (caps), "STEP n / N" and a progress bar, the step text as typed (**bold**, [[Key]]
// keycaps), a keycap row, a hint line and BACK · SHOW ME · SKIP/NEXT · more (Restore chapter, Undo to
// step start, Exit tour). Never takes the focus, so typed values still reach the value boxes and the
// command card. Painted from tokens (surface.overlay, border.default, text.*), both themes.
// Normally a child of the MainWindow above the overlay; while a popup or modal dialog is open it
// becomes an opaque frameless tool window next to it (popups cannot be covered by a child widget).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QFrame>
#include <QPointer>
#include <QStringList>

class QLabel;
class QHBoxLayout;
class QPushButton;
class QToolButton;
class QAction;

namespace fabricad::app::tutorial {

class ProgressStrip;

class TutorialBubble : public QFrame {
  Q_OBJECT
 public:
  static constexpr int kWidth = 340;

  explicit TutorialBubble(QWidget* window);

  struct Content {
    QString chapter;   ///< shown in caps
    QString text;      ///< already expanded placeholders; markup: **bold**, [[Key]]
    QStringList keys;  ///< keycap row
    int step = 0, total = 0;
    bool canBack = false;
    bool canShowMe = false;
    bool nextIsManual = false;  ///< NEXT (the step waits for it) instead of SKIP
    bool last = false;          ///< FINISH
    bool canRestore = false;
  };
  void setContent(const Content& c);
  /// Empty hides the hint line. warning: a correction (status.warning); else help (text.secondary).
  void setHint(const QString& text, bool warning = false);
  QString hint() const;
  QString plainText() const { return plain_; }

  /// Child of the main window (false) or a frameless tool window next to a popup (true).
  void setFloating(bool floating);
  bool isFloating() const { return floating_; }

  /// Markup -> rich text (escaped; ** and [[ ]] converted; keycap colours from tokens).
  static QString richText(const QString& markup);
  static QString plain(const QString& markup);

 signals:
  void backClicked();
  void showMeClicked();
  void nextClicked();
  void restoreClicked();
  void undoToStartClicked();
  void exitClicked();

 protected:
  void paintEvent(QPaintEvent* e) override;

 private:
  void applyTheme();
  QLabel* chapter_ = nullptr;
  QLabel* counter_ = nullptr;
  ProgressStrip* progress_ = nullptr;
  QLabel* body_ = nullptr;
  QWidget* keysRow_ = nullptr;
  QHBoxLayout* keysLayout_ = nullptr;
  QLabel* hint_ = nullptr;
  QPushButton* back_ = nullptr;
  QPushButton* showMe_ = nullptr;
  QPushButton* next_ = nullptr;
  QToolButton* more_ = nullptr;
  QAction* restore_ = nullptr;
  QPointer<QWidget> window_;
  bool floating_ = false;
  QString plain_;
  QString markup_;
  bool hintWarning_ = false;
};

}  // namespace fabricad::app::tutorial
