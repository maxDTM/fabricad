// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/tutorial/TutorialDefinition.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>
#include <set>

namespace fabricad::app::tutorial {

namespace {

QString typeName(const QJsonValue& v) {
  switch (v.type()) {
    case QJsonValue::String: return "string";
    case QJsonValue::Double: return "number";
    case QJsonValue::Bool: return "bool";
    case QJsonValue::Array: return "array";
    case QJsonValue::Object: return "object";
    default: return "null";
  }
}

const QStringList kStepKeys = {"id", "title", "text", "anchor", "keys_shown", "until", "skip_if", "stable_ms",
                               "timeout_ms", "hint", "do", "assert", "context", "pick", "$comment"};
const QStringList kChapterKeys = {"id", "title", "start", "checkpoint", "steps", "$comment"};
const QStringList kTourKeys = {"id", "title", "description", "version", "order", "welcome", "window", "finish",
                               "chapters", "$comment", "$schema"};

void unknownKeys(const QJsonObject& o, const QStringList& allowed, const QString& where, QStringList* errors) {
  for (auto it = o.begin(); it != o.end(); ++it)
    if (!allowed.contains(it.key())) errors->append(where + ": unknown key '" + it.key() + "'");
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Anchor

const QStringList& Anchor::kinds() {
  static const QStringList k = {"widget", "tool", "group", "menu", "prop", "valuebox", "browser", "timeline",
                                "tab", "tabclose", "model", "sketchpoint", "none"};
  return k;
}

std::optional<Anchor> Anchor::parse(const QJsonValue& v, QString* error) {
  Anchor a;
  if (v.isUndefined() || v.isNull()) return a;
  QString kind, target;
  if (v.isString()) {
    const QString s = v.toString().trimmed();
    if (s.isEmpty() || s.compare("none", Qt::CaseInsensitive) == 0) return a;
    const int colon = s.indexOf(':');
    const QString head = colon > 0 ? s.left(colon).toLower() : QString();
    if (colon > 0 && kinds().contains(head)) {
      kind = head;
      target = s.mid(colon + 1);
    } else {
      kind = "widget";  // a bare objectName (path)
      target = s;
    }
  } else if (v.isObject()) {
    const QJsonObject o = v.toObject();
    if (o.size() != 1) {
      if (error) *error = "an anchor object has exactly one key (its kind)";
      return std::nullopt;
    }
    kind = o.begin().key().toLower();
    if (!o.begin().value().isString()) {
      if (error) *error = "anchor '" + kind + "' needs a string";
      return std::nullopt;
    }
    target = o.begin().value().toString().trimmed();
    if (!kinds().contains(kind)) {
      if (error) *error = "unknown anchor kind '" + kind + "'";
      return std::nullopt;
    }
    // {"widget": "prop:distance"}: the inner prefix names the kind (plan example 2.3)
    if (kind == "widget") {
      const int colon = target.indexOf(':');
      const QString head = colon > 0 ? target.left(colon).toLower() : QString();
      if (colon > 0 && kinds().contains(head) && head != "widget") {
        kind = head;
        target = target.mid(colon + 1);
      }
    }
  } else {
    if (error) *error = "an anchor is a string or an object, not " + typeName(v);
    return std::nullopt;
  }
  if (kind != "none" && kind != "tabclose" && target.isEmpty()) {
    if (error) *error = "anchor '" + kind + "' has no target";
    return std::nullopt;
  }
  a.kind = kind;
  a.target = target;
  return a;
}

// ---------------------------------------------------------------------------------------------
// Condition

const QStringList& Condition::leafTypes() {
  static const QStringList t = {"tool", "dialog", "selection", "valuebox", "prompt", "sketch", "workspace",
                                "setting", "camera", "document", "visible", "check", "next", "tour"};
  return t;
}

bool Condition::needsNext() const {
  if (isNext()) return true;
  return std::any_of(children.begin(), children.end(), [](const Condition& c) { return c.needsNext(); });
}

QString Condition::describe() const {
  auto list = [&](const char* sep) {
    QStringList parts;
    for (const Condition& c : children) parts << c.describe();
    return "(" + parts.join(sep) + ")";
  };
  switch (op) {
    case Op::All: return list(" and ");
    case Op::Any: return list(" or ");
    case Op::Not: return "not " + (children.empty() ? QString() : children.front().describe());
    case Op::Leaf: break;
  }
  QString v = value.isString() ? value.toString() : value.isBool() ? (value.toBool() ? "true" : "false")
                                                                     : QString::number(value.toDouble());
  QString extra;
  if (args.contains("count")) extra += QString(" count %1").arg(args.value("count").toInt());
  if (args.contains("is")) extra += " is " + args.value("is").toVariant().toString();
  return type + " " + v + extra;
}

std::optional<Condition> Condition::parse(const QJsonValue& v, QString* error) {
  Condition c;
  if (!v.isObject()) {
    if (error) *error = "a condition is an object, not " + typeName(v);
    return std::nullopt;
  }
  const QJsonObject o = v.toObject();
  auto children = [&](const QJsonValue& arr, Op op) -> std::optional<Condition> {
    if (!arr.isArray() || arr.toArray().isEmpty()) {
      if (error) *error = QString("'%1' needs a non-empty array").arg(op == Op::All ? "all" : "any");
      return std::nullopt;
    }
    c.op = op;
    for (const QJsonValue& e : arr.toArray()) {
      auto ch = parse(e, error);
      if (!ch) return std::nullopt;
      c.children.push_back(*ch);
    }
    return c;
  };
  if (o.contains("all")) return children(o.value("all"), Op::All);
  if (o.contains("any")) return children(o.value("any"), Op::Any);
  if (o.contains("not")) {
    auto ch = parse(o.value("not"), error);
    if (!ch) return std::nullopt;
    c.op = Op::Not;
    c.children.push_back(*ch);
    return c;
  }
  QString leaf;
  for (const QString& t : leafTypes())
    if (o.contains(t)) {
      if (!leaf.isEmpty()) {
        if (error) *error = "a condition has one type, found '" + leaf + "' and '" + t + "' (use all)";
        return std::nullopt;
      }
      leaf = t;
    }
  if (leaf.isEmpty()) {
    if (error) *error = "unknown condition " + QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
    return std::nullopt;
  }
  static const QStringList kExtra = {"count", "selector", "is", "$comment"};
  for (auto it = o.begin(); it != o.end(); ++it)
    if (it.key() != leaf && !kExtra.contains(it.key())) {
      if (error) *error = "condition '" + leaf + "': unknown key '" + it.key() + "'";
      return std::nullopt;
    }
  c.type = leaf;
  c.value = o.value(leaf);
  c.args = o;
  // value checks per type
  const bool str = c.value.isString() && !c.value.toString().trimmed().isEmpty();
  QString bad;
  if (leaf == "next") {
    if (!c.value.isBool()) bad = "needs true";
  } else if (leaf == "sketch") {
    static const QRegularExpression dof(R"(^dof\s*(==|<=|>=|<|>|!=)\s*\d+$)", QRegularExpression::CaseInsensitiveOption);
    const QString s = c.value.toString().trimmed();
    if (!str || !(s == "active" || s == "none" || dof.match(s).hasMatch())) bad = "needs \"active\", \"none\" or \"dof == n\"";
  } else if (leaf == "camera") {
    if (c.value.toString() != "changed") bad = "needs \"changed\"";
  } else if (leaf == "document") {
    if (c.value.toString() != "changed" && c.value.toString() != "unchanged" && c.value.toString() != "saved")
      bad = "needs \"changed\", \"unchanged\" or \"saved\"";
  } else if (leaf == "workspace") {
    static const QStringList ws = {"part", "assembly", "drawing"};
    if (!ws.contains(c.value.toString().toLower())) bad = "needs part, assembly or drawing";
  } else if (leaf == "setting") {
    if (!str || !o.contains("is")) bad = "needs a key and \"is\"";
  } else if (leaf == "selection") {
    if (!str) bad = "needs a kind (or \"any\")";
    if (o.contains("count") && !o.value("count").isDouble()) bad = "count must be a number";
  } else if (!str) {
    bad = "needs a non-empty string";
  }
  if (!bad.isEmpty()) {
    if (error) *error = "condition '" + leaf + "' " + bad;
    return std::nullopt;
  }
  return c;
}

// ---------------------------------------------------------------------------------------------
// Action

const QStringList& Action::types() {
  static const QStringList t = {"click", "dclick", "rclick", "move", "drag", "wheel", "type", "key", "wait", "shot", "assert"};
  return t;
}

std::optional<Action> Action::parse(const QJsonValue& v, QString* error) {
  if (!v.isObject()) {
    if (error) *error = "an action is an object";
    return std::nullopt;
  }
  const QJsonObject o = v.toObject();
  Action a;
  for (const QString& t : types())
    if (o.contains(t)) {
      if (!a.type.isEmpty()) {
        if (error) *error = "an action has one verb, found '" + a.type + "' and '" + t + "'";
        return std::nullopt;
      }
      a.type = t;
    }
  if (a.type.isEmpty()) {
    if (error) *error = "unknown action " + QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
    return std::nullopt;
  }
  a.raw = o;
  const QJsonValue val = o.value(a.type);
  auto targetOk = [&](const QJsonValue& t) {
    if (t.isString() && t.toString() == "@anchor") return true;
    QString e;
    return Anchor::parse(t, &e).has_value();
  };
  if (a.type == "click" || a.type == "dclick" || a.type == "rclick" || a.type == "wheel") {
    if (!targetOk(val)) {
      if (error) *error = a.type + ": bad target";
      return std::nullopt;
    }
  } else if (a.type == "move") {
    const bool delta = val.isObject() && (val.toObject().contains("dx") || val.toObject().contains("dy"));
    if (!delta && !targetOk(val)) {
      if (error) *error = "move: needs a target or {dx, dy}";
      return std::nullopt;
    }
  } else if (a.type == "drag") {
    const QJsonObject d = val.toObject();
    if (!val.isObject() || !d.contains("from") || !d.contains("to")) {
      if (error) *error = "drag: needs {from, to}";
      return std::nullopt;
    }
  } else if (a.type == "type" || a.type == "key" || a.type == "shot") {
    if (!val.isString() || val.toString().isEmpty()) {
      if (error) *error = a.type + ": needs a string";
      return std::nullopt;
    }
  } else if (a.type == "wait") {
    if (!Condition::parse(val, error)) return std::nullopt;
  } else if (a.type == "assert") {
    if (!val.isArray() && !val.isString()) {
      if (error) *error = "assert: needs CHECK strings";
      return std::nullopt;
    }
  }
  return a;
}

// ---------------------------------------------------------------------------------------------
// Tour

int Tour::stepCount() const {
  int n = 0;
  for (const Chapter& c : chapters) n += static_cast<int>(c.steps.size());
  return n;
}

const Step* Tour::stepAt(int flat, int* chapter) const {
  if (flat < 0) return nullptr;
  for (int c = 0; c < static_cast<int>(chapters.size()); ++c) {
    const int n = static_cast<int>(chapters[c].steps.size());
    if (flat < n) {
      if (chapter) *chapter = c;
      return &chapters[c].steps[flat];
    }
    flat -= n;
  }
  return nullptr;
}

int Tour::indexOfStep(const QString& id) const {
  int flat = 0;
  for (const Chapter& c : chapters)
    for (const Step& s : c.steps) {
      if (s.id == id) return flat;
      ++flat;
    }
  return -1;
}

int Tour::firstStepOfChapter(int chapter) const {
  int flat = 0;
  for (int c = 0; c < chapter && c < static_cast<int>(chapters.size()); ++c) flat += static_cast<int>(chapters[c].steps.size());
  return flat;
}

QString Tour::restoreFile(int chapter) const {
  if (chapter < 0 || chapter >= static_cast<int>(chapters.size())) return {};
  auto abs = [&](const QString& f) { return f.isEmpty() ? f : QDir(baseDir).filePath(f); };
  if (!chapters[chapter].start.isEmpty()) return abs(chapters[chapter].start);
  for (int c = chapter - 1; c >= 0; --c)
    if (!chapters[c].checkpoint.isEmpty()) return abs(chapters[c].checkpoint);
  return {};
}

QStringList placeholders(const QString& text) {
  static const QRegularExpression re(R"(\{([A-Za-z_]+)\})");
  QStringList out;
  for (auto it = re.globalMatch(text); it.hasNext();) out << it.next().captured(1);
  return out;
}

const QStringList& knownPlaceholders() {
  static const QStringList k = {"timeline", "orientation"};
  return k;
}

std::optional<Tour> parseTour(const QByteArray& json, const QString& baseDir, QStringList* errors) {
  QStringList local;
  QStringList* err = errors ? errors : &local;
  QJsonParseError pe;
  const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
  if (pe.error != QJsonParseError::NoError) {
    err->append(QString("JSON error at offset %1: %2").arg(pe.offset).arg(pe.errorString()));
    return std::nullopt;
  }
  if (!doc.isObject()) {
    err->append("the tour file must hold one JSON object");
    return std::nullopt;
  }
  const QJsonObject root = doc.object();
  const int before = static_cast<int>(err->size());
  unknownKeys(root, kTourKeys, "tour", err);
  Tour t;
  t.baseDir = baseDir;
  t.id = root.value("id").toString();
  t.title = root.value("title").toString();
  t.description = root.value("description").toString();
  t.version = root.value("version").toInt(1);
  t.order = root.value("order").toInt(100);
  t.welcome = root.value("welcome").toBool(true);
  t.finishText = root.value("finish").toString();
  if (t.id.isEmpty() || !QRegularExpression("^[a-z0-9][a-z0-9_-]*$").match(t.id).hasMatch())
    err->append("tour: 'id' must be a lower-case identifier");
  if (t.title.isEmpty()) err->append("tour: 'title' is missing");
  if (const QJsonArray w = root.value("window").toArray(); w.size() == 2) {
    t.windowW = w[0].toInt();
    t.windowH = w[1].toInt();
  } else if (root.contains("window")) {
    err->append("tour: 'window' is [width, height]");
  }
  const QJsonArray chapters = root.value("chapters").toArray();
  if (chapters.isEmpty()) err->append("tour: no chapters");
  std::set<QString> stepIds, chapterIds;
  for (int ci = 0; ci < chapters.size(); ++ci) {
    const QJsonObject co = chapters[ci].toObject();
    Chapter ch;
    ch.id = co.value("id").toString();
    ch.title = co.value("title").toString();
    ch.start = co.value("start").toString();
    ch.checkpoint = co.value("checkpoint").toString();
    const QString cwhere = "chapter " + (ch.id.isEmpty() ? QString::number(ci + 1) : ch.id);
    unknownKeys(co, kChapterKeys, cwhere, err);
    if (ch.id.isEmpty()) err->append(cwhere + ": 'id' is missing");
    else if (!chapterIds.insert(ch.id).second) err->append(cwhere + ": duplicate chapter id");
    if (ch.title.isEmpty()) err->append(cwhere + ": 'title' is missing");
    const QJsonArray steps = co.value("steps").toArray();
    if (steps.isEmpty()) err->append(cwhere + ": no steps");
    for (int si = 0; si < steps.size(); ++si) {
      const QJsonObject so = steps[si].toObject();
      Step s;
      s.id = so.value("id").toString();
      const QString where = "step " + (s.id.isEmpty() ? cwhere + "/" + QString::number(si + 1) : s.id);
      unknownKeys(so, kStepKeys, where, err);
      if (s.id.isEmpty()) err->append(where + ": 'id' is missing");
      else if (!stepIds.insert(s.id).second) err->append(where + ": duplicate step id");
      s.title = so.value("title").toString();
      s.text = so.value("text").toString();
      if (s.text.trimmed().isEmpty()) err->append(where + ": 'text' is missing");
      for (const QString& p : placeholders(s.text + ' ' + so.value("hint").toString()))
        if (!knownPlaceholders().contains(p)) err->append(where + ": unknown placeholder {" + p + "}");
      QString e;
      if (auto a = Anchor::parse(so.value("anchor"), &e)) s.anchor = *a;
      else err->append(where + ": anchor: " + e);
      for (const QJsonValue& k : so.value("keys_shown").toArray()) s.keysShown << k.toString();
      if (so.contains("until")) {
        if (auto c = Condition::parse(so.value("until"), &e)) s.until = *c;
        else err->append(where + ": until: " + e);
      } else {
        s.until.type = "next";
        s.until.value = true;
      }
      if (so.contains("skip_if")) {
        if (auto c = Condition::parse(so.value("skip_if"), &e)) s.skipIf = *c;
        else err->append(where + ": skip_if: " + e);
      }
      s.stableMs = so.value("stable_ms").toInt(150);
      s.timeoutMs = so.value("timeout_ms").toInt(20000);
      s.hint = so.value("hint").toString();
      for (const QJsonValue& av : so.value("do").toArray()) {
        if (auto a = Action::parse(av, &e)) s.actions.push_back(*a);
        else err->append(where + ": do: " + e);
      }
      if (so.value("assert").isString()) s.asserts << so.value("assert").toString();
      for (const QJsonValue& a : so.value("assert").toArray()) s.asserts << a.toString();
      s.context = so.value("context").toString("idle");
      if (s.context != "idle" && !s.context.startsWith("tool:") && !s.context.startsWith("cmd:"))
        err->append(where + ": context is \"idle\", \"tool:<id>\" or \"cmd:<command line>[; ...]\"");
      s.pick = so.value("pick").toString();
      if (!s.pick.isEmpty() && !QStringList{"edge", "face", "vertex", "body"}.contains(s.pick))
        err->append(where + ": pick is \"edge\", \"face\", \"vertex\" or \"body\"");
      ch.steps.push_back(std::move(s));
    }
    t.chapters.push_back(std::move(ch));
  }
  if (err->size() > before) return std::nullopt;
  return t;
}

std::optional<Tour> loadTour(const QString& path, QStringList* errors) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (errors) errors->append(path + ": cannot open");
    return std::nullopt;
  }
  QStringList local;
  auto t = parseTour(f.readAll(), QFileInfo(path).absolutePath(), &local);
  if (errors)
    for (const QString& e : local) errors->append(path + ": " + e);
  return t;
}

QStringList validateTour(const Tour& tour, const std::function<bool(const QString&)>& toolExists) {
  QStringList out;
  std::function<void(const Condition&, const QString&)> walk = [&](const Condition& c, const QString& where) {
    for (const Condition& ch : c.children) walk(ch, where);
    if (c.op == Condition::Op::Leaf && c.type == "tool") {
      const QString id = c.value.toString();
      if (id != "none" && toolExists && !toolExists(id)) out << where + ": unknown tool '" + id + "'";
    }
  };
  for (const Chapter& ch : tour.chapters)
    for (const Step& s : ch.steps) {
      const QString where = tour.id + " step " + s.id;
      if (s.anchor.kind == "tool" && toolExists && !toolExists(s.anchor.target))
        out << where + ": anchor names unknown tool '" + s.anchor.target + "'";
      if (s.context.startsWith("tool:") && toolExists && !toolExists(s.context.mid(5)))
        out << where + ": context names unknown tool '" + s.context.mid(5) + "'";
      walk(s.until, where);
      if (s.skipIf) walk(*s.skipIf, where);
    }
  return out;
}

QStringList tourFiles() {
  QStringList out;
  auto scan = [&](const QString& dir) {
    const QDir d(dir);
    for (const QString& sub : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
      const QString f = d.filePath(sub + "/tutorial.json");
      if (QFileInfo::exists(f)) out << f;
    }
  };
  scan(":/tutorials");
  for (const QString& dir : qEnvironmentVariable("FABRICAD_TUTORIAL_PATH").split(':', Qt::SkipEmptyParts)) scan(dir);
  return out;
}

std::vector<Tour> discoverTours(QStringList* errors) {
  std::vector<Tour> out;
  for (const QString& f : tourFiles()) {
    auto t = loadTour(f, errors);
    if (!t) continue;
    auto same = std::find_if(out.begin(), out.end(), [&](const Tour& x) { return x.id == t->id; });
    if (same != out.end()) {
      // a folder on FABRICAD_TUTORIAL_PATH overrides the built-in copy (edit a tour without a rebuild)
      if (same->baseDir.startsWith(':') && !t->baseDir.startsWith(':')) *same = std::move(*t);
      else if (errors) errors->append(f + ": tour id '" + t->id + "' is used twice (skipped)");
      continue;
    }
    out.push_back(std::move(*t));
  }
  std::stable_sort(out.begin(), out.end(), [](const Tour& a, const Tour& b) {
    return a.order != b.order ? a.order < b.order : a.title.compare(b.title, Qt::CaseInsensitive) < 0;
  });
  return out;
}

}  // namespace fabricad::app::tutorial
