// FabriCAD viewer: Qt overlay widgets of the 3D viewport (docs/plans/ui-redesign.md 5.3, 7.2-7.4).
//
//   OrientationDial     #OrientationDial  hex dial (top-right): tick ring, projected X/Y/Z tips with
//                                         letters, gold azimuth mark, AZ/EL readout and the
//                                         ISO TOP FRONT RIGHT buttons. With ui/orientation=viewCube
//                                         only the readout and the buttons show (under the cube).
//   Breadcrumb          #Breadcrumb       document > component crumbs + the gold EDITING chip
//   SelectionFilterBar  #SelectionFilterBar  BODIES FACES EDGES SKETCH PROFILES (selection priority)
//   CoordReadout        #CoordReadout     X / Y / Z of the point under the cursor + unit cell
//   ScaleBar            #ScaleBar         1-2-5 ruler at the view centre
//
// All are children of ViewerWidget, placed by it (ViewerWidget::layoutOverlayWidgets) and reported
// to OverlayLayout as fixed regions (ViewerWidget::addOverlayRegions), so the command card, the
// sketch palette and the view-tool column keep clear of them. They paint from the theme tokens
// of the viewer palette's theme at paint time (no cached colours) and use no QGraphicsEffect.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "viewer/ViewerMath.h"

#include <QStringList>
#include <QWidget>

#include <gp_Pnt.hxx>

#include <array>
#include <optional>
#include <vector>

namespace fabricad::viewer {

class ViewerWidget;

// ---------------------------------------------------------------------------------------------
class OrientationDial : public QWidget {
  Q_OBJECT
 public:
  static constexpr int kWidth = 168;    ///< widget width (the button row)
  static constexpr int kPlate = 140;    ///< hexagon plate box
  static constexpr int kReadout = 22;   ///< AZ / EL line
  static constexpr int kButtons = 26;   ///< ISO TOP FRONT RIGHT row
  static constexpr int kGap = 4;  ///< readout -> buttons (the readout sits right under the plate)

  enum class Part { None, AxisTip, Vertex, Ring, Inside, Button };
  struct Hit {
    Part part = Part::None;
    int axis = -1;         ///< AxisTip: 0 X, 1 Y, 2 Z
    bool positive = true;  ///< AxisTip: positive (filled) or negative (hollow) tip
    int vertex = -1;       ///< Vertex: 0..5 counter-clockwise from the top
    int button = -1;       ///< Button: 0 ISO, 1 TOP, 2 FRONT, 3 RIGHT
  };

  explicit OrientationDial(ViewerWidget& viewer);

  /// false: ViewCube mode, only the readout and the buttons (the cube sits above them).
  void setPlateVisible(bool on);
  bool plateVisible() const { return plate_; }
  QSize sizeHint() const override;

  QPointF plateCenter() const;
  double plateRadius() const { return kPlate / 2.0 - 2.0; }
  /// Screen position of an axis tip (positive or negative end) for the current camera.
  QPointF tipPosition(int axis, bool positive) const;
  QRect buttonRect(int index) const;
  QRect readoutRect() const;
  Hit hitTest(const QPointF& p) const;
  QString readoutText() const;  ///< "AZ 045°  EL 35°"
  /// The highlighted view button (the camera shows that standard view), -1 for none.
  int currentButton() const;
  /// Perform the click action for a hit (tests and the mouse handler share it).
  void activate(const Hit& h);

 protected:
  void paintEvent(QPaintEvent*) override;
  void mousePressEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void mouseReleaseEvent(QMouseEvent* e) override;
  void wheelEvent(QWheelEvent* e) override;
  void keyPressEvent(QKeyEvent* e) override;
  void leaveEvent(QEvent* e) override;
  void contextMenuEvent(QContextMenuEvent* e) override;

 private:
  void paintPlate(QPainter& p);
  void paintReadout(QPainter& p);

  ViewerWidget& viewer_;
  bool plate_ = true;
  Hit hover_;
  Hit press_;
  QPointF pressPos_, lastPos_;
  bool dragging_ = false;
  double dragAz0_ = 0.0, dragEl0_ = 0.0, dragAngle0_ = 0.0;
};

// ---------------------------------------------------------------------------------------------
class Breadcrumb : public QWidget {
  Q_OBJECT
 public:
  explicit Breadcrumb(ViewerWidget& viewer);
  /// Crumbs keep the user's case (document, component names); the chip caption is caps.
  void setCrumbs(const QStringList& crumbs);
  const QStringList& crumbs() const { return crumbs_; }
  /// The gold live chip: `caption` ("EDITING", or the command name) + `name` (as typed); both
  /// empty = no chip.
  void setLiveChip(const QString& caption, const QString& name);
  QString chipText() const;
  bool hasChip() const { return !chipCaption_.isEmpty() || !chipName_.isEmpty(); }
  QSize sizeHint() const override;
  QRect crumbRect(int index) const;

 signals:
  void crumbClicked(int index);

 protected:
  void paintEvent(QPaintEvent*) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void mousePressEvent(QMouseEvent* e) override;
  void leaveEvent(QEvent* e) override;

 private:
  struct Layout {
    std::vector<QRect> crumbs;
    std::vector<QRect> separators;
    QRect chip;
    int width = 0;
  };
  Layout computeLayout() const;

  ViewerWidget& viewer_;
  QStringList crumbs_;
  QString chipCaption_, chipName_;
  int hover_ = -1;
};

// ---------------------------------------------------------------------------------------------
class SelectionFilterBar : public QWidget {
  Q_OBJECT
 public:
  enum Segment { Bodies, Faces, Edges, Profiles, SegmentCount };
  explicit SelectionFilterBar(ViewerWidget& viewer);

  static const char* segmentLabel(Segment s);
  static SelectionPriority segmentPriority(Segment s);
  /// Kinds a segment stands for (it hides when the current tool and filter allow none of them).
  static core::ShapeKindMask segmentKinds(Segment s);

  /// Re-read the viewer's priority and filters (ViewerWidget calls it when they change).
  void refresh();
  bool segmentVisible(Segment s) const { return visible_[s]; }
  bool segmentActive(Segment s) const;
  QRect segmentRect(Segment s) const;
  /// Click a segment: its priority, or back to the default when it is the active one.
  void toggle(Segment s);
  QSize sizeHint() const override;

 protected:
  void paintEvent(QPaintEvent*) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void mousePressEvent(QMouseEvent* e) override;
  void leaveEvent(QEvent* e) override;
  bool event(QEvent* e) override;

 private:
  int segmentAt(const QPoint& p) const;
  ViewerWidget& viewer_;
  std::array<bool, SegmentCount> visible_{true, true, true, true};
  int hover_ = -1;
};

// ---------------------------------------------------------------------------------------------
class CoordReadout : public QWidget {
  Q_OBJECT
 public:
  explicit CoordReadout(ViewerWidget& viewer);
  /// The point under the cursor (world mm), or none.
  void setPoint(const std::optional<gp_Pnt>& p);
  const std::optional<gp_Pnt>& point() const { return point_; }
  QString cellText(int axis) const;  ///< "X 62.000" (display units)
  QString unitText() const;          ///< "mm"
  QSize sizeHint() const override;

 protected:
  void paintEvent(QPaintEvent*) override;

 private:
  ViewerWidget& viewer_;
  std::optional<gp_Pnt> point_;
};

// ---------------------------------------------------------------------------------------------
class ScaleBar : public QWidget {
  Q_OBJECT
 public:
  static constexpr int kRuler = 140;
  explicit ScaleBar(ViewerWidget& viewer);
  /// Recompute from the view (camera changes, resize, units).
  void refresh();
  double length() const { return length_; }    ///< ruler length in display units
  double pixels() const { return pixels_; }    ///< drawn ruler length in px
  QString label() const;                       ///< "50 mm"
  QSize sizeHint() const override;

 protected:
  void paintEvent(QPaintEvent*) override;

 private:
  ViewerWidget& viewer_;
  double length_ = 0.0, pixels_ = 0.0;
};

}  // namespace fabricad::viewer
