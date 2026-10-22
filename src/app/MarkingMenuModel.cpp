// FabriCAD app: marking menu content.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/MarkingMenuModel.h"

#include "core/Tool.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <numbers>

namespace fabricad::app {

using core::ShapeKind;

bool MarkingMenuModel::isEmpty() const {
  return std::all_of(radial.begin(), radial.end(), [](const MarkingEntry& e) { return e.command.empty(); }) &&
         overflow.empty();
}

namespace {

/// First registered tool among the candidates (tool ids may differ between module versions).
MarkingEntry firstTool(const core::ToolRegistry& reg, std::initializer_list<const char*> ids) {
  for (const char* id : ids)
    if (const core::ITool* t = reg.tool(id)) return {t->info().id, t->info().name};
  return {};
}

bool allOf(const std::vector<ShapeKind>& kinds, core::ShapeKindMask m) {
  return !kinds.empty() && std::all_of(kinds.begin(), kinds.end(), [m](ShapeKind k) { return core::hasKind(m, k); });
}

}  // namespace

MarkingMenuModel buildMarkingMenu(const core::ToolRegistry& reg, const MarkingContext& ctx) {
  MarkingMenuModel m;
  auto& s = m.radial;
  auto overflow = [&](std::initializer_list<const char*> ids) {
    MarkingEntry e = firstTool(reg, ids);
    if (!e.command.empty() &&
        std::none_of(m.overflow.begin(), m.overflow.end(), [&](const MarkingEntry& o) { return o.command == e.command; }))
      m.overflow.push_back(e);
  };
  // Repeat: inside a sketch the last command; outside a sketch the last non-sketch command
  // (a sketch tool such as 2-Point Rectangle makes no sense on a solid). While a command runs
  // the menu shows that command's context (OK / Cancel / Undo) and no repeat.
  MarkingEntry repeat;
  auto repeatable = [&](const std::string& id) -> const core::ITool* {
    const core::ITool* t = id.empty() ? nullptr : reg.tool(id);
    return t && t->info().repeatable ? t : nullptr;
  };
  if (ctx.inSketch) {
    if (const core::ITool* last = repeatable(ctx.lastToolId)) repeat = {kCmdRepeat, "Repeat " + last->info().name};
  } else {
    const std::string& id = ctx.lastModelToolId.empty() ? ctx.lastToolId : ctx.lastModelToolId;
    if (const core::ITool* last = repeatable(id); last && last->info().tab != "SKETCH")
      repeat = {id == ctx.lastToolId ? std::string(kCmdRepeat) : id, "Repeat " + last->info().name};
  }

  if (ctx.toolActive) {
    s[N] = {kCmdOk, "OK"};
    s[S] = {kCmdCancel, "Cancel"};
    s[NW] = firstTool(reg, {"app.undo"});
    overflow({"view.fit"});
    overflow({"view.look-at"});
    return m;
  }

  s[N] = repeat;
  s[NW] = firstTool(reg, {"app.undo"});
  const std::vector<ShapeKind>& k = ctx.selectionKinds;

  if (ctx.inSketch) {
    s[E] = firstTool(reg, {"sketch.line"});
    s[NE] = firstTool(reg, {"sketch.circle", "sketch.circle-center", "sketch.center-circle"});
    s[SE] = firstTool(reg, {"sketch.dimension"});
    s[S] = firstTool(reg, {"sketch.finish"});
    s[SW] = firstTool(reg, {"sketch.rectangle", "sketch.rectangle-2pt", "sketch.rect"});
    s[W] = firstTool(reg, {"sketch.trim"});
    if (!k.empty()) {
      overflow({"app.delete"});
      overflow({"sketch.construction", "sketch.toggle-construction"});
    }
    overflow({"view.look-at"});
    overflow({"sketch.offset"});
    overflow({"sketch.project"});
    return m;
  }

  s[NE] = k.empty() ? MarkingEntry{} : firstTool(reg, {"app.delete"});
  s[S] = firstTool(reg, {"sketch.create"});
  if (allOf(k, core::mask(ShapeKind::Edge))) {
    s[E] = firstTool(reg, {"part.fillet"});
    s[W] = firstTool(reg, {"part.chamfer"});
    s[SE] = firstTool(reg, {"part.press-pull"});
    s[SW] = firstTool(reg, {"construct.axis"});
  } else if (allOf(k, core::mask(ShapeKind::Face))) {
    s[E] = firstTool(reg, {"part.extrude"});
    s[W] = firstTool(reg, {"part.press-pull"});
    s[SE] = firstTool(reg, {"part.hole"});
    s[SW] = firstTool(reg, {"construct.offset-plane"});
    overflow({"view.look-at"});
  } else if (allOf(k, ShapeKind::Body | ShapeKind::Component)) {
    s[E] = firstTool(reg, {"part.move", "assembly.move"});
    s[W] = firstTool(reg, {"app.toggle-visibility"});
    s[SE] = firstTool(reg, {"part.combine"});
    s[SW] = firstTool(reg, {"assembly.new-component"});
  } else if (allOf(k, ShapeKind::Sketch | ShapeKind::Profile | ShapeKind::SketchCurve)) {
    s[E] = firstTool(reg, {"part.extrude"});
    s[W] = firstTool(reg, {"part.revolve"});
    s[SE] = firstTool(reg, {"sketch.edit"});
    s[SW] = firstTool(reg, {"part.press-pull"});
  } else {
    s[E] = firstTool(reg, {"part.extrude"});
    s[W] = firstTool(reg, {"part.press-pull"});
    s[SE] = firstTool(reg, {"part.hole"});
    s[SW] = firstTool(reg, {"part.move", "assembly.move"});
  }
  if (!k.empty()) overflow({"app.toggle-visibility"});
  overflow({"inspect.measure", "part.measure"});
  overflow({"app.show-all"});
  overflow({"view.fit"});
  overflow({"view.home"});
  return m;
}

int slotForOffset(double dx, double dy, double deadZone) {
  if (dx * dx + dy * dy < deadZone * deadZone) return -1;
  // angle measured clockwise from north (screen y points down)
  double a = std::atan2(dx, -dy) * 180.0 / std::numbers::pi;
  if (a < 0) a += 360.0;
  return static_cast<int>(std::floor((a + 22.5) / 45.0)) % kSlotCount;
}

void slotDirection(int slot, double& dx, double& dy) {
  const double a = slot * 45.0 * std::numbers::pi / 180.0;
  dx = std::sin(a);
  dy = -std::cos(a);
}

}  // namespace fabricad::app
