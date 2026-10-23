// SPDX-License-Identifier: LGPL-2.1-or-later
#include "app/tutorial/TestProbe.h"

#include "app/MainWindow.h"
#include "app/UiSettings.h"
#include "app/tutorial/TutorialBubble.h"
#include "app/tutorial/TutorialController.h"
#include "app/tutorial/TutorialEngine.h"
#include "core/Document.h"
#include "viewer/ViewerWidget.h"

#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>

namespace fabricad::app::tutorial {

TestProbe::TestProbe(MainWindow& w, TutorialController& c, const QString& socketPath, QObject* parent)
    : QObject(parent), w_(w), c_(c) {
  setObjectName("TestProbe");
  server_ = new QLocalServer(this);
  QLocalServer::removeServer(socketPath);
  if (!server_->listen(socketPath)) qWarning("TestProbe: cannot listen on %s: %s", qPrintable(socketPath), qPrintable(server_->errorString()));
  connect(server_, &QLocalServer::newConnection, this, &TestProbe::onConnection);
  qApp->installEventFilter(this);
}

TestProbe::~TestProbe() = default;

bool TestProbe::eventFilter(QObject*, QEvent* e) {
  // real input only: each X event the runner sends advances the sequence once
  if (e->spontaneous()) switch (e->type()) {
      case QEvent::MouseButtonPress:
      case QEvent::MouseButtonRelease:
      case QEvent::MouseButtonDblClick:
      case QEvent::KeyPress:
      case QEvent::KeyRelease:
      case QEvent::Wheel: ++inputSeq_; break;
      default: break;
    }
  return false;
}

void TestProbe::onConnection() {
  while (QLocalSocket* s = server_->nextPendingConnection()) {
    connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
    connect(s, &QLocalSocket::readyRead, this, [this, s] {
      while (s->canReadLine()) {
        const QByteArray line = s->readLine().trimmed();
        if (line.isEmpty()) continue;
        QJsonParseError pe;
        const QJsonDocument d = QJsonDocument::fromJson(line, &pe);
        QJsonObject out = d.isObject() ? answer(d.object()) : QJsonObject{{"ok", false}, {"error", "bad request: " + pe.errorString()}};
        s->write(QJsonDocument(out).toJson(QJsonDocument::Compact) + '\n');
        s->flush();
      }
    });
  }
}

QJsonObject TestProbe::state() {
  QJsonObject o;
  TutorialEngine& e = c_.engine();
  StateQuery& q = e.query();
  o["tool"] = q.activeTool();
  o["prompt"] = w_.promptText();
  QWidget* f = QApplication::focusWidget();
  o["focus"] = f ? (f->objectName().isEmpty() ? QString::fromLatin1(f->metaObject()->className()) : f->objectName()) : QString();
  o["window"] = QJsonArray{w_.x(), w_.y(), w_.width(), w_.height()};
  o["ready"] = w_.isVisible() && (!w_.viewer() || w_.viewer()->isVisible());
  // idle: the answer is produced from the event loop, so queued events have run; no camera
  // animation (Home, Look At on sketch entry) is still moving the view, and the view has painted
  // the frame of the last document / selection / camera change (plan 5.5)
  bool idle = true;
  if (viewer::ViewerWidget* v = w_.viewer()) {
    o["frame"] = static_cast<double>(v->frameCount());
    if (v->isAnimating()) {
      idle = false;
    } else if (v->framePending()) {
      if (pendingFrame_ != v->frameCount()) {
        pendingFrame_ = v->frameCount();
        pendingFor_.start();
      }
      idle = pendingFor_.elapsed() >= kStaleFrameMs;
    } else {
      pendingFrame_ = ~0ull;
    }
  }
  o["idle"] = idle;
  if (QWidget* card = w_.findChild<QWidget*>("TutorialWelcome"); card && card->isVisible()) o["welcome"] = card->property("tourId").toString();
  o["inputSeq"] = static_cast<double>(inputSeq_);
  o["sketchDof"] = q.sketchDof();
  o["undo"] = q.undoIndex();
  QJsonObject tour;
  if (const Tour* t = e.tour()) {
    tour["id"] = t->id;
    tour["index"] = e.stepIndex();
    tour["count"] = t->stepCount();
    tour["step"] = e.step() ? e.step()->id : QString();
    tour["waiting"] = e.waitingFor();
    tour["hint"] = e.hint();
    tour["painted"] = c_.bubble() ? c_.bubble()->paintedStep() : 0;  // 1-based, as index + 1
  }
  o["tour"] = tour;
  QJsonObject ui;
  for (const UiSettings::Key& k : UiSettings::keys()) ui[k.key] = QJsonValue::fromVariant(UiSettings::instance().value(k.key));
  o["ui"] = ui;
  o["ok"] = true;
  return o;
}

QJsonObject TestProbe::answer(const QJsonObject& r) {
  const QString cmd = r.value("cmd").toString();
  if (cmd == "ping") return {{"ok", true}};
  if (cmd == "state") return state();
  if (cmd == "locate") {
    ResolvedAnchor a;
    if (r.value("anchor").toString() == "@step") {
      a = c_.currentAnchor();
    } else {
      QString err;
      auto an = Anchor::parse(r.value("anchor"), &err);
      if (!an) return {{"ok", false}, {"error", err}};
      AnchorResolver res(w_);
      a = res.resolve(*an);
    }
    return {{"ok", true},
            {"found", a.found},
            {"visible", a.visible},
            {"point", QJsonArray{a.point.x(), a.point.y()}},
            {"rect", QJsonArray{a.rect.x(), a.rect.y(), a.rect.width(), a.rect.height()}},
            {"via", a.via},
            {"why", a.why},
            {"pickVerified", a.pickVerified},
            // what is under the point: a QComboBox opens with a left click, a toolbar button's
            // dropdown with a right click (a left click would run its tool)
            {"widget", a.widget ? QString::fromLatin1(a.widget->metaObject()->className()) : QString()}};
  }
  if (cmd == "eval") {
    QString err;
    auto c = Condition::parse(r.value("cond"), &err);
    if (!c) return {{"ok", false}, {"error", err}};
    StepBaseline base;
    base.undoIndex = c_.engine().query().undoIndex();
    EngineFacts facts;
    QString why;
    const bool ok = c_.engine().query().eval(*c, base, facts, &why);
    return {{"ok", true}, {"value", ok}, {"why", why}};
  }
  if (cmd == "check") {
    QString msg;
    const bool ok = c_.engine().query().check(r.value("expr").toString(), &msg);
    return {{"ok", true}, {"value", ok}, {"message", msg}};
  }
  if (cmd == "grab") {
    // the window as Qt renders it now (the GL view included): the Markdown export's screenshots.
    // A screen grab of an occluded private Xwayland display can show a frame several steps old.
    const QString path = r.value("path").toString();
    if (path.isEmpty()) return {{"ok", false}, {"error", "grab needs a path"}};
    const bool ok = w_.grab().save(path, "PNG");
    return {{"ok", ok}, {"error", ok ? QString() : "cannot write " + path}};
  }
  if (cmd == "docjson") {
    if (!w_.hasDocument()) return {{"ok", false}, {"error", "no document"}};
    return {{"ok", true}, {"doc", w_.document().toJson()}};
  }
  return {{"ok", false}, {"error", "unknown command '" + cmd + "' (ping state locate eval check grab docjson)"}};
}

}  // namespace fabricad::app::tutorial
