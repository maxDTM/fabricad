// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/tutorial/AnchorResolver.h"

#include "app/BrowserTree.h"
#include "app/CommandCard.h"
#include "app/FeatureListModel.h"
#include "app/FeatureListWidget.h"
#include "app/MainWindow.h"
#include "app/Selectors.h"
#include "app/TimelineLayout.h"
#include "app/TimelineWidget.h"
#include "core/Json.h"
#include "core/ShapeUtils.h"
#include "viewer/ViewerWidget.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRep_Tool.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>

#include <QAbstractButton>
#include <QApplication>
#include <QLabel>
#include <QMenu>
#include <QTabBar>
#include <QToolButton>
#include <QTreeWidget>

#include <cmath>

namespace fabricad::app::tutorial {

namespace {

QString clean(QString s) {
  s.remove('&');
  s.remove(QString::fromUtf8("▾"));
  s.remove(QStringLiteral("..."));
  s.remove(QString::fromUtf8("…"));
  return s.trimmed();
}

QRect globalRect(const QWidget* w) { return QRect(w->mapToGlobal(QPoint(0, 0)), w->size()); }

bool reallyVisible(const QWidget* w) {
  if (!w || !w->isVisible() || w->width() <= 0 || w->height() <= 0) return false;
  // inside its window (a scrolled-away row or a collapsed toolbar section is not visible)
  const QWidget* win = w->window();
  return QRect(win->mapToGlobal(QPoint(0, 0)), win->size()).intersects(globalRect(w));
}

/// The menus (open or not) that hold an action with property toolId == id.
QAction* actionWithToolId(QMenu* m, const QString& id, QMenu** holder) {
  for (QAction* a : m->actions()) {
    if (a->property("toolId").toString() == id) {
      *holder = m;
      return a;
    }
    if (QMenu* sub = a->menu())
      if (QAction* found = actionWithToolId(sub, id, holder)) return found;
  }
  return nullptr;
}

/// The button that opens `menu` (directly or through parent menus).
QToolButton* opener(QWidget* root, QMenu* menu) {
  for (QToolButton* b : root->findChildren<QToolButton*>()) {
    if (!b->menu() || !reallyVisible(b)) continue;
    if (b->menu() == menu) return b;
    std::function<bool(QMenu*)> holds = [&](QMenu* m) {
      for (QAction* a : m->actions())
        if (QMenu* sub = a->menu(); sub && (sub == menu || holds(sub))) return true;
      return false;
    };
    if (holds(b->menu())) return b;
  }
  return nullptr;
}

QAction* actionByText(QMenu* m, const QString& text) {
  for (QAction* a : m->actions())
    if (!a->isSeparator() && AnchorResolver::textMatches(a->text(), text)) return a;
  return nullptr;
}

ResolvedAnchor fromAction(QMenu* m, QAction* a) {
  ResolvedAnchor r;
  r.found = true;
  const QRect ar = m->actionGeometry(a);
  r.rect = QRect(m->mapToGlobal(ar.topLeft()), ar.size());
  r.point = r.rect.center();
  r.widget = m;
  r.visible = m->isVisible() && a->isVisible();
  return r;
}

}  // namespace

bool AnchorResolver::textMatches(const QString& shown, const QString& wanted) {
  return clean(shown).compare(clean(wanted), Qt::CaseInsensitive) == 0;
}

QWidget* AnchorResolver::findWidget(QWidget* root, const std::function<bool(QWidget*)>& pred, bool visibleOnly) {
  auto scan = [&](QWidget* top) -> QWidget* {
    if (!top) return nullptr;
    if (pred(top) && (!visibleOnly || reallyVisible(top))) return top;
    for (QWidget* w : top->findChildren<QWidget*>())
      if (pred(w) && (!visibleOnly || reallyVisible(w))) return w;
    return nullptr;
  };
  if (QWidget* w = scan(root)) return w;
  for (QWidget* top : QApplication::topLevelWidgets())
    if (top != root && top->isVisible())
      if (QWidget* w = scan(top)) return w;
  return nullptr;
}

ResolvedAnchor AnchorResolver::fromWidget(QWidget* w, const QString& why) {
  ResolvedAnchor r;
  if (!w) {
    r.why = why;
    return r;
  }
  r.found = true;
  r.widget = w;
  r.rect = globalRect(w);
  r.point = r.rect.center();
  r.visible = reallyVisible(w);
  if (!r.visible) r.why = "not visible";
  return r;
}

ResolvedAnchor AnchorResolver::resolve(const Anchor& a) const {
  const QString& k = a.kind;
  if (k == "none") {
    ResolvedAnchor r;
    r.found = r.visible = true;
    return r;
  }
  if (k == "widget") return widgetAnchor(a.target);
  if (k == "tool") return toolAnchor(a.target);
  if (k == "group") return groupAnchor(a.target);
  if (k == "menu") return menuAnchor(a.target);
  if (k == "prop") return propAnchor(a.target);
  if (k == "valuebox") return valueBoxAnchor(a.target);
  if (k == "browser") return browserAnchor(a.target);
  if (k == "timeline") return timelineAnchor(a.target);
  if (k == "tab") return tabAnchor(a.target);
  if (k == "tabclose") return tabCloseAnchor(a.target);
  if (k == "model") return modelAnchor(a.target, false);
  if (k == "sketchpoint") return modelAnchor(a.target, true);
  ResolvedAnchor r;
  r.why = "unknown anchor kind " + k;
  return r;
}

// --- widgets -----------------------------------------------------------------------------------

ResolvedAnchor AnchorResolver::widgetAnchor(const QString& path) const {
  if (path.compare("viewport", Qt::CaseInsensitive) == 0) return fromWidget(w_.viewer(), "no 3D view in this tab");
  if (path.compare("window", Qt::CaseInsensitive) == 0) return fromWidget(&w_);
  // "Parent/Child": each segment an objectName below the previous one
  const QStringList parts = path.split('/', Qt::SkipEmptyParts);
  QWidget* scope = &w_;
  QWidget* hit = nullptr;
  for (int i = 0; i < parts.size(); ++i) {
    const QString name = parts[i];
    const bool last = i == parts.size() - 1;
    auto pred = [&](QWidget* w) { return w->objectName() == name; };
    hit = i == 0 ? findWidget(scope, pred, last) : nullptr;
    if (i > 0) {
      for (QWidget* w : scope->findChildren<QWidget*>(name))
        if (!last || reallyVisible(w)) {
          hit = w;
          break;
        }
    }
    if (!hit && last) hit = i == 0 ? findWidget(scope, pred, false) : scope->findChild<QWidget*>(name);
    if (!hit) return fromWidget(nullptr, "no widget named '" + name + "'");
    scope = hit;
  }
  return fromWidget(hit);
}

ResolvedAnchor AnchorResolver::toolAnchor(const QString& id) const {
  // 1. a visible button (main toolbar, quick toolbar, sketch palette) with property toolId
  if (QWidget* b = findWidget(&w_, [&](QWidget* w) { return w->property("toolId").toString() == id && qobject_cast<QAbstractButton*>(w); }))
    return fromWidget(b);
  // 2. an entry of a section caption / variant / MORE menu: the open menu's row, else the button
  // that opens it (the bubble says to open it first)
  QMenu* holder = nullptr;
  QAction* act = nullptr;
  for (QMenu* m : w_.findChildren<QMenu*>()) {
    if ((act = actionWithToolId(m, id, &holder))) {
      if (holder->isVisible()) return fromAction(holder, act);
      if (QToolButton* b = opener(&w_, holder)) {
        ResolvedAnchor r = fromWidget(b);
        r.via = "menu";
        return r;
      }
    }
  }
  if (QWidget* b = findWidget(&w_, [&](QWidget* w) { return w->property("toolId").toString() == id; }, false)) {
    ResolvedAnchor r = fromWidget(b);
    r.why = "the tool's button is hidden (another workspace or a collapsed section)";
    return r;
  }
  return fromWidget(nullptr, "no button or menu entry for tool '" + id + "'");
}

ResolvedAnchor AnchorResolver::groupAnchor(const QString& spec) const {
  const QStringList p = spec.split('/');
  const QString tab = p.size() > 1 ? p[0] : QString();
  const QString group = p.size() > 1 ? p[1] : p[0];
  // the section's group, or its name (the caption tooltip; MORE has no group)
  auto groupOf = [&](QWidget* w) {
    return textMatches(w->property("toolbarGroup").toString(), group) || textMatches(w->toolTip(), group);
  };
  auto pred = [&](QWidget* w) {
    return w->objectName() == QLatin1String("RibbonGroupLabel") && groupOf(w) &&
           (tab.isEmpty() || textMatches(w->property("toolbarTab").toString(), tab));
  };
  if (QWidget* w = findWidget(&w_, pred)) return fromWidget(w);
  // a collapsed section shows its folded button instead of the caption
  if (QWidget* w = findWidget(&w_, [&](QWidget* x) {
        return x->objectName() == QLatin1String("RibbonCollapsedTool") && groupOf(x) &&
               (tab.isEmpty() || textMatches(x->property("toolbarTab").toString(), tab));
      }))
    return fromWidget(w);
  return fromWidget(findWidget(&w_, pred, false), "no toolbar section '" + spec + "'");
}

ResolvedAnchor AnchorResolver::menuAnchor(const QString& spec) const {
  // menu:Help/Tutorials: a title-bar (#AppBar) button with a menu, matched by objectName
  // ("HelpButton"), tooltip or text; then the action path inside the menu
  const QStringList p = spec.split('/', Qt::SkipEmptyParts);
  if (p.isEmpty()) return fromWidget(nullptr, "empty menu path");
  QToolButton* button = nullptr;
  for (QToolButton* b : w_.findChildren<QToolButton*>()) {
    if (!b->menu()) continue;
    if (textMatches(b->objectName(), p[0] + "Button") || textMatches(b->objectName(), p[0]) || textMatches(b->toolTip(), p[0]) ||
        textMatches(b->text(), p[0])) {
      button = b;
      if (reallyVisible(b)) break;
    }
  }
  if (!button) return fromWidget(nullptr, "no menu button '" + p[0] + "'");
  QMenu* menu = button->menu();
  ResolvedAnchor last = fromWidget(button);
  for (int i = 1; i < p.size(); ++i) {
    QAction* a = actionByText(menu, p[i]);
    if (!a) {
      ResolvedAnchor r;
      r.why = "no entry '" + p[i] + "' in menu '" + p.mid(0, i).join('/') + "'";
      return r;
    }
    if (!menu->isVisible()) {
      last.via = "menu";  // open the menu first; point at what opens it
      last.found = true;
      return last;
    }
    last = fromAction(menu, a);
    if (!a->menu()) break;
    menu = a->menu();
  }
  return last;
}

ResolvedAnchor AnchorResolver::propAnchor(const QString& name) const {
  const QString obj = name.startsWith("prop_") ? name : "prop_" + name;
  QWidget* card = w_.commandCard();
  QWidget* hit = nullptr;
  // a combo shown as a segmented row (redesign 5.4) stays hidden next to its SegmentedControl,
  // which has the same objectName: prefer the visible one
  if (card)
    for (QWidget* w : card->findChildren<QWidget*>(obj))
      if (!hit || (!reallyVisible(hit) && reallyVisible(w))) hit = w;
  if (!hit) hit = findWidget(&w_, [&](QWidget* w) { return w->objectName() == obj; }, false);
  return fromWidget(hit, "no field '" + obj + "' in the command card (is the command running?)");
}

ResolvedAnchor AnchorResolver::valueBoxAnchor(const QString& caption) const {
  QWidget* hit = findWidget(&w_, [&](QWidget* w) {
    if (w->objectName() != QLatin1String("fcValueBox")) return false;
    if (w->property("field").toString().compare(caption, Qt::CaseInsensitive) == 0) return true;
    auto* tag = w->findChild<QLabel*>("fcValueTag");
    return tag && textMatches(tag->text(), caption);
  });
  return fromWidget(hit, "no value box '" + caption + "' (a sketch tool shows them while drawing)");
}

ResolvedAnchor AnchorResolver::browserAnchor(const QString& path) const {
  BrowserTree* tree = w_.browser();
  if (!tree) return fromWidget(nullptr, "no MODEL panel in this tab");
  const QStringList p = path.split('/', Qt::SkipEmptyParts);
  // the first segment may match a top-level item or a child of the root (document) item
  std::vector<QTreeWidgetItem*> level;
  for (int i = 0; i < tree->topLevelItemCount(); ++i) {
    QTreeWidgetItem* top = tree->topLevelItem(i);
    level.push_back(top);
    for (int c = 0; c < top->childCount(); ++c) level.push_back(top->child(c));
  }
  QTreeWidgetItem* item = nullptr;
  for (const QString& seg : p) {
    QTreeWidgetItem* next = nullptr;
    for (QTreeWidgetItem* it : level) {
      // "YZ" matches "YZ Plane" too (origin rows are named "XY Plane")
      const QString t = it->text(0);
      if (textMatches(t, seg) || clean(t).startsWith(clean(seg) + ' ', Qt::CaseInsensitive)) {
        next = it;
        break;
      }
    }
    if (!next) {
      ResolvedAnchor r;
      r.why = "no MODEL item '" + seg + "' (path " + path + ")";
      return r;
    }
    item = next;
    level.clear();
    for (int c = 0; c < item->childCount(); ++c) level.push_back(item->child(c));
  }
  ResolvedAnchor r;
  r.found = true;
  r.widget = tree;
  // a row inside a collapsed parent: point at the deepest visible ancestor
  QTreeWidgetItem* shown = item;
  for (QTreeWidgetItem* a = item->parent(); a; a = a->parent())
    if (!a->isExpanded()) shown = a;
  if (shown != item) r.via = "expand";
  const QRect vr = tree->visualItemRect(shown);
  r.rect = QRect(tree->viewport()->mapToGlobal(vr.topLeft()), vr.size());
  r.point = r.rect.center();
  r.visible = reallyVisible(tree->viewport()) && vr.isValid() && tree->viewport()->rect().intersects(vr);
  if (!r.visible) r.why = "the MODEL panel row is not on screen";
  return r;
}

ResolvedAnchor AnchorResolver::timelineAnchor(const QString& name) const {
  if (!w_.hasDocument()) return fromWidget(nullptr, "no document");
  const core::Feature* f = findFeature(w_.document(), name.toStdString());
  if (!f) return fromWidget(nullptr, "no feature '" + name + "'");
  ResolvedAnchor r;
  if (FeatureListWidget* list = w_.featureList(); list && reallyVisible(list)) {
    const int row = list->model().rowOfFeature(f->id());
    const QRect rr = row >= 0 ? list->rowRect(row) : QRect();
    r.found = true;
    r.widget = list;
    r.rect = QRect(list->viewport()->mapToGlobal(rr.topLeft()), rr.size());
    r.point = r.rect.center();
    r.visible = rr.isValid() && list->viewport()->rect().intersects(rr);
    if (!r.visible) {
      r.why = "the FEATURES row is not on screen (scroll, or the list is compact while a command runs)";
      r.rect = globalRect(list);
    }
    return r;
  }
  if (TimelineWidget* tl = w_.timeline(); tl && reallyVisible(tl->strip())) {
    TimelineStrip* strip = tl->strip();
    const auto& entries = strip->layout().entries();
    for (int i = 0; i < static_cast<int>(entries.size()); ++i)
      if (entries[i].id == f->id()) {
        const QRect ir = strip->layout().itemRect(i, strip->height());
        r.found = true;
        r.widget = strip;
        r.rect = QRect(strip->mapToGlobal(ir.topLeft()), ir.size());
        r.point = r.rect.center();
        r.visible = reallyVisible(strip) && QRect(strip->window()->mapToGlobal(QPoint()), strip->window()->size()).contains(r.point);
        return r;
      }
  }
  r.found = true;
  r.why = "neither the FEATURES list nor the timeline strip is shown";
  return r;
}

ResolvedAnchor AnchorResolver::tabAnchor(const QString& tab) const {
  // the ribbon tab row is gone (redesign): a tab resolves to the toolbar sections holding its tools
  QRect u;
  QWidget* first = nullptr;
  for (QWidget* w : w_.findChildren<QWidget*>()) {
    if (w->objectName() != QLatin1String("RibbonGroup") || !reallyVisible(w)) continue;
    if (!textMatches(w->property("toolbarTab").toString(), tab)) continue;
    u = u.united(globalRect(w));
    if (!first) first = w;
  }
  if (!first) {
    // tools of the tab folded into MORE: the MORE section's caption
    const bool inMore = findWidget(&w_, [&](QWidget* w) {
                          return w->objectName() == QLatin1String("RibbonTool") && textMatches(w->property("toolbarTab").toString(), tab);
                        }, false) != nullptr ||
                        tab.compare("TOOLS", Qt::CaseInsensitive) == 0 || tab.compare("SURFACE", Qt::CaseInsensitive) == 0;
    if (inMore) {
      return groupAnchor("MORE/MORE");
    }
    return fromWidget(nullptr, "no toolbar section for tab '" + tab + "'");
  }
  ResolvedAnchor r = fromWidget(first);
  r.rect = u;
  r.point = u.center();
  return r;
}

ResolvedAnchor AnchorResolver::tabCloseAnchor(const QString& which) const {
  QTabBar* bar = w_.tabBar();
  if (!bar || !reallyVisible(bar)) return fromWidget(nullptr, "no document tab bar");
  const int index = which.isEmpty() || which == "current" ? bar->currentIndex() : which.toInt() - 1;
  if (index < 0 || index >= bar->count()) return fromWidget(nullptr, "no tab " + which);
  // the custom-painted close glyph sits at the right end of the tab (closeRect hook: later)
  if (QWidget* b = bar->tabButton(index, QTabBar::RightSide); b && reallyVisible(b)) return fromWidget(b);
  const QRect tr = bar->tabRect(index);
  const int s = std::min(16, tr.height());
  const QRect cr(tr.right() - s - 10, tr.center().y() - s / 2, s, s);
  ResolvedAnchor r;
  r.found = r.visible = true;
  r.widget = bar;
  r.rect = QRect(bar->mapToGlobal(cr.topLeft()), cr.size());
  r.point = r.rect.center();
  r.via = "approx";
  return r;
}

// --- model ---------------------------------------------------------------------------------------

namespace {

std::vector<gp_Pnt> samplePoints(const core::Document& doc, const core::ShapeRef& ref, double originSize) {
  std::vector<gp_Pnt> pts;
  if (ref.hasPoint) pts.emplace_back(ref.point.x, ref.point.y, ref.point.z);
  if (ref.isBrep() && ref.kind != core::ShapeKind::Body) {
    const TopoDS_Shape body = worldBodyShape(doc, ref.owner);
    if (!body.IsNull()) {
      TopTools_IndexedMapOfShape map;
      const TopAbs_ShapeEnum type = ref.kind == core::ShapeKind::Face ? TopAbs_FACE : ref.kind == core::ShapeKind::Edge ? TopAbs_EDGE : TopAbs_VERTEX;
      TopExp::MapShapes(body, type, map);
      if (ref.index >= 1 && ref.index <= map.Extent()) {
        const TopoDS_Shape& s = map(ref.index);
        try {
          if (type == TopAbs_VERTEX) {
            pts.push_back(BRep_Tool::Pnt(TopoDS::Vertex(s)));
          } else if (type == TopAbs_EDGE) {
            BRepAdaptor_Curve c(TopoDS::Edge(s));
            for (double t : {0.5, 0.25, 0.75}) pts.push_back(c.Value(c.FirstParameter() + t * (c.LastParameter() - c.FirstParameter())));
          } else {
            const TopoDS_Face face = TopoDS::Face(s);
            BRepAdaptor_Surface sf(face);
            const double u0 = sf.FirstUParameter(), u1 = sf.LastUParameter(), v0 = sf.FirstVParameter(), v1 = sf.LastVParameter();
            // interior UV grid, centre first; keep points inside the trimmed face
            for (double fu : {0.5, 0.3, 0.7}) {
              for (double fv : {0.5, 0.3, 0.7}) {
                const gp_Pnt2d uv(u0 + fu * (u1 - u0), v0 + fv * (v1 - v0));
                BRepClass_FaceClassifier cls(face, uv, 1e-6);
                if (cls.State() == TopAbs_IN) pts.push_back(sf.Value(uv.X(), uv.Y()));
              }
            }
          }
        } catch (...) {
        }
      }
    }
  }
  if (ref.kind == core::ShapeKind::OriginPlane) {
    // origin planes are squares in the positive quadrant (centre (h, h), half size 0.9 h, viewer
    // SceneModel): a grid over the square, centre first; the pick check drops the points another
    // plane covers (XZ hides the back half of XY in the iso view)
    const QString n = core::qs(ref.name).toUpper();
    const gp_Ax3 ax = core::originPlane(n.left(2).toStdString());
    const double h = originSize > 0 ? originSize : 40.0;
    for (double fu : {1.0, 0.5, 1.5, 0.25, 1.75})
      for (double fv : {1.0, 0.5, 1.5, 0.25, 1.75})
        pts.push_back(ax.Location().Translated(gp_Vec(ax.XDirection()) * (fu * h) + gp_Vec(ax.YDirection()) * (fv * h)));
  }
  if (pts.empty()) {
    auto m = [&](const char* k) { return refMetric(doc, ref, k); };
    if (auto cx = m("cx"), cy = m("cy"), cz = m("cz"); cx && cy && cz) pts.emplace_back(*cx, *cy, *cz);
  }
  return pts;
}

}  // namespace

ResolvedAnchor AnchorResolver::modelAnchor(const QString& selector, bool sketchPoint) const {
  ResolvedAnchor r;
  viewer::ViewerWidget* v = w_.viewer();
  if (!w_.hasDocument()) {
    r.why = "no document";
    return r;
  }
  const core::Document& doc = w_.document();
  std::vector<gp_Pnt> pts;
  if (sketchPoint) {
    core::Vec3 p;
    if (parsePoint(selector.startsWith('@') ? selector.mid(1).toStdString() : selector.toStdString(), p) < 2) {
      r.why = "sketchpoint needs @x,y[,z]";
      return r;
    }
    pts.emplace_back(p.x, p.y, p.z);
    r.found = true;
  } else {
    SelectorResult sel = resolveSelector(doc, selector.toStdString());
    if (!sel.ok() || sel.refs.empty()) {
      r.why = sel.ok() ? "nothing matches '" + selector + "'" : core::qs(sel.error);
      return r;
    }
    r.found = true;
    r.refs = sel.refs;
    for (const core::ShapeRef& ref : sel.refs)
      for (const gp_Pnt& p : samplePoints(doc, ref, v ? v->originPlaneSize() : 0.0)) pts.push_back(p);
  }
  if (!v || !reallyVisible(v)) {
    r.why = "no 3D view";
    return r;
  }
  const QRect vr = v->rect();
  const QPoint c = vr.center();
  double best = 1e18;
  bool anyOnScreen = false;
  for (const gp_Pnt& p : pts) {
    double x = 0, y = 0;
    if (!v->worldToScreen(p, x, y)) continue;
    const QPoint lp(static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)));
    if (!vr.contains(lp)) continue;
    anyOnScreen = true;
    const QPoint gp = v->mapToGlobal(lp);
    // not covered by a viewport overlay (card, dial, ViewCube, palette, value box, ...)
    // (only the view itself or its GL surface counts as free; any other child, such as the
    // floating command card or a value box, covers the point)
    if (QWidget* top = QApplication::widgetAt(gp); top && top != v && !(v->isAncestorOf(top) && top->inherits("QOpenGLWidget")))
      continue;
    bool verified = false;
    if (!r.refs.empty()) {
      core::ShapeRef hit;
      if (v->pick(lp.x(), lp.y(), core::mask(r.refs.front().kind), hit)) verified = std::find(r.refs.begin(), r.refs.end(), hit) != r.refs.end();
    }
    const double d = std::hypot(lp.x() - c.x(), lp.y() - c.y()) - (verified ? 1e9 : 0);
    if (d < best) {
      best = d;
      r.point = gp;
      r.pickVerified = verified;
      r.visible = true;
    }
  }
  if (r.visible) {
    r.rect = QRect(r.point - QPoint(18, 18), QSize(36, 36));
  } else {
    r.via = anyOnScreen ? "occluded" : "offscreen";
    r.why = anyOnScreen ? "the target is covered: orbit or press Home" : "the target is off screen: orbit or press Home";
    r.rect = QRect(v->mapToGlobal(QPoint(0, 0)), v->size());
  }
  return r;
}

}  // namespace fabricad::app::tutorial
