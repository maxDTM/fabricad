// FabriCAD app: compatibility name for the main toolbar (the tabbed Ribbon was replaced by
// MainToolbar in redesign phase 1). Keeps `#include "app/Ribbon.h"` and `Ribbon*` working for
// branches written against the old shell.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "app/MainToolbar.h"

namespace fabricad::app {
using Ribbon = MainToolbar;
}  // namespace fabricad::app
