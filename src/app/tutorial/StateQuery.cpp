// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/tutorial/StateQuery.h"

#include "app/CommandCard.h"
#include "app/MainWindow.h"
#include "app/ScriptTools.h"
#include "app/UiSettings.h"
#include "app/Selectors.h"
#include "core/Json.h"
#include "core/Selection.h"
#include "core/UndoStack.h"
#include "sketch/Sketch.h"
#include "sketch/SketchSession.h"
#include "viewer/ViewerWidget.h"

#include <algorithm>

#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>

namespace fabricad::app::tutorial {

QString StateQuery::activeTool() const {
  core::ToolManager& m = w_.toolManager();
  return m.isActive() ? core::qs(m.activeToolId()) : QString();
}

bool StateQuery::inSketch() const {
  sketch::SketchSession* s = sketch::SketchSession::current();
  return s && s->sketch();
}

int StateQuery::sketchDof() const {
  sketch::SketchSession* s = sketch::SketchSession::current();
  return s && s->sketch() ? s->sketch()->dofs() : -1;
}

int StateQuery::undoIndex() const { return w_.hasDocument() ? w_.document().undoStack().index() : -1; }

QString StateQuery::focusedValueBox(QString* field) const {
  QWidget* f = QApplication::focusWidget();
  for (QWidget* p = f; p; p = p->parentWidget())
    if (p->objectName() == QLatin1String("fcValueBox")) {
      if (field) *field = p->property("field").toString();
      if (auto* tag = p->findChild<QLabel*>("fcValueTag")) return tag->text();
      return "?";
    }
  return {};
}

bool StateQuery::check(const QString& expr, QString* message) const {
  if (!w_.hasDocument()) {
    if (message) *message = "no document";
    return false;
  }
  CheckContext ctx;
  ctx.tabs = w_.tabCount();
  ctx.activeCommand = activeTool().toStdString();
  ctx.sketchDof = sketchDof();
  const CheckResult r = evaluateCheck(w_.document(), ctx, core::splitCommandLine(expr.toStdString()));
  if (message) *message = core::qs(r.message);
  return r.ok;
}

bool StateQuery::eval(const Condition& c, const StepBaseline& base, const EngineFacts& facts, QString* why) const {
  switch (c.op) {
    case Condition::Op::All:
      for (const Condition& ch : c.children)
        if (!eval(ch, base, facts, why)) return false;
      return true;
    case Condition::Op::Any:
      for (const Condition& ch : c.children)
        if (eval(ch, base, facts, nullptr)) return true;
      if (why) *why = "none of " + c.describe();
      return false;
    case Condition::Op::Not:
      if (!c.children.empty() && eval(c.children.front(), base, facts, nullptr)) {
        if (why) *why = c.describe();
        return false;
      }
      return true;
    case Condition::Op::Leaf: break;
  }
  const bool ok = leaf(c, base, facts, why);
  if (!ok && why && why->isEmpty()) *why = "waiting for " + c.describe();
  return ok;
}

namespace {

bool compareInt(int have, const QString& op, int want) {
  if (op == "==") return have == want;
  if (op == "!=") return have != want;
  if (op == "<") return have < want;
  if (op == "<=") return have <= want;
  if (op == ">") return have > want;
  if (op == ">=") return have >= want;
  return false;
}

QWidget* visibleByName(const QString& name) {
  for (QWidget* top : QApplication::topLevelWidgets()) {
    if (!top->isVisible()) continue;
    if (top->objectName() == name) return top;
    for (QWidget* w : top->findChildren<QWidget*>(name))
      if (w->isVisible()) return w;
  }
  return nullptr;
}

}  // namespace

bool StateQuery::leaf(const Condition& c, const StepBaseline& base, const EngineFacts& facts, QString* why) const {
  const QString v = c.value.toString();
  const QString t = c.type;
  if (t == "next") return facts.nextPressed;
  if (t == "tour") return facts.stepId == v;
  if (t == "tool") {
    const QString have = activeTool();
    const bool ok = v == "none" ? have.isEmpty() : have.compare(v, Qt::CaseInsensitive) == 0;
    if (!ok && why) *why = "active command is " + (have.isEmpty() ? QString("none") : have);
    return ok;
  }
  if (t == "check") return check(v, why);
  if (t == "prompt") return w_.promptText().contains(v, Qt::CaseInsensitive);
  if (t == "valuebox") {
    QString field;
    const QString f = focusedValueBox(&field);
    // the field id ("width") or the caption shown in the box
    return v == "any" ? !f.isEmpty() : f.compare(v, Qt::CaseInsensitive) == 0 || (!field.isEmpty() && field.compare(v, Qt::CaseInsensitive) == 0);
  }
  if (t == "visible") return visibleByName(v) != nullptr;
  if (t == "dialog") {
    if (v == "card") return w_.commandCard() && w_.commandCard()->isVisible();
    if (v == "modal") return QApplication::activeModalWidget() != nullptr;
    if (v == "popup") return QApplication::activePopupWidget() != nullptr;
    if (QWidget* m = QApplication::activeModalWidget(); m && (m->objectName() == v || m->windowTitle().compare(v, Qt::CaseInsensitive) == 0))
      return true;
    return visibleByName(v) != nullptr;
  }
  if (t == "sketch") {
    if (v == "active") return inSketch();
    if (v == "none") return !inSketch();
    static const QRegularExpression re(R"(^dof\s*(==|<=|>=|<|>|!=)\s*(\d+)$)", QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(v.trimmed());
    const int dof = sketchDof();
    if (dof < 0) {
      if (why) *why = "no sketch is being edited";
      return false;
    }
    const bool ok = m.hasMatch() && compareInt(dof, m.captured(1), m.captured(2).toInt());
    if (!ok && why) *why = QString("sketch DOF is %1").arg(dof);
    return ok;
  }
  if (t == "workspace") {
    const QString want = v.toLower();
    const WorkspaceChoice have = w_.workspaceChoice();
    return (want == "part" && have == WorkspaceChoice::Part) || (want == "assembly" && have == WorkspaceChoice::Assembly) ||
           (want == "drawing" && have == WorkspaceChoice::Drawing);
  }
  if (t == "setting") {
    const QString have = UiSettings::find(v) ? UiSettings::instance().string(v) : QString();
    return have.compare(c.args.value("is").toVariant().toString(), Qt::CaseInsensitive) == 0;
  }
  if (t == "camera") return facts.cameraSeq != base.cameraSeq;
  if (t == "document") {
    if (v == "saved") {
      // the active design has a file and no unsaved changes (Ctrl+S done, or just opened)
      const bool saved = w_.hasDocument() && !w_.document().filePath().empty() && !w_.document().isModified();
      if (!saved && why) *why = "the design has unsaved changes";
      return saved;
    }
    const bool changed = base.document != (w_.hasDocument() ? static_cast<const void*>(&w_.document()) : nullptr) ||
                         undoIndex() != base.undoIndex;
    return v == "changed" ? changed : !changed;
  }
  if (t == "selection") {
    if (!w_.hasDocument()) return false;
    core::Document& doc = w_.document();
    std::vector<core::ShapeRef> items = doc.selection().items();
    // a running command's picked inputs count as its selection (the viewer highlights them)
    if (items.empty() && w_.viewer()) items = w_.viewer()->highlightedRefs();
    int n = 0;
    std::vector<core::ShapeRef> want;
    if (const QString sel = c.args.value("selector").toString(); !sel.isEmpty()) {
      SelectorResult r = resolveSelector(doc, sel.toStdString());
      if (!r.ok()) {
        if (why) *why = core::qs(r.error);
        return false;
      }
      want = r.refs;
    }
    for (const core::ShapeRef& r : items) {
      if (v != "any" && QString::fromLatin1(core::shapeKindName(r.kind)).compare(v, Qt::CaseInsensitive) != 0) continue;
      // same entity: B-rep refs from the selector carry history names a pick does not (and a pick
      // carries its hit point), so B-rep refs compare by body, kind and index
      if (!want.empty() && std::none_of(want.begin(), want.end(), [&](const core::ShapeRef& t) {
            return t.isBrep() && r.isBrep() ? t.owner == r.owner && t.kind == r.kind && t.index == r.index : t == r;
          }))
        continue;
      ++n;
    }
    const bool ok = c.args.contains("count") ? n == c.args.value("count").toInt() : n > 0;
    if (!ok && why) *why = QString("%1 %2 selected").arg(n).arg(v);
    return ok;
  }
  if (why) *why = "unknown condition " + t;
  return false;
}

}  // namespace fabricad::app::tutorial
