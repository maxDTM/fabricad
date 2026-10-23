#pragma once
// Dropdown popups open under the button / combo they belong to (user decision 2026-10-23).
//
// Qt places a QToolButton menu or a QComboBox list with mapToGlobal(), i.e. with the window
// position Qt last heard of. Under Xwayland on a tiling compositor that cached position can be
// stale (the compositor tiles or moves the window without a usable ConfigureNotify), and the
// popup then lands far from its label, e.g. in the middle of the window. PopupAnchor is an
// application event filter that
//   * remembers, per top-level window, the offset between the server-reported global position of
//     the last mouse press (QMouseEvent::globalPosition, root coordinates) and the one Qt maps
//     from its cached window position;
//   * moves every anchored popup (a QToolButton's menu, a QComboBox list) to its label, corrected
//     by that offset, when the popup is shown (before it is mapped);
//   * opens a QToolButton's dropdown on a RIGHT click too, anchored under the button (a left
//     click still runs the button's tool).
// FABRICAD_POPUP_DEBUG=1 logs each placement (for the GUI retest on the real desktop).

#include <QObject>
#include <QPointer>
#include <QRect>
#include <QHash>

class QMenu;
class QToolButton;
class QWidget;

namespace fabricad::app {

/// Top-left of a popup of `size` anchored to `anchor` (both global): left-aligned under it,
/// flipped above when it does not fit below, and kept inside `avail` (the screen's available
/// geometry). `rtl` right-aligns instead.
QPoint popupPosUnder(const QRect& anchor, const QSize& size, const QRect& avail, bool rtl = false);

class PopupAnchor : public QObject {
  Q_OBJECT
 public:
  /// Installs the filter on qApp once (MainWindow calls it; idempotent).
  static PopupAnchor& install();
  static PopupAnchor* instance();

  /// Opens `b`'s menu under `b` (what a right click does). False when it has no menu.
  bool showDropdown(QToolButton* b);
  /// The global rectangle of `w`, corrected by its window's press offset.
  QRect globalRect(const QWidget* w) const;
  /// The measured offset (server global - Qt mapped) for `w`'s window; (0,0) when unknown.
  QPoint correction(const QWidget* w) const;
  /// Where the last anchored popup went (tests).
  QRect lastPlacement() const { return lastPlacement_; }

 protected:
  bool eventFilter(QObject* o, QEvent* e) override;

 private:
  explicit PopupAnchor(QObject* parent);
  QWidget* anchorFor(QMenu* m) const;
  void place(QWidget* popup, QWidget* anchor);

  QHash<const QWidget*, QPoint> corr_;  ///< top-level window -> offset
  QPointer<QToolButton> pressed_;       ///< the tool button of the last mouse press
  QPointer<QToolButton> rightOpened_;   ///< the button whose dropdown a right click opened
  QRect lastPlacement_;
};

}  // namespace fabricad::app
