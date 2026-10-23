// Fuzz the .fcs script runner: ScriptRunner (comment stripping, line handling) feeding the
// headless host's command line (splitCommandLine, aliases, every tool's typed input, selectors,
// CHECK expressions). One fresh HeadlessHost per input, keepGoing so every line runs.
//
// Sandboxing: the run happens in the private temp dir (FuzzCommon.h). Lines with an absolute
// path (a word, or a name=value value, starting with '/'), "..", '~', '$' or '\\' are dropped so a
// generated SAVE / EXPORT / OPEN can only touch files below that directory (selectors such as
// body:Body1/face:top stay), and WAIT / SLEEP / SCREENSHOT lines are dropped (stalls, not bugs).
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "FuzzCommon.h"

#include "app/HeadlessHost.h"
#include "app/ScriptRunner.h"
#include "core/Tool.h"

#include <QStringList>

using namespace fabricad;

namespace {

bool allowed(const QString& line) {
  if (line.size() > 240) return false;
  for (QChar c : line)
    if (c == '\\' || c == '~' || c == '$' || c == QChar(0)) return false;
  if (line.contains("..")) return false;
  const std::vector<std::string> words = core::splitCommandLine(line.toStdString());
  if (words.empty()) return true;
  for (const std::string& w : words)
    if (!w.empty() && (w.front() == '/' || w.find("=/") != std::string::npos || w.find(":/") != std::string::npos || w.find("file:") != std::string::npos))
      return false;
  const QString first = QString::fromStdString(words.front()).toUpper();
  return first != "WAIT" && first != "SLEEP" && first != "SCREENSHOT" && first != "SNAP";
}

}  // namespace

extern "C" int LLVMFuzzerInitialize(int*, char***) {
  fuzz::fuzzEnvironment(false);  // HeadlessHost registers the modules itself
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size > 8192) return -1;
  const QString& dir = fuzz::fuzzEnvironment(false);
  const QString text = QString::fromUtf8(reinterpret_cast<const char*>(data), static_cast<qsizetype>(size));
  QStringList lines;
  for (const QString& l : text.split('\n')) {
    if (!allowed(l)) continue;
    lines << l;
    if (lines.size() >= 48) break;
  }
  for (const QString& l : lines) (void)app::ScriptRunner::stripComment(l);
  app::HeadlessHost host;
  app::ScriptRunner runner(host);
  app::ScriptRunner::Options o;
  o.keepGoing = true;
  o.quiet = true;
  o.logPath = dir + "/script.log";
  runner.setOptions(o);
  runner.setLines(lines, "fuzz");
  (void)runner.run();
  return 0;
}
