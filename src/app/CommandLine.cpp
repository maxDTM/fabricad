// FabriCAD app: command box.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/CommandLine.h"

#include "app/Icons.h"
#include "core/Json.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QFrame>
#include <QPointer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QStyle>
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
constexpr int kMaxHistory = 500;     // entries (an entry may have several lines)
constexpr int kMaxPaneBlocks = 5000;  // lines in the pane
constexpr int kPaneMaxWidth = 760;
constexpr int kPaneMaxHeight = 340;
}

CommandLine::CommandLine(core::ToolManager& manager, QWidget* parent) : QWidget(parent), manager_(&manager) {
  // status-bar cell (redesign 5.1): [> typed text            HISTORY ^]
  setObjectName("CommandLineBox");
  setAttribute(Qt::WA_StyledBackground);
  setFixedHeight(22);
  setMinimumWidth(280);
  setMaximumWidth(420);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  auto* h = new QHBoxLayout(this);
  h->setContentsMargins(0, 0, 0, 0);
  h->setSpacing(0);
  label_ = new QLabel(">", this);
  label_->setObjectName("CommandLinePrompt");
  h->addWidget(label_);
  edit_ = new QLineEdit(this);
  edit_->setObjectName("CommandLine");
  edit_->setPlaceholderText("Type a command (LINE, EXTRUDE 10, @50<30)");
  edit_->installEventFilter(this);
  h->addWidget(edit_, 1);
  historyButton_ = new QToolButton(this);
  historyButton_->setObjectName("CommandLineHistory");
  historyButton_->setText(QString::fromUtf8("History \u2191"));
  historyButton_->setToolTip("Command history (F2 in the box)");
  historyButton_->setAutoRaise(true);
  historyButton_->setFocusPolicy(Qt::NoFocus);
  connect(historyButton_, &QToolButton::clicked, this, &CommandLine::toggleHistoryPane);
  h->addWidget(historyButton_);

  completions_ = new QStringListModel(this);
  completer_ = new QCompleter(completions_, this);
  completer_->setCaseSensitivity(Qt::CaseInsensitive);
  completer_->setCompletionMode(QCompleter::PopupCompletion);
  completer_->setFilterMode(Qt::MatchStartsWith);
  edit_->setCompleter(completer_);
  // Enter while the completion popup is open submits right away (the popup would otherwise
  // eat the first Enter). This filter is installed after QCompleter's, so it runs first.
  completer_->popup()->installEventFilter(this);

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
  if (text_) {
    text_->appendPlainText(line);
    if (pane_ && pane_->isVisible()) text_->moveCursor(QTextCursor::End);
  }
  edit_->setToolTip(lines_.mid(std::max<qsizetype>(0, lines_.size() - 8)).join('\n'));
}

void CommandLine::setPrompt(const QString& prompt) {
  edit_->setPlaceholderText(prompt.isEmpty() ? "Type a command (LINE, EXTRUDE 10, @50<30)" : prompt);
  if (!prompt.isEmpty() && (lines_.isEmpty() || lines_.back() != prompt)) appendHistory(prompt);
}

void CommandLine::appendOutput(const QString& text, bool reveal) {
  if (text.isEmpty()) return;
  appendHistory(text);
  if (reveal || (submitting_ > 0 && text.contains('\n'))) showHistoryPane();
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
  ++submitting_;
  const bool ok = manager_->runCommandLine(t.toStdString());
  --submitting_;
  return ok;
}

void CommandLine::focusForTyping() {
  swallowColon_ = true;
  QTimer::singleShot(200, this, [this] { swallowColon_ = false; });
  edit_->setFocus(Qt::ShortcutFocusReason);
}

void CommandLine::ensurePane() {
  QWidget* host = window();
  if (pane_ && paneHost_ == host) return;
  if (!pane_) {
    // [COMMAND HISTORY            CLEAR  x]
    // cmd> list features
    // Features (3), marker after 3:
    //    1. feature:Sketch1 ...
    pane_ = new QFrame(host);
    pane_->setObjectName("CommandHistoryPanel");
    pane_->setAttribute(Qt::WA_StyledBackground);
    pane_->setFrameShape(QFrame::NoFrame);
    auto* v = new QVBoxLayout(pane_);
    v->setContentsMargins(1, 1, 1, 1);
    v->setSpacing(0);
    auto* header = new QWidget(pane_);
    header->setObjectName("CommandHistoryHeader");
    header->setAttribute(Qt::WA_StyledBackground);
    auto* h = new QHBoxLayout(header);
    h->setContentsMargins(8, 2, 2, 2);
    h->setSpacing(4);
    auto* title = new QLabel("Command history", header);
    title->setObjectName("CommandHistoryTitle");
    h->addWidget(title, 1);
    auto* clear = new QToolButton(header);
    clear->setObjectName("CommandHistoryClear");
    clear->setText("Clear");
    clear->setToolTip("Clear the command history");
    clear->setAutoRaise(true);
    clear->setFocusPolicy(Qt::NoFocus);
    connect(clear, &QToolButton::clicked, this, [this] {
      lines_.clear();
      if (text_) text_->clear();
      edit_->setToolTip({});
    });
    h->addWidget(clear);
    auto* close = new QToolButton(header);
    close->setObjectName("CommandHistoryClose");
    close->setText(QString::fromUtf8("×"));
    close->setToolTip("Close (Esc, F2 in the command line)");
    close->setAutoRaise(true);
    close->setFocusPolicy(Qt::NoFocus);
    connect(close, &QToolButton::clicked, this, &CommandLine::hideHistoryPane);
    h->addWidget(close);
    v->addWidget(header);
    text_ = new QPlainTextEdit(pane_);
    text_->setObjectName("CommandHistory");
    text_->setReadOnly(true);
    text_->setLineWrapMode(QPlainTextEdit::NoWrap);  // LIST columns stay aligned
    text_->setMaximumBlockCount(kMaxPaneBlocks);
    text_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    text_->installEventFilter(this);
    for (const QString& l : lines_) text_->appendPlainText(l);
    v->addWidget(text_, 1);
    pane_->hide();
    // the line edit may outlive or predate the window it ends up in: the pane goes with this box
    connect(this, &QObject::destroyed, pane_.data(), &QObject::deleteLater);
  } else {
    pane_->setParent(host);
  }
  if (paneHost_) paneHost_->removeEventFilter(this);
  paneHost_ = host;
  host->installEventFilter(this);  // follow window resizes
}

void CommandLine::placePane() {
  if (!pane_ || !paneHost_) return;
  QWidget* host = paneHost_;
  // above the command box, right edge on the box's right edge when it fits, inside the window
  const QPoint boxTopLeft = mapTo(host, QPoint(0, 0));
  const int margin = 8;
  const int w = std::min(kPaneMaxWidth, std::max(240, host->width() - 2 * margin));
  const int bottom = boxTopLeft.y() - 4;
  const int h = std::max(80, std::min(kPaneMaxHeight, bottom - margin - 40));
  int x = boxTopLeft.x() + width() - w;
  x = std::clamp(x, margin, std::max(margin, host->width() - w - margin));
  pane_->setGeometry(x, std::max(margin, bottom - h), w, h);
}

void CommandLine::showHistoryPane() {
  ensurePane();
  placePane();
  pane_->show();
  pane_->raise();
  text_->moveCursor(QTextCursor::End);
  text_->ensureCursorVisible();
  historyButton_->setProperty("open", true);
  historyButton_->style()->unpolish(historyButton_);
  historyButton_->style()->polish(historyButton_);
}

void CommandLine::hideHistoryPane() {
  if (pane_) pane_->hide();
  historyButton_->setProperty("open", false);
  historyButton_->style()->unpolish(historyButton_);
  historyButton_->style()->polish(historyButton_);
}

bool CommandLine::isHistoryPaneVisible() const { return pane_ && pane_->isVisible(); }

void CommandLine::toggleHistoryPane() {
  if (isHistoryPaneVisible()) hideHistoryPane();
  else showHistoryPane();
}

bool CommandLine::eventFilter(QObject* o, QEvent* e) {
  if (o == paneHost_ && e->type() == QEvent::Resize && isHistoryPaneVisible()) placePane();
  if (o == text_ && e->type() == QEvent::KeyPress) {
    auto* k = static_cast<QKeyEvent*>(e);
    if (k->key() == Qt::Key_Escape || k->key() == Qt::Key_F2) {
      hideHistoryPane();
      edit_->setFocus();
      return true;
    }
  }
  if (o == edit_ && (e->type() == QEvent::FocusIn || e->type() == QEvent::FocusOut)) {
    // focus ring on the whole cell (QSS has no :focus-within)
    setProperty("focused", e->type() == QEvent::FocusIn);
    style()->unpolish(this);
    style()->polish(this);
  }
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
      if (isHistoryPaneVisible() && edit_->text().isEmpty() && !manager_->isActive()) {
        hideHistoryPane();
        return true;
      }
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
