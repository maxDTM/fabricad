// FabriCAD app: SELECTION panel.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/SelectionPanel.h"

#include "app/ThemeManager.h"
#include "core/Document.h"
#include "core/GeometryRefs.h"
#include "core/Json.h"
#include "core/Selection.h"
#include "core/ShapeUtils.h"
#include "core/Units.h"
#include "theme/Tokens.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <TopoDS.hxx>

#include <QPainter>
#include <QTimer>

#include <cmath>
#include <map>

namespace fabricad::app {

using core::ShapeKind;
using core::ShapeRef;
using theme::Tok;

QString formatGrouped(double v, int decimals) {
  QString s = QString::number(std::abs(v), 'f', decimals);
  const int dot = s.indexOf('.');
  QString intPart = dot >= 0 ? s.left(dot) : s;
  const QString frac = dot >= 0 ? s.mid(dot) : QString();
  if (intPart.size() > 3)
    for (int i = intPart.size() - 3; i > 0; i -= 3) intPart.insert(i, QChar(0x2009));
  return (v < 0 && std::abs(v) >= std::pow(10.0, -decimals) / 2 ? "-" : "") + intPart + frac;
}

namespace {

QString lengthText(const core::Document& doc, double mm, int power = 1) {
  const core::UnitSystem& u = doc.units();
  const core::Dimension dim{power, 0};
  const double v = core::toDisplayValue({mm, dim}, u);
  QString unit = core::lengthUnitSymbol(u.length);
  if (power == 2) unit += QChar(0x00B2);
  if (power == 3) unit += QChar(0x00B3);
  return formatGrouped(v, 2) + " " + unit;
}

QString surfaceName(const TopoDS_Shape& s) {
  try {
    switch (BRepAdaptor_Surface(TopoDS::Face(s)).GetType()) {
      case GeomAbs_Plane: return "Face (planar)";
      case GeomAbs_Cylinder: return "Face (cylindrical)";
      case GeomAbs_Cone: return "Face (conical)";
      case GeomAbs_Sphere: return "Face (spherical)";
      case GeomAbs_Torus: return "Face (toroidal)";
      default: return "Face (freeform)";
    }
  } catch (...) {
    return "Face";
  }
}

QString featureName(const core::Document& doc, core::ObjectId id) {
  const core::Feature* f = doc.feature(id);
  return f ? core::qs(f->name()) : QString("?");
}

QString bodyName(const core::Document& doc, core::ObjectId id) {
  const core::Body* b = doc.body(id);
  return b ? core::qs(b->name) : QString("?");
}

QString kindPlural(ShapeKind k) {
  switch (k) {
    case ShapeKind::Face: return "Faces";
    case ShapeKind::Edge: return "Edges";
    case ShapeKind::Vertex: return "Vertices";
    case ShapeKind::Body: return "Bodies";
    case ShapeKind::Profile: return "Sketch profiles";
    case ShapeKind::SketchCurve: return "Sketch curves";
    case ShapeKind::SketchPoint: return "Sketch points";
    case ShapeKind::Component: return "Components";
    default: return "Items";
  }
}

}  // namespace

std::vector<SelectionFact> selectionFacts(const core::Document& doc, const std::vector<ShapeRef>& items) {
  std::vector<SelectionFact> out;
  if (items.empty()) return out;
  auto add = [&](const QString& k, const QString& v, bool caps = false) { out.push_back({k, v, caps}); };

  if (items.size() > 1) {
    std::map<ShapeKind, int> kinds;
    for (const ShapeRef& r : items) ++kinds[r.kind];
    if (kinds.size() == 1) add("Type", kindPlural(kinds.begin()->first), true);
    else add("Type", "Mixed", true);
    add("Count", QString::number(items.size()));
    double area = 0, length = 0, volume = 0;
    bool anyArea = false, anyLength = false, anyVolume = false;
    for (const ShapeRef& r : items) {
      TopoDS_Shape s;
      try {
        s = core::resolveShape(doc, r);
      } catch (...) {
      }
      if (s.IsNull()) continue;
      if (r.kind == ShapeKind::Face || r.kind == ShapeKind::Profile) {
        area += core::area(s);
        anyArea = true;
      } else if (r.kind == ShapeKind::Edge || r.kind == ShapeKind::SketchCurve) {
        GProp_GProps g;
        BRepGProp::LinearProperties(s, g);
        length += g.Mass();
        anyLength = true;
      } else if (r.kind == ShapeKind::Body) {
        volume += core::volume(s);
        anyVolume = true;
      }
    }
    if (anyArea) add("Total area", lengthText(doc, area, 2));
    if (anyLength) add("Total length", lengthText(doc, length));
    if (anyVolume) add("Total volume", lengthText(doc, volume, 3));
    return out;
  }

  const ShapeRef& r = items.front();
  TopoDS_Shape s;
  try {
    if (r.kind != ShapeKind::Component && r.kind != ShapeKind::Feature) s = core::resolveShape(doc, r);
  } catch (...) {
  }
  switch (r.kind) {
    case ShapeKind::Body: {
      const core::Body* b = doc.body(r.owner);
      add("Type", "Solid body", true);
      add("Name", bodyName(doc, r.owner));
      if (!s.IsNull()) {
        add("Volume", lengthText(doc, core::volume(s), 3));
        add("Area", lengthText(doc, core::area(s), 2));
      }
      if (b && !b->material.empty()) add("Material", core::qs(b->material));
      break;
    }
    case ShapeKind::Face: {
      add("Type", s.IsNull() ? QString("Face") : surfaceName(s), true);
      add("Source", bodyName(doc, r.owner));
      if (!s.IsNull()) add("Area", lengthText(doc, core::area(s), 2));
      if (r.signature.radius > 0) add("Radius", lengthText(doc, r.signature.radius));
      break;
    }
    case ShapeKind::Edge:
    case ShapeKind::SketchCurve: {
      QString type = r.kind == ShapeKind::Edge ? "Edge" : "Sketch curve";
      double radius = 0;
      if (!s.IsNull() && s.ShapeType() == TopAbs_EDGE) {
        try {
          BRepAdaptor_Curve c(TopoDS::Edge(s));
          if (c.GetType() == GeomAbs_Line) type += " (line)";
          else if (c.GetType() == GeomAbs_Circle) {
            type += " (arc)";
            radius = c.Circle().Radius();
          } else type += " (curve)";
        } catch (...) {
        }
      }
      add("Type", type, true);
      add("Source", r.kind == ShapeKind::Edge ? bodyName(doc, r.owner) : featureName(doc, r.owner));
      if (!s.IsNull()) {
        GProp_GProps g;
        BRepGProp::LinearProperties(s, g);
        add("Length", lengthText(doc, g.Mass()));
      }
      if (radius > 0) add("Radius", lengthText(doc, radius));
      break;
    }
    case ShapeKind::Vertex:
    case ShapeKind::SketchPoint: {
      add("Type", r.kind == ShapeKind::Vertex ? "Vertex" : "Sketch point", true);
      add("Source", r.kind == ShapeKind::Vertex ? bodyName(doc, r.owner) : featureName(doc, r.owner));
      if (!s.IsNull() && s.ShapeType() == TopAbs_VERTEX) {
        const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(s));
        const int prec = 3;
        add("Position", QString("%1, %2, %3").arg(formatGrouped(p.X(), prec), formatGrouped(p.Y(), prec), formatGrouped(p.Z(), prec)));
      }
      break;
    }
    case ShapeKind::Profile: {
      add("Type", "Sketch profile (closed)", true);
      add("Source", featureName(doc, r.owner));
      if (!s.IsNull()) {
        add("Area", lengthText(doc, core::area(s), 2));
        GProp_GProps g;
        BRepGProp::LinearProperties(s, g);
        add("Perimeter", lengthText(doc, g.Mass()));
      }
      break;
    }
    case ShapeKind::Component: {
      const core::Component* c = doc.component(r.owner);
      add("Type", "Component", true);
      add("Name", c ? core::qs(c->name) : QString("?"));
      add("Bodies", QString::number(doc.displayedBodies(r.owner).size()));
      if (c && c->grounded) add("State", "Grounded", true);
      break;
    }
    case ShapeKind::OriginPlane:
    case ShapeKind::OriginAxis:
    case ShapeKind::OriginPoint:
      add("Type", r.kind == ShapeKind::OriginPlane ? "Origin plane" : r.kind == ShapeKind::OriginAxis ? "Origin axis" : "Origin point", true);
      add("Name", core::qs(r.name.empty() ? std::string("O") : r.name));
      break;
    default: {
      // features as a whole (timeline / FEATURES picks), sketches, construction, joints
      const core::Feature* f = doc.feature(r.owner);
      add("Type", f ? core::qs(f->type()) : QString::fromUtf8(core::shapeKindName(r.kind)), true);
      if (f) {
        add("Name", core::qs(f->name()));
        QString state = f->isSuppressed() ? "Suppressed" : !doc.isFeatureActive(f->id()) ? "Rolled back" : "Active";
        if (f->status() == core::FeatureStatus::Error) state = "Error";
        else if (f->status() == core::FeatureStatus::Warning) state = "Warning";
        add("State", state, true);
        if (!f->statusMessage().empty()) add("Message", core::qs(f->statusMessage()));
        add("Position", QString("%1 of %2").arg(doc.indexOf(f->id()) + 1).arg(doc.featureCount()));
      }
      break;
    }
  }
  return out;
}

// =========================================================================================

SelectionPanel::SelectionPanel(QWidget* parent) : QWidget(parent) {
  setObjectName("SelectionPanel");
  setAttribute(Qt::WA_StyledBackground);
  connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this] { update(); });
}

void SelectionPanel::setDocument(core::Document* doc) {
  for (const auto& c : conns_) disconnect(c);
  conns_.clear();
  doc_ = doc;
  if (doc_) {
    conns_.push_back(connect(&doc_->selection(), &core::SelectionModel::changed, this, &SelectionPanel::scheduleRefresh));
    conns_.push_back(connect(doc_, &core::Document::recomputeFinished, this, &SelectionPanel::scheduleRefresh));
    conns_.push_back(connect(doc_, &core::Document::featureChanged, this, [this](core::ObjectId) { scheduleRefresh(); }));
    conns_.push_back(connect(doc_, &core::Document::bodyChanged, this, [this](core::ObjectId) { scheduleRefresh(); }));
    conns_.push_back(connect(doc_, &core::Document::unitsChanged, this, &SelectionPanel::scheduleRefresh));
    conns_.push_back(connect(doc_, &core::Document::documentReset, this, &SelectionPanel::scheduleRefresh));
  }
  refresh();
}

int SelectionPanel::selectionCount() const { return doc_ ? doc_->selection().count() : 0; }

void SelectionPanel::scheduleRefresh() {
  if (pending_) return;
  pending_ = true;
  QTimer::singleShot(0, this, [this] {
    if (pending_) refresh();
  });
}

void SelectionPanel::refresh() {
  pending_ = false;
  facts_ = doc_ ? selectionFacts(*doc_, doc_->selection().items()) : std::vector<SelectionFact>{};
  updateGeometry();
  update();
  emit countChanged(selectionCount());
}

QSize SelectionPanel::sizeHint() const {
  return QSize(320, 6 + std::max<int>(2, static_cast<int>(facts_.size())) * kRowH + 8);
}
QSize SelectionPanel::minimumSizeHint() const { return QSize(200, kRowH + 12); }

void SelectionPanel::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.fillRect(rect(), theme::color(Tok::SurfacePanel));
  if (facts_.empty()) {
    p.setFont(theme::font(theme::TextStyle::Caption));
    p.setPen(theme::color(Tok::TextTertiary));
    p.drawText(QRect(16, 6, width() - 32, kRowH), Qt::AlignVCenter | Qt::AlignLeft, "Nothing selected");
    return;
  }
  const QFont key = theme::font(theme::TextStyle::Caption);
  const QFont value = theme::font(theme::TextStyle::Value);
  QFont capsValue = theme::font(theme::TextStyle::Body);
  int y = 6;
  for (const SelectionFact& f : facts_) {
    if (y > height()) break;
    p.setFont(key);
    p.setPen(theme::color(Tok::TextTertiary));
    p.drawText(QRect(16, y, kKeyW - 8, kRowH), Qt::AlignVCenter | Qt::AlignLeft, f.key);
    const QFont& vf = f.caps ? capsValue : value;
    p.setFont(vf);
    p.setPen(theme::color(Tok::TextPrimary));
    const int vx = 16 + kKeyW;
    const QString text = QFontMetrics(vf).elidedText(f.value, Qt::ElideMiddle, std::max(10, width() - vx - 12));
    p.drawText(QRect(vx, y, width() - vx - 12, kRowH), Qt::AlignVCenter | Qt::AlignLeft, text);
    y += kRowH;
  }
}

}  // namespace fabricad::app
