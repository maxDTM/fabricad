// FabriCAD app: document tab.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/DocumentTab.h"

#include "app/BrowserTree.h"
#include "app/CommandDialog.h"
#include "app/NavBar.h"
#include "app/SketchPalette.h"
#include "app/TimelineWidget.h"
#include "core/Json.h"
#include "core/UndoStack.h"
#include "sketch/SketchSession.h"
#include "viewer/ViewerWidget.h"

#include <QLabel>
#include <QStackedWidget>

namespace fabricad::app {

DocumentTab::DocumentTab(Kind kind, std::shared_ptr<core::Document> doc, core::ToolRegistry& registry, const Hosts& hosts,
                         const DrawingPageFactory& drawingFactory, QObject* parent)
    : QObject(parent), kind_(kind), doc_(std::move(doc)), manager_(std::make_unique<core::ToolManager>(registry)), hosts_(hosts) {
  if (!doc_) doc_ = std::make_shared<core::Document>();
  manager_->setDocument(doc_.get());
  if (kind_ == Kind::Design) {
    viewer_ = new viewer::ViewerWidget(hosts_.central);
    viewer_->setDocument(doc_.get());
    viewer_->setToolManager(manager_.get());
    manager_->setViewer(viewer_);
    if (hosts_.central) hosts_.central->addWidget(viewer_);
    dialog_ = new CommandDialog(viewer_->overlayParent());
    navBar_ = new NavBar(*viewer_, *manager_);
    palette_ = new SketchPalette(*viewer_, *manager_);
    browser_ = new BrowserTree(*doc_, viewer_, hosts_.browser);
    if (hosts_.browser) hosts_.browser->addWidget(browser_);
    timeline_ = new TimelineWidget(*doc_, hosts_.timeline);
    if (hosts_.timeline) hosts_.timeline->addWidget(timeline_);
  } else {
    if (drawingFactory) drawingPage_ = drawingFactory(*doc_, *manager_, hosts_.central);
    if (!drawingPage_) {
      auto* l = new QLabel("The Drawing workspace is not available in this build.", hosts_.central);
      l->setAlignment(Qt::AlignCenter);
      l->setStyleSheet("background: #FFFFFF; color: #6B7079;");
      drawingPage_ = l;
    }
    if (hosts_.central) hosts_.central->addWidget(drawingPage_);
    dialog_ = new CommandDialog(drawingPage_);
  }
}

DocumentTab::~DocumentTab() {
  // an open sketch of this tab (process-wide session) must not outlive its document
  if (sketch::SketchSession* s = sketch::SketchSession::current(); s && manager_ && s->manager() == manager_.get())
    sketch::SketchSession::end();
  if (manager_ && manager_->isActive()) manager_->cancel();
  // the viewer is a child widget: detach it from the manager before widgets go away
  if (manager_) manager_->setViewer(nullptr);
  delete navBar_;
  delete palette_;
  delete dialog_;
  delete browser_;
  delete timeline_;
  delete viewer_;
  delete drawingPage_;
  manager_.reset();  // disconnects from the document before it may be destroyed
  doc_.reset();
}

QWidget* DocumentTab::centralWidget() const { return isDesign() ? static_cast<QWidget*>(viewer_) : drawingPage_; }

QString DocumentTab::title() const {
  QString t = core::qs(doc_->name());
  if (!isDesign()) t += " Drawing";
  if (doc_->isModified()) t += " *";
  return t;
}

QString DocumentTab::tooltip() const {
  if (doc_->filePath().empty()) return isDesign() ? "Not saved yet" : "Drawing of an unsaved design";
  return core::qs(doc_->filePath());
}

void DocumentTab::initializeViewer() {
  if (viewerInitialized_ || !viewer_) return;
  viewerInitialized_ = true;
  viewer_->initializeView();
  if (navBar_) navBar_->reposition();
  if (dialog_) dialog_->raise();
}

bool DocumentTab::isPristine() const {
  return isDesign() && doc_->featureCount() == 0 && !doc_->isModified() && doc_->filePath().empty() &&
         (doc_->name().empty() || doc_->name() == "Untitled") && doc_.use_count() == 1 && !doc_->undoStack().canUndo();
}

}  // namespace fabricad::app
