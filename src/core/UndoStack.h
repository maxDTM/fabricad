// FabriCAD core: command-pattern undo/redo.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QObject>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fabricad::core {

class UndoCommand {
 public:
  explicit UndoCommand(std::string text = {}) : text_(std::move(text)) {}
  virtual ~UndoCommand() = default;
  /// Apply (called once on push, and again on redo).
  virtual void redo() = 0;
  virtual void undo() = 0;
  const std::string& text() const { return text_; }
  void setText(std::string t) { text_ = std::move(t); }
  /// Commands with the same non-negative id may be merged (e.g. dragging a dimension).
  virtual int mergeId() const { return -1; }
  /// Absorb `other` (same mergeId) into this; return false to refuse.
  virtual bool mergeWith(const UndoCommand& other) { (void)other; return false; }

 private:
  std::string text_;
};

/// Several commands applied as one step.
class MacroCommand : public UndoCommand {
 public:
  explicit MacroCommand(std::string text) : UndoCommand(std::move(text)) {}
  void add(std::unique_ptr<UndoCommand> c) { children_.push_back(std::move(c)); }
  bool empty() const { return children_.empty(); }
  void redo() override;
  void undo() override;

 private:
  std::vector<std::unique_ptr<UndoCommand>> children_;
};

/// Quick command from two lambdas.
class LambdaCommand : public UndoCommand {
 public:
  LambdaCommand(std::string text, std::function<void()> redoFn, std::function<void()> undoFn)
      : UndoCommand(std::move(text)), redo_(std::move(redoFn)), undo_(std::move(undoFn)) {}
  void redo() override { if (redo_) redo_(); }
  void undo() override { if (undo_) undo_(); }

 private:
  std::function<void()> redo_, undo_;
};

class UndoStack : public QObject {
  Q_OBJECT
 public:
  explicit UndoStack(QObject* parent = nullptr);
  ~UndoStack() override;

  /// Executes cmd->redo() and records it (drops the redo branch). Inside a macro the
  /// command is executed and collected into the macro.
  void push(std::unique_ptr<UndoCommand> cmd);
  void undo();
  void redo();
  bool canUndo() const { return !inMacro() && index_ > 0; }
  bool canRedo() const { return !inMacro() && index_ < static_cast<int>(commands_.size()); }
  std::string undoText() const;
  std::string redoText() const;
  /// Group following pushes into one undo step until endMacro().
  void beginMacro(const std::string& text);
  void endMacro();
  bool inMacro() const { return !macros_.empty(); }
  void clear();
  int count() const { return static_cast<int>(commands_.size()); }
  int index() const { return index_; }
  void setUndoLimit(int n) { limit_ = n; }
  /// Clean state tracking (for the "modified" flag).
  void setClean();
  bool isClean() const { return cleanIndex_ == index_; }
  /// No index is clean any more: a change was made outside the stack (or merged into the saved
  /// step), so no undo or redo gets back to the saved state.
  void resetClean();
  /// True while a command's redo()/undo() runs (push, undo, redo).
  bool isExecuting() const { return executing_ > 0; }

 signals:
  void changed();          ///< anything: push/undo/redo/clear
  void indexChanged(int index);
  void cleanChanged(bool clean);

 private:
  void setIndex(int i);

  std::vector<std::unique_ptr<UndoCommand>> commands_;
  std::vector<std::unique_ptr<MacroCommand>> macros_;
  int index_ = 0;
  int cleanIndex_ = 0;
  int limit_ = 200;
  int executing_ = 0;
  struct Busy {
    explicit Busy(UndoStack& s) : s_(s) { ++s_.executing_; }
    ~Busy() { --s_.executing_; }
    UndoStack& s_;
  };
};

}  // namespace fabricad::core
