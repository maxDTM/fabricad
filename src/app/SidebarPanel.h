// FabriCAD app: one section of the sidebar (FEATURES, MODEL, SELECTION): a panel header and a
// body (docs/plans/ui-redesign.md 5.2, strengthened after the user feedback of 2026-10-22).
//
// Panel header (36 px): a full-width band in surface.header with a 1 px border.strong line above
// and below, a 3 px accent.indicator tick at the left, a section glyph, the title in the `header`
// style (12 px, weight 800, +1.5 tracking, text.primary), a count chip, optional tool buttons
// (FEATURES: search and filter; MODEL: master eye) and a chevron at the right. A click on the
// band (or Space / Enter while it has the focus) collapses the panel to its header. The body
// starts 6 px below the header and is indented, so the header always reads as its own level.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QWidget>

class QHBoxLayout;
class QToolButton;
class QVBoxLayout;

namespace fabricad::app {

class PanelHeader : public QWidget {
  Q_OBJECT
 public:
  static constexpr int kHeight = 36;
  PanelHeader(const QString& title, const QString& glyph, QWidget* parent = nullptr);

  QString title() const { return title_; }
  /// Count chip ("13"); a negative count hides the chip.
  void setCount(int n);
  int count() const { return count_; }
  void setCollapsed(bool c);
  bool isCollapsed() const { return collapsed_; }
  /// A tool button at the right (before the chevron); objectName and tooltip given.
  QToolButton* addTool(const QString& objectName, const QString& icon, const QString& tooltip);
  /// Rectangle of the title text (tests: no clipping at 260 px).
  QRect titleRect() const;
  QSize sizeHint() const override;

 signals:
  void toggled(bool collapsed);

 protected:
  void paintEvent(QPaintEvent* e) override;
  void mousePressEvent(QMouseEvent* e) override;
  void keyPressEvent(QKeyEvent* e) override;
  void enterEvent(QEnterEvent* e) override;
  void leaveEvent(QEvent* e) override;

 private:
  int toolsLeft() const;
  QString title_;
  QString glyph_;
  int count_ = -1;
  bool collapsed_ = false;
  bool hover_ = false;
  QHBoxLayout* tools_ = nullptr;
};

class SidebarPanel : public QWidget {
  Q_OBJECT
 public:
  SidebarPanel(const QString& key, const QString& title, const QString& glyph, QWidget* parent = nullptr);
  /// "features", "model", "selection" (ui/panels).
  QString key() const { return key_; }
  PanelHeader* header() const { return header_; }
  void setBody(QWidget* body);
  QWidget* body() const { return body_; }
  /// Collapsed: only the header shows (the splitter gives the panel its header height).
  void setCollapsed(bool c);
  bool isCollapsed() const { return collapsed_; }
  /// Body space before the first row (5.2: 6 px).
  static constexpr int kBodyTop = 0;

 signals:
  void collapsedChanged(bool collapsed);

 private:
  QString key_;
  PanelHeader* header_ = nullptr;
  QWidget* body_ = nullptr;
  QVBoxLayout* layout_ = nullptr;
  bool collapsed_ = false;
};

}  // namespace fabricad::app
