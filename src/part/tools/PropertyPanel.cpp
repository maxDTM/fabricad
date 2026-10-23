// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/tools/PropertyPanel.h"

#include "core/Document.h"
#include "core/Json.h"
#include "core/Body.h"
#include "core/Feature.h"
#include "core/Property.h"
#include "core/Units.h"
#include "part/PartFeature.h"
#include "theme/StatusGlyph.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>

namespace fabricad::part {

using core::qs;
using core::ss;

namespace {
/// Invalid text (PART-02): the theme QSS draws QLineEdit[invalid="true"] in status.error.
void markInvalid(QLineEdit* edit, bool invalid) {
  if (edit->property("invalid").toBool() == invalid) return;
  edit->setProperty("invalid", invalid);
  edit->style()->unpolish(edit);
  edit->style()->polish(edit);
}
}  // namespace

PropertyPanel::PropertyPanel(core::Document& doc, PartFeature& feature, QWidget* parent)
    : QWidget(parent), doc_(doc), feature_(feature) {
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(6, 6, 6, 6);
  form_ = new QFormLayout();
  form_->setLabelAlignment(Qt::AlignLeft);
  form_->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
  outer->addLayout(form_);
  message_ = new QLabel(this);
  message_->setWordWrap(true);
  message_->setObjectName("messageLabel");
  outer->addWidget(message_);
  // fits a docked card in the narrowest sidebar (260 - card padding; P5-TOGGLES-1)
  setMinimumWidth(220);
  debounce_.setSingleShot(true);
  connect(&debounce_, &QTimer::timeout, this, &PropertyPanel::flushPending);
  rebuild();
}

PropertyPanel::~PropertyPanel() {
  // the children outlive this object's members: a field losing the focus while the widget tree
  // is torn down must not reach the (destroyed) maps through its editingFinished handler
  for (QLineEdit* e : findChildren<QLineEdit*>()) e->disconnect(this);
}

void PropertyPanel::setValueChangeDelay(int ms) {
  delayMs_ = std::max(0, ms);
  debounce_.setInterval(delayMs_);
}

void PropertyPanel::flushPending() {
  debounce_.stop();
  if (pendingProp_.empty()) return;
  std::string name = pendingProp_;
  pendingProp_.clear();
  emit valueChanged(name);
}

QString PropertyPanel::refText(const core::Property& p) const {
  // selection chip (5.4): "1 PROFILE", "2 FACES"; mixed kinds: "3 SELECTED"
  std::vector<core::ShapeRef> refs;
  if (p.type() == core::PropertyType::Ref) {
    if (p.ref().isValid()) refs.push_back(p.ref());
  } else {
    refs = p.refs();
  }
  const int n = static_cast<int>(refs.size());
  if (n == 0) return tr("Select");
  core::ShapeKind kind = refs.front().kind;
  for (const auto& r : refs)
    if (r.kind != kind) kind = core::ShapeKind::None;
  const char* one = nullptr;
  const char* many = nullptr;
  switch (kind) {
    case core::ShapeKind::Profile: one = "profile"; many = "profiles"; break;
    case core::ShapeKind::Face: one = "face"; many = "faces"; break;
    case core::ShapeKind::Edge: one = "edge"; many = "edges"; break;
    case core::ShapeKind::Vertex: one = "vertex"; many = "vertices"; break;
    case core::ShapeKind::Body: one = "body"; many = "bodies"; break;
    case core::ShapeKind::Sketch: one = "sketch"; many = "sketches"; break;
    default: break;
  }
  if (!one) return n == 1 ? tr("1 selected") : tr("%n selected", nullptr, n);
  return QString("%1 %2").arg(n).arg(QString::fromLatin1(n == 1 ? one : many));
}

QString PropertyPanel::refSource(const core::Property& p) const {
  std::vector<core::ShapeRef> refs;
  if (p.type() == core::PropertyType::Ref) {
    if (p.ref().isValid()) refs.push_back(p.ref());
  } else {
    refs = p.refs();
  }
  QStringList names;
  for (const auto& r : refs) {
    std::string name;
    if (r.isBrep() || r.kind == core::ShapeKind::Body) {
      if (const core::Body* b = doc_.body(r.owner)) name = b->name;
    } else if (const core::Feature* f = doc_.feature(r.owner)) {
      name = f->name();
    }
    if (!name.empty() && !names.contains(qs(name))) names << qs(name);
  }
  return names.join(", ");
}

void PropertyPanel::updateExpression(const std::string& name) {
  auto it = exprLabels_.find(name);
  const core::Property* p = feature_.property(name);
  if (it == exprLabels_.end() || !it->second || !p) return;  // the label may be gone (panel teardown)
  // the `fx` line (5.4): the expression behind the value, in the value font
  const bool show = p->hasExpression() && !invalid_.count(name);
  if (show)
    it->second->setText(qs(std::format("fx  {} = {}", p->expression(),
                                       core::formatQuantity(core::Quantity{p->toDouble(), p->dimension()}, doc_.units()))));
  it->second->setVisible(show);
}

void PropertyPanel::retireFormRows(QFormLayout* form) {
  // Detach the rows without deleting their widgets synchronously. rebuild() runs from inside
  // the widgets' own signals (a QComboBox::currentIndexChanged handler ends up here), so the
  // emitting widget must stay alive until control returns to the event loop.
  std::function<void(QLayoutItem*)> retire = [&](QLayoutItem* item) {
    if (!item) return;
    if (QWidget* w = item->widget()) {
      w->hide();
      w->setParent(nullptr);  // findChild() must not see retired rows
      w->deleteLater();
    } else if (QLayout* l = item->layout()) {
      while (QLayoutItem* child = l->takeAt(0)) retire(child);
    }
    delete item;
  };
  while (form->rowCount() > 0) {
    QFormLayout::TakeRowResult row = form->takeRow(0);
    retire(row.labelItem);
    retire(row.fieldItem);
  }
}

std::vector<std::string> PropertyPanel::visibleRows() const {
  std::vector<std::string> out;
  for (const auto& p : feature_.properties()) {
    if (p.flags().hidden || p.flags().output) continue;
    if (p.type() == core::PropertyType::Json) continue;
    out.push_back(p.name());
  }
  return out;
}

bool PropertyPanel::needsRebuild() const { return visibleRows() != rows_; }

std::string PropertyPanel::invalidMessage() const {
  // first invalid field in dialog order
  for (const auto& name : rows_) {
    auto it = invalid_.find(name);
    if (it == invalid_.end()) continue;
    const core::Property* p = feature_.property(name);
    return std::format("{}: {}", p ? p->label() : name, it->second.error);
  }
  return invalid_.empty() ? std::string() : invalid_.begin()->second.error;
}

void PropertyPanel::setInvalid(const std::string& name, const std::string& text, const std::string& error) {
  auto it = invalid_.find(name);
  bool changed = it == invalid_.end() || it->second.error != error;
  invalid_[name] = {text, error};
  if (changed) emit validityChanged();
}

void PropertyPanel::clearInvalid(const std::string& name) {
  if (invalid_.erase(name)) emit validityChanged();
}

void PropertyPanel::rebuild() {
  rebuilding_ = true;
  retireFormRows(form_);
  refButtons_.clear();
  numericEdits_.clear();
  enumCombos_.clear();
  boolChecks_.clear();
  refSources_.clear();
  exprLabels_.clear();
  trailSlots_.clear();
  rows_ = visibleRows();
  for (const auto& name : rows_)
    if (core::Property* p = feature_.property(name)) addRow(*p);
  // invalid text survives a rebuild in rows that still exist; hidden rows can't block OK
  bool dropped = false;
  for (auto it = invalid_.begin(); it != invalid_.end();) {
    auto e = numericEdits_.find(it->first);
    if (e == numericEdits_.end()) {
      it = invalid_.erase(it);
      dropped = true;
      continue;
    }
    e->second->setText(qs(it->second.text));
    markInvalid(e->second, true);
    e->second->setToolTip(qs(it->second.error));
    ++it;
  }
  if (!active_.empty() && !refButtons_.count(active_)) active_.clear();
  if (active_.empty() && !refButtons_.empty()) {
    // first empty ref input, else the first
    for (auto& prop : feature_.properties()) {
      if (!refButtons_.count(prop.name())) continue;
      bool empty = prop.type() == core::PropertyType::Ref ? !prop.ref().isValid() : prop.refs().empty();
      if (empty) {
        active_ = prop.name();
        break;
      }
    }
    if (active_.empty()) active_ = refButtons_.begin()->first;
  }
  for (auto& [name, btn] : refButtons_) btn->setChecked(name == active_);
  rebuilding_ = false;
  if (dropped) emit validityChanged();
}

void PropertyPanel::refresh() {
  for (auto& [name, btn] : refButtons_) {
    if (const core::Property* p = feature_.property(name)) {
      btn->setText(refText(*p));
      if (QLabel* src = refSources_.count(name) ? refSources_[name].data() : nullptr) {
        src->setText(refSource(*p));
        src->setVisible(!src->text().isEmpty());
      }
    }
    btn->setChecked(name == active_);
  }
  for (auto& [name, lbl] : exprLabels_) updateExpression(name);
  for (auto& [name, edit] : numericEdits_) {
    if (invalid_.count(name)) continue;  // keep the user's (invalid) text until it is fixed
    if (const core::Property* p = feature_.property(name); p && !edit->hasFocus()) edit->setText(qs(p->displayText(doc_.units())));
  }
  for (auto& [name, combo] : enumCombos_) {
    const core::Property* p = feature_.property(name);
    if (!p || combo->currentIndex() == p->enumIndex()) continue;
    QSignalBlocker block(combo);
    combo->setCurrentIndex(p->enumIndex());
  }
  for (auto& [name, box] : boolChecks_) {
    const core::Property* p = feature_.property(name);
    if (!p || box->isChecked() == p->toBool()) continue;
    QSignalBlocker block(box);
    box->setChecked(p->toBool());
  }
}

void PropertyPanel::setActiveInput(const std::string& propName) {
  if (propName == active_) return;
  active_ = propName;
  for (auto& [name, btn] : refButtons_) btn->setChecked(name == active_);
  emit activeInputChanged(active_);
}

void PropertyPanel::setLiveField(const std::string& propName) {
  if (propName == live_) return;
  live_ = propName;
  for (auto& [name, edit] : numericEdits_) {
    const bool on = name == live_;
    if (edit->property("live").toBool() == on) continue;
    edit->setProperty("live", on);
    edit->style()->unpolish(edit);
    edit->style()->polish(edit);
  }
}

void PropertyPanel::setMessage(const std::string& text, bool error) {
  message_->setText(qs(text));
  // colour from the theme QSS (QLabel[messageLevel=...]) and the shape glyph: an error is never
  // colour alone (decision 9, P5-CVD-1)
  const char* level = text.empty() ? "" : error ? "error" : "warning";
  if (message_->property("messageLevel").toString() != QLatin1StringView(level)) {
    message_->setProperty("messageLevel", level);
    message_->style()->unpolish(message_);
    message_->style()->polish(message_);
  }
  theme::setStatusGlyph(message_, text.empty() ? theme::StatusLevel::None
                                  : error      ? theme::StatusLevel::Error
                                               : theme::StatusLevel::Warning);
  message_->setVisible(!text.empty());
}

void PropertyPanel::applyNumericText(core::Property& p, QLineEdit* edit, bool immediate) {
  std::string text = ss(edit->text().trimmed());
  if (text.empty()) {
    // an emptied field is not an expression; editingFinished restores the value
    markInvalid(edit, false);
    edit->setToolTip(qs(p.tooltip()));
    clearInvalid(p.name());
    return;
  }
  core::EvalResult r = doc_.evaluate(text, p.dimension());
  if (!r.ok) {
    markInvalid(edit, true);
    edit->setToolTip(qs(r.error));
    setInvalid(p.name(), text, r.error.empty() ? std::string("invalid expression") : r.error);
    return;
  }
  clearInvalid(p.name());
  markInvalid(edit, false);
  edit->setToolTip(qs(p.tooltip()));
  // plain numbers are stored as values, anything else as an expression
  bool plain = true;
  for (char c : text)
    if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '+' || c == 'e' || c == 'E')) plain = false;
  if (plain) p.setDouble(r.value.value);
  else {
    p.setExpression(text);
    p.setEvaluatedValue(r.value.value);
  }
  if (!immediate && delayMs_ > 0) {
    // the value is already in the property; the (expensive) preview waits until typing stops
    pendingProp_ = p.name();
    debounce_.start();
    return;
  }
  pendingProp_.clear();
  debounce_.stop();
  emit valueChanged(p.name());
}

void PropertyPanel::addRow(core::Property& p) {
  const QString label = qs(p.label());
  const std::string name = p.name();
  switch (p.type()) {
    case core::PropertyType::Length:
    case core::PropertyType::Angle:
    case core::PropertyType::Double: {
      // value field (5.4): the edit with room for a trailing action (Flip), and the `fx` line
      auto* cell = new QWidget(this);
      cell->setObjectName(qs("cell_" + name));
      auto* cv = new QVBoxLayout(cell);
      cv->setContentsMargins(0, 0, 0, 0);
      cv->setSpacing(2);
      auto* line = new QHBoxLayout();
      line->setContentsMargins(0, 0, 0, 0);
      line->setSpacing(0);
      cv->addLayout(line);
      auto* edit = new QLineEdit(qs(p.displayText(doc_.units())), cell);
      edit->setToolTip(qs(p.tooltip()));
      edit->setObjectName(qs("prop_" + name));
      edit->setProperty("live", name == live_);
      line->addWidget(edit, 1);
      trailSlots_[name] = line;
      auto* fx = new QLabel(cell);
      fx->setObjectName("PropertyExpression");
      fx->setProperty("textStyle", "value");
      fx->hide();
      cv->addWidget(fx);
      exprLabels_[name] = fx;
      numericEdits_[name] = edit;
      connect(edit, &QLineEdit::textEdited, this, [this, name, edit] {
        if (auto* prop = feature_.property(name)) applyNumericText(*prop, edit, false);
      });
      connect(edit, &QLineEdit::editingFinished, this, [this, name, edit] {
        if (auto* prop = feature_.property(name)) {
          applyNumericText(*prop, edit, true);
          // PART-02: invalid text stays (red) instead of silently snapping back to the last value
          if (!invalid_.count(name)) edit->setText(qs(prop->displayText(doc_.units())));
          updateExpression(name);
        }
      });
      form_->addRow(label, cell);
      updateExpression(name);
      break;
    }
    case core::PropertyType::Int: {
      auto* spin = new QSpinBox(this);
      spin->setRange(p.flags().allowNegative ? p.flags().intMin : std::max(0, p.flags().intMin), p.flags().intMax);
      // Apply on Enter / focus-out / arrows only: typing "1000" must not recompute at 1, 10, 100.
      spin->setKeyboardTracking(false);
      spin->setValue(static_cast<int>(p.toInt()));
      spin->setObjectName(qs("prop_" + name));
      connect(spin, &QSpinBox::valueChanged, this, [this, name](int v) {
        if (auto* prop = feature_.property(name)) {
          prop->setInt(v);
          emit valueChanged(name);
        }
      });
      form_->addRow(label, spin);
      break;
    }
    case core::PropertyType::Bool: {
      auto* box = new QCheckBox(this);
      box->setChecked(p.toBool());
      box->setObjectName(qs("prop_" + name));
      boolChecks_[name] = box;
      connect(box, &QCheckBox::toggled, this, [this, name](bool v) {
        if (auto* prop = feature_.property(name)) {
          prop->setBool(v);
          emit valueChanged(name);
        }
      });
      form_->addRow(label, box);
      // Flip: the value field's trailing ⇄ action (5.4) instead of a row of its own; the
      // check box stays (hidden) as the model, so scripts and tests drive it as before
      const std::string primary = feature_.primaryNumeric();
      if (name == "flip" && trailSlots_.count(primary)) {
        auto* flip = new QToolButton(this);
        flip->setObjectName("prop_flip_toggle");
        flip->setCheckable(true);
        flip->setChecked(p.toBool());
        flip->setText(QString::fromUtf8("\u21C4"));
        flip->setToolTip(label);
        flip->setFocusPolicy(Qt::TabFocus);
        trailSlots_[primary]->addWidget(flip);
        connect(flip, &QToolButton::toggled, box, [box](bool v) {
          if (box->isChecked() != v) box->setChecked(v);
        });
        connect(box, &QCheckBox::toggled, flip, [flip](bool v) {
          if (flip->isChecked() != v) flip->setChecked(v);
        });
        form_->setRowVisible(box, false);
      }
      break;
    }
    case core::PropertyType::Enum: {
      auto* combo = new QComboBox(this);
      for (const auto& o : p.options()) combo->addItem(qs(o));
      combo->setCurrentIndex(p.enumIndex());
      combo->setObjectName(qs("prop_" + name));
      enumCombos_[name] = combo;
      connect(combo, &QComboBox::currentIndexChanged, this, [this, name](int idx) {
        if (rebuilding_) return;
        if (auto* prop = feature_.property(name)) {
          prop->setEnum(idx);
          emit valueChanged(name);
        }
      });
      form_->addRow(label, combo);
      break;
    }
    case core::PropertyType::String: {
      auto* edit = new QLineEdit(qs(p.toString()), this);
      edit->setObjectName(qs("prop_" + name));
      connect(edit, &QLineEdit::editingFinished, this, [this, name, edit] {
        if (auto* prop = feature_.property(name)) {
          prop->setString(ss(edit->text()));
          emit valueChanged(name);
        }
      });
      form_->addRow(label, edit);
      break;
    }
    case core::PropertyType::Ref:
    case core::PropertyType::RefList: {
      auto* row = new QWidget(this);
      auto* h = new QHBoxLayout(row);
      h->setContentsMargins(0, 0, 0, 0);
      auto* btn = new QPushButton(refText(p), row);
      btn->setCheckable(true);
      btn->setToolTip(qs(p.tooltip()));
      btn->setObjectName(qs("prop_" + name));
      btn->setProperty("selectionInput", true);  // styled by the theme QSS (p0-check 5)
      auto* clear = new QToolButton(row);
      clear->setText(QString::fromUtf8("\xC3\x97"));  // multiplication sign as "x"
      clear->setToolTip(tr("Clear selection"));
      clear->setAutoRaise(true);
      // chip + sub-label (5.4): "■ 1 PROFILE ×  Sketch3"
      auto* src = new QLabel(refSource(p), row);
      src->setObjectName("SelectionSource");
      src->setProperty("textStyle", "value");  // names as typed (decision 3)
      src->setVisible(!src->text().isEmpty());
      h->addWidget(btn, 1);
      h->addWidget(clear);
      h->addWidget(src);
      refSources_[name] = src;
      refButtons_[name] = btn;
      connect(btn, &QPushButton::clicked, this, [this, name] { setActiveInput(name); });
      connect(clear, &QToolButton::clicked, this, [this, name] {
        if (auto* prop = feature_.property(name)) {
          if (prop->type() == core::PropertyType::Ref) prop->setRef(core::ShapeRef());
          else prop->setRefs({});
          setActiveInput(name);
          refresh();
          emit refsCleared(name);
          emit valueChanged(name);
        }
      });
      form_->addRow(label, row);
      break;
    }
    default: break;
  }
}

}  // namespace fabricad::part
