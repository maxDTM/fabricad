// FabriCAD app: command box.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/CommandLine.h"

#include "app/Icons.h"
#include "core/Json.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QHBoxLayout>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMetaMethod>
#include <QPlainTextEdit>
#include <QStringListModel>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <set>

namespace fabricad::app {

namespace {
constexpr int kMaxHistory = 500;
}

CommandLine::CommandLine(core::ToolManager& manager, QWidget* parent) : QWidget(parent), manager_(&manager) {
  auto* h = new QHBoxLayout(this);
  h->setContentsMargins(0, 0, 0, 0);
  h->setSpacing(2);
  historyButton_ = new QToolButton(this);
  historyButton_->setIcon(resourceIcon("command-line"));
  historyButton_->setToolTip("Command history");
  historyButton_->setAutoRaise(true);
  historyButton_->setFocusPolicy(Qt::NoFocus);
  connect(historyButton_, &QToolButton::clicked, this, &CommandLine::toggleHistoryPane);
  h->addWidget(historyButton_);
  label_ = new QLabel("cmd>", this);
  label_->setObjectName("StatusDim");
  h->addWidget(label_);
  edit_ = new QLineEdit(this);
  edit_->setObjectName("CommandLine");
  edit_->setPlaceholderText("Type a command (LINE, EXTRUDE 10, @50<30)");
  edit_->installEventFilter(this);
  h->addWidget(edit_, 1);

  completions_ = new QStringListModel(this);
  completer_ = new QCompleter(completions_, this);
  completer_->setCaseSensitivity(Qt::CaseInsensitive);
  completer_->setCompletionMode(QCompleter::PopupCompletion);
  completer_->setFilterMode(Qt::MatchStartsWith);
  edit_->setCompleter(completer_);
  // Enter while the completion popup is open submits right away (the popup would otherwise
  // eat the first Enter). This filter is installed after QCompleter's, so it runs first.
  completer_->popup()->installEventFilter(this);

  pane_ = new QPlainTextEdit(nullptr);
  pane_->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
  pane_->setReadOnly(true);
  pane_->setObjectName("CommandHistory");
  pane_->setMaximumBlockCount(kMaxHistory);
  pane_->resize(520, 180);
  pane_->hide();
  connect(this, &QObject::destroyed, pane_, &QObject::deleteLater);
  connect(edit_, &QLineEdit::returnPressed, this, [this] {
    // completer popup visible: let it fill the text first
    if (completer_->popup() && completer_->popup()->isVisible()) return;
    submit(edit_->text());
  });
}

void CommandLine::refreshCompletions() {
  std::set<QString> words;
  for (core::ITool* t : manager_->registry().tools()) {
    const core::ToolInfo& i = t->info();
    if (i.hidden && i.aliases.empty()) continue;
    for (const std::string& a : i.aliases) words.insert(core::qs(a).toUpper());
    if (!i.hidden) words.insert(core::qs(i.id));
  }
  completions_->setStringList(QStringList(words.begin(), words.end()));
}

void CommandLine::appendHistory(const QString& line) {
  if (line.isEmpty()) return;
  lines_.append(line);
  while (lines_.size() > kMaxHistory) lines_.removeFirst();
  pane_->appendPlainText(line);
  edit_->setToolTip(lines_.mid(std::max<qsizetype>(0, lines_.size() - 8)).join('\n'));
}

void CommandLine::setPrompt(const QString& prompt) {
  edit_->setPlaceholderText(prompt.isEmpty() ? "Type a command (LINE, EXTRUDE 10, @50<30)" : prompt);
  if (!prompt.isEmpty() && (lines_.isEmpty() || lines_.back() != prompt)) appendHistory(prompt);
}

bool CommandLine::submit(const QString& text) {
  QString t = text.trimmed();
  // safety net for APP-07: the ':' shortcut character is never part of a command
  while (t.startsWith(':')) t = t.mid(1).trimmed();
  edit_->setText(QString());  // unlike clear(), resets the line edit's undo history (APP-08)
  typedPos_ = -1;
  swallowColon_ = false;
  if (t.isEmpty() && manager_->isActive()) {
    // APP-06: an empty Enter belongs to the active tool (accept / next step), not repeatLast()
    // which would restart a command and cancel the running one.
    if (isSignalConnected(QMetaMethod::fromSignal(&CommandLine::emptyEnter))) {
      emit emptyEnter();
      return true;
    }
    return false;
  }
  if (!t.isEmpty()) {
    typed_.removeAll(t);
    typed_.append(t);
    appendHistory("cmd> " + t);
  }
  return manager_->runCommandLine(t.toStdString());
}

void CommandLine::focusForTyping() {
  swallowColon_ = true;
  QTimer::singleShot(200, this, [this] { swallowColon_ = false; });
  edit_->setFocus(Qt::ShortcutFocusReason);
}

void CommandLine::toggleHistoryPane() {
  if (pane_->isVisible()) {
    pane_->hide();
    return;
  }
  // above the command box, kept inside the main window
  const QPoint p = edit_->mapToGlobal(QPoint(0, 0));
  const QRect win = window()->geometry();
  const int w = std::min(520, std::max(200, win.width() - 16));
  const int h = std::max(60, std::min(180, p.y() - 4 - win.top() - 8));
  pane_->resize(w, h);
  const int x = std::clamp(p.x(), win.left() + 8, std::max(win.left() + 8, win.right() - w - 8));
  pane_->move(x, p.y() - h - 4);
  pane_->show();
  pane_->raise();
  pane_->moveCursor(QTextCursor::End);
}

bool CommandLine::eventFilter(QObject* o, QEvent* e) {
  if (completer_ && o == completer_->popup() && e->type() == QEvent::KeyPress) {
    auto* k = static_cast<QKeyEvent*>(e);
    if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) {
      QAbstractItemView* popup = completer_->popup();
      const QModelIndex cur = popup->currentIndex();
      // the highlighted row, else the first match (unless the typed text is itself a command)
      QString text = edit_->text();
      if (cur.isValid()) text = cur.data(Qt::DisplayRole).toString();
      else if (!completions_->stringList().contains(text.trimmed(), Qt::CaseInsensitive) &&
               !completer_->currentCompletion().isEmpty())
        text = completer_->currentCompletion();
      popup->hide();
      submit(text);
      return true;
    }
  }
  if (o == edit_ && e->type() == QEvent::InputMethod && swallowColon_) {
    auto* im = static_cast<QInputMethodEvent*>(e);
    if (im->commitString() == ":") {
      swallowColon_ = false;
      return true;
    }
  }
  if (o == edit_ && e->type() == QEvent::ShortcutOverride) {
    // keep Ctrl+Z/Y in the box (QLineEdit accepts them anyway); handled on KeyPress below
    auto* k = static_cast<QKeyEvent*>(e);
    if (k->matches(QKeySequence::Undo) || k->matches(QKeySequence::Redo) ||
        (k->key() == Qt::Key_Y && k->modifiers() == Qt::ControlModifier)) {
      e->accept();
      return true;
    }
  }
  if (o == edit_ && e->type() == QEvent::KeyPress) {
    auto* k = static_cast<QKeyEvent*>(e);
    if (swallowColon_) {
      swallowColon_ = false;
      if (k->text() == ":") return true;
    }
    // APP-08: with nothing typed to undo, Ctrl+Z / Ctrl+Y undo / redo the model
    const bool undoKey = k->matches(QKeySequence::Undo);
    const bool redoKey = k->matches(QKeySequence::Redo) || (k->key() == Qt::Key_Y && k->modifiers() == Qt::ControlModifier);
    // same path as the key on the canvas (ToolManager shortcut dispatch)
    if (undoKey && !edit_->isUndoAvailable()) {
      manager_->triggerShortcut("Ctrl+Z");
      return true;
    }
    if (redoKey && !edit_->isRedoAvailable()) {
      manager_->triggerShortcut("Ctrl+Y");
      return true;
    }
    if (k->key() == Qt::Key_Escape) {
      edit_->setText(QString());
      if (manager_->isActive()) manager_->cancel();
      edit_->clearFocus();
      return true;
    }
    if (k->key() == Qt::Key_F2) {
      toggleHistoryPane();
      return true;
    }
    if ((k->key() == Qt::Key_Up || k->key() == Qt::Key_Down) && !typed_.isEmpty() &&
        !(completer_->popup() && completer_->popup()->isVisible())) {
      if (typedPos_ < 0) typedPos_ = static_cast<int>(typed_.size());
      typedPos_ += k->key() == Qt::Key_Up ? -1 : 1;
      typedPos_ = std::clamp(typedPos_, 0, static_cast<int>(typed_.size()));
      edit_->setText(typedPos_ < typed_.size() ? typed_[typedPos_] : QString());
      return true;
    }
  }
  return QWidget::eventFilter(o, e);
}

}  // namespace fabricad::app
