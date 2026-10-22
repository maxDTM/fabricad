// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/tools/PropertyPanel.h"

#include "core/Document.h"
#include "core/Json.h"
#include "core/Property.h"
#include "part/PartFeature.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace fabricad::part {

using core::qs;
using core::ss;

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
  setMinimumWidth(280);
  debounce_.setSingleShot(true);
  connect(&debounce_, &QTimer::timeout, this, &PropertyPanel::flushPending);
  rebuild();
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
  if (p.type() == core::PropertyType::Ref) return p.ref().isValid() ? tr("1 selected") : tr("Select");
  int n = static_cast<int>(p.refs().size());
  return n == 0 ? tr("Select") : tr("%n selected", nullptr, n);
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

void PropertyPanel::rebuild() {
  rebuilding_ = true;
  retireFormRows(form_);
  refButtons_.clear();
  numericEdits_.clear();
  enumCombos_.clear();
  boolChecks_.clear();
  for (auto& p : feature_.properties()) {
    if (p.flags().hidden || p.flags().output) continue;
    if (p.type() == core::PropertyType::Json) continue;
    addRow(p);
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
}

void PropertyPanel::refresh() {
  for (auto& [name, btn] : refButtons_) {
    if (const core::Property* p = feature_.property(name)) btn->setText(refText(*p));
    btn->setChecked(name == active_);
  }
  for (auto& [name, edit] : numericEdits_) {
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

void PropertyPanel::setMessage(const std::string& text, bool error) {
  message_->setText(qs(text));
  message_->setStyleSheet(error ? "color: #D93025;" : "color: #6B7079;");
  message_->setVisible(!text.empty());
}

void PropertyPanel::applyNumericText(core::Property& p, QLineEdit* edit, bool immediate) {
  std::string text = ss(edit->text().trimmed());
  if (text.empty()) return;
  core::EvalResult r = doc_.evaluate(text, p.dimension());
  if (!r.ok) {
    edit->setStyleSheet("border: 1px solid #D93025;");
    edit->setToolTip(qs(r.error));
    return;
  }
  edit->setStyleSheet({});
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
      auto* edit = new QLineEdit(qs(p.displayText(doc_.units())), this);
      edit->setToolTip(qs(p.tooltip()));
      edit->setObjectName(qs("prop_" + name));
      numericEdits_[name] = edit;
      connect(edit, &QLineEdit::textEdited, this, [this, name, edit] {
        if (auto* prop = feature_.property(name)) applyNumericText(*prop, edit, false);
      });
      connect(edit, &QLineEdit::editingFinished, this, [this, name, edit] {
        if (auto* prop = feature_.property(name)) {
          applyNumericText(*prop, edit, true);
          edit->setText(qs(prop->displayText(doc_.units())));
        }
      });
      form_->addRow(label, edit);
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
      btn->setStyleSheet("QPushButton:checked { background: #0B6CFF; color: white; }");
      auto* clear = new QToolButton(row);
      clear->setText(QString::fromUtf8("\xC3\x97"));  // multiplication sign as "x"
      clear->setToolTip(tr("Clear selection"));
      clear->setAutoRaise(true);
      h->addWidget(btn, 1);
      h->addWidget(clear);
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
