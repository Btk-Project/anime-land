#pragma once

#include "./config.h"

#ifdef ANIME_LAND_USE_SPDLOG
#include <spdlog/spdlog.h>

#define AL_LOG_TRACE(...) SPDLOG_TRACE(__VA_ARGS__)
#define AL_LOG_DEBUG(...) SPDLOG_DEBUG(__VA_ARGS__)
#define AL_LOG_INFO(...) SPDLOG_INFO(__VA_ARGS__)
#define AL_LOG_WARN(...) SPDLOG_WARN(__VA_ARGS__)
#define AL_LOG_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)
#define AL_LOG_CRITICAL(...) SPDLOG_CRITICAL(__VA_ARGS__)
#else
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <format>
#include <string_view>
#include <utility>

namespace anime_land::detail {

enum class FallbackLogLevel { Trace, Debug, Info, Warn, Error, Critical };

inline auto fallbackLogLevel() noexcept -> FallbackLogLevel {
  static const auto level = [] {
    const char *configured = std::getenv("ANIME_LAND_LOG_LEVEL");
    const std::string_view value = configured != nullptr ? configured : "info";
    if (value == "trace") {
      return FallbackLogLevel::Trace;
    }
    if (value == "debug") {
      return FallbackLogLevel::Debug;
    }
    if (value == "warn") {
      return FallbackLogLevel::Warn;
    }
    if (value == "error") {
      return FallbackLogLevel::Error;
    }
    if (value == "critical") {
      return FallbackLogLevel::Critical;
    }
    return FallbackLogLevel::Info;
  }();
  return level;
}

template <typename... Args>
inline constexpr void fallbackLog(FallbackLogLevel level, const char *name,
                        std::format_string<Args...> message, Args &&...args) {
  if (level < fallbackLogLevel()) {
    return;
  }
  const auto formatted = std::format(message, std::forward<Args>(args)...);
  const std::time_t now = std::time(nullptr);
  std::tm localTime{};
#if defined(_WIN32)
  localtime_s(&localTime, &now);
#else
  localtime_r(&now, &localTime);
#endif
  char timestamp[20]{};
  std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &localTime);
  std::fprintf(stderr, "[%s] [%s] %s\n", timestamp, name, formatted.c_str());
  std::fflush(stderr);
}

} // namespace anime_land::detail

#define AL_DETAIL_LOG(level, name, ...)                                        \
  ::anime_land::detail::fallbackLog(level, name, __VA_ARGS__)
#define AL_LOG_TRACE(...)                                                      \
  AL_DETAIL_LOG(::anime_land::detail::FallbackLogLevel::Trace, "trace",        \
                __VA_ARGS__)
#define AL_LOG_DEBUG(...)                                                      \
  AL_DETAIL_LOG(::anime_land::detail::FallbackLogLevel::Debug, "debug",        \
                __VA_ARGS__)
#define AL_LOG_INFO(...)                                                       \
  AL_DETAIL_LOG(::anime_land::detail::FallbackLogLevel::Info, "info",          \
                __VA_ARGS__)
#define AL_LOG_WARN(...)                                                       \
  AL_DETAIL_LOG(::anime_land::detail::FallbackLogLevel::Warn, "warn",          \
                __VA_ARGS__)
#define AL_LOG_ERROR(...)                                                      \
  AL_DETAIL_LOG(::anime_land::detail::FallbackLogLevel::Error, "error",        \
                __VA_ARGS__)
#define AL_LOG_CRITICAL(...)                                                   \
  AL_DETAIL_LOG(::anime_land::detail::FallbackLogLevel::Critical, "critical",  \
                __VA_ARGS__)
#endif // ANIME_LAND_USE_SPDLOG
