// FabriCAD app: floating quick toolbar.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/QuickToolbar.h"

#include "app/Icons.h"
#include "app/ToolbarModel.h"
#include "core/Json.h"

#include <QSettings>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace fabricad::app {

namespace {
constexpr const char* kUsageKey = "quickToolbar/usage";

// the strip before anything has been used (5.3: the six most-used tools)
const std::vector<std::string>& modelDefaults() {
  static const std::vector<std::string> d = {"sketch.create", "part.extrude", "part.revolve",
                                             "part.fillet",   "part.hole",    "part.shell"};
  return d;
}
const std::vector<std::string>& sketchDefaults() {
  static const std::vector<std::string> d = {"sketch.line", "sketch.rectangle", "sketch.circle",
                                             "sketch.arc-3point", "sketch.trim", "sketch.dimension"};
  return d;
}

bool fitsContext(const core::ToolInfo& i, core::Workspace ws, bool sketching) {
  if (i.hidden || i.workspace != ws) return false;
  if (i.id == "sketch.finish") return false;  // its own button
  if (i.keepsActiveTool) return false;        // view and filter toggles are not "commands"
  if (i.id.starts_with("app.") || i.id.starts_with("view.") || i.id.starts_with("select.")) return false;
  return sketching == (i.tab == "SKETCH");
}
}  // namespace

ToolUsage loadToolUsage() {
  ToolUsage out;
  const QVariantMap m = QSettings().value(kUsageKey).toMap();
  for (auto it = m.begin(); it != m.end(); ++it)
    if (const int n = it.value().toInt(); n > 0) out.insert(it.key(), n);
  return out;
}

void saveToolUsage(const ToolUsage& usage) {
  QVariantMap m;
  for (auto it = usage.begin(); it != usage.end(); ++it) m.insert(it.key(), it.value());
  QSettings().setValue(kUsageKey, m);
}

std::vector<std::string> pickQuickTools(core::ToolRegistry& registry, core::Workspace ws, bool sketching,
                                        const ToolUsage& usage, int count) {
  struct Cand {
    std::string id;
    int uses;
    size_t order;
  };
  std::vector<Cand> used;
  size_t order = 0;
  for (core::ITool* t : registry.tools()) {
    const core::ToolInfo& i = t->info();
    ++order;
    if (!fitsContext(i, ws, sketching)) continue;
    if (const int n = usage.value(core::qs(i.id), 0); n > 0) used.push_back({i.id, n, order});
  }
  std::stable_sort(used.begin(), used.end(), [](const Cand& a, const Cand& b) { return a.uses > b.uses; });
  std::vector<std::string> out;
  for (const Cand& c : used) {
    if (static_cast<int>(out.size()) >= count) break;
    out.push_back(c.id);
  }
  for (const std::string& id : sketching ? sketchDefaults() : modelDefaults()) {
    if (static_cast<int>(out.size()) >= count) break;
    const core::ITool* t = registry.tool(id);
    if (t && fitsContext(t->info(), ws, sketching) && std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
  }
  return out;
}

QuickToolbar::QuickToolbar(core::ToolManager& manager, QWidget* parent) : QFrame(parent), manager_(&manager) {
  setObjectName("QuickToolbar");
  setAttribute(Qt::WA_StyledBackground);
  setFixedWidth(kWidth);
  layout_ = new QVBoxLayout(this);
  layout_->setContentsMargins(3, kGap, 3, kGap);
  layout_->setSpacing(kGap);
  layout_->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
  repeat_ = makeButton("QuickRepeat");
  repeat_->setIcon(resourceIcon("redo"));  // the repeat arrow
  connect(repeat_, &QToolButton::clicked, this, &QuickToolbar::repeatRequested);
  layout_->addWidget(repeat_);
  auto* sep = new QFrame(this);
  sep->setObjectName("QuickToolbarSeparator");  // coloured by the theme QSS
  sep->setFrameShape(QFrame::HLine);
  sep->setFixedHeight(1);
  layout_->addWidget(sep);
  finish_ = makeButton("FinishSketchButton");
  finish_->setIcon(resourceIcon("finish-sketch"));
  finish_->setProperty("toolId", "sketch.finish");
  connect(finish_, &QToolButton::clicked, this, [this] { emit toolTriggered("sketch.finish"); });
  finish_->hide();
  layout_->addWidget(finish_);
  hide();
}

QToolButton* QuickToolbar::makeButton(const QString& objectName) {
  auto* b = new QToolButton(this);
  b->setObjectName(objectName);
  b->setFixedSize(kButton, kButton);
  b->setIconSize(QSize(20, 20));
  b->setToolButtonStyle(Qt::ToolButtonIconOnly);
  b->setAutoRaise(true);
  b->setFocusPolicy(Qt::NoFocus);
  return b;
}

void QuickToolbar::setToolManager(core::ToolManager* m) {
  if (!m || m == manager_) return;
  manager_ = m;
  built_ = false;
  rebuild(ws_, sketching_);
}

void QuickToolbar::rebuild(core::Workspace ws, bool sketching) {
  const std::vector<std::string> ids = pickQuickTools(manager_->registry(), ws, sketching, loadToolUsage());
  ws_ = ws;
  sketching_ = sketching;
  if (built_ && ids == ids_) {
    finish_->setVisible(sketching);
    refreshStates();
    return;
  }
  built_ = true;
  ids_ = ids;
  for (QToolButton* b : buttons_) {
    layout_->removeWidget(b);
    b->deleteLater();
  }
  buttons_.clear();
  int at = layout_->indexOf(finish_);
  for (const std::string& id : ids_) {
    core::ITool* t = manager_->registry().tool(id);
    if (!t) continue;
    QToolButton* b = makeButton("QuickTool");
    b->setIcon(toolIcon(t->info()));
    b->setToolTip(core::qs(toolTooltip(t->info())));
    b->setProperty("toolId", core::qs(id));
    connect(b, &QToolButton::clicked, this, [this, id] {
      emit toolTriggered(id);
      QTimer::singleShot(0, this, [this] { refreshStates(); });
    });
    layout_->insertWidget(at++, b);
    buttons_.push_back(b);
  }
  finish_->setVisible(sketching);
  refreshStates();
  adjustSize();
}

void QuickToolbar::noteUsed(const core::ToolInfo& info) {
  if (!fitsContext(info, info.workspace, info.tab == "SKETCH")) return;
  ToolUsage usage = loadToolUsage();
  usage[core::qs(info.id)] += 1;
  saveToolUsage(usage);
}

void QuickToolbar::refreshStates() {
  const core::ToolContext ctx = manager_->makeContext();
  for (QToolButton* b : buttons_) {
    core::ITool* t = manager_->registry().tool(b->property("toolId").toString().toStdString());
    const bool enabled = t && t->isEnabled(ctx);
    b->setEnabled(enabled);
    b->setCheckable(true);
    b->setChecked(t && manager_->activeToolId() == t->info().id);
  }
  // repeat: the last command outside the sketch, or the last sketch command while sketching
  const std::string& last = sketching_ ? manager_->lastToolId() : manager_->lastModelToolId();
  const core::ITool* lt = last.empty() ? nullptr : manager_->registry().tool(last);
  repeat_->setEnabled(lt != nullptr);
  repeat_->setToolTip(lt ? "Repeat " + core::qs(lt->info().name) + " (Enter)" : QString("Repeat the last command (Enter)"));
  if (const core::ITool* f = manager_->registry().tool("sketch.finish")) {
    finish_->setToolTip(core::qs(toolTooltip(f->info())));
    finish_->setEnabled(f->isEnabled(ctx));
  }
}

QSize QuickToolbar::sizeHint() const {
  int n = 1 + static_cast<int>(buttons_.size()) + (finish_->isVisibleTo(this) ? 1 : 0);
  return QSize(kWidth, 2 * kGap + n * kButton + n * kGap + 1);
}

}  // namespace fabricad::app
