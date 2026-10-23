// FabriCAD tutorial engine: full-window painter (docs/plans/tutorial.md 4, "Layering", "Spotlight", "Show me").
// A child of the MainWindow, raised above the central widget, docks, toolbar and viewport overlays,
// transparent for the mouse (every click reaches the app). Paints a light dim (token tutorial.dim,
// optional) with a cut-out around the anchor, a pulsing accent.focus ring, the bubble's pointer
// notch and the "show me" ghost cursor (click ripples, typed-text keycaps). It never moves the real
// pointer and never sends input. Colours come from tokens at paint time (both themes, no literals).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QPointer>
#include <QRect>
#include <QWidget>

#include <vector>

class QVariantAnimation;

namespace fabricad::app::tutorial {

class TutorialOverlay : public QWidget {
  Q_OBJECT
 public:
  explicit TutorialOverlay(QWidget* window);

  /// Spotlight rect in global coordinates (null rect: dim everything, no ring).
  void setSpotlight(const QRect& globalRect, bool ring = true);
  QRect spotlight() const { return spot_; }
  void setDimEnabled(bool on);
  bool dimEnabled() const { return dim_; }
  /// No dim while a popup or modal dialog is open (it cannot be covered and would look broken).
  void setSuppressDim(bool on);
  /// The bubble's rect (global) and the point its notch aims at (null: no notch).
  void setBubble(const QRect& globalRect, const QPoint& aim);

  // --- ghost cursor ("show me") --------------------------------------------------------------
  struct GhostStep {
    enum Kind { Move, Click, DoubleClick, RightClick, Type, Key } kind = Move;
    QPoint to;       ///< global target (Move / clicks)
    QString text;    ///< Type / Key
  };
  void playGhost(const QPoint& fromGlobal, std::vector<GhostStep> steps);
  void stopGhost();
  bool ghostRunning() const;

  /// Keep on top of siblings and sized to the window (called by the event filter and the engine).
  void sync();

 signals:
  void ghostFinished();
  /// sync() raised the overlay: widgets that must stay above it raise themselves again.
  void raised();

 protected:
  void paintEvent(QPaintEvent* e) override;
  bool eventFilter(QObject* o, QEvent* e) override;

 private:
  void nextGhostStep();
  QRect local(const QRect& g) const { return QRect(mapFromGlobal(g.topLeft()), g.size()); }

  QRect spot_;
  bool ring_ = true;
  bool dim_ = true;
  bool suppressDim_ = false;
  QRect bubble_;
  QPoint aim_;
  double pulse_ = 0;  ///< 0..1
  QVariantAnimation* pulseAnim_ = nullptr;

  // ghost state
  std::vector<GhostStep> ghost_;
  int ghostIndex_ = -1;
  QPoint ghostPos_;
  QPoint ghostFrom_;
  QString ghostText_;
  double ripple_ = -1;  ///< 0..1 while a click ripple runs
  QVariantAnimation* ghostAnim_ = nullptr;
};

}  // namespace fabricad::app::tutorial
