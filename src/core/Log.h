// FabriCAD core: minimal logging (no third-party dependency).
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <format>
#include <functional>
#include <string>

namespace fabricad::core {

enum class LogLevel { Debug, Info, Warning, Error };

using LogSink = std::function<void(LogLevel, const std::string&)>;

void log(LogLevel level, const std::string& message);
/// Replace the default stderr sink (the app routes messages to its console/status bar).
void setLogSink(LogSink sink);
void setMinimumLogLevel(LogLevel level);

template <class... Args>
void logf(LogLevel level, std::format_string<Args...> fmt, Args&&... args) {
  log(level, std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace fabricad::core

#define FC_DEBUG(...) ::fabricad::core::logf(::fabricad::core::LogLevel::Debug, __VA_ARGS__)
#define FC_INFO(...) ::fabricad::core::logf(::fabricad::core::LogLevel::Info, __VA_ARGS__)
#define FC_WARN(...) ::fabricad::core::logf(::fabricad::core::LogLevel::Warning, __VA_ARGS__)
#define FC_ERROR(...) ::fabricad::core::logf(::fabricad::core::LogLevel::Error, __VA_ARGS__)
