// FabriCAD app: shell verbs (OK, CANCEL, SELECT, PICK, INFO, LIST, CHECK, OPTIONS, ECHO, WAIT,
// SCREENSHOT, CLOSE, TAB). See ScriptTools.h and docs/SCRIPTING.md.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/ScriptTools.h"

#include "app/AppActions.h"
#include "app/AppShell.h"
#include "app/Selectors.h"
#include "core/AppHost.h"
#include "core/Document.h"
#include "core/Commands.h"
#include "core/GroupCommands.h"
#include "core/Json.h"
#include "core/Selection.h"
#include "core/ShapeUtils.h"
#include "core/UndoStack.h"
#include "core/ViewerContext.h"
#include "part/MassProperties.h"
#include "part/tools/FeatureTool.h"
#include "sketch/SketchFeature.h"
#include "sketch/SketchSession.h"
#include "viewer/ViewerWidget.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QPixmap>
#include <QTimer>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <functional>
#include <optional>
#include <sstream>

namespace fabricad::app {

using core::ObjectId;
using core::ShapeKind;
using core::ShapeRef;
using core::ToolContext;
using core::ToolInfo;
using core::Vec3;

namespace {

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::string join(const std::vector<std::string>& v, size_t from = 0, const char* sep = " ") {
  std::string out;
  for (size_t i = from; i < v.size(); ++i) out += (out.empty() ? "" : sep) + v[i];
  return out;
}

std::string fmt(double v) {
  if (std::abs(v - std::round(v)) < 1e-9 && std::abs(v) < 1e15) return std::format("{}", static_cast<long long>(std::llround(v)));
  return std::format("{:.6g}", v);
}

bool parseNumber(const std::string& s, double& out) {
  if (s.empty()) return false;
  char* end = nullptr;
  out = std::strtod(s.c_str(), &end);
  return end && *end == '\0';
}

bool startsWithNoCase(const std::string& s, const std::string& prefix) {
  return s.size() >= prefix.size() && lower(s.substr(0, prefix.size())) == prefix;
}

/// A timeline group named by `group:<name>` or `group:#<id>` (kInvalidId if none).
ObjectId groupByText(const core::Document& doc, std::string name) {
  if (startsWithNoCase(name, "group:")) name = name.substr(6);
  if (name.size() > 1 && name[0] == '#') {
    double id = 0;
    if (parseNumber(name.substr(1), id) && doc.group(static_cast<ObjectId>(id))) return static_cast<ObjectId>(id);
  }
  return doc.findGroup(name);
}

/// A feature or group of the timeline: `group:<name|#id>`, a feature selector (`feature:Extrude1`,
/// `sketch:Sketch1`, `feature:#20`), `#<id>` (quoted in scripts, where ` #` starts a comment), or a
/// bare feature or group name (features first).
ObjectId timelineItem(const core::Document& doc, const std::string& text, std::string* err) {
  if (startsWithNoCase(text, "group:")) {
    if (ObjectId g = groupByText(doc, text)) return g;
    if (err) *err = std::format("no group '{}'", text.substr(6));
    return core::kInvalidId;
  }
  if (text.size() > 1 && text[0] == '#') {
    double id = 0;
    if (parseNumber(text.substr(1), id)) {
      const auto oid = static_cast<ObjectId>(id);
      if (doc.feature(oid) || doc.group(oid)) return oid;
    }
  }
  if (text.find(':') == std::string::npos) {
    for (const auto& f : doc.features())
      if (lower(f->name()) == lower(text)) return f->id();
    if (ObjectId g = doc.findGroup(text)) return g;
  }
  SelectorResult r = resolveSelector(doc, text);
  if (r.ok() && r.refs.size() == 1 && doc.feature(r.refs.front().owner)) return r.refs.front().owner;
  if (err) *err = r.ok() ? std::format("'{}' is not a timeline feature or group", text) : r.error;
  return core::kInvalidId;
}

/// Timeline items named by args[from..], or the features in the selection when there are none.
bool timelineItems(const ToolContext& c, size_t from, std::vector<ObjectId>& out, std::string* err) {
  out.clear();
  for (size_t i = from; i < c.args.size(); ++i) {
    const ObjectId id = timelineItem(*c.document, c.args[i], err);
    if (id == core::kInvalidId) return false;
    if (std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
  }
  if (c.args.size() <= from && c.selection)
    for (const ShapeRef& r : c.selection->items())
      if (c.document->feature(r.owner) && std::find(out.begin(), out.end(), r.owner) == out.end()) out.push_back(r.owner);
  if (out.empty() && err) *err = "name the features or groups (or select features first)";
  return !out.empty();
}

std::string suppressionName(core::GroupSuppression s) {
  return s == core::GroupSuppression::All ? "all" : s == core::GroupSuppression::Some ? "some" : "none";
}

/// Push `cmd` on the document's stack (or just run it without one).
void pushCommand(ToolContext& c, std::unique_ptr<core::UndoCommand> cmd) {
  if (c.undo) c.undo->push(std::move(cmd));
  else cmd->redo();
}

ToolInfo verb(const std::string& id, const std::string& name, const std::string& tip, std::vector<std::string> aliases,
              bool needsDocument = true) {
  ToolInfo i;
  i.id = id;
  i.name = name;
  i.tooltip = tip;
  i.aliases = std::move(aliases);
  i.icon = "command-line";
  i.hidden = true;
  i.repeatable = false;
  i.needsDocument = needsDocument;
  i.keepsActiveTool = true;  // runs beside the active command (ToolManager::runCommandLine)
  return i;
}

/// Pending-event drain used after shell verbs that trigger queued work.
void settle() {
  for (int i = 0; i < 3; ++i) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

// --- CHECK -------------------------------------------------------------------------------------

bool compare(double a, const std::string& op, double b, double tol, std::string* why) {
  bool ok = false;
  if (op == "==" || op == "=") ok = std::abs(a - b) <= std::max(tol, 1e-6 * std::max(1.0, std::abs(b)));
  else if (op == "!=") ok = std::abs(a - b) > std::max(tol, 1e-6 * std::max(1.0, std::abs(b)));
  else if (op == "~" || op == "~=" || op == "approx") ok = std::abs(a - b) <= tol;
  else if (op == "<") ok = a < b;
  else if (op == "<=") ok = a <= b;
  else if (op == ">") ok = a > b;
  else if (op == ">=") ok = a >= b;
  else {
    if (why) *why = "unknown comparison '" + op + "' (use == != ~ < <= > >=)";
    return false;
  }
  if (!ok && why) *why = std::format("got {}", fmt(a));
  return ok;
}

/// Parse "[op] value [tolerance]" starting at args[i]. Tolerance "1%" is relative to the value.
bool parseComparison(const std::vector<std::string>& args, size_t i, std::string& op, double& value, double& tol,
                     std::string* err) {
  if (i >= args.size()) {
    if (err) *err = "missing comparison (e.g. == 2, ~ 1000 1%)";
    return false;
  }
  op = args[i];
  if (parseNumber(op, value)) {
    op = "==";
  } else {
    ++i;
    if (i >= args.size() || !parseNumber(args[i], value)) {
      if (err) *err = "missing or invalid number after '" + op + "'";
      return false;
    }
  }
  tol = (op == "~" || op == "~=" || op == "approx") ? std::abs(value) * 0.001 : 0.0;
  if (i + 1 < args.size()) {
    std::string t = args[i + 1];
    // "±0.1" / "+-0.1" as a tolerance (CHECK mass 47.1 ±0.1)
    if (t.rfind("\xC2\xB1", 0) == 0) t = t.substr(2);
    else if (t.rfind("+-", 0) == 0) t = t.substr(2);
    double tv;
    if (!t.empty() && t.back() == '%') {
      t.pop_back();
      if (parseNumber(t, tv)) tol = std::abs(value) * tv / 100.0;
    } else if (parseNumber(t, tv)) {
      tol = tv;
    }
  }
  return true;
}

std::optional<double> countOf(const core::Document& doc, const CheckContext& ctx, const std::string& word) {
  if (word == "bodies") return static_cast<double>(doc.liveBodies(false).size());
  if (word == "features") return static_cast<double>(doc.featureCount());
  if (word == "components") return static_cast<double>(doc.components().size() - 1);  // without the root
  if (word == "sketches" || word == "joints" || word == "construction") {
    int n = 0;
    for (const auto& f : doc.features()) {
      const std::string folder = lower(f->browserFolder());
      if (word == folder) ++n;
    }
    return n;
  }
  if (word == "selection" || word == "selected") return static_cast<double>(const_cast<core::Document&>(doc).selection().count());
  if (word == "tabs") return ctx.tabs;
  if (word == "dof") return ctx.sketchDof < 0 ? std::nullopt : std::optional<double>(ctx.sketchDof);
  if (word == "params" || word == "parameters") return static_cast<double>(doc.parameters().size());
  if (word == "rollback" || word == "marker") return doc.rollbackIndex();
  if (word == "groups") return static_cast<double>(doc.groupCount());
  if (word == "undo") return static_cast<double>(const_cast<core::Document&>(doc).undoStack().canUndo() ? 1 : 0);
  return std::nullopt;
}

}  // namespace

CheckResult evaluateCheck(const core::Document& doc, const CheckContext& ctx, const std::vector<std::string>& args) {
  CheckResult r;
  const std::string expr = join(args);
  if (args.empty()) {
    r.message = "CHECK: nothing to check (see docs/SCRIPTING.md)";
    return r;
  }
  const std::string first = lower(args[0]);
  auto finish = [&](bool ok, const std::string& detail) {
    r.ok = ok;
    r.message = std::format("CHECK {}: {}{}", expr, ok ? "ok" : "FAILED", detail.empty() ? "" : " (" + detail + ")");
    return r;
  };
  // status of all active features
  if (first == "status") {
    const std::string want = args.size() > 1 ? lower(args[1]) : "ok";
    std::string bad;
    for (const auto& f : doc.features()) {
      if (!doc.isFeatureActive(f->id())) continue;
      const std::string st = lower(core::featureStatusName(f->status()));
      if (st != want) bad += (bad.empty() ? "" : ", ") + f->name() + " is " + st + (f->statusMessage().empty() ? "" : " '" + f->statusMessage() + "'");
    }
    return finish(bad.empty(), bad);
  }
  // active command
  if (first == "command" || first == "active") {
    const std::string want = args.size() > 1 ? lower(args.back()) : "none";
    const std::string have = ctx.activeCommand.empty() ? "none" : lower(ctx.activeCommand);
    return finish(have == want, "active command is " + have);
  }
  // user parameter
  if (first == "param" || first == "parameter") {
    if (args.size() < 3) return finish(false, "usage: CHECK param <name> <op> <value>");
    const core::Document::Parameter* p = doc.parameter(args[1]);
    if (!p) return finish(false, "no parameter '" + args[1] + "'");
    std::string op, err;
    double value, tol;
    if (!parseComparison(args, 2, op, value, tol, &err)) return finish(false, err);
    std::string why;
    const bool ok = compare(p->value.value, op, value, tol, &why);
    return finish(ok, why);
  }
  // file on disk
  if (first == "file") {
    if (args.size() < 2) return finish(false, "usage: CHECK file <path> [exists|missing|size > n]");
    const QFileInfo fi(core::qs(args[1]));
    const std::string what = args.size() > 2 ? lower(args[2]) : "exists";
    if (what == "exists") return finish(fi.exists() && fi.size() > 0, fi.exists() ? std::format("{} bytes", fi.size()) : "missing");
    if (what == "missing") return finish(!fi.exists(), fi.exists() ? "exists" : "");
    if (what == "size") {
      std::string op, err;
      double value, tol;
      if (!parseComparison(args, 3, op, value, tol, &err)) return finish(false, err);
      std::string why;
      return finish(compare(static_cast<double>(fi.size()), op, value, tol, &why), why);
    }
    return finish(false, "unknown file check '" + what + "'");
  }
  // mass properties of the whole design (production-features P1):
  //   CHECK mass 47.1 ±0.1   |   CHECK cog 5,10,15 0.01   (g, mm; tolerance absolute or n%)
  if (first == "mass" || first == "cog" || first == "cg" || first == "centroid") {
    const part::MassProperties mp = part::computeMassProperties(part::massItemsFor(doc, {}));
    if (first == "mass") {
      std::string op, err;
      double value, tol;
      if (!parseComparison(args, 1, op, value, tol, &err)) return finish(false, err);
      if (op == "==" && tol == 0) tol = 1e-6 * std::max(1.0, std::abs(value));
      std::string why;
      return finish(compare(mp.mass, op == "==" ? "~" : op, value, tol, &why), why);
    }
    if (args.size() < 2) return finish(false, "usage: CHECK cog x,y,z [tolerance]");
    std::vector<double> want;
    std::string t = args[1];
    for (char& ch : t)
      if (ch == ',' || ch == ';') ch = ' ';
    std::istringstream in(t);
    for (double v; in >> v;) want.push_back(v);
    if (want.size() != 3) return finish(false, "CHECK cog needs x,y,z");
    double tol = 1e-3;
    if (args.size() > 2) {
      std::string tt = args[2];
      if (tt.rfind("\xC2\xB1", 0) == 0) tt = tt.substr(2);
      else if (tt.rfind("+-", 0) == 0) tt = tt.substr(2);
      parseNumber(tt, tol);
    }
    const double d = std::hypot(mp.cog.x - want[0], mp.cog.y - want[1], mp.cog.z - want[2]);
    return finish(d <= tol, std::format("got {}, {}, {}", fmt(mp.cog.x), fmt(mp.cog.y), fmt(mp.cog.z)));
  }
  // counts: CHECK bodies == 2
  if (auto n = countOf(doc, ctx, first)) {
    std::string op, err;
    double value, tol;
    if (!parseComparison(args, 1, op, value, tol, &err)) return finish(false, err);
    std::string why;
    return finish(compare(*n, op, value, tol, &why), why);
  }
  if (first == "dof") return finish(false, "no sketch is being edited");
  // timeline group: CHECK group:<name> exists | missing | suppressed all|none|some | collapsed |
  // expanded | members <op> <n> | parent <group|none>
  if (startsWithNoCase(first, "group:")) {
    const ObjectId g = groupByText(doc, args[0]);
    const std::string what = args.size() > 1 ? lower(args[1]) : "exists";
    if (what == "missing") return finish(g == core::kInvalidId, g ? "found" : "");
    if (g == core::kInvalidId) return finish(false, "no group '" + args[0].substr(6) + "'");
    const core::TimelineGroup& tg = *doc.group(g);
    if (what == "exists") return finish(true, std::format("#{}", g));
    if (what == "suppressed") {
      const std::string want = args.size() > 2 ? lower(args[2]) : "all";
      const std::string have = suppressionName(doc.groupSuppression(g));
      return finish(have == want, "suppressed: " + have);
    }
    if (what == "collapsed" || what == "expanded") return finish(tg.collapsed == (what == "collapsed"), tg.collapsed ? "collapsed" : "expanded");
    if (what == "members" || what == "features" || what == "count") {
      std::string op, err;
      double value, tol;
      if (!parseComparison(args, 2, op, value, tol, &err)) return finish(false, err);
      std::string why;
      return finish(compare(static_cast<double>(doc.groupFeatures(g).size()), op, value, tol, &why), why);
    }
    if (what == "parent") {
      const std::string want = args.size() > 2 ? args[2] : "none";
      const core::TimelineGroup* p = doc.group(tg.parent);
      const bool ok = p ? groupByText(doc, want) == tg.parent : lower(want) == "none";
      return finish(ok, "parent " + (p ? p->name : std::string("none")));
    }
    return finish(false, "unknown group check '" + what + "' (exists missing suppressed collapsed expanded members parent)");
  }
  // selector based: CHECK <selector> exists | missing | status <s> | <metric> <op> <value> [tol]
  SelectorResult sel = resolveSelector(doc, args[0]);
  if (args.size() >= 2 && lower(args[1]) == "missing") return finish(!sel.ok() || sel.refs.empty(), sel.ok() ? "found" : "");
  if (!sel.ok()) return finish(false, sel.error);
  if (args.size() < 2 || lower(args[1]) == "exists") return finish(!sel.refs.empty(), std::format("{} item(s)", sel.refs.size()));
  const std::string what = lower(args[1]);
  if (what == "status") {
    const std::string want = args.size() > 2 ? lower(args[2]) : "ok";
    std::string bad;
    for (const ShapeRef& ref : sel.refs) {
      const core::Feature* f = doc.feature(ref.owner);
      if (!f) continue;
      const std::string st = lower(core::featureStatusName(f->status()));
      if (st != want) bad += (bad.empty() ? "" : ", ") + f->name() + " is " + st + (f->statusMessage().empty() ? "" : " '" + f->statusMessage() + "'");
    }
    return finish(bad.empty(), bad);
  }
  if (what == "count") {
    std::string op, err;
    double value, tol;
    if (!parseComparison(args, 2, op, value, tol, &err)) return finish(false, err);
    std::string why;
    return finish(compare(static_cast<double>(sel.refs.size()), op, value, tol, &why), why);
  }
  // metric over the (first) selected entity; several entities -> sum for volume/area/length, else first
  double total = 0;
  int found = 0;
  for (const ShapeRef& ref : sel.refs) {
    auto m = refMetric(doc, ref, what);
    if (!m) continue;
    ++found;
    if (what == "volume" || what == "area" || what == "length" || what == "mass") total += *m;
    else if (found == 1) total = *m;
  }
  if (found == 0) return finish(false, std::format("'{}' has no metric '{}'", refLabel(doc, sel.refs.front()), what));
  std::string op, err;
  double value, tol;
  if (!parseComparison(args, 2, op, value, tol, &err)) return finish(false, err);
  std::string why;
  return finish(compare(total, op, value, tol, &why), why);
}

// --- LIST --------------------------------------------------------------------------------------

std::string listText(const core::Document& doc, const std::string& categoryIn) {
  const std::string cat = lower(categoryIn);
  std::string out;
  auto line = [&](const std::string& s) { out += (out.empty() ? "" : "\n") + s; };
  if (cat == "bodies" || cat == "all") {
    line(std::format("Bodies ({}):", doc.bodies().size()));
    for (const core::Body* b : doc.bodies()) {
      const core::Component* c = doc.component(b->component);
      const TopoDS_Shape s = worldBodyShape(doc, b->id);
      line(std::format("  body:{}  #{}  component {}  {}{}  volume {} mm3", b->name, b->id, c ? c->name : "?",
                       b->visible ? "visible" : "hidden", s.IsNull() ? " (no shape)" : "", s.IsNull() ? 0.0 : core::volume(s)));
    }
  }
  if (cat == "features" || cat == "timeline" || cat == "all") {
    line(std::format("Features ({}), marker after {}:", doc.featureCount(), doc.rollbackIndex()));
    int i = 0;
    // group rows head their (indented) members
    for (const core::TimelineItem& it : doc.timelineItems()) {
      const std::string indent(2 + 2 * static_cast<size_t>(it.depth), ' ');
      if (it.kind == core::TimelineItem::Kind::Group) {
        const core::TimelineGroup& g = *doc.group(it.id);
        line(std::format("{}group:{}  #{}  {} features{}{}", indent, g.name, g.id, doc.groupFeatures(g.id).size(),
                         g.collapsed ? "  collapsed" : "", doc.groupSuppression(g.id) == core::GroupSuppression::None
                                                              ? ""
                                                              : "  suppressed " + suppressionName(doc.groupSuppression(g.id))));
        continue;
      }
      const core::Feature* f = doc.feature(it.id);
      ++i;
      line(std::format("{}{:>2}. feature:{}  #{}  {}  {}{}{}{}", indent, i, f->name(), f->id(), f->type(), core::featureStatusName(f->status()),
                       f->statusMessage().empty() ? "" : " '" + f->statusMessage() + "'", f->isSuppressed() ? "  suppressed" : "",
                       doc.isFeatureActive(f->id()) ? "" : "  rolled back"));
    }
  }
  if (cat == "groups") {
    line(std::format("Groups ({}):", doc.groupCount()));
    for (ObjectId gid : doc.groupIds()) {
      const core::TimelineGroup& g = *doc.group(gid);
      const auto [first, last] = doc.groupRange(gid);
      const core::TimelineGroup* p = doc.group(g.parent);
      line(std::format("  {}group:{}  #{}  features {}-{} ({}){}{}  suppressed {}", std::string(2 * doc.groupChain(gid).size(), ' '),
                       g.name, gid, first + 1, last + 1, doc.groupFeatures(gid).size(), p ? "  in " + p->name : "",
                       g.collapsed ? "  collapsed" : "", suppressionName(doc.groupSuppression(gid))));
    }
  }
  if (cat == "components" || cat == "all") {
    line(std::format("Components ({}, tree order; indented = child of the line above it):", doc.components().size()));
    // depth first from the root so the hierarchy is visible (CFB nests the new components under the
    // component that owned the body)
    std::function<void(const core::Component*, int)> walk = [&](const core::Component* c, int depth) {
      gp_XYZ t = doc.worldPlacement(c->id).TranslationPart();
      const core::Component* parent = doc.component(c->parent);
      line(std::format("{}component:{}  #{}  {}{}{}{}  {} bodies, {} children  at ({}, {}, {}){}", std::string(2 + 2 * depth, ' '),
                       c->name, c->id, c->isRoot() ? "root" : "", c->grounded ? " grounded" : "", c->isOccurrence() ? " occurrence" : "",
                       parent && !parent->isRoot() ? "  in " + parent->name : "", doc.displayedBodies(c->id).size(), c->children.size(),
                       fmt(t.X()), fmt(t.Y()), fmt(t.Z()), c->id == doc.activeComponent() ? "  (active)" : ""));
      for (ObjectId child : c->children)
        if (const core::Component* cc = doc.component(child)) walk(cc, depth + 1);
    };
    walk(&doc.rootComponent(), 0);
  }
  if (cat == "sketches") {
    for (const auto& f : doc.features())
      if (const auto* sk = dynamic_cast<const sketch::SketchFeature*>(f.get())) line(describeRef(doc, ShapeRef::sketch(sk->id())));
    if (out.empty()) out = "No sketches";
  }
  if (cat == "joints") {
    for (const auto& f : doc.features())
      if (f->browserFolder() == "Joints") line(describeRef(doc, featureRef(*f)));
    if (out.empty()) out = "No joints";
  }
  if (cat == "params" || cat == "parameters" || cat == "all") {
    line(std::format("Parameters ({}):", doc.parameters().size()));
    for (const auto& p : doc.parameters())
      line(std::format("  {} = {}  ({}{})", p.name, p.expression, p.ok ? fmt(p.value.value) : "error", p.ok ? "" : ": " + p.error));
  }
  if (cat == "selection") {
    const auto& items = const_cast<core::Document&>(doc).selection().items();
    line(std::format("Selection ({}):", items.size()));
    for (const ShapeRef& r : items) line("  " + refLabel(doc, r));
  }
  return out;
}

// --- registration --------------------------------------------------------------------------------

void registerScriptTools(AppShell& w, core::ToolRegistry& reg) {
  AppShell* win = &w;
  auto add = [&](ToolInfo i, std::function<void(ToolContext&)> fn) {
    reg.registerTool(std::make_unique<core::LambdaTool>(std::move(i), std::move(fn)));
  };

  // --- OK / CANCEL ------------------------------------------------------------------------------
  add(verb("app.ok", "OK", "Accept the active command (Enter)", {"OK", "ACCEPT", "ENTER"}, false),
      [win](ToolContext&) { win->acceptActiveTool(); });
  add(verb("app.cancel", "Cancel", "Cancel the active command or clear the selection (Esc)", {"CANCEL", "ESC", "ESCAPE"}, false),
      [win](ToolContext& c) {
        core::ToolManager& m = win->toolManager();
        if (m.isActive() && m.activeToolId() != "app.cancel") m.cancel();
        else if (c.selection) c.selection->clear();
      });

  // --- SELECT -----------------------------------------------------------------------------------
  add(verb("app.select", "Select", "Select entities by name: SELECT body:Seat/face:top [+] (docs/SCRIPTING.md)", {"SELECT", "SEL"}),
      [](ToolContext& c) {
        if (!c.document || !c.selection) return;
        std::vector<std::string> texts;
        bool addMode = false;
        for (const std::string& a : c.args) {
          if (a == "+" || lower(a) == "add") addMode = true;
          else if (!a.empty() && a[0] == '+') {
            addMode = true;
            texts.push_back(a.substr(1));
          } else texts.push_back(a);
        }
        SelectorResult r = resolveSelectors(*c.document, texts);
        if (!r.ok()) {
          c.showMessage("SELECT: " + r.error, true);
          return;
        }
        std::vector<ShapeRef> allowed;
        std::string rejected;
        for (const ShapeRef& ref : r.refs) {
          if (c.selection->isAllowed(ref)) allowed.push_back(ref);
          else rejected += (rejected.empty() ? "" : ", ") + refLabel(*c.document, ref);
        }
        if (!rejected.empty()) {
          c.showMessage("SELECT: the active command does not accept " + rejected, true);
          return;
        }
        if (addMode)
          for (const ShapeRef& ref : allowed) c.selection->add(ref);
        else
          c.selection->set(allowed);
        std::string what;
        for (const ShapeRef& ref : allowed) {
          if (what.size() > 300) {
            what += ", ...";
            break;
          }
          what += (what.empty() ? "" : ", ") + refLabel(*c.document, ref);
        }
        c.showMessage(allowed.empty() ? "Selection cleared" : std::format("Selected {}: {}", allowed.size(), what));
      });

  // --- PICK -------------------------------------------------------------------------------------
  add(verb("app.pick", "Pick", "Click at a point: PICK x,y (sketch) | PICK x,y,z [dir dx,dy,dz] [+] [tol=0.5]", {"PICK", "CLICK"}),
      [win](ToolContext& c) {
        if (!c.document || !c.selection) return;
        core::ToolManager& m = win->toolManager();
        Vec3 p;
        int coords = 0;
        std::optional<Vec3> dir;
        int modifiers = core::ModNone;
        double tol = 0.5;
        bool dbl = false;
        for (size_t i = 0; i < c.args.size(); ++i) {
          const std::string& a = c.args[i];
          const std::string l = lower(a);
          if (l == "+" || l == "add" || l == "shift") modifiers |= core::ModShift;
          else if (l == "ctrl" || l == "toggle" || l == "nosnap" || l == "exact") modifiers |= core::ModCtrl;  // Ctrl: no inference snapping
          else if (l == "double" || l == "dblclick") dbl = true;
          else if (l.starts_with("tol=")) parseNumber(a.substr(4), tol);
          else if (l == "dir" && i + 1 < c.args.size()) {
            Vec3 d;
            if (parsePoint(c.args[++i], d) == 3) dir = d;
          } else if (coords == 0) coords = parsePoint(a, p);
        }
        if (coords == 0) {
          c.showMessage("PICK: give a point as x,y (sketch) or x,y,z (model)", true);
          return;
        }
        sketch::SketchSession* session = sketch::SketchSession::current();
        if (session && session->manager() != &m) session = nullptr;
        core::IViewerContext* viewer = c.viewer;
        core::ViewerMouseEvent e;
        e.button = core::MouseButton::Left;
        e.modifiers = modifiers;
        e.clickCount = dbl ? 2 : 1;
        Vec3 world = p;
        if (coords == 2) {
          if (!session) {
            c.showMessage("PICK x,y needs an open sketch; use x,y,z in the model", true);
            return;
          }
          world = session->toWorld({p.x, p.y});
        }
        if (session) {
          const gp_Ax3 pl = session->feature() ? session->feature()->plane() : gp_Ax3();
          Vec3 n = Vec3::from(pl.Direction()), o = Vec3::from(pl.Location());
          Vec3 onPlane = world - n * n.dot(world - o);
          e.hasPlanePoint = true;
          e.planePoint = onPlane;
          e.rayDir = -n;
          e.rayOrigin = onPlane + n * 1000.0;
        } else {
          Vec3 d = dir.value_or(viewer ? -Vec3::from(viewer->viewDirection()) : Vec3{0, 0, -1});
          if (d.isZero()) d = Vec3{0, 0, -1};
          e.rayDir = d.normalized();
          e.rayOrigin = dir ? world : world - e.rayDir * 1000.0;
        }
        if (viewer) {
          double sx, sy;
          if (viewer->worldToScreen(world.toPnt(), sx, sy)) {
            e.x = static_cast<int>(std::lround(sx));
            e.y = static_cast<int>(std::lround(sy));
          }
        }
        // what the click lands on (honouring the active command's selection filter)
        std::optional<PickHit> hit = pickAt(*c.document, world, c.selection->filter(), tol, dir,
                                               session && session->feature() ? session->feature()->id() : ObjectId(0));
        if (hit) {
          e.hasPick = true;
          e.pick = hit->ref;
          e.pickPoint = hit->point;
        }
        if (!hit && !session) {
          c.showMessage(std::format("PICK: nothing at ({}, {}, {}) within {} mm", fmt(world.x), fmt(world.y), fmt(world.z), fmt(tol)), true);
          return;
        }
        if (hit && c.selection->preselection() != std::optional<ShapeRef>(hit->ref)) c.selection->setPreselection(hit->ref);
        m.mouseMove(e);
        bool consumed = false;
        if (dbl) consumed = m.mouseDoubleClick(e);
        else {
          consumed = m.mousePress(e);
          m.mouseRelease(e);
        }
        if (!consumed && hit) {
          // the viewer's default click selection
          if (modifiers & core::ModCtrl) c.selection->toggle(hit->ref);
          else if (modifiers & core::ModShift) c.selection->add(hit->ref);
          else c.selection->set(hit->ref);
        } else if (!consumed && !hit && !(modifiers & (core::ModCtrl | core::ModShift))) {
          c.selection->clear();
        }
        c.selection->setPreselection(std::nullopt);
        if (hit) c.showMessage(std::format("Picked {} at ({}, {}, {})", refLabel(*c.document, hit->ref), fmt(hit->point.x), fmt(hit->point.y), fmt(hit->point.z)));
        else c.showMessage(std::format("Clicked sketch point ({}, {})", fmt(p.x), fmt(p.y)));
      });

  // --- INFO / LIST / CHECK / OPTIONS -----------------------------------------------------------------
  add(verb("app.info", "Info", "Describe entities: INFO [selectors] (default: the selection)", {"INFO", "DESCRIBE", "WHAT"}),
      [win](ToolContext& c) {
        if (!c.document) return;
        core::Document& doc = *c.document;
        std::vector<ShapeRef> refs;
        if (c.args.empty()) {
          refs = c.selection ? c.selection->items() : std::vector<ShapeRef>{};
          if (refs.empty()) {
            std::string s = std::format("Document '{}'{}: {} features, {} bodies, {} components, units {}, active component '{}'",
                                        doc.name(), doc.filePath().empty() ? "" : " (" + doc.filePath() + ")", doc.featureCount(),
                                        doc.bodies().size(), doc.components().size(), core::lengthUnitSymbol(doc.units().length),
                                        doc.component(doc.activeComponent()) ? doc.component(doc.activeComponent())->name : "?");
            s += doc.isModified() ? ", modified" : ", unmodified";
            core::ToolManager& m = win->toolManager();
            s += m.isActive() ? ", active command " + m.activeToolId() : ", no active command";
            if (sketch::SketchSession* ss = sketch::SketchSession::current()) s += ", editing " + (ss->feature() ? ss->feature()->name() : std::string("sketch")) + " (" + ss->statusText() + ")";
            s += std::format(", tab {}/{}", win->activeTabIndex() + 1, win->tabCount());
            c.showMessage(s);
            return;
          }
        } else {
          SelectorResult r = resolveSelectors(doc, c.args);
          if (!r.ok()) {
            c.showMessage("INFO: " + r.error, true);
            return;
          }
          refs = r.refs;
        }
        std::string out;
        for (const ShapeRef& ref : refs) out += (out.empty() ? "" : "\n") + describeRef(doc, ref);
        c.showMessage(out);
      });
  add(verb("app.list", "List", "List document objects: LIST [bodies|features|groups|components|sketches|joints|params|selection|tabs|tools|all]",
           {"LIST", "LS"}),
      [win](ToolContext& c) {
        if (!c.document) return;
        const std::string cat = c.args.empty() ? "all" : lower(c.args[0]);
        if (cat == "tabs") {
          std::string out = std::format("Tabs ({}):", win->tabCount());
          for (int i = 0; i < win->tabCount(); ++i) {
            const AppShell::TabInfo t = win->tabInfo(i);
            out += std::format("\n  {}. {}  [{}]{}{}", i + 1, t.title.toStdString(), t.design ? "design" : "drawing",
                               t.filePath.empty() ? "" : "  " + t.filePath, i == win->activeTabIndex() ? "  (active)" : "");
          }
          c.showMessage(out);
          return;
        }
        if (cat == "tools" || cat == "commands") {
          const std::string filter = c.args.size() > 1 ? lower(c.args[1]) : "";
          std::string out = "Commands (alias -> id):";
          for (core::ITool* t : win->tools().tools()) {
            const ToolInfo& i = t->info();
            if (i.aliases.empty()) continue;
            if (!filter.empty() && lower(i.id).find(filter) == std::string::npos && lower(i.name).find(filter) == std::string::npos) continue;
            std::string al;
            for (const std::string& a : i.aliases) al += (al.empty() ? "" : ", ") + a;
            out += std::format("\n  {:<28} {}  ({}{})", al, i.id, i.name, t->isEnabled(c) ? "" : ", disabled now");
          }
          c.showMessage(out);
          return;
        }
        std::string out = listText(*c.document, cat);
        if (out.empty()) {
          c.showMessage("LIST: unknown category '" + cat + "' (bodies, features, components, sketches, joints, params, selection, tabs, tools, all)", true);
          return;
        }
        c.showMessage(out);
      });
  add(verb("app.check", "Check", "Assert a fact (fails the script line): CHECK bodies == 2 | CHECK body:Seat volume ~ 8565976 0.1%",
           {"CHECK", "ASSERT", "EXPECT"}),
      [win](ToolContext& c) {
        if (!c.document) return;
        CheckContext ctx;
        ctx.tabs = win->tabCount();
        core::ToolManager& m = win->toolManager();
        ctx.activeCommand = m.isActive() && m.activeToolId() != "app.check" ? m.activeToolId() : "";
        if (sketch::SketchSession* s = sketch::SketchSession::current(); s && s->sketch()) ctx.sketchDof = s->sketch()->dofs();
        CheckResult r = evaluateCheck(*c.document, ctx, c.args);
        c.showMessage(r.message, !r.ok);
      });
  add(verb("app.options", "Options", "List the active command's inputs and options (set them with name=value)", {"OPTIONS", "OPTS", "?"},
           false),
      [win](ToolContext& c) {
        core::ToolManager& m = win->toolManager();
        core::ITool* t = m.activeTool();
        if (!t || t->info().id == "app.options") {
          c.showMessage("No command is active");
          return;
        }
        std::string out = std::format("Command {} ({})", t->info().name, t->info().id);
        if (!win->promptText().isEmpty()) out += "\n  prompt: " + win->promptText().toStdString();
        if (auto* ft = dynamic_cast<part::FeatureTool*>(t)) {
          if (part::PartFeature* f = ft->feature()) {
            out += std::format("\n  active input: {}   valid: {}{}", ft->activeInput(), ft->isValid() ? "yes" : "no",
                               ft->message().empty() ? "" : "  (" + ft->message() + ")");
            for (const core::Property& p : f->properties()) {
              if (p.flags().hidden) continue;
              out += std::format("\n  {}={}   [{}]", p.name(), c.document ? propertyText(*c.document, p) : p.toString(), p.label());
            }
          }
        } else {
          out += "\n  (this command has no property list; type values or keywords as its prompt says)";
        }
        c.showMessage(out);
      });

  // --- ECHO / WAIT / SCREENSHOT -------------------------------------------------------------------
  add(verb("app.echo", "Echo", "Write text to the log / history", {"ECHO", "REM"}, false),
      [](ToolContext& c) { c.showMessage(join(c.args)); });
  add(verb("app.wait", "Wait", "Wait for N milliseconds (processing events): WAIT 500", {"WAIT", "SLEEP"}, false), [](ToolContext& c) {
    double ms = 100;
    if (!c.args.empty()) parseNumber(c.args[0], ms);
    QEventLoop loop;
    QTimer::singleShot(static_cast<int>(std::max(0.0, ms)), &loop, &QEventLoop::quit);
    loop.exec();
  });
  add(verb("app.screenshot", "Screenshot", "Save a PNG of the window (or 'viewport'): SCREENSHOT out.png [viewport]",
           {"SCREENSHOT", "SNAP"}, false),
      [win](ToolContext& c) {
        if (c.args.empty()) {
          c.showMessage("SCREENSHOT: give a file name", true);
          return;
        }
        settle();
        QWidget* target = win->screenshotTarget(c.args.size() > 1 && lower(c.args[1]) == "viewport");
        if (!target) {
          c.showMessage("SCREENSHOT: no window in a headless run (use --script without --offscreen, or set FABRICAD_SCRIPT_WINDOW=1)", true);
          return;
        }
        QPixmap pm = target->grab();
        if (pm.isNull() || !pm.save(core::qs(c.args[0]))) {
          c.showMessage("SCREENSHOT: cannot write " + c.args[0], true);
          return;
        }
        c.showMessage(std::format("Screenshot {}x{} written to {}", pm.width(), pm.height(), c.args[0]));
      });

  // --- tabs -------------------------------------------------------------------------------------
  {
    ToolInfo i = verb("app.close", "Close Tab", "Close the active document tab (or TAB <n|name>)", {"CLOSE", "CLOSETAB"}, false);
    i.shortcut = "Ctrl+W";
    add(i, [win](ToolContext& c) {
      const int tab = c.args.empty() ? win->activeTabIndex() : win->findTabIndex(c.args[0]);
      if (tab < 0) {
        c.showMessage("CLOSE: no tab '" + join(c.args) + "'", true);
        return;
      }
      win->closeTabLater(tab);  // deferred: the tool runs inside the tab's own ToolManager
    });
  }
  add(verb("app.tab", "Switch Tab", "Activate a document tab: TAB <n|name|next|prev>", {"TAB", "GOTO"}, false), [win](ToolContext& c) {
    if (c.args.empty()) {
      std::string out = std::format("Tabs ({}):", win->tabCount());
      for (int i = 0; i < win->tabCount(); ++i)
        out += std::format("\n  {}. {}{}", i + 1, win->tabInfo(i).title.toStdString(), i == win->activeTabIndex() ? "  (active)" : "");
      c.showMessage(out);
      return;
    }
    const std::string spec = join(c.args);
    const std::string l = lower(spec);
    int tab = -1;
    const int cur = win->activeTabIndex();
    if (l == "next") tab = (cur + 1) % win->tabCount();
    else if (l == "prev" || l == "previous") tab = (cur + win->tabCount() - 1) % win->tabCount();
    else tab = win->findTabIndex(spec);
    if (tab < 0) {
      c.showMessage("TAB: no tab '" + spec + "'", true);
      return;
    }
    win->activateTabLater(tab);
  });

  // --- RENAME -----------------------------------------------------------------------------------
  add(verb("app.rename", "Rename", "Rename a body, component or feature: RENAME body:Body1 Seat", {"RENAME", "REN"}),
      [](ToolContext& c) {
        if (!c.document) return;
        if (c.args.size() < 2) {
          c.showMessage("RENAME: usage RENAME <body:|component:|feature:|group:selector> <new name>", true);
          return;
        }
        if (startsWithNoCase(c.args[0], "group:")) {
          const ObjectId g = groupByText(*c.document, c.args[0]);
          const std::string name = join(c.args, 1);
          if (!g) {
            c.showMessage("RENAME: no group '" + c.args[0].substr(6) + "'", true);
            return;
          }
          const std::string before = c.document->group(g)->name;
          if (auto cmd = core::makeRenameGroupCommand(*c.document, g, name)) pushCommand(c, std::move(cmd));
          c.showMessage(std::format("Renamed group:{} to '{}'", before, name));
          return;
        }
        SelectorResult r = resolveSelector(*c.document, c.args[0]);
        if (!r.ok() || r.refs.size() != 1) {
          c.showMessage("RENAME: " + (r.ok() ? std::string("selector must name exactly one object") : r.error), true);
          return;
        }
        const std::string name = join(c.args, 1);
        const ShapeRef& ref = r.refs.front();
        const std::string before = refLabel(*c.document, ref);
        std::unique_ptr<core::UndoCommand> cmd;
        if (ref.kind == ShapeKind::Body) cmd = makeRenameBody(*c.document, ref.owner, name);
        else if (ref.kind == ShapeKind::Component) cmd = makeRenameComponent(*c.document, ref.owner, name);
        else if (c.document->feature(ref.owner)) cmd = makeRenameFeature(*c.document, ref.owner, name);
        if (!cmd) {
          c.showMessage("RENAME: cannot rename " + before, true);
          return;
        }
        if (c.undo) c.undo->push(std::move(cmd));
        else cmd->redo();
        c.showMessage(std::format("Renamed {} to '{}'", before, name));
      });

  // --- GROUP / UNGROUP / SUPPRESS / UNSUPPRESS / COLLAPSE / EXPAND: timeline groups ----------------
  add(verb("app.group", "Group Features", "Group consecutive features (or groups): GROUP <name> [features...] (default: the selection)",
           {"GROUP"}),
      [](ToolContext& c) {
        if (!c.document) return;
        if (c.args.empty()) {
          c.showMessage("GROUP: usage GROUP <name> [feature|group ...] (no items: the selected features)", true);
          return;
        }
        std::vector<ObjectId> items;
        std::string err;
        if (!timelineItems(c, 1, items, &err)) {
          c.showMessage("GROUP: " + err, true);
          return;
        }
        auto cmd = core::makeCreateGroupCommand(*c.document, items, c.args[0], &err);
        if (!cmd) {
          c.showMessage("GROUP: " + err, true);
          return;
        }
        core::CreateGroupCommand* raw = cmd.get();
        pushCommand(c, std::move(cmd));
        const ObjectId g = raw->groupId();
        c.showMessage(std::format("Group '{}' (#{}): {} features", c.args[0], g, c.document->groupFeatures(g).size()));
      });
  add(verb("app.ungroup", "Ungroup", "Dissolve timeline groups (their features stay): UNGROUP <group> ...", {"UNGROUP"}),
      [](ToolContext& c) {
        if (!c.document) return;
        if (c.args.empty()) {
          c.showMessage("UNGROUP: usage UNGROUP <group> ...", true);
          return;
        }
        std::vector<ObjectId> groups;
        for (const std::string& a : c.args) {
          const ObjectId g = groupByText(*c.document, a);
          if (!g) {
            c.showMessage("UNGROUP: no group '" + a + "'", true);
            return;
          }
          groups.push_back(g);
        }
        const bool macro = c.undo && groups.size() > 1;
        if (macro) c.undo->beginMacro("Ungroup");
        for (ObjectId g : groups)
          if (auto cmd = core::makeUngroupCommand(*c.document, g)) pushCommand(c, std::move(cmd));
        if (macro) c.undo->endMacro();
        c.showMessage(std::format("Ungrouped {}", join(c.args)));
      });
  for (const bool suppress : {true, false}) {
    const std::string word = suppress ? "SUPPRESS" : "UNSUPPRESS";
    add(verb(suppress ? "app.suppress" : "app.unsuppress", suppress ? "Suppress" : "Unsuppress",
             word + " features and groups (one undo step): " + word + " <group|feature> ... (default: the selection)", {word}),
        [suppress, word](ToolContext& c) {
          if (!c.document) return;
          std::vector<ObjectId> items;
          std::string err;
          if (!timelineItems(c, 0, items, &err)) {
            c.showMessage(word + ": " + err, true);
            return;
          }
          auto cmd = core::makeSuppressCommand(*c.document, items, suppress);
          if (!cmd) {
            c.showMessage(std::format("{}: nothing to change (already {})", word, suppress ? "suppressed" : "unsuppressed"));
            return;
          }
          const std::string text = cmd->text();
          pushCommand(c, std::move(cmd));
          c.showMessage(text);
        });
  }
  for (const bool collapse : {true, false}) {
    const std::string word = collapse ? "COLLAPSE" : "EXPAND";
    add(verb(collapse ? "app.collapse-group" : "app.expand-group", collapse ? "Collapse Group" : "Expand Group",
             (collapse ? "Fold" : "Unfold") + std::string(" timeline groups in the feature list: ") + word + " <group> ...", {word}),
        [collapse, word](ToolContext& c) {
          if (!c.document) return;
          if (c.args.empty()) {
            c.showMessage(word + ": usage " + word + " <group> ...", true);
            return;
          }
          for (const std::string& a : c.args) {
            const ObjectId g = groupByText(*c.document, a);
            if (!g) {
              c.showMessage(word + ": no group '" + a + "'", true);
              return;
            }
            if (auto cmd = core::makeSetGroupCollapsedCommand(*c.document, g, collapse)) pushCommand(c, std::move(cmd));
          }
          c.showMessage(std::format("{} {}", collapse ? "Collapsed" : "Expanded", join(c.args)));
        });
  }

  // --- ROLLBACK: move the history marker -----------------------------------------------------------
  add(verb("app.rollback", "Roll History Marker", "Move the rollback marker: ROLLBACK <n | feature | end> (after n features / after that feature)",
           {"ROLLBACK"}),
      [](ToolContext& c) {
        if (!c.document) return;
        core::Document& doc = *c.document;
        int index = -1;
        const std::string a = c.args.empty() ? "" : join(c.args);
        double n = 0;
        if (a.empty()) {
          c.showMessage(std::format("ROLLBACK: marker after {} of {} features", doc.rollbackIndex(), doc.featureCount()));
          return;
        }
        if (lower(a) == "end") index = doc.featureCount();
        else if (parseNumber(a, n)) index = static_cast<int>(n);
        else {
          std::string err;
          const ObjectId id = timelineItem(doc, a, &err);
          if (id && doc.feature(id)) index = doc.indexOf(id) + 1;
          else if (id) index = doc.groupRange(id).second + 1;  // a group: after its last feature
          else {
            c.showMessage("ROLLBACK: " + err, true);
            return;
          }
        }
        if (index < 0 || index > doc.featureCount()) {
          c.showMessage(std::format("ROLLBACK: {} is outside 0..{}", index, doc.featureCount()), true);
          return;
        }
        if (index != doc.rollbackIndex()) pushCommand(c, std::make_unique<core::SetRollbackCommand>(doc, index));
        c.showMessage(std::format("Marker after {} of {} features", doc.rollbackIndex(), doc.featureCount()));
      });

  // --- INPUT: choose the selection input of a feature command ---------------------------------------
  add(verb("app.input", "Input", "Make a property of the active feature command the selection target: INPUT splitTool [selectors]",
           {"INPUT", "INTO"}),
      [win](ToolContext& c) {
        core::ToolManager& m = win->toolManager();
        auto* ft = dynamic_cast<part::FeatureTool*>(m.activeTool());
        part::PartFeature* f = ft ? ft->feature() : nullptr;
        if (!f) {
          c.showMessage("INPUT: no feature command with a property list is active (see OPTIONS)", true);
          return;
        }
        if (c.args.empty()) {
          c.showMessage(std::format("Active input: {}", ft->activeInput()));
          return;
        }
        const core::Property* p = f->property(c.args[0]);
        if (!p || (p->type() != core::PropertyType::Ref && p->type() != core::PropertyType::RefList)) {
          std::string names;
          for (const core::Property& q : f->properties())
            if (!q.flags().hidden && (q.type() == core::PropertyType::Ref || q.type() == core::PropertyType::RefList))
              names += (names.empty() ? "" : ", ") + q.name();
          c.showMessage(std::format("INPUT: '{}' is not a selection input of {} (inputs: {})", c.args[0], f->name(), names), true);
          return;
        }
        core::ToolContext* tctx = m.activeContext();
        ft->setActiveInput(tctx ? *tctx : c, p->name());
        if (c.args.size() > 1 && c.document && c.selection) {
          std::vector<std::string> texts(c.args.begin() + 1, c.args.end());
          SelectorResult r = resolveSelectors(*c.document, texts);
          if (!r.ok()) {
            c.showMessage("INPUT: " + r.error, true);
            return;
          }
          for (const ShapeRef& ref : r.refs)
            if (!c.selection->isAllowed(ref)) {
              c.showMessage(std::format("INPUT: {} does not accept {}", p->label(), refLabel(*c.document, ref)), true);
              return;
            }
          c.selection->set(r.refs);
          c.showMessage(std::format("{} = {}", p->name(), propertyText(*c.document, *f->property(p->name()))));
        } else {
          c.showMessage(std::format("Active input: {} ({})", p->name(), p->label()));
        }
      });
}

}  // namespace fabricad::app
