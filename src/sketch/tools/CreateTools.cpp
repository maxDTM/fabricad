// FabriCAD sketch tools: geometry creation (line, rectangles, circles, arcs, polygon, slot, point)
// with Fusion-style rubber-banding, inference snapping and on-screen value boxes (dynamic input):
// typed values lock the preview and become driving dimensions when the geometry is created.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sketch/tools/SketchToolBase.h"
#include "sketch/tools/SketchTools.h"

#include "core/Document.h"
#include "core/Units.h"

#include <QtCore/Qt>

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>

namespace fabricad::sketch {

using core::ToolContext;
using core::ToolInfo;
using core::ViewerKeyEvent;
using core::ViewerMouseEvent;

namespace {

constexpr double kEps = 1e-9;

/// Keep only the coincidence part of an inference (used for points where H/V would be wrong).
InferenceResult coincidenceOnly(InferenceResult r) {
  r.horizontal = r.vertical = false;
  r.parallelTo.reset();
  r.perpendicularTo.reset();
  return r;
}

/// Which end of an arc/line equals p.
PointPos endMatching(const Geometry& g, Pt2 p) { return g.start().near(p, 1e-6) ? PointPos::Start : PointPos::End; }

/// Unit tangent pointing away from the curve at one of its ends.
Pt2 outwardTangent(const Geometry& g, PointPos pos) {
  if (g.type == GeomType::Line) return pos == PointPos::End ? g.direction() : -g.direction();
  Pt2 p = g.point(pos);
  Pt2 t = (p - g.center()).perp().normalized();  // ccw tangent
  return pos == PointPos::End ? t : -t;
}

double deg(double rad) { return rad * 180.0 / kPi; }
double rad(double degrees) { return degrees * kPi / 180.0; }
/// Angle in [0, 360).
double deg360(double radians) { return deg(normalizeAngle(radians)); }
/// Map [0,360) to (-180,180] (the sign convention of Sketch::measure for angles).
double signedDeg(double d360) { return d360 > 180.0 ? d360 - 360.0 : d360; }
double sign(double v) { return v < 0 ? -1.0 : 1.0; }

ValueField lengthField(const char* id, const char* label) { return ValueField{.id = id, .label = label, .kind = ValueKind::Length}; }
ValueField angleField(const char* id, const char* label) { return ValueField{.id = id, .label = label, .kind = ValueKind::Angle}; }

/// Shared "collect N clicked points" tool: subclasses implement preview() and create(), declare
/// their value boxes (valueFields / updateValueVisibility) and how locked boxes move the cursor
/// point (applyLocks).
class PointsTool : public SketchTool {
 public:
  PointsTool(ToolInfo info, int needed) : SketchTool(std::move(info)), needed_(needed) {}

  void activate(ToolContext& ctx) override {
    SketchTool::activate(ctx);
    pts_.clear();
    defineValues(valueFields());
    updateValueVisibility();
    ctx.setPrompt(prompt());
  }

  bool mouseMove(ToolContext& ctx, const ViewerMouseEvent& e) override {
    auto c = cursor(e);
    if (!c) return false;
    InferenceResult r = constrained(inferFor(*c, e));
    lastCursor_ = r.point;
    hasCursor_ = true;
    clearTemp();
    showInference(r);
    preview(ctx, r.point);
    showValueBoxes(r.point);
    return true;
  }

  bool mousePress(ToolContext& ctx, const ViewerMouseEvent& e) override {
    if (e.button != core::MouseButton::Left) return false;
    auto c = cursor(e);
    if (!c) return true;
    if (values().isTyping()) values().lockActive();  // a click applies the typed value (Fusion)
    addPoint(ctx, constrained(inferFor(*c, e)));
    return true;
  }

  bool commandLineInput(ToolContext& ctx, const std::string& text) override {
    TypedInput in = parseTypedInput(text);
    std::optional<Pt2> last = pts_.empty() ? std::nullopt : std::optional<Pt2>(pts_.back().point);
    double f = displayFactor();
    switch (in.kind) {
      case TypedInput::Kind::Pair:
        // "w,h" after the first point fills two boxes; otherwise it is an absolute coordinate
        if (last && values().visibleCount() >= 2 && typedValuesToBoxes(ctx, text)) return true;
        addPoint(ctx, inferExact({in.a * f, in.b * f}, last));
        return true;
      case TypedInput::Kind::Relative:
        if (!last) return false;
        addPoint(ctx, inferExact(*last + Pt2{in.a * f, in.b * f}, last));
        return true;
      case TypedInput::Kind::Polar:
        if (!last) return false;
        addPoint(ctx, inferExact(*last + Pt2::polar(in.a * f, rad(in.b)), last));
        return true;
      case TypedInput::Kind::Number:
      case TypedInput::Kind::Text: {
        if (values().visibleCount() > 0 && typedValuesToBoxes(ctx, text)) return true;
        std::string err;
        auto v = evaluateTyped(ctx, text, false, &err);
        if (!v) {
          ctx.showMessage(err, true);
          return true;
        }
        return typedNumber(ctx, *v);
      }
      default: return false;
    }
  }

 protected:
  virtual std::string prompt() const = 0;
  virtual void preview(ToolContext& ctx, Pt2 cursor) = 0;
  virtual void create(ToolContext& ctx) = 0;
  /// Boxes shown by this tool (all phases); updateValueVisibility() shows the ones for the phase.
  virtual std::vector<ValueField> valueFields() const { return {}; }
  virtual void updateValueVisibility() {}
  /// Move the cursor point so that it honours the locked boxes.
  virtual Pt2 applyLocks(Pt2 p) const { return p; }
  /// A typed single value with no box to take it (legacy fallback: length along the cursor direction).
  virtual bool typedNumber(ToolContext& ctx, double v) {
    if (pts_.empty()) return false;
    Pt2 d = lastCursor_ - pts_.back().point;
    if (d.length() < kEps) d = {1, 0};
    addPoint(ctx, inferExact(pts_.back().point + d.normalized() * v, pts_.back().point));
    return true;
  }
  virtual InferenceResult inferFor(Pt2 c, const ViewerMouseEvent& e) {
    InferenceInput in;
    in.cursor = c;
    if (!pts_.empty()) in.from = pts_.back().point;
    return infer(in, snapSuppressed(e));
  }

  /// With locked boxes the snapped point is replaced by the constrained one (exact hits only).
  InferenceResult constrained(const InferenceResult& r) const {
    if (pts_.empty() || !values().anyLocked()) return r;
    Pt2 p = applyLocks(r.point);
    if (p.near(r.point, kEps)) return r;
    return inferExact(p, pts_.back().point);
  }

  void valuesChanged(ToolContext& ctx) override {
    if (pts_.empty()) return;
    Pt2 p = applyLocks(hasCursor_ ? lastCursor_ : defaultCursor());
    clearTemp();
    preview(ctx, p);
    showValueBoxes(p);
  }

  bool commitValues(ToolContext& ctx) override {
    if (pts_.empty() || !values().anyLocked()) return false;
    Pt2 p = applyLocks(hasCursor_ ? lastCursor_ : defaultCursor());
    addPoint(ctx, inferExact(p, pts_.back().point));
    return true;
  }

  /// Cursor stand-in when nothing was moved yet (typed values only): right of the last point.
  virtual Pt2 defaultCursor() const { return pts_.back().point + Pt2{1, 0}; }

  void addPoint(ToolContext& ctx, const InferenceResult& r) {
    if (!pts_.empty() && r.point.distance(pts_.back().point) < kEps) return;
    pts_.push_back(r);
    if (static_cast<int>(pts_.size()) >= needed_) {
      create(ctx);
      pts_.clear();
      values().resetInput();
    }
    updateValueVisibility();
    clearTemp();
    ctx.setPrompt(prompt());
  }

  /// Dimension for a locked box (no-op when the box is not locked).
  void dimensionFor(ToolContext& ctx, const std::string& id, Constraint c) {
    const ValueField* f = values().field(id);
    if (f && f->locked) addLockedDimension(ctx, *f, std::move(c));
  }

  double displayFactor() const {
    core::UnitSystem u = document() ? document()->units() : core::UnitSystem{};
    return core::lengthUnitToMm(u.length);
  }

  int needed_;
  std::vector<InferenceResult> pts_;
  Pt2 lastCursor_;
  bool hasCursor_ = false;
};

// ---------------------------------------------------------------------------
// Line (chained, closes loops, drag from the last point for a tangent arc)
// ---------------------------------------------------------------------------

/// Dimensions to add to the next line segment (from the boxes or typed @dx,dy / @len<ang).
struct PendingLineDims {
  std::optional<ValueField> length;    ///< Distance (length)
  std::optional<ValueField> angle;     ///< Angle: relative to `referenceLine` or to +X when < 0
  int referenceLine = -1;
  std::optional<Pt2> delta;            ///< @dx,dy: DistanceX / DistanceY (or H/V + length)
};

class LineTool : public SketchTool {
 public:
  LineTool()
      : SketchTool(ToolInfo{.id = "sketch.line",
                            .name = "Line",
                            .tooltip = "Draw lines: click points or type length [Tab] angle; drag from the last point for a tangent arc; double-click or Esc ends the chain",
                            .icon = "sketch-line",
                            .shortcut = "L",
                            .aliases = {"LINE", "L"},
                            .tab = "SKETCH",
                            .group = "CREATE",
                            .order = 1,
                            .promoted = true}) {}

  void activate(ToolContext& ctx) override {
    SketchTool::activate(ctx);
    resetChain();
    defineValues({lengthField("length", "Length"), angleField("angle", "Angle")});
    updateBoxes();
    ctx.setPrompt("Specify first point");
  }

  bool mouseMove(ToolContext& ctx, const ViewerMouseEvent& e) override {
    auto c = cursor(e);
    if (!c) return false;
    clearTemp();
    if (last_ && pressOnLast_ && (e.buttons & 1)) {
      if (!arcMode_ && c->distance(*last_) > tol(*last_, 4.0)) arcMode_ = true;
      if (arcMode_) {
        InferenceResult r = inferFor(*c, e);
        showInference(r);
        bool startIsP = false;
        if (auto arc = tangentArc(*last_, lastDir_.value_or(Pt2{1, 0}), r.point, &startIsP)) {
          previewGeometry(*arc, rubberStyle());
          showDynamicText(r.point, "R" + formatLength(arc->radius()));
        }
        return true;
      }
    }
    InferenceResult r = constrained(inferFor(*c, e));
    lastCursor_ = r.point;
    hasCursor_ = true;
    showInference(r);
    previewSegment(r.point);
    (void)ctx;
    return true;
  }

  bool mousePress(ToolContext& ctx, const ViewerMouseEvent& e) override {
    if (e.button != core::MouseButton::Left) return false;
    auto c = cursor(e);
    if (!c) return true;
    InferenceResult r = inferFor(*c, e);
    if (last_ && r.point.distance(*last_) <= tol(*last_)) {
      pressOnLast_ = true;
      return true;
    }
    if (last_) {
      if (values().isTyping()) values().lockActive();  // a click applies the typed value (Fusion)
      r = constrained(r);
      pending_ = pendingFromBoxes();
    }
    addPoint(ctx, r);
    return true;
  }

  bool mouseRelease(ToolContext& ctx, const ViewerMouseEvent& e) override {
    if (e.button != core::MouseButton::Left) return false;
    if (arcMode_) {
      auto c = cursor(e);
      if (c) finishArc(ctx, inferFor(*c, e));
    }
    arcMode_ = false;
    pressOnLast_ = false;
    clearTemp();
    return true;
  }

  bool mouseDoubleClick(ToolContext& ctx, const ViewerMouseEvent&) override {
    endChain(ctx);
    return true;
  }

  bool keyPress(ToolContext& ctx, const ViewerKeyEvent& e) override {
    if (handleValueKey(ctx, e)) return true;
    if (e.key == Qt::Key_Escape) {
      if (last_) endChain(ctx);
      else cancel(ctx);
      return true;
    }
    if (e.key == Qt::Key_Return || e.key == Qt::Key_Enter) {
      if (last_) endChain(ctx);
      else accept(ctx);
      return true;
    }
    return false;
  }

  bool commandLineInput(ToolContext& ctx, const std::string& text) override {
    TypedInput in = parseTypedInput(text);
    const double f = displayFactor();
    switch (in.kind) {
      case TypedInput::Kind::Pair:
        addPoint(ctx, inferExact(Pt2{in.a * f, in.b * f}, last_));
        return true;
      case TypedInput::Kind::Relative: {
        if (!last_) return false;
        Pt2 d{in.a * f, in.b * f};
        pending_ = PendingLineDims{};
        pending_.delta = d;
        addPoint(ctx, inferExact(*last_ + d, last_));
        return true;
      }
      case TypedInput::Kind::Polar: {
        if (!last_) return false;
        // AutoCAD polar input: absolute angle from +X, length -> both become dimensions
        pending_ = PendingLineDims{};
        ValueField len = lengthField("length", "Length");
        len.value = in.a * f;
        len.locked = true;
        ValueField ang = angleField("angle", "Angle");
        ang.value = in.b;
        ang.locked = true;
        pending_.length = len;
        pending_.angle = ang;
        pending_.referenceLine = -1;
        addPoint(ctx, inferExact(*last_ + Pt2::polar(in.a * f, rad(in.b)), last_));
        return true;
      }
      case TypedInput::Kind::Number:
      case TypedInput::Kind::Text: {
        if (!last_) return false;
        return typedValuesToBoxes(ctx, text);
      }
      default: return false;
    }
  }

 protected:
  void valuesChanged(ToolContext&) override {
    if (!last_) return;
    clearTemp();
    previewSegment(applyLocks(hasCursor_ ? lastCursor_ : *last_ + lastDir_.value_or(Pt2{1, 0})));
  }

  bool commitValues(ToolContext& ctx) override {
    if (!last_ || !values().anyLocked()) return false;
    Pt2 p = applyLocks(hasCursor_ ? lastCursor_ : *last_ + lastDir_.value_or(Pt2{1, 0}));
    pending_ = pendingFromBoxes();
    addPoint(ctx, inferExact(p, last_));
    return true;
  }

 private:
  double displayFactor() const {
    core::UnitSystem u = document() ? document()->units() : core::UnitSystem{};
    return core::lengthUnitToMm(u.length);
  }

  /// Direction the angle box is measured from: the previous line, else +X.
  Pt2 referenceDir() const { return lastLine_ >= 0 && lastDir_ ? *lastDir_ : Pt2{1, 0}; }

  Pt2 applyLocks(Pt2 p) const {
    if (!last_) return p;
    Pt2 d = p - *last_;
    auto len = lockedValue("length");
    auto ang = lockedValue("angle");
    if (ang) {
      Pt2 dir = Pt2::polar(1.0, referenceDir().angle() + rad(*ang));
      double along = len ? *len : std::abs(d.dot(dir));
      if (!len && along < kEps) along = 1.0;
      return *last_ + dir * along;
    }
    if (len) {
      if (d.length() < kEps) d = lastDir_.value_or(Pt2{1, 0});
      return *last_ + d.normalized() * (*len);
    }
    return p;
  }

  InferenceResult constrained(const InferenceResult& r) const {
    if (!last_ || !values().anyLocked()) return r;
    Pt2 p = applyLocks(r.point);
    if (p.near(r.point, kEps)) return r;
    return inferExact(p, last_);
  }

  void previewSegment(Pt2 p) {
    if (!last_) return;
    previewLines({*last_, p}, rubberStyle());
    Pt2 d = p - *last_;
    setValue("length", d.length(), p);
    setValue("angle", deg360(d.angle() - referenceDir().angle()), p);
    showValueBoxes(p);
  }

  void updateBoxes() {
    setValueVisible("length", last_.has_value());
    setValueVisible("angle", last_.has_value());
  }

  PendingLineDims pendingFromBoxes() const {
    PendingLineDims p;
    if (const ValueField* f = values().field("length"); f && f->locked) p.length = *f;
    if (const ValueField* f = values().field("angle"); f && f->locked) p.angle = *f;
    p.referenceLine = lastLine_;
    return p;
  }

  InferenceResult inferFor(Pt2 c, const ViewerMouseEvent& e) {
    InferenceInput in;
    in.cursor = c;
    in.from = last_;
    if (lastDir_) in.referenceDirection = lastDir_;
    if (lastLine_ >= 0) {
      in.referenceCurve = lastLine_;
      in.exclude.insert(lastLine_);
    }
    return infer(in, snapSuppressed(e));
  }

  void resetChain() {
    last_.reset();
    lastRef_.reset();
    lastDir_.reset();
    lastLine_ = -1;
    firstRef_.reset();
    pressOnLast_ = false;
    arcMode_ = false;
    hasCursor_ = false;
    pending_ = {};
  }

  void endChain(ToolContext& ctx) {
    resetChain();
    values().resetInput();
    updateBoxes();
    clearTemp();
    ctx.setPrompt("Specify first point");
  }

  bool isChainStart(const InferenceResult& r) const {
    if (!firstRef_) return false;
    if (r.onPoint && *r.onPoint == *firstRef_) return true;
    const Sketch* sk = sketch();
    if (!sk) return false;
    auto fp = sk->pointOf(*firstRef_);
    return fp && fp->near(r.point, kEps);
  }

  /// Driving dimensions for the segment `id` from pending_ (typed boxes / command line).
  /// addLockedDimension for a chain segment, except that a typed value the geometry already has
  /// through other constraints is not added again as a duplicate driven dimension (SK-12).
  void lineDim(ToolContext& ctx, const ValueField& f, Constraint c) {
    Sketch* sk = sketch();
    if (!sk) return;
    Sketch probe = *sk;
    Constraint pc = c;
    pc.value = f.value;
    const int pid = probe.addConstraint(pc);
    const SolveResult r = probe.solve();
    const bool redundant = std::count(r.redundant.begin(), r.redundant.end(), pid) > 0;
    if (redundant) {
      auto m = sk->measure(pc);
      if (m && std::abs(*m - f.value) < 1e-6) return;
    }
    addLockedDimension(ctx, f, std::move(c));
  }

  void addPendingDims(ToolContext& ctx, int id) {
    PendingLineDims p = pending_;
    pending_ = {};
    Sketch* sk = sketch();
    if (!sk) return;
    if (p.delta) {
      Pt2 d = *p.delta;
      ValueField f = lengthField("length", "Length");
      f.locked = true;
      if (std::abs(d.x) < kEps || std::abs(d.y) < kEps) {
        // axis-aligned: H/V (from inference, re-added here to be safe) + the length
        sk->addConstraint(Constraint{.type = std::abs(d.y) < kEps ? ConstraintType::Horizontal : ConstraintType::Vertical, .a = {id}});
        f.value = d.length();
        lineDim(ctx, f, Constraint{.type = ConstraintType::Distance, .a = {id}});
      } else {
        f.value = d.x;
        lineDim(ctx, f, Constraint{.type = ConstraintType::DistanceX, .a = {id, PointPos::Start}, .b = {id, PointPos::End}});
        f.value = d.y;
        lineDim(ctx, f, Constraint{.type = ConstraintType::DistanceY, .a = {id, PointPos::Start}, .b = {id, PointPos::End}});
      }
      return;
    }
    if (p.length) lineDim(ctx, *p.length, Constraint{.type = ConstraintType::Distance, .a = {id}});
    if (p.angle) {
      if (addRightAngleInstead(ctx, id, p.angle->value, p.referenceLine)) return;
      Constraint c{.type = ConstraintType::Angle};
      ValueField f = *p.angle;
      double v = signedDeg(std::fmod(std::fmod(f.value, 360.0) + 360.0, 360.0));
      if (p.referenceLine >= 0) {
        c.a = {p.referenceLine};
        c.b = {id};
        if (v < 0) {  // Sketch::measure(Angle a,b) is the ccw angle from a to b: keep it positive
          std::swap(c.a, c.b);
          v = -v;
        }
      } else {
        c.a = {id};
      }
      f.value = v;
      lineDim(ctx, f, c);
    }
  }

  void addPoint(ToolContext& ctx, const InferenceResult& r) {
    Sketch* sk = sketch();
    if (!sk || !session()) return;
    Pt2 p = r.point;
    if (!last_) {
      last_ = p;
      startInf_ = coincidenceOnly(r);
      updateBoxes();
      ctx.setPrompt("Specify next point (or type length, Tab, angle)");
      return;
    }
    if (p.distance(*last_) < kEps) return;
    // a segment straight back over the previous one would only overlap it (SK-12)
    if (lastLine_ >= 0 && lastDir_) {
      const Pt2 d = (p - *last_).normalized();
      const Pt2 prev = lastDir_->normalized();
      if (std::abs(prev.cross(d)) < 1e-6 && prev.dot(d) < 0) {
        ctx.showMessage("Segment would retrace the previous line: not added", true);
        pending_ = {};
        values().resetInput();
        updateBoxes();
        return;
      }
    }
    session()->beginEdit();
    int id = sk->addGeometry(asCreated(Geometry::makeLine(*last_, p)));
    if (lastRef_) sk->addConstraint(Constraint{.type = ConstraintType::Coincident, .a = {id, PointPos::Start}, .b = *lastRef_});
    else applyInference(*sk, startInf_, {id, PointPos::Start}, -1);
    bool closing = isChainStart(r);
    InferenceResult er = r;
    if (closing && !er.onPoint) er.onPoint = firstRef_;
    if (pending_.angle) er = coincidenceOnly(er);  // the angle dimension decides the direction
    applyInference(*sk, er, {id, PointPos::End}, id);
    addPendingDims(ctx, id);
    session()->commitEdit("Line");
    values().resetInput();
    const Geometry* g = sk->geometry(id);
    if (!g || closing) {
      endChain(ctx);
      return;
    }
    if (!firstRef_) firstRef_ = GeoRef{id, PointPos::Start};
    last_ = g->end();
    lastRef_ = GeoRef{id, PointPos::End};
    lastDir_ = g->direction();
    lastLine_ = id;
    hasCursor_ = false;
    ctx.setPrompt("Specify next point (double-click or Esc to end)");
  }

  void finishArc(ToolContext& ctx, const InferenceResult& r) {
    Sketch* sk = sketch();
    if (!sk || !session() || !last_) return;
    Pt2 t = lastDir_.value_or(Pt2{1, 0});
    bool startIsP = false;
    auto arc = tangentArc(*last_, t, r.point, &startIsP);
    if (!arc) return;
    session()->beginEdit();
    int id = sk->addGeometry(asCreated(*arc));
    GeoRef arcP{id, startIsP ? PointPos::Start : PointPos::End};
    GeoRef arcQ{id, startIsP ? PointPos::End : PointPos::Start};
    if (lastRef_) {
      sk->addConstraint(Constraint{.type = ConstraintType::Coincident, .a = arcP, .b = *lastRef_});
      sk->addConstraint(Constraint{.type = ConstraintType::Tangent, .a = *lastRef_, .b = arcP});
    } else {
      applyInference(*sk, startInf_, arcP, -1);
    }
    bool closing = isChainStart(r);
    InferenceResult er = coincidenceOnly(r);
    if (closing && !er.onPoint) er.onPoint = firstRef_;
    applyInference(*sk, er, arcQ, -1);
    session()->commitEdit("Tangent arc");
    values().resetInput();
    const Geometry* g = sk->geometry(id);
    if (!g || closing) {
      endChain(ctx);
      return;
    }
    if (!firstRef_) firstRef_ = arcP;
    last_ = g->point(arcQ.pos);
    lastRef_ = arcQ;
    lastDir_ = outwardTangent(*g, arcQ.pos);
    lastLine_ = -1;
    hasCursor_ = false;
  }

  std::optional<Pt2> last_;
  std::optional<GeoRef> lastRef_;
  std::optional<Pt2> lastDir_;
  int lastLine_ = -1;
  std::optional<GeoRef> firstRef_;
  InferenceResult startInf_;
  Pt2 lastCursor_;
  bool hasCursor_ = false;
  bool pressOnLast_ = false;
  bool arcMode_ = false;
  PendingLineDims pending_;
};

// ---------------------------------------------------------------------------
// Rectangles
// ---------------------------------------------------------------------------

enum class RectMode { TwoPoint, Center, ThreePoint };

class RectangleTool : public PointsTool {
 public:
  RectangleTool(RectMode mode, ToolInfo info) : PointsTool(std::move(info), mode == RectMode::ThreePoint ? 3 : 2), mode_(mode) {}

 protected:
  std::string prompt() const override {
    switch (mode_) {
      case RectMode::TwoPoint: return pts_.empty() ? "Specify first corner" : "Specify opposite corner (or type width, Tab, height)";
      case RectMode::Center: return pts_.empty() ? "Specify center" : "Specify corner (or type width, Tab, height)";
      case RectMode::ThreePoint:
        if (pts_.empty()) return "Specify first corner";
        if (pts_.size() == 1) return "Specify second corner (or type the width)";
        return "Specify height (or type a value)";
    }
    return {};
  }

  std::vector<ValueField> valueFields() const override { return {lengthField("width", "Width"), lengthField("height", "Height")}; }

  void updateValueVisibility() override {
    if (mode_ == RectMode::ThreePoint) {
      setValueVisible("width", pts_.size() == 1);
      setValueVisible("height", pts_.size() == 2);
    } else {
      setValueVisible("width", pts_.size() == 1);
      setValueVisible("height", pts_.size() == 1);
    }
  }

  Pt2 applyLocks(Pt2 p) const override {
    auto w = lockedValue("width");
    auto h = lockedValue("height");
    if (mode_ == RectMode::ThreePoint) {
      if (pts_.size() == 1 && w) {
        Pt2 d = p - pts_[0].point;
        if (d.length() < kEps) d = {1, 0};
        return pts_[0].point + d.normalized() * (*w);
      }
      if (pts_.size() == 2 && h) {
        Pt2 n = (pts_[1].point - pts_[0].point).normalized().perp();
        double side = sign((p - pts_[0].point).dot(n));
        return pts_[1].point + n * (side * (*h));
      }
      return p;
    }
    if (pts_.size() != 1) return p;
    Pt2 a = pts_[0].point;
    Pt2 d = p - a;
    double k = mode_ == RectMode::Center ? 0.5 : 1.0;
    if (w) d.x = sign(d.x) * (*w) * k;
    if (h) d.y = sign(d.y) * (*h) * k;
    return a + d;
  }

  std::optional<std::array<Pt2, 4>> corners(const std::vector<Pt2>& p) const {
    switch (mode_) {
      case RectMode::TwoPoint:
      case RectMode::Center: {
        if (p.size() < 2) return std::nullopt;
        Pt2 a = p[0], b = p[1];
        if (mode_ == RectMode::Center) a = p[0] * 2.0 - b;
        if (std::abs(b.x - a.x) < kEps || std::abs(b.y - a.y) < kEps) return std::nullopt;
        return std::array<Pt2, 4>{a, Pt2{b.x, a.y}, b, Pt2{a.x, b.y}};
      }
      case RectMode::ThreePoint: {
        if (p.size() < 3) return std::nullopt;
        Pt2 d = p[1] - p[0];
        if (d.length() < kEps) return std::nullopt;
        Pt2 n = d.normalized().perp();
        double w = (p[2] - p[0]).dot(n);
        if (std::abs(w) < kEps) return std::nullopt;
        return std::array<Pt2, 4>{p[0], p[1], p[1] + n * w, p[0] + n * w};
      }
    }
    return std::nullopt;
  }

  std::vector<Pt2> pointsPlus(Pt2 c) const {
    std::vector<Pt2> p;
    for (const auto& r : pts_) p.push_back(r.point);
    p.push_back(c);
    return p;
  }

  void preview(ToolContext&, Pt2 c) override {
    if (pts_.empty()) return;
    auto cs = corners(pointsPlus(c));
    if (!cs) {
      if (mode_ == RectMode::ThreePoint && pts_.size() == 1) {
        previewLines({pts_[0].point, c}, rubberStyle());
        setValue("width", pts_[0].point.distance(c), c);
      }
      return;
    }
    previewPolyline({(*cs)[0], (*cs)[1], (*cs)[2], (*cs)[3], (*cs)[0]}, rubberStyle());
    if (mode_ == RectMode::ThreePoint) {
      setValue("width", (*cs)[1].distance((*cs)[0]), c);
      setValue("height", (*cs)[2].distance((*cs)[1]), c);
      return;
    }
    // Fusion style: Width at the middle of a horizontal edge and Height at the middle of a
    // vertical edge, just outside the shape; not stacked at the cursor, where the second click
    // would land on the (lagging) boxes instead of the viewer and focus Height (UX-31)
    const std::array<Pt2, 4>& q = *cs;
    const Pt2 mid = (q[0] + q[2]) * 0.5;
    auto outside = [&](Pt2 a, Pt2 b) {
      Pt2 m = (a + b) * 0.5;
      Pt2 n = (m - mid).normalized();
      return m + n * tol(m, 12.0);
    };
    // the edges through the first corner: away from the cursor at the opposite corner
    setValue("width", q[1].distance(q[0]), outside(q[0], q[1]));
    setValue("height", q[2].distance(q[1]), outside(q[0], q[3]));
  }

  void create(ToolContext& ctx) override {
    Sketch* sk = sketch();
    std::vector<Pt2> p;
    for (const auto& r : pts_) p.push_back(r.point);
    auto cs = corners(p);
    if (!sk || !session() || !cs) {
      ctx.showMessage("Degenerate rectangle ignored", true);
      return;
    }
    session()->beginEdit();
    int l[4];
    for (int i = 0; i < 4; ++i) l[i] = sk->addGeometry(asCreated(Geometry::makeLine((*cs)[i], (*cs)[(i + 1) % 4])));
    for (int i = 0; i < 4; ++i)
      sk->addConstraint(Constraint{.type = ConstraintType::Coincident, .a = {l[i], PointPos::End}, .b = {l[(i + 1) % 4], PointPos::Start}});
    if (mode_ == RectMode::ThreePoint) {
      for (int i = 0; i < 3; ++i)
        sk->addConstraint(Constraint{.type = ConstraintType::Perpendicular, .a = {l[i]}, .b = {l[i + 1]}});
      applyInference(*sk, coincidenceOnly(pts_[0]), {l[0], PointPos::Start}, -1);
      applyInference(*sk, coincidenceOnly(pts_[1]), {l[0], PointPos::End}, -1);
    } else {
      sk->addConstraint(Constraint{.type = ConstraintType::Horizontal, .a = {l[0]}});
      sk->addConstraint(Constraint{.type = ConstraintType::Horizontal, .a = {l[2]}});
      sk->addConstraint(Constraint{.type = ConstraintType::Vertical, .a = {l[1]}});
      sk->addConstraint(Constraint{.type = ConstraintType::Vertical, .a = {l[3]}});
      if (mode_ == RectMode::TwoPoint) {
        applyInference(*sk, coincidenceOnly(pts_[0]), {l[0], PointPos::Start}, -1);
        applyInference(*sk, coincidenceOnly(pts_[1]), {l[1], PointPos::End}, -1);
      } else {
        Geometry cp = Geometry::makePoint(pts_[0].point);
        cp.construction = true;
        int c = sk->addGeometry(cp);
        sk->addConstraint(Constraint{.type = ConstraintType::Symmetric, .a = {l[0], PointPos::Start}, .b = {l[1], PointPos::End}, .c = {c, PointPos::Center}});
        sk->addConstraint(Constraint{.type = ConstraintType::Symmetric, .a = {l[0], PointPos::End}, .b = {l[2], PointPos::End}, .c = {c, PointPos::Center}});
        applyInference(*sk, coincidenceOnly(pts_[0]), {c, PointPos::Center}, -1);
        applyInference(*sk, coincidenceOnly(pts_[1]), {l[1], PointPos::End}, -1);
      }
    }
    // typed width / height -> driving dimensions on the first two edges
    dimensionFor(ctx, "width", Constraint{.type = ConstraintType::Distance, .a = {l[0]}});
    dimensionFor(ctx, "height", Constraint{.type = ConstraintType::Distance, .a = {l[1]}});
    session()->commitEdit("Rectangle");
  }

 private:
  RectMode mode_;
};

// ---------------------------------------------------------------------------
// Circles
// ---------------------------------------------------------------------------

enum class CircleMode { CenterDiameter, TwoPoint, ThreePoint };

class CircleTool : public PointsTool {
 public:
  CircleTool(CircleMode mode, ToolInfo info) : PointsTool(std::move(info), mode == CircleMode::ThreePoint ? 3 : 2), mode_(mode) {}

 protected:
  std::string prompt() const override {
    switch (mode_) {
      case CircleMode::CenterDiameter: return pts_.empty() ? "Specify center point" : "Specify diameter (or type a value)";
      case CircleMode::TwoPoint: return pts_.empty() ? "Specify first point on the diameter" : "Specify second point (or type the diameter)";
      case CircleMode::ThreePoint: return pts_.size() < 2 ? "Specify point on the circle" : "Specify third point";
    }
    return {};
  }

  std::vector<ValueField> valueFields() const override {
    if (mode_ == CircleMode::ThreePoint) return {};
    return {lengthField("diameter", "Diameter")};
  }

  void updateValueVisibility() override {
    if (mode_ != CircleMode::ThreePoint) setValueVisible("diameter", pts_.size() == 1);
  }

  Pt2 applyLocks(Pt2 p) const override {
    auto dia = lockedValue("diameter");
    if (!dia || pts_.size() != 1) return p;
    Pt2 d = p - pts_[0].point;
    if (d.length() < kEps) d = {1, 0};
    double dist = mode_ == CircleMode::CenterDiameter ? *dia / 2 : *dia;
    return pts_[0].point + d.normalized() * dist;
  }

  std::optional<Geometry> circleFor(const std::vector<Pt2>& p) const {
    switch (mode_) {
      case CircleMode::CenterDiameter: {
        if (p.size() < 2) return std::nullopt;
        double r = p[0].distance(p[1]);
        if (r < kEps) return std::nullopt;
        return Geometry::makeCircle(p[0], r);
      }
      case CircleMode::TwoPoint: {
        if (p.size() < 2) return std::nullopt;
        double r = p[0].distance(p[1]) / 2;
        if (r < kEps) return std::nullopt;
        return Geometry::makeCircle((p[0] + p[1]) * 0.5, r);
      }
      case CircleMode::ThreePoint: {
        if (p.size() < 3) return std::nullopt;
        auto c = circumcenter(p[0], p[1], p[2]);
        if (!c) return std::nullopt;
        return Geometry::makeCircle(*c, c->distance(p[0]));
      }
    }
    return std::nullopt;
  }

  void preview(ToolContext&, Pt2 c) override {
    if (pts_.empty()) return;
    std::vector<Pt2> p;
    for (const auto& r : pts_) p.push_back(r.point);
    p.push_back(c);
    if (auto g = circleFor(p)) {
      previewGeometry(*g, rubberStyle());
      if (mode_ == CircleMode::ThreePoint) showDynamicText(c, "⌀" + formatLength(2 * g->radius()));
      else setValue("diameter", 2 * g->radius(), c);
    } else if (pts_.size() == 1) {
      previewLines({pts_[0].point, c}, rubberStyle());
    }
  }

  void create(ToolContext& ctx) override {
    Sketch* sk = sketch();
    std::vector<Pt2> p;
    for (const auto& r : pts_) p.push_back(r.point);
    auto g = circleFor(p);
    if (!sk || !session() || !g) {
      ctx.showMessage("Degenerate circle ignored", true);
      return;
    }
    session()->beginEdit();
    int id = sk->addGeometry(asCreated(*g));
    if (mode_ == CircleMode::CenterDiameter) {
      applyInference(*sk, coincidenceOnly(pts_[0]), {id, PointPos::Center}, -1);
      if (pts_[1].onPoint) sk->addConstraint(Constraint{.type = ConstraintType::PointOnCurve, .a = *pts_[1].onPoint, .b = {id}});
    } else {
      for (const auto& r : pts_)
        if (r.onPoint) sk->addConstraint(Constraint{.type = ConstraintType::PointOnCurve, .a = *r.onPoint, .b = {id}});
    }
    dimensionFor(ctx, "diameter", Constraint{.type = ConstraintType::Diameter, .a = {id}});
    session()->commitEdit("Circle");
  }

 private:
  CircleMode mode_;
};

// ---------------------------------------------------------------------------
// Arcs
// ---------------------------------------------------------------------------

enum class ArcMode { ThreePoint, Center, Tangent };

class ArcTool : public PointsTool {
 public:
  ArcTool(ArcMode mode, ToolInfo info) : PointsTool(std::move(info), mode == ArcMode::Tangent ? 2 : 3), mode_(mode) {}

 protected:
  std::string prompt() const override {
    switch (mode_) {
      case ArcMode::ThreePoint:
        if (pts_.empty()) return "Specify start point";
        if (pts_.size() == 1) return "Specify end point";
        return "Specify a point on the arc (or type the radius)";
      case ArcMode::Center:
        if (pts_.empty()) return "Specify center point";
        if (pts_.size() == 1) return "Specify start point (or type the radius)";
        return "Specify end point (or type the sweep angle)";
      case ArcMode::Tangent: return pts_.empty() ? "Select the end point of a line or arc" : "Specify end point (or type radius, Tab, angle)";
    }
    return {};
  }

  std::vector<ValueField> valueFields() const override { return {lengthField("radius", "Radius"), angleField("sweep", "Angle")}; }

  void updateValueVisibility() override {
    switch (mode_) {
      case ArcMode::ThreePoint:
        setValueVisible("radius", pts_.size() == 2);
        setValueVisible("sweep", false);
        break;
      case ArcMode::Center:
        setValueVisible("radius", pts_.size() == 1);
        setValueVisible("sweep", pts_.size() == 2);
        break;
      case ArcMode::Tangent:
        setValueVisible("radius", pts_.size() == 1);
        setValueVisible("sweep", pts_.size() == 1);
        break;
    }
  }

  Pt2 applyLocks(Pt2 p) const override {
    auto R = lockedValue("radius");
    auto S = lockedValue("sweep");
    switch (mode_) {
      case ArcMode::Center: {
        if (pts_.size() == 1 && R) {
          Pt2 d = p - pts_[0].point;
          if (d.length() < kEps) d = {1, 0};
          return pts_[0].point + d.normalized() * (*R);
        }
        if (pts_.size() == 2 && S) {
          Pt2 c = pts_[0].point, s = pts_[1].point;
          double r = c.distance(s), a0 = (s - c).angle();
          bool ccw = (s - c).cross(p - c) >= 0;
          return c + Pt2::polar(r, a0 + (ccw ? rad(*S) : -rad(*S)));
        }
        return p;
      }
      case ArcMode::ThreePoint: {
        if (pts_.size() != 2 || !R) return p;
        Pt2 a = pts_[0].point, b = pts_[1].point;
        Pt2 m = (a + b) * 0.5;
        double half = a.distance(b) / 2;
        if (half < kEps) return p;
        double r = std::max(*R, half);
        double h = std::sqrt(std::max(0.0, r * r - half * half));
        Pt2 n = (b - a).normalized().perp();
        double side = sign((p - m).dot(n));
        bool minor = std::abs((p - m).dot(n)) <= r;
        return m + n * (side * (minor ? r - h : r + h));
      }
      case ArcMode::Tangent: {
        if (pts_.size() != 1 || !tangent_ || (!R && !S)) return p;
        Pt2 s = pts_[0].point, t = tangent_->normalized();
        Pt2 n = t.perp();
        double side = sign(t.cross(p - s));
        if (side == 0) side = 1;
        double r;
        if (R) r = *R;
        else {
          bool sp = false;
          auto arc = tangentArc(s, t, p, &sp);
          if (!arc) return p;
          r = arc->radius();
        }
        Pt2 c = s + n * (side * r);
        double a0 = (s - c).angle();
        double sweep;
        if (S) sweep = rad(*S);
        else {
          sweep = std::abs(angleDelta(a0, (p - c).angle()));
          if (sweep < kEps) sweep = kPi / 2;
        }
        return c + Pt2::polar(r, a0 + side * sweep);
      }
    }
    return p;
  }

  std::optional<Geometry> arcFor(const std::vector<Pt2>& p, bool* startIsFirst = nullptr) const {
    if (startIsFirst) *startIsFirst = true;
    switch (mode_) {
      case ArcMode::ThreePoint: {
        if (p.size() < 3) return std::nullopt;
        auto a = Geometry::makeArc3Points(p[0], p[2], p[1]);
        if (a && startIsFirst) *startIsFirst = a->start().near(p[0], 1e-6);
        return a;
      }
      case ArcMode::Center: {
        if (p.size() < 3) return std::nullopt;
        double r = p[0].distance(p[1]);
        if (r < kEps) return std::nullopt;
        double a0 = (p[1] - p[0]).angle(), a1 = (p[2] - p[0]).angle();
        if (std::abs(normalizeAngle(a1 - a0)) < kEps) return std::nullopt;
        bool ccw = (p[1] - p[0]).cross(p[2] - p[0]) >= 0;
        if (startIsFirst) *startIsFirst = ccw;
        return ccw ? Geometry::makeArc(p[0], r, a0, a1) : Geometry::makeArc(p[0], r, a1, a0);
      }
      case ArcMode::Tangent: {
        if (p.size() < 2 || !tangent_) return std::nullopt;
        bool sp = false;
        auto a = tangentArc(p[0], *tangent_, p[1], &sp);
        if (startIsFirst) *startIsFirst = sp;
        return a;
      }
    }
    return std::nullopt;
  }

  InferenceResult inferFor(Pt2 c, const ViewerMouseEvent& e) override {
    InferenceInput in;
    in.cursor = c;
    if (mode_ == ArcMode::Tangent && pts_.empty()) {
      in.allowCurves = false;
      in.allowAlignment = false;
      in.allowDirections = false;
    } else if (!pts_.empty()) {
      in.from = pts_.back().point;
    }
    return infer(in, snapSuppressed(e));
  }

  void preview(ToolContext&, Pt2 c) override {
    if (pts_.empty()) return;
    std::vector<Pt2> p;
    for (const auto& r : pts_) p.push_back(r.point);
    p.push_back(c);
    if (auto g = arcFor(p)) {
      previewGeometry(*g, rubberStyle());
      setValue("radius", g->radius(), c);
      setValue("sweep", deg(g->sweep()), c);
    } else if (pts_.size() == 1) {
      previewLines({pts_[0].point, c}, rubberStyle());
      if (mode_ == ArcMode::Center) setValue("radius", pts_[0].point.distance(c), c);
    } else if (mode_ == ArcMode::ThreePoint && pts_.size() == 2) {
      previewLines({pts_[0].point, pts_[1].point}, guideStyle());
    }
  }

  void addPoint(ToolContext& ctx, const InferenceResult& r) {
    if (mode_ == ArcMode::Tangent && pts_.empty()) {
      // the first point must be a free end of a line or arc
      const Sketch* sk = sketch();
      const Geometry* g = (sk && r.onPoint) ? sk->geometry(r.onPoint->geo) : nullptr;
      if (!g || (g->type != GeomType::Line && g->type != GeomType::Arc) ||
          (r.onPoint->pos != PointPos::Start && r.onPoint->pos != PointPos::End)) {
        ctx.showMessage("Select the end point of a line or arc to start a tangent arc", true);
        return;
      }
      tangent_ = outwardTangent(*g, r.onPoint->pos);
      startRef_ = *r.onPoint;
    }
    PointsTool::addPoint(ctx, r);
  }

  bool mousePress(ToolContext& ctx, const ViewerMouseEvent& e) override {
    if (e.button != core::MouseButton::Left) return false;
    auto c = cursor(e);
    if (!c) return true;
    if (values().isTyping()) values().lockActive();
    addPoint(ctx, constrained(inferFor(*c, e)));
    return true;
  }

  void create(ToolContext& ctx) override {
    Sketch* sk = sketch();
    std::vector<Pt2> p;
    for (const auto& r : pts_) p.push_back(r.point);
    bool startIsFirst = true;
    auto g = arcFor(p, &startIsFirst);
    if (!sk || !session() || !g) {
      ctx.showMessage("Degenerate arc ignored", true);
      return;
    }
    session()->beginEdit();
    int id = sk->addGeometry(asCreated(*g));
    const Geometry* created = sk->geometry(id);
    auto endOf = [&](Pt2 pt) { return GeoRef{id, endMatching(*created, pt)}; };
    switch (mode_) {
      case ArcMode::ThreePoint:
        applyInference(*sk, coincidenceOnly(pts_[0]), endOf(p[0]), -1);
        applyInference(*sk, coincidenceOnly(pts_[1]), endOf(p[1]), -1);
        if (pts_[2].onPoint) sk->addConstraint(Constraint{.type = ConstraintType::PointOnCurve, .a = *pts_[2].onPoint, .b = {id}});
        break;
      case ArcMode::Center:
        applyInference(*sk, coincidenceOnly(pts_[0]), {id, PointPos::Center}, -1);
        applyInference(*sk, coincidenceOnly(pts_[1]), endOf(p[1]), -1);
        applyInference(*sk, coincidenceOnly(pts_[2]), endOf(created->point(endMatching(*created, p[1]) == PointPos::Start ? PointPos::End : PointPos::Start)), -1);
        break;
      case ArcMode::Tangent: {
        GeoRef arcP = endOf(p[0]);
        GeoRef arcQ{id, arcP.pos == PointPos::Start ? PointPos::End : PointPos::Start};
        sk->addConstraint(Constraint{.type = ConstraintType::Coincident, .a = arcP, .b = startRef_});
        sk->addConstraint(Constraint{.type = ConstraintType::Tangent, .a = startRef_, .b = arcP});
        applyInference(*sk, coincidenceOnly(pts_[1]), arcQ, -1);
        break;
      }
    }
    dimensionFor(ctx, "radius", Constraint{.type = ConstraintType::Radius, .a = {id}});
    dimensionFor(ctx, "sweep", Constraint{.type = ConstraintType::Angle, .a = {id}});
    session()->commitEdit("Arc");
  }

 private:
  ArcMode mode_;
  std::optional<Pt2> tangent_;
  GeoRef startRef_;
};

// ---------------------------------------------------------------------------
// Polygon (center + vertex / edge midpoint)
// ---------------------------------------------------------------------------

class PolygonTool : public PointsTool {
 public:
  PolygonTool(bool inscribed, ToolInfo info, bool edge = false)
      : PointsTool(std::move(info), 2), inscribed_(inscribed || edge), edge_(edge) {}

  void activate(ToolContext& ctx) override {
    if (!ctx.args.empty()) {
      try {
        int n = std::stoi(ctx.args[0]);
        if (n >= 3 && n <= 128) sides_ = n;
      } catch (...) {
      }
    }
    PointsTool::activate(ctx);
    values().setMeasured("sides", sides_);
  }

 protected:
  std::string prompt() const override {
    if (edge_) {
      if (pts_.empty()) return std::format("Specify first edge point ({} sides; type a number of sides to change)", sides_);
      return "Specify second edge point (the polygon lies to the left; or type the edge length)";
    }
    if (pts_.empty()) return std::format("Specify center ({} sides; type a number of sides to change)", sides_);
    return inscribed_ ? "Specify a vertex (or type the radius)" : "Specify the midpoint of an edge (or type the radius)";
  }

  std::vector<ValueField> valueFields() const override {
    ValueField sides{.id = "sides", .label = "Sides", .kind = ValueKind::Count};
    sides.value = sides_;
    return {sides, edge_ ? lengthField("radius", "Edge") : lengthField("radius", "Radius")};
  }

  void updateValueVisibility() override {
    setValueVisible("sides", true);
    setValueVisible("radius", pts_.size() == 1);
    // after the centre click the size is what the user types next (Fusion); the sides box stays
    // reachable with Shift+Tab
    if (pts_.size() == 1) values().setActive("radius");
    // the sides box is anchored at the cursor, then the centre (preview()); until the mouse moves
    // it starts at the last cursor position
    if (pts_.empty() && session() && hasCursor_) values().setAnchor("sides", session()->toWorld(lastCursor_));
    else if (pts_.empty() && session()) values().setAnchor("sides", session()->toWorld({0, 0}));
  }

  Pt2 applyLocks(Pt2 p) const override {
    auto R = lockedValue("radius");
    if (!R || pts_.size() != 1) return p;
    Pt2 d = p - pts_[0].point;
    if (d.length() < kEps) d = {1, 0};
    return pts_[0].point + d.normalized() * (*R);
  }

  int effectiveSides() const {
    if (auto s = lockedValue("sides")) {
      int n = static_cast<int>(std::lround(*s));
      if (n >= 3 && n <= 128) return n;
    }
    return sides_;
  }

  /// Edge mode: centre of the polygon whose first edge runs p0 -> p1 (centre on the left).
  Pt2 edgeCentre(Pt2 p0, Pt2 p1) const {
    const int n = effectiveSides();
    Pt2 d = p1 - p0;
    return (p0 + p1) * 0.5 + d.normalized().perp() * (d.length() / 2 / std::tan(kPi / n));
  }

  std::vector<Pt2> vertices(Pt2 c, Pt2 v, double* circleRadius) const {
    std::vector<Pt2> out;
    if (edge_) {
      // c, v are the edge endpoints
      if (c.distance(v) < kEps) return out;
      Pt2 ctr = edgeCentre(c, v);
      const int n = effectiveSides();
      double r = ctr.distance(c), a0 = (c - ctr).angle();
      if (circleRadius) *circleRadius = r;
      for (int i = 0; i < n; ++i) out.push_back(ctr + Pt2::polar(r, a0 + 2 * kPi / n * i));
      return out;
    }
    double d = c.distance(v);
    if (d < kEps) return out;
    const int n = effectiveSides();
    double step = 2 * kPi / n;
    double r = inscribed_ ? d : d / std::cos(step / 2);
    double a0 = (v - c).angle() + (inscribed_ ? 0.0 : step / 2);
    if (circleRadius) *circleRadius = inscribed_ ? r : d;
    for (int i = 0; i < n; ++i) out.push_back(c + Pt2::polar(r, a0 + step * i));
    return out;
  }

  void preview(ToolContext&, Pt2 c) override {
    // the sides box follows the cursor until the centre is placed, then stays at the centre
    // (the radius box follows the cursor) (SK-11)
    if (pts_.empty()) {
      setValue("sides", effectiveSides(), c);
      return;
    }
    setValue("sides", effectiveSides(), pts_[0].point);
    auto vs = vertices(pts_[0].point, c, nullptr);
    if (vs.empty()) return;
    vs.push_back(vs.front());
    previewPolyline(vs, rubberStyle());
    setValue("radius", pts_[0].point.distance(c), c);
    if (edge_) previewGeometry(Geometry::makeCircle(edgeCentre(pts_[0].point, c), vs[0].distance(edgeCentre(pts_[0].point, c))), constructionStyle());
  }

  bool commitValues(ToolContext& ctx) override {
    if (pts_.empty()) {
      // Enter on a typed side count before the centre is picked: just adopt it
      if (auto s = lockedValue("sides")) {
        int n = static_cast<int>(std::lround(*s));
        if (n < 3 || n > 128) {
          ctx.showMessage("A polygon needs 3 to 128 sides", true);
          values().unlockAll();
          return true;
        }
        sides_ = n;
        values().unlock(values().indexOf("sides"));
        values().setMeasured("sides", sides_);
        ctx.setPrompt(prompt());
        return true;
      }
      return false;
    }
    return PointsTool::commitValues(ctx);
  }

  bool typedNumber(ToolContext& ctx, double v) override {
    if (pts_.empty()) {
      int n = static_cast<int>(std::lround(v));
      if (std::abs(v - n) < kEps && n >= 3 && n <= 128) {
        sides_ = n;
        values().setMeasured("sides", sides_);
        ctx.setPrompt(prompt());
        return true;
      }
      return false;
    }
    return PointsTool::typedNumber(ctx, v);
  }

  void create(ToolContext& ctx) override {
    Sketch* sk = sketch();
    if (!sk || !session()) return;
    double R = 0;
    auto vs = vertices(pts_[0].point, pts_[1].point, &R);
    if (vs.empty()) {
      ctx.showMessage("Degenerate polygon ignored", true);
      return;
    }
    sides_ = effectiveSides();
    session()->beginEdit();
    const int n = sides_;
    std::vector<int> l(n);
    for (int i = 0; i < n; ++i) l[i] = sk->addGeometry(asCreated(Geometry::makeLine(vs[i], vs[(i + 1) % n])));
    Geometry circle = Geometry::makeCircle(edge_ ? edgeCentre(pts_[0].point, pts_[1].point) : pts_[0].point, R);
    circle.construction = true;
    int c = sk->addGeometry(circle);
    for (int i = 0; i < n; ++i) {
      sk->addConstraint(Constraint{.type = ConstraintType::Coincident, .a = {l[i], PointPos::End}, .b = {l[(i + 1) % n], PointPos::Start}});
      if (i > 0) sk->addConstraint(Constraint{.type = ConstraintType::Equal, .a = {l[0]}, .b = {l[i]}});
      if (inscribed_) sk->addConstraint(Constraint{.type = ConstraintType::PointOnCurve, .a = {l[i], PointPos::Start}, .b = {c}, .internal = true});
      else sk->addConstraint(Constraint{.type = ConstraintType::Tangent, .a = {l[i]}, .b = {c}, .internal = true});
    }
    if (edge_) {
      applyInference(*sk, coincidenceOnly(pts_[0]), {l[0], PointPos::Start}, -1);
      applyInference(*sk, coincidenceOnly(pts_[1]), {l[0], PointPos::End}, -1);
      if (pts_[1].horizontal) sk->addConstraint(Constraint{.type = ConstraintType::Horizontal, .a = {l[0]}});
      else if (pts_[1].vertical) sk->addConstraint(Constraint{.type = ConstraintType::Vertical, .a = {l[0]}});
      // typed edge length -> length of the first edge
      dimensionFor(ctx, "radius", Constraint{.type = ConstraintType::Distance, .a = {l[0]}});
      session()->commitEdit("Polygon");
      return;
    }
    applyInference(*sk, coincidenceOnly(pts_[0]), {c, PointPos::Center}, -1);
    if (inscribed_) applyInference(*sk, coincidenceOnly(pts_[1]), {l[0], PointPos::Start}, -1);
    // typed size -> radius dimension on the construction circle (circumscribed: inscribed circle)
    dimensionFor(ctx, "radius", Constraint{.type = ConstraintType::Radius, .a = {c}});
    session()->commitEdit("Polygon");
  }

 private:
  bool inscribed_;
  bool edge_ = false;
  int sides_ = 6;
};

// ---------------------------------------------------------------------------
// Slot (center to center)
// ---------------------------------------------------------------------------

enum class SlotMode { CenterToCenter, Overall, CenterPoint };

class SlotTool : public PointsTool {
 public:
  SlotTool()
      : SlotTool(SlotMode::CenterToCenter,
                 ToolInfo{.id = "sketch.slot",
                          .name = "Center to Center Slot",
                          .tooltip = "Slot from two arc centers and a width (type the length, then the width)",
                          .icon = "sketch-slot",
                          .aliases = {"SLOT"},
                          .tab = "SKETCH",
                          .group = "CREATE",
                          .order = 12}) {}
  SlotTool(SlotMode mode, ToolInfo info) : PointsTool(std::move(info), 3), mode_(mode) {}

 protected:
  std::string prompt() const override {
    switch (mode_) {
      case SlotMode::Overall:
        if (pts_.empty()) return "Specify first slot end";
        if (pts_.size() == 1) return "Specify second slot end (or type the overall length)";
        break;
      case SlotMode::CenterPoint:
        if (pts_.empty()) return "Specify slot center";
        if (pts_.size() == 1) return "Specify an arc center (or type the center-to-arc distance)";
        break;
      case SlotMode::CenterToCenter:
        if (pts_.empty()) return "Specify first slot center";
        if (pts_.size() == 1) return "Specify second slot center (or type the center distance)";
        break;
    }
    return "Specify slot width (or type a value)";
  }

  std::vector<ValueField> valueFields() const override { return {lengthField("length", "Length"), lengthField("width", "Width")}; }

  void updateValueVisibility() override {
    setValueVisible("length", pts_.size() == 1);
    setValueVisible("width", pts_.size() == 2);
  }

  /// Arc centres from the two picked points and the half width, per mode.
  std::optional<std::pair<Pt2, Pt2>> centres(Pt2 p0, Pt2 p1, double r) const {
    Pt2 d = p1 - p0;
    if (d.length() < kEps) return std::nullopt;
    switch (mode_) {
      case SlotMode::Overall: {
        if (d.length() <= 2 * r + kEps) return std::nullopt;
        Pt2 dn = d.normalized();
        return std::pair{p0 + dn * r, p1 - dn * r};
      }
      case SlotMode::CenterPoint:
        return std::pair{p0 * 2.0 - p1, p1};
      case SlotMode::CenterToCenter:
        break;
    }
    return std::pair{p0, p1};
  }

  Pt2 applyLocks(Pt2 p) const override {
    if (pts_.size() == 1) {
      if (auto L = lockedValue("length")) {
        Pt2 d = p - pts_[0].point;
        if (d.length() < kEps) d = {1, 0};
        return pts_[0].point + d.normalized() * (*L);
      }
    } else if (pts_.size() == 2) {
      if (auto W = lockedValue("width")) {
        Pt2 a = pts_[0].point, b = pts_[1].point;
        Pt2 n = (b - a).normalized().perp();
        double side = sign((p - (a + b) * 0.5).dot(n));
        return (a + b) * 0.5 + n * (side * (*W) / 2);
      }
    }
    return p;
  }

  Pt2 defaultCursor() const override {
    if (pts_.size() == 2) return (pts_[0].point + pts_[1].point) * 0.5 + (pts_[1].point - pts_[0].point).normalized().perp();
    return PointsTool::defaultCursor();
  }

  struct SlotGeom {
    Geometry l1, l2, a1, a2, axis;
  };

  std::optional<SlotGeom> slotFor(Pt2 c1, Pt2 c2, double r) const {
    Pt2 d = c2 - c1;
    if (d.length() < kEps || r < kEps) return std::nullopt;
    Pt2 dn = d.normalized(), n = dn.perp();
    SlotGeom s;
    s.l1 = Geometry::makeLine(c1 + n * r, c2 + n * r);
    s.l2 = Geometry::makeLine(c2 - n * r, c1 - n * r);
    s.a2 = Geometry::makeArc(c2, r, (-n).angle(), (-n).angle() + kPi);  // start c2-n r, end c2+n r
    s.a1 = Geometry::makeArc(c1, r, n.angle(), n.angle() + kPi);        // start c1+n r, end c1-n r
    s.axis = Geometry::makeLine(c1, c2);
    s.axis.construction = true;
    return s;
  }

  double halfWidth(Pt2 c) const {
    Pt2 a = pts_[0].point, b = pts_[1].point;
    return std::abs(signedDistanceToLine(a, b - a, c));
  }

  void preview(ToolContext&, Pt2 c) override {
    if (pts_.empty()) return;
    if (pts_.size() == 1) {
      previewLines({pts_[0].point, c}, guideStyle());
      setValue("length", pts_[0].point.distance(c), c);
      return;
    }
    auto cs = centres(pts_[0].point, pts_[1].point, halfWidth(c));
    auto s = cs ? slotFor(cs->first, cs->second, halfWidth(c)) : std::nullopt;
    if (!s) return;
    for (const Geometry* g : {&s->l1, &s->l2, &s->a1, &s->a2}) previewGeometry(*g, rubberStyle());
    previewGeometry(s->axis, constructionStyle());
    setValue("width", 2 * halfWidth(c), c);
  }

  void create(ToolContext& ctx) override {
    Sketch* sk = sketch();
    if (!sk || !session()) return;
    const double r = halfWidth(pts_[2].point);
    auto cs = centres(pts_[0].point, pts_[1].point, r);
    auto s = cs ? slotFor(cs->first, cs->second, r) : std::nullopt;
    if (!s) {
      ctx.showMessage("Degenerate slot ignored", true);
      return;
    }
    session()->beginEdit();
    int l1 = sk->addGeometry(asCreated(s->l1));
    int l2 = sk->addGeometry(asCreated(s->l2));
    int a1 = sk->addGeometry(asCreated(s->a1));
    int a2 = sk->addGeometry(asCreated(s->a2));
    int ax = sk->addGeometry(s->axis);
    using CT = ConstraintType;
    auto join = [&](GeoRef p, GeoRef q) {
      sk->addConstraint(Constraint{.type = CT::Coincident, .a = p, .b = q});
      sk->addConstraint(Constraint{.type = CT::Tangent, .a = p, .b = q});
    };
    join({l1, PointPos::End}, {a2, PointPos::End});
    join({l2, PointPos::Start}, {a2, PointPos::Start});
    join({l1, PointPos::Start}, {a1, PointPos::Start});
    join({l2, PointPos::End}, {a1, PointPos::End});
    sk->addConstraint(Constraint{.type = CT::Equal, .a = {a1}, .b = {a2}});
    sk->addConstraint(Constraint{.type = CT::Coincident, .a = {ax, PointPos::Start}, .b = {a1, PointPos::Center}});
    sk->addConstraint(Constraint{.type = CT::Coincident, .a = {ax, PointPos::End}, .b = {a2, PointPos::Center}});
    if (mode_ != SlotMode::Overall) {
      if (mode_ == SlotMode::CenterToCenter) applyInference(*sk, coincidenceOnly(pts_[0]), {a1, PointPos::Center}, -1);
      applyInference(*sk, coincidenceOnly(pts_[1]), {a2, PointPos::Center}, -1);
    }
    if (pts_[1].horizontal) sk->addConstraint(Constraint{.type = CT::Horizontal, .a = {ax}});
    else if (pts_[1].vertical) sk->addConstraint(Constraint{.type = CT::Vertical, .a = {ax}});
    // typed center distance -> length of the construction axis; typed width -> arc diameter
    // (overall / centre-point: the typed length places the points exactly; only the
    // centre-to-centre axis carries a length dimension)
    if (mode_ == SlotMode::CenterToCenter) dimensionFor(ctx, "length", Constraint{.type = CT::Distance, .a = {ax}});
    dimensionFor(ctx, "width", Constraint{.type = CT::Diameter, .a = {a1}});
    session()->commitEdit("Slot");
  }

 private:
  SlotMode mode_;
};

// ---------------------------------------------------------------------------
// Point
// ---------------------------------------------------------------------------

class PointTool : public PointsTool {
 public:
  PointTool()
      : PointsTool(ToolInfo{.id = "sketch.point",
                            .name = "Point",
                            .tooltip = "Sketch point",
                            .icon = "sketch-point",
                            .aliases = {"POINT", "PO"},
                            .tab = "SKETCH",
                            .group = "CREATE",
                            .order = 13},
                   1) {}

 protected:
  std::string prompt() const override { return "Specify point"; }
  void preview(ToolContext&, Pt2) override {}
  void create(ToolContext&) override {
    Sketch* sk = sketch();
    if (!sk || !session()) return;
    session()->beginEdit();
    int id = sk->addGeometry(asCreated(Geometry::makePoint(pts_[0].point)));
    applyInference(*sk, coincidenceOnly(pts_[0]), {id, PointPos::Center}, -1);
    session()->commitEdit("Point");
  }
};

}  // namespace

void registerCreateTools(core::ToolRegistry& reg) {
  reg.registerTool(std::make_unique<LineTool>());
  reg.registerTool(std::make_unique<RectangleTool>(
      RectMode::TwoPoint, ToolInfo{.id = "sketch.rectangle", .name = "2-Point Rectangle", .tooltip = "Rectangle from two opposite corners",
                                   .icon = "sketch-rectangle", .shortcut = "R", .aliases = {"RECTANGLE", "REC", "RECT"}, .tab = "SKETCH",
                                   .group = "CREATE", .order = 2, .promoted = true}));
  reg.registerTool(std::make_unique<RectangleTool>(
      RectMode::ThreePoint, ToolInfo{.id = "sketch.rectangle-3point", .name = "3-Point Rectangle", .tooltip = "Rectangle from an edge and a height",
                                     .icon = "sketch-rectangle-3point", .aliases = {"REC3"}, .tab = "SKETCH", .group = "CREATE", .order = 3}));
  reg.registerTool(std::make_unique<RectangleTool>(
      RectMode::Center, ToolInfo{.id = "sketch.rectangle-center", .name = "Center Rectangle", .tooltip = "Rectangle from its center and a corner",
                                 .icon = "sketch-rectangle-center", .aliases = {"RECC"}, .tab = "SKETCH", .group = "CREATE", .order = 4}));
  reg.registerTool(std::make_unique<CircleTool>(
      CircleMode::CenterDiameter, ToolInfo{.id = "sketch.circle", .name = "Center Diameter Circle", .tooltip = "Circle from center and diameter",
                                           .icon = "sketch-circle", .shortcut = "C", .aliases = {"CIRCLE", "C"}, .tab = "SKETCH",
                                           .group = "CREATE", .order = 5, .promoted = true}));
  reg.registerTool(std::make_unique<CircleTool>(
      CircleMode::TwoPoint, ToolInfo{.id = "sketch.circle-2point", .name = "2-Point Circle", .tooltip = "Circle from two diameter points",
                                     .icon = "sketch-circle-2point", .aliases = {"CIRCLE2"}, .tab = "SKETCH", .group = "CREATE", .order = 6}));
  reg.registerTool(std::make_unique<CircleTool>(
      CircleMode::ThreePoint, ToolInfo{.id = "sketch.circle-3point", .name = "3-Point Circle", .tooltip = "Circle through three points",
                                       .icon = "sketch-circle-3point", .aliases = {"CIRCLE3"}, .tab = "SKETCH", .group = "CREATE", .order = 7}));
  reg.registerTool(std::make_unique<ArcTool>(
      ArcMode::ThreePoint, ToolInfo{.id = "sketch.arc-3point", .name = "3-Point Arc", .tooltip = "Arc from start, end and a point on the arc",
                                    .icon = "sketch-arc-3point", .aliases = {"ARC", "A"}, .tab = "SKETCH", .group = "CREATE", .order = 8,
                                    .promoted = true}));
  reg.registerTool(std::make_unique<ArcTool>(
      ArcMode::Center, ToolInfo{.id = "sketch.arc-center", .name = "Center Point Arc", .tooltip = "Arc from center, start and end",
                                .icon = "sketch-arc-center", .aliases = {"ARCC"}, .tab = "SKETCH", .group = "CREATE", .order = 9}));
  reg.registerTool(std::make_unique<ArcTool>(
      ArcMode::Tangent, ToolInfo{.id = "sketch.arc-tangent", .name = "Tangent Arc", .tooltip = "Arc tangent to the end of a line or arc",
                                 .icon = "sketch-arc-tangent", .aliases = {"ARCT"}, .tab = "SKETCH", .group = "CREATE", .order = 10}));
  reg.registerTool(std::make_unique<PolygonTool>(
      false, ToolInfo{.id = "sketch.polygon", .name = "Circumscribed Polygon", .tooltip = "Regular polygon around a circle (argument: number of sides)",
                      .icon = "sketch-polygon", .aliases = {"POLYGON", "POL"}, .tab = "SKETCH", .group = "CREATE", .order = 11}));
  reg.registerTool(std::make_unique<PolygonTool>(
      true, ToolInfo{.id = "sketch.polygon-inscribed", .name = "Inscribed Polygon", .tooltip = "Regular polygon inside a circle (argument: number of sides)",
                     .icon = "sketch-polygon-inscribed", .aliases = {"POLI"}, .tab = "SKETCH", .group = "CREATE", .order = 11}));
  reg.registerTool(std::make_unique<PolygonTool>(
      false,
      ToolInfo{.id = "sketch.polygon-edge", .name = "Edge Polygon", .tooltip = "Regular polygon from the two endpoints of one edge (argument: number of sides)",
               .icon = "sketch-polygon", .aliases = {"POLE"}, .tab = "SKETCH", .group = "CREATE", .order = 11},
      true));
  reg.registerTool(std::make_unique<SlotTool>());
  reg.registerTool(std::make_unique<SlotTool>(
      SlotMode::Overall, ToolInfo{.id = "sketch.slot-overall", .name = "Overall Slot", .tooltip = "Slot from its two ends and a width (type the overall length, then the width)",
                                  .icon = "sketch-slot", .aliases = {"SLOTO"}, .tab = "SKETCH", .group = "CREATE", .order = 12}));
  reg.registerTool(std::make_unique<SlotTool>(
      SlotMode::CenterPoint, ToolInfo{.id = "sketch.slot-center", .name = "Center Point Slot", .tooltip = "Slot from its center, one arc center and a width",
                                      .icon = "sketch-slot", .aliases = {"SLOTC"}, .tab = "SKETCH", .group = "CREATE", .order = 12}));
  reg.registerTool(std::make_unique<PointTool>());
}

}  // namespace fabricad::sketch
