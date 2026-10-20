// FabriCAD app: keyboard routing rules.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/ShortcutRouter.h"

#include <QKeySequence>

namespace fabricad::app {

namespace {
constexpr int kModMask = Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;

bool isModifierKey(int key) {
  return key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_Meta ||
         key == Qt::Key_AltGr || key == Qt::Key_CapsLock || key == Qt::Key_unknown || key == 0;
}
}  // namespace

std::string keySequenceText(int key, int modifiers) {
  const auto mods = Qt::KeyboardModifiers(modifiers & kModMask);
  return QKeySequence(QKeyCombination(mods, Qt::Key(key))).toString(QKeySequence::PortableText).toStdString();
}

std::string normalizeShortcut(const std::string& s) {
  if (s.empty()) return {};
  QKeySequence seq = QKeySequence::fromString(QString::fromStdString(s), QKeySequence::PortableText);
  if (seq.isEmpty()) return s;
  return seq.toString(QKeySequence::PortableText).toStdString();
}

bool isStandardEditKey(int key, int modifiers) {
  const int mods = modifiers & kModMask;
  if (mods == Qt::ControlModifier) {
    switch (key) {
      case Qt::Key_Z:
      case Qt::Key_Y:
      case Qt::Key_X:
      case Qt::Key_C:
      case Qt::Key_V:
      case Qt::Key_A:
      case Qt::Key_Left:
      case Qt::Key_Right:
      case Qt::Key_Backspace:
      case Qt::Key_Delete:
      case Qt::Key_Home:
      case Qt::Key_End: return true;
      default: return false;
    }
  }
  if (mods == (Qt::ControlModifier | Qt::ShiftModifier))
    return key == Qt::Key_Z || key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Home || key == Qt::Key_End;
  if (mods == Qt::ShiftModifier) return key == Qt::Key_Insert || key == Qt::Key_Delete;
  return false;
}

KeyRoute routeKey(int key, int modifiers, const KeyContext& ctx) {
  KeyRoute r;
  if (isModifierKey(key)) return r;
  const int mods = modifiers & kModMask;
  const bool combo = (mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) != 0;
  r.sequence = keySequenceText(key, mods);

  if (!ctx.canvasFocus) {
    // text fields and other widgets keep their keys; global combos still work
    if (combo && !(ctx.textInput && isStandardEditKey(key, mods))) {
      r.action = KeyAction::Shortcut;
      return r;
    }
    r.action = KeyAction::None;
    return r;
  }

  r.toolFirst = ctx.toolActive;
  switch (key) {
    case Qt::Key_Escape: r.action = KeyAction::Cancel; return r;
    case Qt::Key_Return:
    case Qt::Key_Enter:
      if (mods == Qt::ControlModifier) {  // Ctrl+Enter: a shortcut (Finish Sketch)
        r.action = KeyAction::Shortcut;
        return r;
      }
      r.action = KeyAction::AcceptOrRepeat;
      return r;
    case Qt::Key_Space:
      if (mods == 0) {
        r.action = ctx.toolActive ? KeyAction::ForwardOnly : KeyAction::Repeat;
        return r;
      }
      break;
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
      r.action = ctx.toolActive ? KeyAction::ForwardOnly : KeyAction::None;
      return r;
    case Qt::Key_Colon:
      r.action = KeyAction::FocusCommandLine;
      r.toolFirst = false;
      return r;
    case Qt::Key_S:
      if (mods == 0) {
        r.action = KeyAction::OpenSearch;
        return r;
      }
      break;
    default: break;
  }
  r.action = KeyAction::Shortcut;
  return r;
}

}  // namespace fabricad::app
