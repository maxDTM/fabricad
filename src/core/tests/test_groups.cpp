// Timeline groups (docs/plans/feature-list-timeline.md, core part): creation from a selection,
// nesting, rename / ungroup / collapse with undo, the invariants under insert, delete, move,
// group move, undo and rollback, group suppress (one undo step, tri-state) and the .fcad round trip.
#include "core/Commands.h"
#include "core/Document.h"
#include "core/GroupCommands.h"
#include "core/UndoStack.h"
#include "support/TestFeatures.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <set>

using namespace fabricad::core;
using namespace fabricad::core::test;

namespace {

/// Every group non-empty, consecutive, with a known parent; rows consistent with groupOf.
bool invariantsHold(const Document& doc, std::string* why = nullptr) {
  auto fail = [&](std::string w) {
    if (why) *why = std::move(w);
    return false;
  };
  for (ObjectId g : doc.groupIds()) {
    const TimelineGroup* tg = doc.group(g);
    if (!tg) return fail("listed group missing");
    if (tg->parent != kInvalidId && !doc.group(tg->parent)) return fail("unknown parent of " + tg->name);
    const auto members = doc.groupFeatures(g);
    if (members.empty()) return fail("empty group " + tg->name);
    const auto [a, b] = doc.groupRange(g);
    if (b - a + 1 != static_cast<int>(members.size())) return fail("group not consecutive: " + tg->name);
    if (tg->parent != kInvalidId) {
      const auto [pa, pb] = doc.groupRange(tg->parent);
      if (a < pa || b > pb) return fail("group outside its parent: " + tg->name);
    }
  }
  if (static_cast<int>(doc.groupIds().size()) != doc.groupCount()) return fail("groupIds/groupCount mismatch");
  int features = 0;
  for (const TimelineItem& it : doc.timelineItems()) {
    if (it.kind == TimelineItem::Kind::Feature) {
      if (doc.groupOf(it.id) != it.parent) return fail("row parent mismatch");
      if (doc.featureAt(features++)->id() != it.id) return fail("row order mismatch");
    }
    if (static_cast<int>(doc.groupChain(it.id).size()) != it.depth) return fail("row depth mismatch");
  }
  if (features != doc.featureCount()) return fail("rows miss features");
  return true;
}

#define VERIFY_INVARIANTS(doc)                       \
  do {                                               \
    std::string why_;                                \
    QVERIFY2(invariantsHold(doc, &why_), why_.c_str()); \
  } while (0)

/// n independent boxes (no dependencies, every order is valid).
std::vector<ObjectId> boxes(Document& doc, int n) {
  std::vector<ObjectId> ids;
  for (int i = 0; i < n; ++i) ids.push_back(doc.addFeature(std::make_unique<BoxFeature>())->id());
  doc.recompute();
  return ids;
}

std::vector<ObjectId> order(const Document& doc) {
  std::vector<ObjectId> out;
  for (const auto& f : doc.features()) out.push_back(f->id());
  return out;
}

}  // namespace

class GroupsTest : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase() { registerTestFeatures(); }

  void createFromSelection() {
    Document doc;
    auto b = boxes(doc, 5);
    QSignalSpy changed(&doc, &Document::groupsChanged);
    ObjectId g = doc.createGroup({b[3], b[1], b[2]}, "Base plate");  // any order
    QVERIFY(g != kInvalidId);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(doc.group(g)->name, std::string("Base plate"));
    QCOMPARE(doc.groupFeatures(g), (std::vector<ObjectId>{b[1], b[2], b[3]}));
    QCOMPARE(doc.groupRange(g), std::make_pair(1, 3));
    QCOMPARE(doc.groupOf(b[2]), g);
    QCOMPARE(doc.groupOf(b[0]), kInvalidId);
    QCOMPARE(doc.groupChildren(kInvalidId), (std::vector<ObjectId>{b[0], g, b[4]}));
    QCOMPARE(doc.findGroup("base PLATE"), g);
    auto rows = doc.timelineItems();
    QCOMPARE(rows.size(), size_t(6));
    QCOMPARE(rows[1], (TimelineItem{TimelineItem::Kind::Group, g, 0, kInvalidId}));
    QCOMPARE(rows[2], (TimelineItem{TimelineItem::Kind::Feature, b[1], 1, g}));
    QCOMPARE(rows[5], (TimelineItem{TimelineItem::Kind::Feature, b[4], 0, kInvalidId}));
    QVERIFY(doc.isModified());
    // default names
    ObjectId g2 = doc.createGroup({b[4]});
    QCOMPARE(doc.group(g2)->name, std::string("Group1"));
    VERIFY_INVARIANTS(doc);
  }

  void createRefusedOrNested() {
    Document doc;
    auto b = boxes(doc, 6);
    std::string why;
    QVERIFY(!doc.canCreateGroup({}, &why));
    QVERIFY(!doc.canCreateGroup({b[0], b[2]}, &why));
    QVERIFY(QString::fromStdString(why).contains("consecutive"));
    QVERIFY(!doc.canCreateGroup({9999}, &why));
    ObjectId g = doc.createGroup({b[1], b[2], b[3]}, "G");
    QVERIFY(!doc.canCreateGroup({b[0], b[1]}, &why));
    QVERIFY(QString::fromStdString(why).contains("would split group 'G'"));
    QCOMPARE(doc.createGroup({b[3], b[4]}, "X", kInvalidId, &why), kInvalidId);
    // nested inside
    ObjectId inner = doc.createGroup({b[2], b[3]}, "Inner");
    QCOMPARE(doc.group(inner)->parent, g);
    QCOMPARE(doc.groupOf(b[3]), inner);
    QCOMPARE(doc.groupChain(b[3]), (std::vector<ObjectId>{inner, g}));
    QVERIFY(doc.isInGroup(b[3], g));
    QVERIFY(doc.isInGroup(inner, g));
    QCOMPARE(doc.groupChildren(g), (std::vector<ObjectId>{b[1], inner}));
    // enclosing: a group plus a feature next to it
    ObjectId outer = doc.createGroup({g, b[4]}, "Outer");
    QCOMPARE(doc.group(g)->parent, outer);
    QCOMPARE(doc.groupOf(b[4]), outer);
    QCOMPARE(doc.groupChildren(outer), (std::vector<ObjectId>{g, b[4]}));
    // the same features as a group -> inside it; the group itself -> around it
    ObjectId same = doc.createGroup({b[2], b[3]}, "Same");
    QCOMPARE(doc.group(same)->parent, inner);
    ObjectId wrap = doc.createGroup({inner}, "Wrap");
    QCOMPARE(doc.group(inner)->parent, wrap);
    QCOMPARE(doc.group(wrap)->parent, g);
    QCOMPARE(doc.groupChain(b[2]), (std::vector<ObjectId>{same, inner, wrap, g, outer}));
    QCOMPARE(doc.groupIds(), (std::vector<ObjectId>{outer, g, wrap, inner, same}));
    // an id that is in use is refused
    QCOMPARE(doc.createGroup({b[5]}, "Dup", g), kInvalidId);
    VERIFY_INVARIANTS(doc);
  }

  void commandsUndoRedo() {
    Document doc;
    auto b = boxes(doc, 4);
    UndoStack& u = doc.undoStack();
    u.clear();
    const TimelineGroupState empty = doc.groupState();
    std::string why;
    QVERIFY(!makeCreateGroupCommand(doc, {b[0], b[2]}, "Bad", &why));
    auto create = makeCreateGroupCommand(doc, {b[1], b[2]}, "Wall");
    QVERIFY(create);
    CreateGroupCommand* raw = create.get();
    u.push(std::move(create));
    const ObjectId g = raw->groupId();
    QVERIFY(doc.group(g));
    const TimelineGroupState created = doc.groupState();
    u.undo();
    QCOMPARE(doc.groupState(), empty);
    u.redo();
    QCOMPARE(doc.groupState(), created);
    QCOMPARE(raw->groupId(), g);  // same id on redo

    QVERIFY(!makeRenameGroupCommand(doc, g, "Wall"));  // no change
    QVERIFY(!makeRenameGroupCommand(doc, g, ""));
    u.push(makeRenameGroupCommand(doc, g, "Back wall"));
    QCOMPARE(doc.group(g)->name, std::string("Back wall"));
    QCOMPARE(u.undoText(), std::string("Rename group Wall"));
    u.push(makeSetGroupCollapsedCommand(doc, g, true));
    QVERIFY(doc.group(g)->collapsed);
    QVERIFY(!makeSetGroupCollapsedCommand(doc, g, true));
    u.undo();
    QVERIFY(!doc.group(g)->collapsed);
    u.undo();
    QCOMPARE(doc.group(g)->name, std::string("Wall"));
    u.redo();
    u.redo();
    QVERIFY(doc.group(g)->collapsed);

    // ungroup with a nested group: the child moves up to the parent
    ObjectId inner = doc.createGroup({b[2]}, "Inner");
    u.clear();
    const TimelineGroupState nested = doc.groupState();
    u.push(makeUngroupCommand(doc, g));
    QVERIFY(!doc.group(g));
    QCOMPARE(doc.group(inner)->parent, kInvalidId);
    QCOMPARE(doc.groupOf(b[1]), kInvalidId);
    VERIFY_INVARIANTS(doc);
    u.undo();
    QCOMPARE(doc.groupState(), nested);
    QVERIFY(!makeUngroupCommand(doc, 12345));
  }

  void insertJoinsTheGroupItLandsIn() {
    Document doc;
    auto b = boxes(doc, 4);
    ObjectId g = doc.createGroup({b[1], b[2]}, "G");
    // between two members -> member
    Feature* in = doc.addFeature(std::make_unique<BoxFeature>(), 2);
    QCOMPARE(doc.groupOf(in->id()), g);
    // right after the last member or before the first -> outside
    Feature* after = doc.addFeature(std::make_unique<BoxFeature>(), 4);
    QCOMPARE(doc.groupOf(after->id()), kInvalidId);
    Feature* before = doc.addFeature(std::make_unique<BoxFeature>(), 1);
    QCOMPARE(doc.groupOf(before->id()), kInvalidId);
    VERIFY_INVARIANTS(doc);
  }

  void rollbackInsideAGroup() {
    Document doc;
    auto b = boxes(doc, 4);
    ObjectId g = doc.createGroup({b[0], b[1], b[2]}, "G");
    doc.setRollbackIndex(2);  // marker between b[1] and b[2], inside G
    QCOMPARE(doc.rollbackIndex(), 2);
    QVERIFY(doc.isFeatureActive(b[1]));
    QVERIFY(!doc.isFeatureActive(b[2]));
    QCOMPARE(doc.groupFeatures(g).size(), size_t(3));  // rolled-back members stay members
    // a new feature at the marker lands inside the group and the marker advances past it
    doc.undoStack().push(std::make_unique<AddFeatureCommand>(doc, std::make_unique<BoxFeature>()));
    ObjectId added = doc.featureAt(2)->id();
    QCOMPARE(doc.groupOf(added), g);
    QCOMPARE(doc.rollbackIndex(), 3);
    QCOMPARE(doc.featureAt(2)->status(), FeatureStatus::Ok);
    doc.undoStack().undo();
    QCOMPARE(doc.rollbackIndex(), 2);
    QCOMPARE(doc.groupFeatures(g), (std::vector<ObjectId>{b[0], b[1], b[2]}));
    // the marker inside a group survives save/load
    QJsonObject json = doc.toJson();
    Document copy;
    QVERIFY(copy.fromJson(json));
    QCOMPARE(copy.rollbackIndex(), 2);
    QCOMPARE(copy.groupState(), doc.groupState());
    // group suppress while the marker sits inside: all members, active or not
    doc.undoStack().push(makeGroupSuppressCommand(doc, g, true));
    QCOMPARE(doc.groupSuppression(g), GroupSuppression::All);
    VERIFY_INVARIANTS(doc);
  }

  void deleteKeepsOrRemovesGroups() {
    Document doc;
    auto b = boxes(doc, 5);
    ObjectId g = doc.createGroup({b[1], b[2], b[3]}, "G");
    ObjectId solo = doc.createGroup({b[4]}, "Solo");
    doc.setGroupCollapsed(solo, true);
    UndoStack& u = doc.undoStack();
    u.clear();
    const TimelineGroupState full = doc.groupState();
    // deleting a middle member keeps the group consecutive
    u.push(std::make_unique<RemoveFeatureCommand>(doc, b[2]));
    QCOMPARE(doc.groupFeatures(g), (std::vector<ObjectId>{b[1], b[3]}));
    // deleting the last member removes the group
    u.push(std::make_unique<RemoveFeatureCommand>(doc, b[4]));
    QVERIFY(!doc.group(solo));
    VERIFY_INVARIANTS(doc);
    // undo brings members and the removed group back exactly (id, name, collapsed)
    u.undo();
    QVERIFY(doc.group(solo));
    QVERIFY(doc.group(solo)->collapsed);
    QCOMPARE(doc.group(solo)->name, std::string("Solo"));
    u.undo();
    QCOMPARE(doc.groupState(), full);
    u.redo();
    u.redo();
    QVERIFY(!doc.group(solo));
    QCOMPARE(doc.groupFeatures(g), (std::vector<ObjectId>{b[1], b[3]}));
    VERIFY_INVARIANTS(doc);
  }

  void deleteNested() {
    Document doc;
    auto b = boxes(doc, 4);
    ObjectId outer = doc.createGroup({b[1], b[2]}, "Outer");
    ObjectId inner = doc.createGroup({b[2]}, "Inner");
    ObjectId lone = doc.createGroup({b[3]}, "LoneOuter");
    ObjectId loneInner = doc.createGroup({b[3]}, "LoneInner");
    QCOMPARE(doc.group(loneInner)->parent, lone);
    doc.takeFeature(b[2]);
    QVERIFY(!doc.group(inner));
    QVERIFY(doc.group(outer));  // still holds b[1]
    doc.takeFeature(b[3]);
    QVERIFY(!doc.group(loneInner));
    QVERIFY(!doc.group(lone));  // both emptied at once
    VERIFY_INVARIANTS(doc);
  }

  void directTakeAndReinsertRestoresGroups() {
    // modules that take features and put them back themselves (component delete + undo) get the
    // groups back without knowing about them, including groups that vanished on the way
    Document doc;
    auto b = boxes(doc, 5);
    ObjectId g = doc.createGroup({b[1], b[2], b[3]}, "G");
    ObjectId edge = doc.createGroup({b[3]}, "Edge");
    const TimelineGroupState before = doc.groupState();
    std::vector<std::pair<int, std::unique_ptr<Feature>>> taken;
    for (ObjectId id : {b[3], b[1]}) {  // highest index first
      const int idx = doc.indexOf(id);
      taken.emplace_back(idx, doc.takeFeature(id));
    }
    QVERIFY(!doc.group(edge));
    QCOMPARE(doc.groupFeatures(g), (std::vector<ObjectId>{b[2]}));
    for (auto it = taken.rbegin(); it != taken.rend(); ++it) doc.addFeature(std::move(it->second), it->first);
    QCOMPARE(order(doc), b);
    QCOMPARE(doc.groupState(), before);
    // the same when the whole group vanished
    for (ObjectId id : {b[3], b[2], b[1]}) taken.emplace_back(doc.indexOf(id), doc.takeFeature(id));
    QCOMPARE(doc.groupCount(), 0);
    for (int i = static_cast<int>(taken.size()) - 1; i >= 2; --i) doc.addFeature(std::move(taken[static_cast<size_t>(i)].second), taken[static_cast<size_t>(i)].first);
    QCOMPARE(doc.groupState(), before);
    VERIFY_INVARIANTS(doc);
  }

  void moveAutomaticMembership() {
    Document doc;
    auto b = boxes(doc, 6);  // b0 [b1 b2 b3] b4 b5
    ObjectId g = doc.createGroup({b[1], b[2], b[3]}, "G");
    UndoStack& u = doc.undoStack();
    u.clear();
    const TimelineGroupState start = doc.groupState();
    // into the inside of the group -> joins
    u.push(std::make_unique<MoveFeatureCommand>(doc, b[5], 2));
    QCOMPARE(doc.groupOf(b[5]), g);
    QCOMPARE(doc.groupFeatures(g).size(), size_t(4));
    u.undo();
    QCOMPARE(order(doc), b);
    QCOMPARE(doc.groupState(), start);
    // past the group (from before to right after it) -> stays outside
    u.push(std::make_unique<MoveFeatureCommand>(doc, b[0], 3));
    QCOMPARE(order(doc), (std::vector<ObjectId>{b[1], b[2], b[3], b[0], b[4], b[5]}));
    QCOMPARE(doc.groupOf(b[0]), kInvalidId);
    u.undo();
    // a member to the group's own edge -> stays a member
    u.push(std::make_unique<MoveFeatureCommand>(doc, b[1], 3));
    QCOMPARE(order(doc), (std::vector<ObjectId>{b[0], b[2], b[3], b[1], b[4], b[5]}));
    QCOMPARE(doc.groupOf(b[1]), g);
    u.undo();
    // a member moved away -> leaves
    u.push(std::make_unique<MoveFeatureCommand>(doc, b[2], 5));
    QCOMPARE(doc.groupOf(b[2]), kInvalidId);
    QCOMPARE(doc.groupFeatures(g), (std::vector<ObjectId>{b[1], b[3]}));
    VERIFY_INVARIANTS(doc);
    u.undo();
    QCOMPARE(doc.groupState(), start);
    QCOMPARE(order(doc), b);
    // the only member of a group moved away -> the group is gone; undo brings it back
    ObjectId solo = doc.createGroup({b[5]}, "Solo");
    u.clear();
    const TimelineGroupState withSolo = doc.groupState();
    u.push(std::make_unique<MoveFeatureCommand>(doc, b[5], 0));
    QVERIFY(!doc.group(solo));
    u.undo();
    QCOMPARE(doc.groupState(), withSolo);
    u.redo();
    QVERIFY(!doc.group(solo));
    VERIFY_INVARIANTS(doc);
  }

  void moveExplicitTarget() {
    Document doc;
    auto b = boxes(doc, 5);  // b0 [b1 b2] b3 b4
    ObjectId g = doc.createGroup({b[1], b[2]}, "G");
    std::string why;
    // b3 sits right after G: it can join G at the same index
    QVERIFY(doc.canMoveFeatureTo(b[3], 3, g, &why));
    QVERIFY(doc.moveFeatureTo(b[3], 3, g));
    QCOMPARE(doc.groupOf(b[3]), g);
    // and leave it again at the same place
    QVERIFY(doc.moveFeatureTo(b[3], 3, kInvalidId));
    QCOMPARE(doc.groupOf(b[3]), kInvalidId);
    // top level in the middle of G would split it
    QVERIFY(!doc.canMoveFeatureTo(b[4], 2, kInvalidId, &why));
    QVERIFY(QString::fromStdString(why).contains("would split group 'G'"));
    // G does not reach index 4
    QVERIFY(!doc.canMoveFeatureTo(b[0], 4, g, &why));
    QVERIFY(QString::fromStdString(why).contains("does not reach"));
    QVERIFY(!doc.canMoveFeatureTo(b[0], 4, 777, &why));
    // a group whose only member moves goes with it when targeted explicitly
    ObjectId solo = doc.createGroup({b[4]}, "Solo");
    QVERIFY(doc.moveFeatureTo(b[4], 0, solo));
    QCOMPARE(doc.featureAt(0)->id(), b[4]);
    QCOMPARE(doc.groupOf(b[4]), solo);
    // a command with a target, undo restores
    doc.undoStack().clear();
    const TimelineGroupState s = doc.groupState();
    const auto o = order(doc);
    doc.undoStack().push(std::make_unique<MoveFeatureCommand>(doc, b[0], doc.groupRange(g).second, g));
    QCOMPARE(doc.groupOf(b[0]), g);
    doc.undoStack().undo();
    QCOMPARE(doc.groupState(), s);
    QCOMPARE(order(doc), o);
    VERIFY_INVARIANTS(doc);
  }

  void moveRespectsDependencies() {
    Document doc;
    Feature* box = doc.addFeature(std::make_unique<BoxFeature>());
    doc.recompute();
    auto hole = std::make_unique<HoleFeature>();
    hole->prop("body").setRef(ShapeRef::body(box->createdBodies()[0]));
    Feature* h = doc.addFeature(std::move(hole));
    Feature* other = doc.addFeature(std::make_unique<BoxFeature>());
    doc.recompute();
    ObjectId g = doc.createGroup({h->id(), other->id()}, "G");
    std::string why;
    // the hole cannot move before its box, whatever the group target
    QVERIFY(!doc.canMoveFeatureTo(h->id(), 0, std::nullopt, &why));
    QVERIFY(QString::fromStdString(why).contains("input"));
    QVERIFY(!doc.canMoveFeatureTo(h->id(), 0, kInvalidId, &why));
    // nor can the group as a whole
    QVERIFY(!doc.canMoveGroup(g, 0, &why));
    QVERIFY(QString::fromStdString(why).contains("before its input"));
    // the box cannot move into or past the group (after its dependent)
    QVERIFY(!doc.canMoveFeature(box->id(), 1, &why));
    QVERIFY(!doc.canMoveFeatureTo(box->id(), 2, g, &why));
    // inside the group the independent box may move freely
    QVERIFY(doc.moveFeature(other->id(), 1));
    QCOMPARE(doc.groupOf(other->id()), g);
    VERIFY_INVARIANTS(doc);
  }

  void moveWholeGroup() {
    Document doc;
    auto b = boxes(doc, 6);  // b0 [b1 b2] b3 [b4 b5]
    ObjectId g = doc.createGroup({b[1], b[2]}, "G");
    ObjectId h = doc.createGroup({b[4], b[5]}, "H");
    UndoStack& u = doc.undoStack();
    u.clear();
    const TimelineGroupState start = doc.groupState();
    std::string why;
    QVERIFY(!doc.canMoveGroup(g, 5, &why));  // out of range: 2 features need index <= 4
    QVERIFY(!doc.canMoveGroup(99, 0, &why));
    // past b3 -> b0 b3 [b1 b2] [b4 b5]
    u.push(std::make_unique<MoveGroupCommand>(doc, g, 2));
    QCOMPARE(order(doc), (std::vector<ObjectId>{b[0], b[3], b[1], b[2], b[4], b[5]}));
    QCOMPARE(doc.group(g)->parent, kInvalidId);
    u.undo();
    QCOMPARE(order(doc), b);
    QCOMPARE(doc.groupState(), start);
    // into the middle of H -> nested in H
    u.push(std::make_unique<MoveGroupCommand>(doc, g, 3));
    QCOMPARE(order(doc), (std::vector<ObjectId>{b[0], b[3], b[4], b[1], b[2], b[5]}));
    QCOMPARE(doc.group(g)->parent, h);
    VERIFY_INVARIANTS(doc);
    // and out again to the front: leaves H
    u.push(std::make_unique<MoveGroupCommand>(doc, g, 0));
    QCOMPARE(doc.group(g)->parent, kInvalidId);
    QCOMPARE(doc.groupFeatures(h), (std::vector<ObjectId>{b[4], b[5]}));
    u.undo();
    u.undo();
    QCOMPARE(order(doc), b);
    QCOMPARE(doc.groupState(), start);
  }

  void moveAcrossTheRollbackMarker() {
    Document doc;
    auto b = boxes(doc, 5);
    ObjectId g = doc.createGroup({b[0], b[1]}, "G");
    doc.setRollbackIndex(3);  // [b0 b1] b2 | b3 b4
    // to the end: both members pass the marker (as single moves do), b2 stays active
    doc.undoStack().push(std::make_unique<MoveGroupCommand>(doc, g, 3));
    QCOMPARE(order(doc), (std::vector<ObjectId>{b[2], b[3], b[4], b[0], b[1]}));
    QVERIFY(!doc.isFeatureActive(b[0]));
    QVERIFY(!doc.isFeatureActive(b[1]));
    QVERIFY(doc.isFeatureActive(b[2]));
    QCOMPARE(doc.rollbackIndex(), 1);
    doc.undoStack().undo();
    QCOMPARE(order(doc), b);
    QCOMPARE(doc.rollbackIndex(), 3);
    VERIFY_INVARIANTS(doc);
  }

  void groupSuppressIsOneStepAndTriState() {
    Document doc;
    auto b = boxes(doc, 4);
    ObjectId g = doc.createGroup({b[1], b[2]}, "Lightening");
    ObjectId inner = doc.createGroup({b[2]}, "Inner");
    UndoStack& u = doc.undoStack();
    u.clear();
    QCOMPARE(doc.groupSuppression(g), GroupSuppression::None);
    QCOMPARE(doc.groupSuppression(12345), GroupSuppression::None);
    QVERIFY(!makeGroupSuppressCommand(doc, g, false));  // nothing to change
    QVERIFY(!makeGroupSuppressCommand(doc, 12345, true));
    QSignalSpy recomputes(&doc, &Document::recomputeFinished);
    u.push(makeGroupSuppressCommand(doc, g, true));
    QCOMPARE(u.count(), 1);
    QCOMPARE(recomputes.count(), 1);
    QCOMPARE(u.undoText(), std::string("Suppress group Lightening"));
    QCOMPARE(doc.groupSuppression(g), GroupSuppression::All);
    QCOMPARE(doc.groupSuppression(inner), GroupSuppression::All);
    QVERIFY(doc.feature(b[1])->isSuppressed());
    QVERIFY(doc.feature(b[2])->isSuppressed());
    QVERIFY(!doc.feature(b[3])->isSuppressed());
    QCOMPARE(doc.liveBodies().size(), size_t(2));
    // one member back -> Some
    u.push(makeSuppressCommand(doc, {b[2]}, false));
    QCOMPARE(doc.groupSuppression(g), GroupSuppression::Some);
    QCOMPARE(doc.groupSuppression(inner), GroupSuppression::None);
    // unsuppressing the group touches only the suppressed member
    auto un = makeGroupSuppressCommand(doc, g, false);
    QVERIFY(un);
    u.push(std::move(un));
    QCOMPARE(doc.groupSuppression(g), GroupSuppression::None);
    QCOMPARE(doc.liveBodies().size(), size_t(4));
    u.undo();
    QCOMPARE(doc.groupSuppression(g), GroupSuppression::Some);
    u.undo();
    u.undo();
    QCOMPARE(doc.groupSuppression(g), GroupSuppression::None);
    QCOMPARE(doc.liveBodies().size(), size_t(4));
    // a mix of groups and features in one step
    u.push(makeSuppressCommand(doc, {inner, b[0]}, true));
    QVERIFY(doc.feature(b[0])->isSuppressed());
    QVERIFY(doc.feature(b[2])->isSuppressed());
    QVERIFY(!doc.feature(b[1])->isSuppressed());
    QCOMPARE(doc.groupSuppression(g), GroupSuppression::Some);
  }

  void suppressedInputTurnsDependentsRed() {
    Document doc;
    Feature* box = doc.addFeature(std::make_unique<BoxFeature>());
    Feature* fail = doc.addFeature(std::make_unique<BoxFeature>());
    doc.recompute();
    auto hole = std::make_unique<HoleFeature>();
    hole->prop("body").setRef(ShapeRef::body(box->createdBodies()[0]));
    Feature* h = doc.addFeature(std::move(hole));
    doc.recompute();
    ObjectId g = doc.createGroup({box->id(), fail->id()}, "G");
    doc.undoStack().push(makeGroupSuppressCommand(doc, g, true));
    // existing behaviour: the hole's body is gone, so the hole fails
    QCOMPARE(h->status(), FeatureStatus::Error);
    doc.undoStack().undo();
    QCOMPARE(h->status(), FeatureStatus::Ok);
  }

  void saveLoadRoundTrip() {
    Document doc;
    auto b = boxes(doc, 7);
    ObjectId base = doc.createGroup({b[0], b[1], b[2]}, "Base plate");
    ObjectId wall = doc.createGroup({b[3], b[4]}, "Back wall");
    ObjectId inner = doc.createGroup({b[4]}, "Ribs");
    doc.setGroupCollapsed(wall, true);
    doc.setRollbackIndex(4);
    doc.undoStack().push(makeGroupSuppressCommand(doc, inner, true));
    QTemporaryDir dir;
    const std::string path = dir.filePath("groups.fcad").toStdString();
    std::string err;
    QVERIFY2(doc.save(path, &err), err.c_str());
    Document back;
    QVERIFY2(back.load(path, &err), err.c_str());
    QCOMPARE(back.groupState(), doc.groupState());
    QCOMPARE(back.timelineItems(), doc.timelineItems());
    QCOMPARE(back.groupIds(), (std::vector<ObjectId>{base, wall, inner}));
    QVERIFY(back.group(wall)->collapsed);
    QCOMPARE(back.group(base)->name, std::string("Base plate"));
    QCOMPARE(back.rollbackIndex(), 4);
    QCOMPARE(back.groupSuppression(inner), GroupSuppression::All);
    QCOMPARE(back.groupSuppression(wall), GroupSuppression::Some);
    // ids stay unique after load
    ObjectId fresh = back.allocateId();
    for (ObjectId g : back.groupIds()) QVERIFY(fresh > g);
    // the group data is written back unchanged
    QCOMPARE(back.toJson()["groups"].toArray(), doc.toJson()["groups"].toArray());
    QCOMPARE(doc.toJson()["groups"].toArray().size(), 3);
    // the snapshot command (fromJson) keeps groups too
    back.undoStack().push(std::make_unique<DocumentSnapshotCommand>(back, "edit", [&] { back.ungroup(wall); }));
    QVERIFY(!back.group(wall));
    back.undoStack().undo();
    QCOMPARE(back.groupState(), doc.groupState());
    VERIFY_INVARIANTS(back);
  }

  void filesWithoutGroupsAndBrokenGroupData() {
    Document doc;
    auto b = boxes(doc, 5);
    QJsonObject json = doc.toJson();
    QVERIFY(!json.contains("groups"));  // documents without groups are written as before
    Document plain;
    QVERIFY(plain.fromJson(json));
    QCOMPARE(plain.groupCount(), 0);

    auto grp = [](double id, const char* name, std::vector<ObjectId> f, double parent = 0) {
      QJsonObject o;
      o["id"] = id;
      o["name"] = name;
      if (parent) o["parent"] = parent;
      QJsonArray a;
      for (ObjectId x : f) a.append(static_cast<double>(x));
      o["features"] = a;
      return o;
    };
    QJsonArray groups;
    groups.append(grp(900, "Split", {b[0], b[2]}));          // not consecutive -> dissolved
    groups.append(grp(901, "Ghost", {424242}));              // unknown feature -> empty -> dropped
    groups.append(grp(902, "A", {b[3]}, 903));               // cycle A <-> B -> broken
    groups.append(grp(903, "B", {b[4]}, 902));
    groups.append(grp(904, "Orphan", {b[1]}, 5555));         // unknown parent -> top level
    json["groups"] = groups;
    Document repaired;
    QVERIFY(repaired.fromJson(json));
    VERIFY_INVARIANTS(repaired);
    QVERIFY(!repaired.group(900));
    QVERIFY(!repaired.group(901));
    QVERIFY(repaired.group(904));
    QCOMPARE(repaired.group(904)->parent, kInvalidId);
    QVERIFY(repaired.allocateId() > 904);
  }

  /// Random edits with every invariant checked after each step; undoing everything gets back
  /// to the start exactly.
  void randomEditsKeepInvariants() {
    QRandomGenerator rng(20261022);
    for (int round = 0; round < 6; ++round) {
      Document doc;
      boxes(doc, 8);
      doc.undoStack().clear();
      const TimelineGroupState startGroups = doc.groupState();
      const auto startOrder = order(doc);
      int pushed = 0;
      for (int step = 0; step < 60; ++step) {
        const int n = doc.featureCount();
        const auto ids = order(doc);
        const auto groups = doc.groupIds();
        std::unique_ptr<UndoCommand> cmd;
        switch (rng.bounded(9)) {
          case 0: {  // group a random range
            if (n == 0) break;
            const int a = static_cast<int>(rng.bounded(n));
            const int len = 1 + static_cast<int>(rng.bounded(std::min(4, n - a)));
            std::vector<ObjectId> items(ids.begin() + a, ids.begin() + a + len);
            cmd = makeCreateGroupCommand(doc, items, "R" + std::to_string(step));
            break;
          }
          case 1:
            if (!groups.empty()) cmd = makeUngroupCommand(doc, groups[rng.bounded(static_cast<int>(groups.size()))]);
            break;
          case 2:
          case 3:
            if (n > 1) {
              const ObjectId f = ids[rng.bounded(n)];
              const int to = static_cast<int>(rng.bounded(n));
              std::optional<ObjectId> target;
              if (!groups.empty() && rng.bounded(2)) target = groups[rng.bounded(static_cast<int>(groups.size()))];
              if (doc.canMoveFeatureTo(f, to, target)) cmd = std::make_unique<MoveFeatureCommand>(doc, f, to, target);
            }
            break;
          case 4:
            if (!groups.empty()) {
              const ObjectId g = groups[rng.bounded(static_cast<int>(groups.size()))];
              const int to = static_cast<int>(rng.bounded(n));
              if (doc.canMoveGroup(g, to)) cmd = std::make_unique<MoveGroupCommand>(doc, g, to);
            }
            break;
          case 5:
            if (n > 2) cmd = std::make_unique<RemoveFeatureCommand>(doc, ids[rng.bounded(n)]);
            break;
          case 6:
            cmd = std::make_unique<AddFeatureCommand>(doc, std::make_unique<BoxFeature>(), static_cast<int>(rng.bounded(n + 1)));
            break;
          case 7:
            if (!groups.empty()) cmd = makeGroupSuppressCommand(doc, groups[rng.bounded(static_cast<int>(groups.size()))], rng.bounded(2));
            break;
          case 8:
            if (doc.undoStack().canUndo() && rng.bounded(2)) {
              doc.undoStack().undo();
              --pushed;
            } else if (doc.undoStack().canRedo()) {
              doc.undoStack().redo();
              ++pushed;
            }
            break;
        }
        if (cmd) {
          doc.undoStack().push(std::move(cmd));
          pushed = doc.undoStack().index();
        }
        std::string why;
        QVERIFY2(invariantsHold(doc, &why), (std::to_string(round) + "/" + std::to_string(step) + ": " + why).c_str());
        // the file round trip never changes the groups
        Document copy;
        QVERIFY(copy.fromJson(doc.toJson()));
        QCOMPARE(copy.groupState(), doc.groupState());
      }
      while (doc.undoStack().canUndo()) {
        doc.undoStack().undo();
        std::string why;
        QVERIFY2(invariantsHold(doc, &why), why.c_str());
      }
      QCOMPARE(order(doc), startOrder);
      QCOMPARE(doc.groupState(), startGroups);
      (void)pushed;
    }
  }
};

QTEST_GUILESS_MAIN(GroupsTest)
#include "test_groups.moc"
