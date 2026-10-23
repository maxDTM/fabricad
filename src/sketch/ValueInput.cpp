// FabriCAD sketch: ValueInputModel implementation (see ValueInput.h).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sketch/ValueInput.h"

#include "core/Tool.h"

#include <QtCore/Qt>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>

namespace fabricad::sketch {

namespace {

std::string trimmed(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return {};
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

/// Fallback evaluator: plain numbers only (the tools install one with units/expressions).
std::optional<double> plainNumber(const std::string& text, ValueKind, std::string* error) {
  std::string t = trimmed(text);
  if (t.empty()) {
    if (error) *error = "empty value";
    return std::nullopt;
  }
  char* end = nullptr;
  double v = std::strtod(t.c_str(), &end);
  if (!end || *end != '\0') {
    if (error) *error = "not a number: " + t;
    return std::nullopt;
  }
  return v;
}

std::string plainFormat(double v, ValueKind kind) {
  switch (kind) {
    case ValueKind::Count: return std::to_string(static_cast<long long>(std::llround(v)));
    case ValueKind::Angle: return std::format("{:.1f}°", v);
    default: return std::format("{:.2f}", v);
  }
}

}  // namespace

ValueInputModel::ValueInputModel() : evaluator_(plainNumber), formatter_(plainFormat) {}

// --- fields ----------------------------------------------------------------------------------------

void ValueInputModel::clear() {
  fields_.clear();
  active_ = -1;
  notify();
}

int ValueInputModel::addField(ValueField f) {
  if (f.label.empty()) f.label = f.id;
  fields_.push_back(std::move(f));
  int idx = static_cast<int>(fields_.size()) - 1;
  if (active_ < 0 && fields_.back().visible) active_ = idx;
  notify();
  return idx;
}

const ValueField* ValueInputModel::field(const std::string& id) const {
  int i = indexOf(id);
  return i < 0 ? nullptr : &fields_[static_cast<size_t>(i)];
}

ValueField* ValueInputModel::fieldPtr(const std::string& id) {
  int i = indexOf(id);
  return i < 0 ? nullptr : &fields_[static_cast<size_t>(i)];
}

int ValueInputModel::indexOf(const std::string& id) const {
  for (size_t i = 0; i < fields_.size(); ++i)
    if (fields_[i].id == id) return static_cast<int>(i);
  return -1;
}

void ValueInputModel::setVisible(const std::string& id, bool visible) {
  ValueField* f = fieldPtr(id);
  if (!f || f->visible == visible) return;
  f->visible = visible;
  if (!visible && active_ == indexOf(id)) {
    int n = nextVisible(active_, 1);
    active_ = n;
  }
  if (active_ < 0 && visible) active_ = indexOf(id);
  notify();
}

int ValueInputModel::visibleCount() const {
  return static_cast<int>(std::count_if(fields_.begin(), fields_.end(), [](const ValueField& f) { return f.visible; }));
}

const ValueField* ValueInputModel::activeField() const {
  if (active_ < 0 || active_ >= fieldCount()) return nullptr;
  return &fields_[static_cast<size_t>(active_)];
}

void ValueInputModel::setActive(int index) {
  if (index < 0 || index >= fieldCount() || !fields_[static_cast<size_t>(index)].visible) return;
  if (active_ == index) return;
  active_ = index;
  notify();
}

bool ValueInputModel::setActive(const std::string& id) {
  int i = indexOf(id);
  if (i < 0) return false;
  setActive(i);
  return active_ == i;
}

bool ValueInputModel::isTyping() const {
  const ValueField* f = activeField();
  return f && !f->locked && !f->text.empty();
}

bool ValueInputModel::hasUserInput() const {
  return std::any_of(fields_.begin(), fields_.end(), [](const ValueField& f) { return f.locked || !f.text.empty(); });
}

bool ValueInputModel::anyLocked() const {
  return std::any_of(fields_.begin(), fields_.end(), [](const ValueField& f) { return f.locked; });
}

// --- values ------------------------------------------------------------------------------------------

void ValueInputModel::setMeasured(const std::string& id, double value) {
  ValueField* f = fieldPtr(id);
  if (!f || f->locked) return;
  if (f->value == value) return;
  f->value = value;
  notify();
}

void ValueInputModel::setPresentation(const std::string& id, const std::string& unit, bool stepper) {
  ValueField* f = fieldPtr(id);
  if (!f || (f->unit == unit && f->stepper == stepper)) return;
  f->unit = unit;
  f->stepper = stepper;
  notify();
}

void ValueInputModel::setAnchor(const std::string& id, const core::Vec3& world) {
  ValueField* f = fieldPtr(id);
  if (!f) return;
  if (f->hasAnchor && f->anchor == world) return;
  f->anchor = world;
  f->hasAnchor = true;
  notify();
}

bool ValueInputModel::isLocked(const std::string& id) const {
  const ValueField* f = field(id);
  return f && f->locked;
}

std::optional<double> ValueInputModel::lockedValue(const std::string& id) const {
  const ValueField* f = field(id);
  if (!f || !f->locked) return std::nullopt;
  return f->value;
}

double ValueInputModel::value(const std::string& id) const {
  const ValueField* f = field(id);
  return f ? f->value : 0.0;
}

// --- typing --------------------------------------------------------------------------------------------

bool ValueInputModel::isStartChar(char c) {
  return std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '+' || c == '(';
}

bool ValueInputModel::typeText(const std::string& chars) {
  if (active_ < 0 || chars.empty()) return false;
  ValueField& f = fields_[static_cast<size_t>(active_)];
  if (f.locked) {
    // typing into a locked box replaces its value
    f.locked = false;
    f.text.clear();
  }
  if (f.text.empty() && !isStartChar(chars[0])) return false;
  f.text += chars;
  f.valid = true;
  f.error.clear();
  notify();
  return true;
}

bool ValueInputModel::backspace() {
  if (active_ < 0) return false;
  ValueField& f = fields_[static_cast<size_t>(active_)];
  if (f.locked) {
    f.locked = false;  // Backspace on a locked value starts editing it again (keeps the text)
    if (!f.text.empty()) f.text.pop_back();
    notify();
    return true;
  }
  if (f.text.empty()) return false;
  f.text.pop_back();
  f.valid = true;
  f.error.clear();
  notify();
  return true;
}

void ValueInputModel::setText(int index, const std::string& text) {
  if (index < 0 || index >= fieldCount()) return;
  ValueField& f = fields_[static_cast<size_t>(index)];
  if (f.text == text && !f.locked) return;
  f.text = text;
  f.locked = false;
  f.valid = true;
  f.error.clear();
  notify();
}

bool ValueInputModel::evaluate(ValueField& f, std::string* error) {
  std::string err;
  std::optional<double> v = evaluator_ ? evaluator_(f.text, f.kind, &err) : plainNumber(f.text, f.kind, &err);
  if (v && f.kind == ValueKind::Count) {
    double r = std::round(*v);
    if (std::abs(*v - r) > 1e-9 || r < 1) {
      v.reset();
      err = "expected a whole number";
    } else {
      v = r;
    }
  }
  if (!v) {
    f.valid = false;
    f.error = err;
    if (error) *error = err;
    notify();
    return false;
  }
  f.value = *v;
  f.valid = true;
  f.error.clear();
  f.locked = true;
  notify();
  return true;
}

bool ValueInputModel::lockActive(std::string* error) {
  if (active_ < 0) return false;
  ValueField& f = fields_[static_cast<size_t>(active_)];
  if (f.locked) return true;
  if (trimmed(f.text).empty()) {
    if (error) *error = "empty value";
    return false;
  }
  return evaluate(f, error);
}

bool ValueInputModel::lock(const std::string& id, const std::string& text, std::string* error) {
  ValueField* f = fieldPtr(id);
  if (!f) {
    if (error) *error = "unknown field " + id;
    return false;
  }
  f->text = trimmed(text);
  f->locked = false;
  if (f->text.empty()) {
    if (error) *error = "empty value";
    return false;
  }
  return evaluate(*f, error);
}

void ValueInputModel::lockValue(const std::string& id, double value, const std::string& text) {
  ValueField* f = fieldPtr(id);
  if (!f) return;
  f->value = value;
  f->text = text;
  f->locked = true;
  f->valid = true;
  f->error.clear();
  notify();
}

void ValueInputModel::unlock(int index) {
  if (index < 0 || index >= fieldCount()) return;
  ValueField& f = fields_[static_cast<size_t>(index)];
  if (!f.locked && f.text.empty()) return;
  f.locked = false;
  f.text.clear();
  notify();
}

void ValueInputModel::unlockAll() {
  bool changed = false;
  for (ValueField& f : fields_) {
    if (f.locked || !f.text.empty()) changed = true;
    f.locked = false;
    f.text.clear();
    f.valid = true;
    f.error.clear();
  }
  if (changed) notify();
}

void ValueInputModel::clearActive() {
  if (active_ < 0) return;
  ValueField& f = fields_[static_cast<size_t>(active_)];
  if (f.text.empty() && !f.locked) return;
  f.text.clear();
  f.locked = false;
  f.valid = true;
  f.error.clear();
  notify();
}

int ValueInputModel::nextVisible(int from, int step) const {
  const int n = fieldCount();
  if (n == 0) return -1;
  for (int k = 1; k <= n; ++k) {
    int i = ((from + step * k) % n + n) % n;
    if (fields_[static_cast<size_t>(i)].visible) return i;
  }
  return -1;
}

bool ValueInputModel::next(std::string* error) {
  if (active_ < 0) return false;
  const ValueField& f = fields_[static_cast<size_t>(active_)];
  if (!f.locked && !trimmed(f.text).empty() && !lockActive(error)) return false;
  int n = nextVisible(active_, 1);
  if (n >= 0 && n != active_) {
    active_ = n;
    notify();
  }
  return true;
}

bool ValueInputModel::previous(std::string* error) {
  if (active_ < 0) return false;
  const ValueField& f = fields_[static_cast<size_t>(active_)];
  if (!f.locked && !trimmed(f.text).empty() && !lockActive(error)) return false;
  int n = nextVisible(active_, -1);
  if (n >= 0 && n != active_) {
    active_ = n;
    notify();
  }
  return true;
}

bool ValueInputModel::separatorIsText(const std::string& text) const {
  // "max(1,2)": a comma inside an open parenthesis is part of the expression
  int depth = 0;
  for (char c : text) {
    if (c == '(') ++depth;
    else if (c == ')') --depth;
  }
  return depth > 0;
}

ValueInputModel::KeyResult ValueInputModel::keyPress(int key, int modifiers, const std::string& text) {
  if (fields_.empty() || active_ < 0) return KeyResult::Ignored;
  const bool combo = (modifiers & (core::ModCtrl | core::ModAlt)) != 0;
  switch (key) {
    case Qt::Key_Tab:
      if (combo) return KeyResult::Ignored;
      next();
      return KeyResult::Consumed;
    case Qt::Key_Backtab:
      if (combo) return KeyResult::Ignored;
      previous();
      return KeyResult::Consumed;
    case Qt::Key_Backspace:
      if (combo) return KeyResult::Ignored;
      return backspace() ? KeyResult::Consumed : KeyResult::Ignored;
    case Qt::Key_Escape: {
      if (isTyping()) {
        clearActive();
        return KeyResult::Consumed;
      }
      if (anyLocked()) {
        unlockAll();
        return KeyResult::Consumed;
      }
      return KeyResult::Ignored;
    }
    case Qt::Key_Return:
    case Qt::Key_Enter: {
      if (combo) return KeyResult::Ignored;
      if (isTyping()) {
        if (!lockActive()) return KeyResult::Consumed;  // invalid text: stay, the box shows the error
        return KeyResult::Commit;
      }
      return anyLocked() ? KeyResult::Commit : KeyResult::Ignored;
    }
    default: break;
  }
  if (combo || text.empty()) return KeyResult::Ignored;
  // printable characters
  std::string printable;
  for (char c : text)
    if (std::isprint(static_cast<unsigned char>(c)) || static_cast<unsigned char>(c) >= 0x80) printable += c;
  if (printable.empty()) return KeyResult::Ignored;
  const ValueField& f = fields_[static_cast<size_t>(active_)];
  if ((printable == "," || printable == "<") && !separatorIsText(f.text)) {
    if (f.locked || !f.text.empty()) {
      next();
      return KeyResult::Consumed;
    }
    return KeyResult::Ignored;
  }
  return typeText(printable) ? KeyResult::Consumed : KeyResult::Ignored;
}

// --- display -----------------------------------------------------------------------------------------------

std::string ValueInputModel::displayText(int index) const {
  if (index < 0 || index >= fieldCount()) return {};
  const ValueField& f = fields_[static_cast<size_t>(index)];
  if (!f.locked && !f.text.empty()) return f.text;
  if (f.locked && !f.text.empty()) return f.text;
  return formatter_ ? formatter_(f.value, f.kind) : plainFormat(f.value, f.kind);
}

std::string ValueInputModel::summary() const {
  std::string s;
  for (int i = 0; i < fieldCount(); ++i) {
    const ValueField& f = fields_[static_cast<size_t>(i)];
    if (!f.visible) continue;
    if (!s.empty()) s += "  ";
    std::string v = f.text.empty() ? (formatter_ ? formatter_(f.value, f.kind) : plainFormat(f.value, f.kind)) : f.text;
    if (f.locked) v = "[" + v + "]";
    else if (i == active_ && fieldCount() > 1) v += "_";
    s += v;
  }
  return s;
}

// --- commit ------------------------------------------------------------------------------------------------

std::vector<ValueField> ValueInputModel::lockedFields() const {
  std::vector<ValueField> out;
  for (const ValueField& f : fields_)
    if (f.locked) out.push_back(f);
  return out;
}

void ValueInputModel::resetInput() {
  bool changed = false;
  for (ValueField& f : fields_) {
    if (f.locked || !f.text.empty() || !f.valid) changed = true;
    f.locked = false;
    f.text.clear();
    f.valid = true;
    f.error.clear();
  }
  int first = nextVisible(-1, 1);
  if (first >= 0 && first != active_) {
    active_ = first;
    changed = true;
  }
  if (changed) notify();
}

void ValueInputModel::notify() {
  ++revision_;
  if (changed_) changed_();
}

}  // namespace fabricad::sketch
