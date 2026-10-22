// FabriCAD app: inline tip (docs/plans/ui-redesign.md 5.2): a small two-line note next to the
// sidebar, e.g. "GROUP SUPPRESSED / Tick to include its 2 features again" or a refused move.
// surface.overlay with a 1 px border.default edge; the title in `body` caps, the text in
// text.secondary (an error tip gets a 3 px status.error bar). One shared instance per window.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QFrame>

class QLabel;

namespace fabricad::app {

class InlineTip : public QFrame {
  Q_OBJECT
 public:
  enum class Kind { Note, Error };
  /// The tip of `anchor`'s window (created on demand).
  static InlineTip* forWidget(QWidget* anchor);

  /// Show beside `rowRect` (in `anchor` coordinates): right of it when there is room inside the
  /// window, else left (so it works with the sidebar on either side).
  void showBeside(QWidget* anchor, const QRect& rowRect, const QString& title, const QString& text, Kind kind = Kind::Note);
  QString title() const;
  QString text() const;
  Kind kind() const { return kind_; }

 protected:
  void paintEvent(QPaintEvent* e) override;

 private:
  explicit InlineTip(QWidget* window);
  QLabel* title_ = nullptr;
  QLabel* text_ = nullptr;
  Kind kind_ = Kind::Note;
};

}  // namespace fabricad::app
