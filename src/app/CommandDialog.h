// FabriCAD app: floating command dialog that hosts ITool::createDialog() bodies (ux.md 6.2).
//
//   +--------------------------------+
//   | [ico] EXTRUDE                  |  title bar (drag to move)
//   +--------------------------------+
//   | <tool body widget>             |
//   | message (prompt / error)       |
//   |              [ OK ] [Cancel]   |  (when ITool::wantsDialogButtons())
//   +--------------------------------+
//
// The frame is a child of the viewport (overlay, right edge, below the ViewCube); the
// viewport stays interactive. Enter -> accepted(), Esc -> cancelled() (Enter is ignored
// inside multi-line editors and while OK is disabled).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QFrame>
#include <QIcon>
#include <QPointer>

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace fabricad::app {

class CommandDialog : public QFrame {
  Q_OBJECT
 public:
  explicit CommandDialog(QWidget* viewport);

  /// Show `body` (ownership taken; may be null for "title only") for a tool.
  void present(const QString& title, const QIcon& icon, QWidget* body, bool buttons);
  /// Remove and delete the current body; hides the frame.
  void dismiss();
  bool isPresenting() const { return isVisible() || body_; }
  QWidget* body() const { return body_; }
  void setAcceptEnabled(bool enabled);
  bool isAcceptEnabled() const { return acceptEnabled_; }
  void setMessage(const QString& text, bool error = false);
  /// Keep the frame inside the viewport after a resize.
  void reposition();
  QPushButton* okButton() const { return ok_; }

 signals:
  void accepted();
  void cancelled();

 protected:
  void keyPressEvent(QKeyEvent* e) override;
  bool eventFilter(QObject* o, QEvent* e) override;

 private:
  QWidget* viewport_;
  QWidget* title_ = nullptr;
  QLabel* titleIcon_ = nullptr;
  QLabel* titleText_ = nullptr;
  QVBoxLayout* bodyLayout_ = nullptr;
  QPointer<QWidget> body_;
  QLabel* message_ = nullptr;
  QWidget* buttonRow_ = nullptr;
  QPushButton* ok_ = nullptr;
  QPushButton* cancel_ = nullptr;
  bool acceptEnabled_ = true;
  bool userMoved_ = false;
  QPoint dragOffset_;
  bool dragging_ = false;
};

}  // namespace fabricad::app
