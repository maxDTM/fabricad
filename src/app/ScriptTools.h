// FabriCAD app: shell verbs for the command line and scripts (docs/SCRIPTING.md).
//
// These are instant tools flagged keepsActiveTool, so ToolManager::runCommandLine runs them
// beside the active command instead of handing the text to it as typed input:
//   OK / CANCEL            accept or cancel the active command (Enter / Esc)
//   SELECT <selectors>     set the selection from selector text (body:Seat/face:top ...)
//   PICK x,y[,z] [...]     emulate a click: sketch point, or a ray-pick against the model
//   INFO / LIST / CHECK    inspect the document, assert facts (CHECK fails the script line)
//   OPTIONS                list the active command's properties (name=value input)
//   ECHO / WAIT / SCREENSHOT / CLOSE / TAB
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Tool.h"

#include <string>
#include <vector>

namespace fabricad::core {
class Document;
}

namespace fabricad::app {

class AppShell;

/// `w` is MainWindow or the HeadlessHost of a script run.
void registerScriptTools(AppShell& w, core::ToolRegistry& reg);

/// Facts about the shell a CHECK may refer to besides the document.
struct CheckContext {
  int tabs = 1;
  std::string activeCommand;  ///< tool id, empty when idle
  int sketchDof = -1;         ///< -1 = no sketch is being edited
};
struct CheckResult {
  bool ok = false;
  std::string message;  ///< "bodies == 2: ok (2)" or the reason
};
/// Evaluate a CHECK expression (tokens after the verb). See docs/SCRIPTING.md "CHECK".
CheckResult evaluateCheck(const core::Document& doc, const CheckContext& ctx, const std::vector<std::string>& args);

/// LIST output (one line per entry) for a category: bodies, features, components, sketches,
/// joints, params, selection, all. Unknown category -> empty string.
std::string listText(const core::Document& doc, const std::string& category);

}  // namespace fabricad::app
