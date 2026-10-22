// FabriCAD app: one document tab of the main window (Fusion-style multi-document tabs).
//
// A Design tab owns its Document (undo stack and selection live inside it), its ToolManager
// (shared ToolRegistry), its own 3D viewer (camera), browser tree, timeline, command dialog,
// nav bar and sketch palette. A Drawing tab shares the Document of the design it was opened
// from (the drawing data lives in that document's extension data) but has its own ToolManager
// and Drawing workspace page, so a drawing command never disturbs the design tab. The
// Document is reference counted between a design tab and its drawing tabs: closing one of
// them keeps the document alive for the others; the unsaved-changes prompt comes with the
// last tab.
//
// MainWindow keeps the tab list, the tab bar in the application bar and the three
// QStackedWidgets (central area, browser dock, timeline dock) that hold each tab's widgets.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "core/Document.h"
#include "core/Tool.h"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

class QStackedWidget;
class QWidget;

namespace fabricad::viewer {
class ViewerWidget;
}

namespace fabricad::app {

class BrowserTree;
class CommandCard;
using CommandDialog = CommandCard;
class FeatureListWidget;
class NavBar;
class SketchPalette;
class TimelineWidget;

class DocumentTab : public QObject {
  Q_OBJECT
 public:
  enum class Kind { Design, Drawing };

  /// Where a tab's widgets live (owned by MainWindow).
  struct Hosts {
    QStackedWidget* central = nullptr;   ///< viewer / drawing page
    QStackedWidget* browser = nullptr;   ///< browser tree (design tabs)
    QStackedWidget* timeline = nullptr;  ///< timeline (design tabs)
    QStackedWidget* features = nullptr;  ///< FEATURES list of the sidebar (design tabs)
  };
  using DrawingPageFactory = std::function<QWidget*(core::Document&, core::ToolManager&, QWidget* parent)>;

  DocumentTab(Kind kind, std::shared_ptr<core::Document> doc, core::ToolRegistry& registry, const Hosts& hosts,
              const DrawingPageFactory& drawingFactory, QObject* parent = nullptr);
  ~DocumentTab() override;

  Kind kind() const { return kind_; }
  bool isDesign() const { return kind_ == Kind::Design; }
  core::Workspace workspace() const { return isDesign() ? core::Workspace::Design : core::Workspace::Drawing; }

  core::Document& document() { return *doc_; }
  const core::Document& document() const { return *doc_; }
  const std::shared_ptr<core::Document>& documentPtr() const { return doc_; }
  core::ToolManager& manager() { return *manager_; }

  /// Widgets; the viewer, browser and timeline are null for drawing tabs, the drawing page for
  /// design tabs.
  viewer::ViewerWidget* viewer() const { return viewer_; }
  BrowserTree* browser() const { return browser_; }
  TimelineWidget* timeline() const { return timeline_; }
  /// The FEATURES list (sidebar); null for drawing tabs.
  FeatureListWidget* featureList() const { return featureList_; }
  QWidget* drawingPage() const { return drawingPage_; }
  QWidget* centralWidget() const;
  CommandDialog* dialog() const { return dialog_; }
  NavBar* navBar() const { return navBar_; }
  SketchPalette* palette() const { return palette_; }

  /// Tab text: "Bracket", "Bracket *", "Bracket Drawing *".
  QString title() const;
  QString tooltip() const;

  /// Create the OCCT GL view of a design tab (once the window is shown). Safe to repeat.
  void initializeViewer();
  bool viewerInitialized() const { return viewerInitialized_; }

  /// Autosave bookkeeping (MainWindow): last recovery file written for this document.
  QString lastRecoveryFile;

  /// An unmodified, unnamed, empty design that was never saved (File > Open reuses it).
  bool isPristine() const;

 private:
  Kind kind_;
  std::shared_ptr<core::Document> doc_;
  std::unique_ptr<core::ToolManager> manager_;
  Hosts hosts_;
  viewer::ViewerWidget* viewer_ = nullptr;
  BrowserTree* browser_ = nullptr;
  TimelineWidget* timeline_ = nullptr;
  FeatureListWidget* featureList_ = nullptr;
  QWidget* drawingPage_ = nullptr;
  CommandDialog* dialog_ = nullptr;
  NavBar* navBar_ = nullptr;
  SketchPalette* palette_ = nullptr;
  bool viewerInitialized_ = false;
};

/// A name the app gave a new document ("Untitled", "Untitled 2", ...) rather than the user.
bool isAutoDocumentName(const std::string& name);

}  // namespace fabricad::app
