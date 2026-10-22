// FabriCAD app: generic property dialog + edit tool.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/PropertyEditor.h"

#include "core/AppHost.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Feature.h"
#include "core/Json.h"
#include "core/Selection.h"
#include "core/ViewerContext.h"
#include "part/tools/PropertyPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QStyle>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <format>

namespace fabricad::app {

using core::Property;
using core::PropertyType;

namespace {

bool isPlainNumber(const std::string& s) {
  if (s.empty()) return false;
  char* end = nullptr;
  std::strtod(s.c_str(), &end);
  while (end && *end == ' ') ++end;
  return end && *end == '\0';
}

void setInvalidLook(QWidget* w, bool invalid, const QString& tip) {
  w->setProperty("invalid", invalid);
  w->setToolTip(tip);
  w->style()->unpolish(w);
  w->style()->polish(w);
}

}  // namespace

bool setNumericProperty(const core::Document& doc, Property& p, const std::string& text, std::string* error) {
  if (!p.isNumeric()) {
    if (error) *error = "not a numeric property";
    return false;
  }
  core::EvalResult r = doc.evaluate(text, p.dimension());
  if (!r.ok) {
    if (error) *error = r.error.empty() ? "invalid expression" : r.error;
    return false;
  }
  if (r.value.dim != p.dimension() && !r.value.dim.isScalar()) {
    if (error) *error = "wrong unit: expected " + p.dimension().toString();
    return false;
  }
  if (!p.flags().allowNegative && r.value.value < 0) {
    if (error) *error = "value must not be negative";
    return false;
  }
  if (isPlainNumber(text)) {
    p.setDouble(r.value.value);
  } else {
    p.setExpression(text);
    p.setEvaluatedValue(r.value.value);
  }
  return true;
}

// =========================================================================================
// PropertyEditor
// =========================================================================================

PropertyEditor::PropertyEditor(core::Document& doc, core::Feature& feature, QWidget* parent)
    : QWidget(parent), doc_(doc), feature_(feature) {
  setObjectName("PropertyEditor");
  build();
}

void PropertyEditor::build() {
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(8, 6, 8, 6);
  form_ = new QFormLayout();
  form_->setLabelAlignment(Qt::AlignLeft);
  form_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  outer->addLayout(form_);
  message_ = new QLabel(this);
  message_->setObjectName("CommandDialogMessage");
  message_->setWordWrap(true);
  message_->hide();
  outer->addWidget(message_);
  rebuild();
}

void PropertyEditor::rebuild() {
  updating_ = true;
  // Rows are retired with deleteLater: rebuild() may run from a row widget's own signal.
  part::PropertyPanel::retireFormRows(form_);
  numericEdits_.clear();
  refButtons_.clear();
  invalid_.clear();
  auto* nameEdit = new QLineEdit(core::qs(feature_.name()), this);
  nameEdit->setObjectName("prop_name");
  connect(nameEdit, &QLineEdit::editingFinished, this, [this, nameEdit] {
    const std::string n = nameEdit->text().trimmed().toStdString();
    if (!n.empty() && n != feature_.name()) {
      feature_.setName(n);
      emit propertyEdited("name");
    }
  });
  form_->addRow("Name", nameEdit);
  feature_.updatePropertyFlags();
  for (Property& p : feature_.properties())
    if (!p.flags().hidden && !p.flags().output) addRow(p);
  for (auto& [name, b] : refButtons_) b->setChecked(name == activeRef_);
  updating_ = false;
  updateValidity();
}

void PropertyEditor::scheduleRebuild() {
  if (rebuildPending_) return;
  rebuildPending_ = true;
  QTimer::singleShot(0, this, [this] {
    rebuildPending_ = false;
    rebuild();
  });
}

QString PropertyEditor::refText(const Property& p) const {
  const size_t n = p.type() == PropertyType::Ref ? (p.ref().isValid() ? 1 : 0) : p.refs().size();
  if (n == 0) return "Select";
  return QString("%1 selected").arg(n);
}

void PropertyEditor::addRow(Property& p) {
  const QString label = core::qs(p.label());
  const std::string name = p.name();
  QWidget* field = nullptr;
  switch (p.type()) {
    case PropertyType::Length:
    case PropertyType::Angle:
    case PropertyType::Double: {
      auto* e = new QLineEdit(core::qs(p.displayText(doc_.units())), this);
      numericEdits_[name] = e;
      connect(e, &QLineEdit::editingFinished, this, [this, name, e] {
        if (updating_) return;
        if (Property* prop = feature_.property(name); prop && applyNumeric(*prop, e)) emit propertyEdited(name);
      });
      connect(e, &QLineEdit::textEdited, this, [this, name, e] {
        // live validation while typing (value is applied on editingFinished)
        Property* prop = feature_.property(name);
        if (!prop) return;
        core::EvalResult r = doc_.evaluate(e->text().toStdString(), prop->dimension());
        invalid_[name] = !r.ok;
        setInvalidLook(e, !r.ok, r.ok ? QString() : core::qs(r.error));
        updateValidity();
      });
      field = e;
      break;
    }
    case PropertyType::Int: {
      auto* s = new QSpinBox(this);
      s->setRange(p.flags().allowNegative ? p.flags().intMin : std::max(0, p.flags().intMin), p.flags().intMax);
      s->setKeyboardTracking(false);
      s->setValue(static_cast<int>(p.toInt()));
      connect(s, &QSpinBox::valueChanged, this, [this, name](int v) {
        if (updating_) return;
        if (Property* prop = feature_.property(name)) {
          prop->setInt(v);
          emit propertyEdited(name);
        }
      });
      field = s;
      break;
    }
    case PropertyType::Bool: {
      auto* c = new QCheckBox(this);
      c->setChecked(p.toBool());
      connect(c, &QCheckBox::toggled, this, [this, name](bool v) {
        if (updating_) return;
        if (Property* prop = feature_.property(name)) {
          prop->setBool(v);
          if (flagsChangeAfter(name)) scheduleRebuild();
          emit propertyEdited(name);
        }
      });
      field = c;
      break;
    }
    case PropertyType::Enum: {
      auto* c = new QComboBox(this);
      for (const std::string& o : p.options()) c->addItem(core::qs(o));
      c->setCurrentIndex(p.enumIndex());
      connect(c, &QComboBox::currentIndexChanged, this, [this, name](int i) {
        if (updating_ || i < 0) return;
        if (Property* prop = feature_.property(name)) {
          prop->setEnum(i);
          // an enum choice usually hides/shows other rows: rebuild once the combo has finished
          // emitting (deleting it synchronously from here would be a use-after-free)
          flagsChangeAfter(name);
          scheduleRebuild();
          emit propertyEdited(name);
        }
      });
      field = c;
      break;
    }
    case PropertyType::String: {
      auto* e = new QLineEdit(core::qs(p.toString()), this);
      connect(e, &QLineEdit::editingFinished, this, [this, name, e] {
        if (updating_) return;
        Property* prop = feature_.property(name);
        if (prop && prop->toString() != e->text().toStdString()) {
          prop->setString(e->text().toStdString());
          emit propertyEdited(name);
        }
      });
      field = e;
      break;
    }
    case PropertyType::Vec3: {
      auto* w = new QWidget(this);
      auto* h = new QHBoxLayout(w);
      h->setContentsMargins(0, 0, 0, 0);
      const core::Vec3 v = p.toVec3();
      const double comps[3] = {v.x, v.y, v.z};
      for (int i = 0; i < 3; ++i) {
        auto* e = new QLineEdit(QString::number(comps[i]), w);
        e->setObjectName(QString("vec%1").arg(i));
        h->addWidget(e);
        connect(e, &QLineEdit::editingFinished, this, [this, name, e, i] {
          if (updating_) return;
          Property* prop = feature_.property(name);
          bool ok = false;
          const double d = e->text().toDouble(&ok);
          if (!prop || !ok) return;
          core::Vec3 nv = prop->toVec3();
          (i == 0 ? nv.x : i == 1 ? nv.y : nv.z) = d;
          prop->setVec3(nv);
          emit propertyEdited(name);
        });
      }
      field = w;
      break;
    }
    case PropertyType::Ref:
    case PropertyType::RefList: {
      auto* b = new QToolButton(this);
      b->setObjectName("SelectionInput");
      b->setCheckable(true);
      b->setToolButtonStyle(Qt::ToolButtonTextOnly);
      b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
      b->setText(refText(p));
      refButtons_[name] = b;
      connect(b, &QToolButton::clicked, this, [this, name] {
        setActiveRefInput(name);
        emit activeRefInputChanged(name);
      });
      field = b;
      break;
    }
    case PropertyType::Json: return;  // module specific, not editable generically
  }
  if (!field) return;
  field->setObjectName(core::qs("prop_" + name));
  if (!p.tooltip().empty()) field->setToolTip(core::qs(p.tooltip()));
  field->setEnabled(!p.flags().readOnly);
  form_->addRow(label, field);
}

bool PropertyEditor::flagsChangeAfter(const std::string& changed) {
  (void)changed;
  std::vector<std::pair<bool, bool>> before;
  for (const Property& p : feature_.properties()) before.emplace_back(p.flags().hidden, p.flags().readOnly);
  feature_.updatePropertyFlags();
  size_t i = 0;
  for (const Property& p : feature_.properties()) {
    if (before[i].first != p.flags().hidden || before[i].second != p.flags().readOnly) return true;
    ++i;
  }
  return false;
}

bool PropertyEditor::applyNumeric(Property& p, QLineEdit* edit) {
  std::string err;
  const std::string text = edit->text().trimmed().toStdString();
  if (text == p.displayText(doc_.units())) return false;
  const bool ok = setNumericProperty(doc_, p, text, &err);
  invalid_[p.name()] = !ok;
  setInvalidLook(edit, !ok, ok ? QString() : core::qs(err));
  updateValidity();
  return ok;
}

void PropertyEditor::updateValidity() {
  bool valid = true;
  for (const auto& [n, bad] : invalid_) valid = valid && !bad;
  setMessage(valid ? QString() : QString("Invalid value"), !valid);
  emit validityChanged(valid);
}

void PropertyEditor::refresh() {
  updating_ = true;
  for (auto& [name, e] : numericEdits_)
    if (const Property* p = feature_.property(name); p && !e->hasFocus()) e->setText(core::qs(p->displayText(doc_.units())));
  for (auto& [name, b] : refButtons_)
    if (const Property* p = feature_.property(name)) {
      b->setText(refText(*p));
      b->setChecked(name == activeRef_);
    }
  updating_ = false;
}

void PropertyEditor::setActiveRefInput(const std::string& prop) {
  activeRef_ = prop;
  for (auto& [name, b] : refButtons_) b->setChecked(name == prop);
}

void PropertyEditor::setMessage(const QString& text, bool error) {
  if (!message_) return;
  message_->setVisible(!text.isEmpty());
  message_->setText(text);
  message_->setProperty("error", error);
  message_->style()->unpolish(message_);
  message_->style()->polish(message_);
}

// =========================================================================================
// EditPropertiesTool
// =========================================================================================

namespace {
core::ToolInfo editInfo() {
  core::ToolInfo i;
  i.id = "app.edit-properties";
  i.name = "Edit Properties";
  i.tooltip = "Edit the inputs of a timeline feature";
  i.icon = "parameters";
  i.aliases = {"PROPERTIES", "EDITPROPS"};
  i.repeatable = false;
  i.hidden = true;
  return i;
}
}  // namespace

EditPropertiesTool::EditPropertiesTool() : ToolBase(editInfo()) {}

core::Feature* EditPropertiesTool::feature(core::ToolContext& ctx) const {
  return ctx.document ? ctx.document->feature(id_) : nullptr;
}

void EditPropertiesTool::activate(core::ToolContext& ctx) {
  id_ = core::kInvalidId;
  activeRef_.clear();
  editor_.clear();
  if (!ctx.args.empty()) {
    const std::string& a = ctx.args.size() >= 2 && ctx.args[0] == "edit" ? ctx.args[1] : ctx.args[0];
    std::from_chars(a.data(), a.data() + a.size(), id_);
  }
  if (id_ == core::kInvalidId && ctx.preselection.size() == 1 && ctx.preselection[0].kind == core::ShapeKind::Feature)
    id_ = ctx.preselection[0].owner;
  core::Feature* f = feature(ctx);
  if (!f) {
    ctx.showMessage("Select a feature to edit", true);
    ctx.cancel();
    return;
  }
  before_ = f->toJson();
  ctx.setPrompt(std::format("Edit {}", f->name()));
}

void EditPropertiesTool::deactivate(core::ToolContext& ctx) {
  if (ctx.selection) ctx.selection->setFilter(core::kAllKinds);
  editor_.clear();
}

QWidget* EditPropertiesTool::createDialog(core::ToolContext& ctx, QWidget* parent) {
  core::Feature* f = feature(ctx);
  if (!f || !ctx.document) return nullptr;
  auto* ed = new PropertyEditor(*ctx.document, *f, parent);
  editor_ = ed;
  core::ToolContext* c = &ctx;
  QObject::connect(ed, &PropertyEditor::propertyEdited, ed, [this, c](const std::string&) { previewNow(*c); });
  QObject::connect(ed, &PropertyEditor::activeRefInputChanged, ed,
                   [this, c](const std::string& p) { setActiveRefInput(*c, p); });
  QObject::connect(ed, &PropertyEditor::validityChanged, ed, [c](bool ok) {
    if (c->host) c->host->setToolAcceptEnabled(ok);
  });
  if (f->status() == core::FeatureStatus::Error || f->status() == core::FeatureStatus::Warning)
    ed->setMessage(core::qs(f->statusMessage()), f->status() == core::FeatureStatus::Error);
  return ed;
}

bool EditPropertiesTool::setNumeric(core::ToolContext& ctx, const std::string& prop, const std::string& text) {
  core::Feature* f = feature(ctx);
  core::Property* p = f ? f->property(prop) : nullptr;
  if (!p) return false;
  std::string err;
  if (!setNumericProperty(*ctx.document, *p, text, &err)) {
    ctx.showMessage(err, true);
    return false;
  }
  previewNow(ctx);
  return true;
}

void EditPropertiesTool::previewNow(core::ToolContext& ctx) {
  core::Feature* f = feature(ctx);
  if (!f) return;
  ctx.document->notifyFeatureChanged(id_);
  ctx.document->recompute();
  if (editor_) {
    editor_->refresh();
    if (f->status() == core::FeatureStatus::Error || f->status() == core::FeatureStatus::Warning)
      editor_->setMessage(core::qs(f->statusMessage()), f->status() == core::FeatureStatus::Error);
    else
      editor_->setMessage({}, false);
  }
}

void EditPropertiesTool::applyFilter(core::ToolContext& ctx) {
  core::Feature* f = feature(ctx);
  const core::Property* p = f && !activeRef_.empty() ? f->property(activeRef_) : nullptr;
  const core::ShapeKindMask m = p ? p->refFilter() : core::kAllKinds;
  if (ctx.selection) ctx.selection->setFilter(m);
  if (ctx.viewer) ctx.viewer->setSelectionFilter(m);
}

void EditPropertiesTool::setActiveRefInput(core::ToolContext& ctx, const std::string& prop) {
  core::Feature* f = feature(ctx);
  const core::Property* p = f ? f->property(prop) : nullptr;
  activeRef_ = p && (p->type() == PropertyType::Ref || p->type() == PropertyType::RefList) ? prop : std::string();
  applyFilter(ctx);
  if (editor_) editor_->setActiveRefInput(activeRef_);
  if (!p || !ctx.selection) return;
  // show the current refs as the selection so the user sees and edits them in place
  applyingSelection_ = true;
  if (p->type() == PropertyType::Ref) ctx.selection->set(p->ref().isValid() ? std::vector{p->ref()} : std::vector<core::ShapeRef>{});
  else ctx.selection->set(p->refs());
  applyingSelection_ = false;
  if (ctx.viewer) ctx.viewer->setHighlighted({});
  ctx.setPrompt(std::format("Select {}", p->label()));
}

void EditPropertiesTool::selectionChanged(core::ToolContext& ctx) {
  if (applyingSelection_ || activeRef_.empty()) return;
  core::Feature* f = feature(ctx);
  core::Property* p = f ? f->property(activeRef_) : nullptr;
  if (!p || !ctx.selection) return;
  std::vector<core::ShapeRef> items;
  for (const core::ShapeRef& r : ctx.selection->items())
    if (core::hasKind(p->refFilter(), r.kind)) items.push_back(r);
  if (p->type() == PropertyType::Ref) {
    p->setRef(items.empty() ? core::ShapeRef{} : items.back());
  } else {
    if (p->maxRefs() > 0 && static_cast<int>(items.size()) > p->maxRefs())
      items.erase(items.begin(), items.end() - p->maxRefs());
    p->setRefs(items);
  }
  previewNow(ctx);
}

bool EditPropertiesTool::commandLineInput(core::ToolContext& ctx, const std::string& text) {
  // "distance=20 mm" or a bare value for the first numeric property
  core::Feature* f = feature(ctx);
  if (!f) return false;
  std::string name, value = text;
  if (auto eq = text.find('='); eq != std::string::npos) {
    name = text.substr(0, eq);
    value = text.substr(eq + 1);
    while (!name.empty() && name.back() == ' ') name.pop_back();
  } else {
    for (const core::Property& p : f->properties())
      if (p.isNumeric() && !p.flags().hidden && !p.flags().readOnly) {
        name = p.name();
        break;
      }
  }
  if (name.empty()) return false;
  return setNumeric(ctx, name, value);
}

void EditPropertiesTool::accept(core::ToolContext& ctx) {
  core::Feature* f = feature(ctx);
  if (!f) {
    ctx.cancel();
    return;
  }
  QJsonObject after = f->toJson();
  if (after != before_ && ctx.undo)
    ctx.undo->push(std::make_unique<core::ModifyFeatureCommand>(*ctx.document, id_, before_, after, "Edit " + f->name()));
  ctx.finish();
}

void EditPropertiesTool::cancel(core::ToolContext& ctx) {
  if (core::Feature* f = feature(ctx); f && f->toJson() != before_) {
    f->fromJson(before_);
    ctx.document->notifyFeatureChanged(id_);
    ctx.document->recompute();
  }
  ctx.cancel();
}

}  // namespace fabricad::app
