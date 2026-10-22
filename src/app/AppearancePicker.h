// FabriCAD app: Appearance command UI (A, SOLID > MODIFY > Appearance, browser context menu).
// One colour dialog for all target bodies, one undo step (APP-04).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Types.h"

#include <vector>

class QWidget;

namespace fabricad::core {
class Document;
}

namespace fabricad::app {

/// Ask for a colour (modal QColorDialog, starting from the first body's colour) and apply it to
/// `bodyIds`. Returns false when cancelled or nothing changed.
bool pickAppearance(QWidget* parent, core::Document& doc, const std::vector<core::ObjectId>& bodyIds);

}  // namespace fabricad::app
