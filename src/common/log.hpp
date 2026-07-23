#pragma once

#include "./config.h"

#ifdef ANIME_LAND_USE_SPDLOG
#include <spdlog/spdlog.h>

#include <string_view>

namespace anime_land {

inline auto setLogLevel(std::string_view value) noexcept -> bool {
  if (value == "trace") {
    spdlog::set_level(spdlog::level::trace);
  } else if (value == "debug") {
    spdlog::set_level(spdlog::level::debug);
  } else if (value == "info") {
    spdlog::set_level(spdlog::level::info);
  } else if (value == "warn") {
    spdlog::set_level(spdlog::level::warn);
  } else if (value == "error") {
    spdlog::set_level(spdlog::level::err);
  } else if (value == "critical") {
    spdlog::set_level(spdlog::level::critical);
  } else {
    return false;
  }
  return true;
}

} // namespace anime_land

#define AL_LOG_TRACE(...) ::spdlog::trace(__VA_ARGS__)
#define AL_LOG_DEBUG(...) ::spdlog::debug(__VA_ARGS__)
#define AL_LOG_INFO(...) ::spdlog::info(__VA_ARGS__)
#define AL_LOG_WARN(...) ::spdlog::warn(__VA_ARGS__)
#define AL_LOG_ERROR(...) ::spdlog::error(__VA_ARGS__)
#define AL_LOG_CRITICAL(...) ::spdlog::critical(__VA_ARGS__)
#else
#include <QDebug>
#include <QString>

#include <cstdlib>
#include <format>
#include <string_view>
#include <utility>

namespace anime_land::detail {

enum class FallbackLogLevel { Trace, Debug, Info, Warn, Error, Critical };

inline auto parseFallbackLogLevel(std::string_view value,
                                  FallbackLogLevel &level) noexcept -> bool {
  if (value == "trace") {
    level = FallbackLogLevel::Trace;
  } else if (value == "debug") {
    level = FallbackLogLevel::Debug;
  } else if (value == "info") {
    level = FallbackLogLevel::Info;
  } else if (value == "warn") {
    level = FallbackLogLevel::Warn;
  } else if (value == "error") {
    level = FallbackLogLevel::Error;
  } else if (value == "critical") {
    level = FallbackLogLevel::Critical;
  } else {
    return false;
  }
  return true;
}

inline auto fallbackLogLevelStorage() noexcept -> FallbackLogLevel & {
  static auto level = [] {
    const char *configured = std::getenv("ANIME_LAND_LOG_LEVEL");
    const std::string_view value = configured != nullptr ? configured : "info";
    FallbackLogLevel parsed = FallbackLogLevel::Info;
    static_cast<void>(parseFallbackLogLevel(value, parsed));
    return parsed;
  }();
  return level;
}

inline auto fallbackLogLevel() noexcept -> FallbackLogLevel {
  return fallbackLogLevelStorage();
}

template <typename... Args>
inline auto formatQtLog(std::format_string<Args...> message, Args &&...args)
    -> QString {
  const auto formatted = std::format(message, std::forward<Args>(args)...);
  return QString::fromUtf8(
      formatted.data(), static_cast<qsizetype>(formatted.size()));
}

} // namespace anime_land::detail

namespace anime_land {

inline auto setLogLevel(std::string_view value) noexcept -> bool {
  detail::FallbackLogLevel parsed;
  if (!detail::parseFallbackLogLevel(value, parsed)) {
    return false;
  }
  detail::fallbackLogLevelStorage() = parsed;
  return true;
}

} // namespace anime_land

#define AL_DETAIL_QT_LOG(level, logger, ...)                                   \
  do {                                                                         \
    if ((level) >= ::anime_land::detail::fallbackLogLevel()) {                 \
      logger().noquote()                                                       \
          << ::anime_land::detail::formatQtLog(__VA_ARGS__);                   \
    }                                                                          \
  } while (false)
#define AL_LOG_TRACE(...)                                                      \
  AL_DETAIL_QT_LOG(::anime_land::detail::FallbackLogLevel::Trace, qDebug,       \
                   __VA_ARGS__)
#define AL_LOG_DEBUG(...)                                                      \
  AL_DETAIL_QT_LOG(::anime_land::detail::FallbackLogLevel::Debug, qDebug,       \
                   __VA_ARGS__)
#define AL_LOG_INFO(...)                                                       \
  AL_DETAIL_QT_LOG(::anime_land::detail::FallbackLogLevel::Info, qInfo,         \
                   __VA_ARGS__)
#define AL_LOG_WARN(...)                                                       \
  AL_DETAIL_QT_LOG(::anime_land::detail::FallbackLogLevel::Warn, qWarning,      \
                   __VA_ARGS__)
#define AL_LOG_ERROR(...)                                                      \
  AL_DETAIL_QT_LOG(::anime_land::detail::FallbackLogLevel::Error, qCritical,    \
                   __VA_ARGS__)
#define AL_LOG_CRITICAL(...)                                                   \
  AL_DETAIL_QT_LOG(::anime_land::detail::FallbackLogLevel::Critical, qCritical, \
                   __VA_ARGS__)
#endif // ANIME_LAND_USE_SPDLOG
