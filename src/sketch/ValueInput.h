// FabriCAD sketch: headless model of Fusion-style on-screen value boxes ("dynamic input").
//
// A tool declares the fields it shows next to the cursor (line: length + angle, rectangle:
// width + height, circle: diameter, ...). While the user moves the mouse the tool feeds the
// measured values; typing fills the active field, Tab (or ',' / '<' like AutoCAD) locks it and
// moves to the next one, Enter commits, Esc clears. A locked field drives the rubber-band
// preview instead of following the cursor and becomes a driving dimension when the geometry is
// created. Evaluation (units, expressions, document parameters) is injected, so the model has
// no dependency on the sketch session and is fully unit-testable. ValueInputOverlay.h renders
// the model as QLineEdit widgets floating over the viewer.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Types.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace fabricad::sketch {

enum class ValueKind { Length, Angle, Count, Scalar };

struct ValueField {
  std::string id;              ///< "length", "angle", "width", "height", "diameter", "radius", "sweep", "sides", "distance"
  std::string label;           ///< short label shown in the box ("Length")
  ValueKind kind = ValueKind::Length;
  std::string text;            ///< text typed by the user (empty = the field follows the cursor)
  double value = 0;            ///< current value in mm / degrees / count: the locked value, else the measured one
  bool locked = false;         ///< fixed by the user; drives the preview and becomes a dimension
  bool valid = true;           ///< last evaluation of `text` succeeded
  std::string error;           ///< evaluation error when !valid
  bool visible = true;         ///< relevant in the tool's current phase (Tab skips hidden fields)
  core::Vec3 anchor;           ///< world point the box is attached to
  bool hasAnchor = false;
  std::string unit;            ///< unit suffix shown after the value ("mm", "°"); empty = none
  bool stepper = false;        ///< the box shows a ▴▾ stepper (feature value boxes, 5.3)
};

class ValueInputModel {
 public:
  /// Evaluate typed text to a value in mm / deg / count. Return nullopt and set *error on failure.
  using Evaluator = std::function<std::optional<double>(const std::string& text, ValueKind kind, std::string* error)>;
  /// Format a value for display in a box that is not being typed into.
  using Formatter = std::function<std::string(double value, ValueKind kind)>;

  enum class KeyResult { Ignored, Consumed, Commit };

  ValueInputModel();

  void setEvaluator(Evaluator e) { evaluator_ = std::move(e); }
  void setFormatter(Formatter f) { formatter_ = std::move(f); }
  /// Called after every change (the overlay syncs its widgets from here).
  void setChangedCallback(std::function<void()> cb) { changed_ = std::move(cb); }

  // --- fields -------------------------------------------------------------------------------
  /// Remove every field.
  void clear();
  /// Add a field; returns its index. The first visible field becomes active.
  int addField(ValueField f);
  int fieldCount() const { return static_cast<int>(fields_.size()); }
  bool empty() const { return fields_.empty(); }
  const ValueField& field(int i) const { return fields_[static_cast<size_t>(i)]; }
  const ValueField* field(const std::string& id) const;
  const std::vector<ValueField>& fields() const { return fields_; }
  int indexOf(const std::string& id) const;
  /// Show/hide fields for the tool's current phase; the active field moves to a visible one.
  void setVisible(const std::string& id, bool visible);
  int visibleCount() const;

  int activeIndex() const { return active_; }
  const ValueField* activeField() const;
  void setActive(int index);
  bool setActive(const std::string& id);
  /// True while the active field holds text that is not locked yet.
  bool isTyping() const;
  /// True when any field is locked or typed into (the tool should treat Enter as "commit").
  bool hasUserInput() const;
  bool anyLocked() const;

  // --- values from the tool ---------------------------------------------------------------------
  /// Live value from the cursor; ignored for locked fields.
  void setMeasured(const std::string& id, double value);
  void setAnchor(const std::string& id, const core::Vec3& world);
  /// Unit suffix and stepper of a field (the formatter then gives the bare number).
  void setPresentation(const std::string& id, const std::string& unit, bool stepper);
  bool isLocked(const std::string& id) const;
  /// The locked value of a field, nullopt if it is following the cursor.
  std::optional<double> lockedValue(const std::string& id) const;
  /// Locked value if any, else the measured one.
  double value(const std::string& id) const;

  // --- typing ------------------------------------------------------------------------------------
  /// Characters a user may type to start entering a value in an empty field (digits, '.', '-', '+', '(').
  static bool isStartChar(char c);
  /// Append typed characters to the active field (unlocks it first). False if nothing was consumed.
  bool typeText(const std::string& chars);
  bool backspace();
  /// Replace the text of a field (from a widget). Unlocks it; empty text makes it follow the cursor.
  void setText(int index, const std::string& text);
  /// Evaluate the active field's text and lock it. False (with *error) when it does not evaluate.
  bool lockActive(std::string* error = nullptr);
  /// Set text + evaluate + lock a field by id. False when it does not evaluate.
  bool lock(const std::string& id, const std::string& text, std::string* error = nullptr);
  /// Lock a field to a value without text (programmatic, e.g. a command-line argument).
  void lockValue(const std::string& id, double value, const std::string& text = {});
  void unlock(int index);
  void unlockAll();
  /// Clear the active field's text (keeps it unlocked).
  void clearActive();
  /// Tab: lock the active field if it has text, then move to the next visible field. False when
  /// the text does not evaluate (the field stays active, `error` is set).
  bool next(std::string* error = nullptr);
  bool previous(std::string* error = nullptr);

  /// Keyboard handling shared by every tool (Qt::Key values, core::KeyModifier bits, typed text):
  ///   printable  -> typed into the active field (an empty field only accepts isStartChar())
  ///   ',' / '<'  -> next field (AutoCAD "30,20", "@50<30" habits); plain text inside parentheses
  ///   Tab / Shift+Tab -> next / previous field; Backspace deletes; Esc clears text, then unlocks all
  ///   Enter      -> Commit when something was typed or locked (the active text is locked first)
  KeyResult keyPress(int key, int modifiers, const std::string& text);

  // --- display -----------------------------------------------------------------------------------
  /// Text for the box: the typed text while editing, else the formatted value.
  std::string displayText(int index) const;
  /// Single-line summary for a headless preview label: "25.00 mm  30.0°" (locked values with a lock mark).
  std::string summary() const;

  // --- commit --------------------------------------------------------------------------------------
  /// Fields locked by the user, in field order.
  std::vector<ValueField> lockedFields() const;
  /// After the geometry was created: unlock everything and clear all text (fields stay).
  void resetInput();

  /// Incremented on every change (tests, overlays).
  int revision() const { return revision_; }

 private:
  ValueField* fieldPtr(const std::string& id);
  void notify();
  bool evaluate(ValueField& f, std::string* error);
  int nextVisible(int from, int step) const;
  bool separatorIsText(const std::string& text) const;

  std::vector<ValueField> fields_;
  int active_ = -1;
  Evaluator evaluator_;
  Formatter formatter_;
  std::function<void()> changed_;
  int revision_ = 0;
};

}  // namespace fabricad::sketch
