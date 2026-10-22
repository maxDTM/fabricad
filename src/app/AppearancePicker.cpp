// FabriCAD app: Appearance colour picker.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/AppearancePicker.h"

#include "app/AppActions.h"
#include "core/Document.h"
#include "core/Json.h"

#include <QColorDialog>

namespace fabricad::app {

bool pickAppearance(QWidget* parent, core::Document& doc, const std::vector<core::ObjectId>& bodyIds) {
  if (bodyIds.empty()) return false;
  const core::Body* b = doc.body(bodyIds.front());
  if (!b) return false;
  const QColor current = QColor::fromRgbF(b->color[0], b->color[1], b->color[2], b->color[3]);
  const QString title = bodyIds.size() == 1 ? "Appearance of " + core::qs(b->name)
                                            : QString("Appearance of %1 bodies").arg(bodyIds.size());
  const QColor c = QColorDialog::getColor(current, parent, title, QColorDialog::ShowAlphaChannel);
  if (!c.isValid()) return false;
  const std::array<float, 4> rgba{static_cast<float>(c.redF()), static_cast<float>(c.greenF()),
                                  static_cast<float>(c.blueF()), static_cast<float>(c.alphaF())};
  auto cmd = makeSetBodiesColor(doc, bodyIds, rgba);
  if (!cmd) return false;
  doc.undoStack().push(std::move(cmd));
  return true;
}

}  // namespace fabricad::app
