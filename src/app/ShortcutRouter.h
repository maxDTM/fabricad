// FabriCAD app: keyboard routing rules (docs/research/ux.md section 9), widget-free.
//
// Single-letter shortcuts work only while the viewport, browser or timeline has focus (never
// inside text fields); Ctrl/Alt combinations work everywhere except for the standard text
// editing keys inside text fields; Esc cancels, Enter accepts the active command or repeats
// the last one, Space repeats when idle, S opens the command search. While a tool is
// active, canvas keys go to ITool::keyPress first.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <string>

namespace fabricad::app {

enum class KeyAction {
  None,              ///< not ours: let Qt deliver it normally
  ForwardOnly,       ///< give it to the active tool (eat it either way)
  Cancel,            ///< Esc: tool cancel / clear selection
  AcceptOrRepeat,    ///< Enter: OK the active tool, else repeat last command
  Repeat,            ///< Space when idle
  OpenSearch,        ///< S: command search popup
  FocusCommandLine,  ///< ':' focuses the command box
  Shortcut,          ///< look up `sequence` in the tool shortcuts
};

struct KeyContext {
  bool canvasFocus = false;  ///< viewport / browser / timeline (or nothing) has focus
  bool textInput = false;    ///< a line edit / spin box / text edit has focus
  bool toolActive = false;
};

struct KeyRoute {
  KeyAction action = KeyAction::None;
  bool toolFirst = false;    ///< offer the key to ITool::keyPress before acting
  std::string sequence;      ///< portable key sequence text ("Ctrl+Shift+S", "E", "Shift+J", "Del")
};

/// `key` is a Qt::Key, `modifiers` Qt::KeyboardModifiers bits.
KeyRoute routeKey(int key, int modifiers, const KeyContext& ctx);
/// Portable text for a key + modifiers ("Ctrl+Z").
std::string keySequenceText(int key, int modifiers);
/// Normalise a ToolInfo::shortcut string ("ctrl+z" -> "Ctrl+Z", "Delete" -> "Del").
std::string normalizeShortcut(const std::string& s);
/// Undo/redo/cut/copy/paste/select-all and friends: owned by text fields.
bool isStandardEditKey(int key, int modifiers);

}  // namespace fabricad::app
