// SPDX-License-Identifier: LGPL-2.1-or-later
#include "core/Log.h"

#include <iostream>
#include <mutex>

namespace fabricad::core {

namespace {
std::mutex g_logMutex;
LogSink g_sink;
LogLevel g_min = LogLevel::Info;

const char* levelName(LogLevel l) {
  switch (l) {
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO";
    case LogLevel::Warning: return "WARN";
    case LogLevel::Error: return "ERROR";
  }
  return "?";
}
}  // namespace

void log(LogLevel level, const std::string& message) {
  if (level < g_min) return;
  std::lock_guard<std::mutex> lock(g_logMutex);
  if (g_sink) {
    g_sink(level, message);
  } else {
    std::cerr << "[fabricad " << levelName(level) << "] " << message << '\n';
  }
}

void setLogSink(LogSink sink) {
  std::lock_guard<std::mutex> lock(g_logMutex);
  g_sink = std::move(sink);
}

void setMinimumLogLevel(LogLevel level) { g_min = level; }

}  // namespace fabricad::core
