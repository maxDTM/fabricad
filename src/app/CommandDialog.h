// FabriCAD app: CommandDialog is now CommandCard (docs/plans/ui-redesign.md phase 3). This alias
// keeps older includes and tests compiling; new code uses app/CommandCard.h.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "app/CommandCard.h"

namespace fabricad::app {
using CommandDialog = CommandCard;
}  // namespace fabricad::app
