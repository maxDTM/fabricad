// FabriCAD tutorial engine: tour definition (docs/plans/tutorial.md section 3).
//
// A tour is one strict-JSON file (resources/tutorials/<id>/tutorial.json, compiled into the qrc, or a
// folder on FABRICAD_TUTORIAL_PATH). The same file drives the in-app overlay, the offscreen lint
// test (every anchor resolves) and the opt-in real-input GUI runner (tools/uitest/tutorial_run.py).
//
// Step schema (all keys but "id" and "text" optional; see docs/plans/tutorial.md "Schema"):
//   { "id": "2.3",                         unique in the tour
//     "title": "Extrude",                  bubble title (caps); default: the chapter title
//     "text": "Type **8**, press [[Enter]].",   body, shown as typed; **bold**, [[Key]] keycap,
//                                          {timeline} / {orientation} placeholders
//     "anchor": "prop:distance" | {"widget": "FinishSketchButton"} | {"model": "plane:XY"},
//     "keys_shown": ["8", "Enter"],        keycap row under the text
//     "until": <condition>,                completion (default {"next": true}: the NEXT button)
//     "skip_if": <condition>,              true when the step starts: the step is skipped
//     "stable_ms": 150, "timeout_ms": 20000,
//     "hint": "...",                       shown after timeout_ms or on a wrong action
//     "do": [<action>...],                 canonical real input (GUI runner; "show me" path)
//     "assert": ["body:Body1 volume ~ 32000 0.01%"],   CHECK expressions after the step (runner)
//     "context": "idle" | "tool:<id>" | "cmd:<line>; <line>",  app state the lint test resolves the
//                                          anchor in (on the chapter's start file, else its checkpoint);
//                                          Back re-enters it when the step's tool is gone
//     "pick": "edge" | "face" | "vertex" | "body" }  selection priority while the step runs
//                                          (the step's clicks hit the right kind first)
// Conditions: {"tool": id|"none"} {"dialog": "card"|"modal"|"popup"|<objectName>}
//   {"selection": kind|"any", "count": n, "selector": s} {"valuebox": caption} {"prompt": text}
//   {"sketch": "active"|"none"|"dof == 0"} {"workspace": "part"|"assembly"|"drawing"}
//   {"setting": "ui/key", "is": value} {"camera": "changed"} {"document": "changed"|"unchanged"|"saved"}
//   {"visible": objectName} {"check": "<CHECK expression>"} {"next": true} {"tour": stepId}
//   {"all": [...]} {"any": [...]} {"not": <condition>}
// Actions: {"click"|"dclick"|"rclick": target, "mods": "shift"} {"move": target | {"dx","dy"}}
//   {"drag": {"from", "to", "button", "mods", "steps"}} {"wheel": target, "clicks": n}
//   {"type": text} {"key": "ctrl+Return"} {"wait": <condition>} {"shot": name} {"assert": [..]}
//   target: "@anchor" (the step anchor) or any anchor string.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>
#include <vector>

namespace fabricad::app::tutorial {

/// What a coach mark points at. `kind` is one of kinds(); `target` the rest ("part.extrude").
struct Anchor {
  QString kind = "none";
  QString target;

  bool isNone() const { return kind == QLatin1String("none"); }
  bool isModel() const { return kind == QLatin1String("model") || kind == QLatin1String("sketchpoint"); }
  QString toString() const { return isNone() ? QStringLiteral("none") : kind + ':' + target; }
  /// "tool:part.extrude", {"widget": "OkButton"}, {"model": "plane:XY"}, "FinishSketchButton" (widget).
  static std::optional<Anchor> parse(const QJsonValue& v, QString* error);
  static const QStringList& kinds();
};

/// A completion / skip / wait condition: a leaf ({"tool": "part.extrude"}) or all / any / not.
struct Condition {
  enum class Op { Leaf, All, Any, Not };
  Op op = Op::Leaf;
  QString type;       ///< leaf type ("tool", "check", ...)
  QJsonValue value;   ///< the leaf's main value
  QJsonObject args;   ///< the whole leaf object (extra fields: count, selector, is)
  std::vector<Condition> children;

  bool isNext() const { return op == Op::Leaf && type == QLatin1String("next"); }
  /// True when the condition (or any child) waits for the NEXT button.
  bool needsNext() const;
  QString describe() const;
  static std::optional<Condition> parse(const QJsonValue& v, QString* error);
  static const QStringList& leafTypes();
};

struct Action {
  QString type;       ///< click dclick rclick move drag wheel type key wait shot assert
  QJsonObject raw;
  static std::optional<Action> parse(const QJsonValue& v, QString* error);
  static const QStringList& types();
};

struct Step {
  QString id;
  QString title;
  QString text;
  Anchor anchor;
  QStringList keysShown;
  Condition until;  ///< default {"next": true}
  std::optional<Condition> skipIf;
  int stableMs = 150;
  int timeoutMs = 20000;
  QString hint;
  std::vector<Action> actions;
  QStringList asserts;
  QString context = "idle";  ///< lint: "idle", "tool:<id>" or "cmd:<command line>; ..."; Back re-enters it
  QString pick;              ///< "edge" / "face" / "vertex" / "body": selection priority during the step
};

struct Chapter {
  QString id;
  QString title;
  QString start;       ///< .fcad (relative to the tour folder) the chapter starts from; empty: previous checkpoint
  QString checkpoint;  ///< .fcad saved at the end of the chapter (by the GUI runner)
  std::vector<Step> steps;
};

struct Tour {
  QString id;
  QString title;
  QString description;
  int version = 1;
  int order = 100;      ///< Help > Tutorials order (lower first)
  bool welcome = true;  ///< offered by the first-launch welcome card
  int windowW = 0, windowH = 0;  ///< the GUI runner's window size
  QString finishText;
  QString baseDir;      ///< folder of tutorial.json (":/tutorials/<id>" or a disk path)
  std::vector<Chapter> chapters;

  int stepCount() const;
  /// Flat index -> (chapter, step); nullptr when out of range.
  const Step* stepAt(int flat, int* chapter = nullptr) const;
  int indexOfStep(const QString& id) const;
  int firstStepOfChapter(int chapter) const;
  /// The .fcad a "Restore chapter" opens for `chapter` (absolute or ":/" path), empty for an empty document.
  QString restoreFile(int chapter) const;
};

/// Text helpers shared by the bubble and the lint.
QStringList placeholders(const QString& text);  ///< {name} occurrences
const QStringList& knownPlaceholders();          ///< timeline, orientation

/// Parse one tour. `errors` collects every problem ("step 2.3: unknown anchor kind 'foo'").
/// Returns nullopt when the file is not a usable tour (the errors say why).
std::optional<Tour> parseTour(const QByteArray& json, const QString& baseDir, QStringList* errors);
std::optional<Tour> loadTour(const QString& path, QStringList* errors);

/// Semantic checks that need the app: unknown tool ids (`toolExists`), duplicate ids are already
/// reported by parseTour. Returns the problems found.
QStringList validateTour(const Tour& tour, const std::function<bool(const QString&)>& toolExists);

/// Every tour: ":/tutorials/*/tutorial.json" plus "<dir>/*/tutorial.json" for each folder on
/// FABRICAD_TUTORIAL_PATH (':'-separated; a folder there overrides the built-in tour with the same id).
/// Sorted by order, then title. Broken files are skipped and reported in `errors`.
std::vector<Tour> discoverTours(QStringList* errors = nullptr);
/// The tour files discoverTours() reads (lint test).
QStringList tourFiles();

}  // namespace fabricad::app::tutorial
