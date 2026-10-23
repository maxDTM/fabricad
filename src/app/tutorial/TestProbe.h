// FabriCAD tutorial engine: read-only test probe for the real-input GUI runner (docs/plans/tutorial.md 3, 5).
// Only with FABRICAD_TEST_PROBE=<socket path>: a QLocalServer answering one JSON object per line.
// Every request is a query; the probe has NO write commands (the runner acts through real X events).
//   {"cmd": "ping"}                                -> {"ok": true}
//   {"cmd": "state"}                               -> tool, prompt, focus, window, ready, idle, inputSeq,
//                                                     tour {id, step, index, count, waiting, hint}, ui/* keys
//   {"cmd": "locate", "anchor": "tool:part.extrude"}  (or "anchor": "@step" for the current step)
//                                                  -> found, visible, point [x, y], rect [x, y, w, h], via, why
//   {"cmd": "eval", "cond": {...}}                 -> ok, why (StateQuery, the tour's own evaluator)
//   {"cmd": "check", "expr": "bodies == 1"}        -> ok, message (script CHECK)
//   {"cmd": "docjson"}                             -> the active document as JSON
// Coordinates are global logical pixels (the X root with QT_SCALE_FACTOR=1).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QJsonObject>
#include <QObject>

class QLocalServer;
class QLocalSocket;

namespace fabricad::app {
class MainWindow;
}

namespace fabricad::app::tutorial {

class TutorialController;

class TestProbe : public QObject {
  Q_OBJECT
 public:
  TestProbe(MainWindow& w, TutorialController& c, const QString& socketPath, QObject* parent = nullptr);
  ~TestProbe() override;

  QJsonObject answer(const QJsonObject& request);
  quint64 inputSeq() const { return inputSeq_; }

 protected:
  bool eventFilter(QObject* o, QEvent* e) override;

 private:
  void onConnection();
  QJsonObject state();

  MainWindow& w_;
  TutorialController& c_;
  QLocalServer* server_ = nullptr;
  quint64 inputSeq_ = 0;
};

}  // namespace fabricad::app::tutorial
