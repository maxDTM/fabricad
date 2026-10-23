// FabriCAD app: the SELECTION panel body (docs/plans/ui-redesign.md 5.2): a key / value grid
// describing what is selected, from the SelectionModel of the active document.
//
//   TYPE        SKETCH PROFILE (CLOSED)
//   SOURCE      Sketch3 · ON TOP FACE
//   AREA        1 256.64 mm²
//   PERIMETER   125.66 mm
//
// Rows are 26 px; the key column (92 px) is `caption` in text.tertiary, the value `value` (names
// and numbers as typed). Nothing selected shows NOTHING SELECTED. The rows come from
// selectionFacts() (widget-free, unit-tested). objectName SelectionPanel.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/ShapeRef.h"

#include <QPointer>
#include <QString>
#include <QWidget>

#include <utility>
#include <vector>

namespace fabricad::core {
class Document;
}

namespace fabricad::app {

struct SelectionFact {
  QString key;    ///< caption text ("Type"; shown in caps)
  QString value;  ///< as typed
  bool caps = false;  ///< value is app text (shown in caps), not user data
};

/// What the panel lists for a selection (empty = nothing selected).
std::vector<SelectionFact> selectionFacts(const core::Document& doc, const std::vector<core::ShapeRef>& items);
/// "1 256.64" (thin-space thousands, '.' decimal point, 3.3).
QString formatGrouped(double v, int decimals);

class SelectionPanel : public QWidget {
  Q_OBJECT
 public:
  static constexpr int kRowH = 26;
  static constexpr int kKeyW = 92;
  explicit SelectionPanel(QWidget* parent = nullptr);

  /// Follow another document (tab switch); null shows nothing.
  void setDocument(core::Document* doc);
  core::Document* document() const { return doc_; }
  const std::vector<SelectionFact>& facts() const { return facts_; }
  int selectionCount() const;
  /// What the running command has picked (its highlighted inputs): listed while the selection
  /// itself is empty, because a command consumes the clicks (P5-MOCKUP-6, P5-LIGHT-3).
  void setPicked(const std::vector<core::ShapeRef>& refs);
  const std::vector<core::ShapeRef>& picked() const { return picked_; }
  /// Rebuild now (normally coalesced).
  void refresh();
  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

 signals:
  void countChanged(int count);

 protected:
  void paintEvent(QPaintEvent* e) override;

 private:
  void scheduleRefresh();
  QPointer<core::Document> doc_;
  std::vector<QMetaObject::Connection> conns_;
  std::vector<SelectionFact> facts_;
  std::vector<core::ShapeRef> picked_;
  bool pending_ = false;
};

}  // namespace fabricad::app
