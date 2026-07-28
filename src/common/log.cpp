#include "pch.hpp"

#include "common/log.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QString>

#include <cstdio>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <system_error>

#ifdef ANIME_LAND_USE_SPDLOG
#include <spdlog/logger.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <vector>
#else
#include <QtLogging>
#endif

namespace anime_land {
namespace {

auto logBaseName() -> QString {
    QString name = QCoreApplication::applicationName().trimmed();
    if (name.isEmpty()) {
        name = QStringLiteral("anime-land");
    }
    for (QChar &character : name) {
        if (!character.isLetterOrNumber() && character != u'-'
            && character != u'_') {
            character = u'_';
        }
    }
    return name + u'-'
           + QDateTime::currentDateTime().toString(
               QStringLiteral("yyyyMMdd-HHmmss"))
           + QStringLiteral(".log");
}

auto makeLogFilePath(const std::filesystem::path &directory)
    -> std::filesystem::path {
    const QByteArray fileName = logBaseName().toUtf8();
    return directory
           / std::filesystem::path(
               std::string(fileName.constData(),
                           static_cast<std::size_t>(fileName.size())));
}

auto validateOptions(const LogFileOptions &options)
    -> std::optional<std::string> {
    if (options.directory.empty()) {
        return "log directory is empty";
    }
    if (options.maxFileSize == 0) {
        return "maximum log file size must be greater than zero";
    }
    if (options.maxFileCount == 0) {
        return "maximum log file count must be greater than zero";
    }
    return std::nullopt;
}

#ifdef ANIME_LAND_USE_SPDLOG

struct SpdlogState {
    std::mutex mutex;
    std::shared_ptr<spdlog::logger> previousLogger;
    std::filesystem::path filePath;
};

auto logState() -> SpdlogState & {
    static SpdlogState state;
    return state;
}

#else

struct QtLogState {
    std::mutex mutex;
    std::ofstream output;
    std::filesystem::path filePath;
    std::uint64_t bytesWritten = 0;
    std::uint64_t maxFileSize = 0;
    std::size_t maxFileCount = 0;
    QtMessageHandler previousHandler = nullptr;
    bool installed = false;
};

auto logState() -> QtLogState & {
    // Keep the storage alive through Qt's process-wide message handler teardown.
    static auto *state = new QtLogState;
    return *state;
}

auto rotatedPath(const std::filesystem::path &path, std::size_t index)
    -> std::filesystem::path {
    auto rotated = path.parent_path()
                   / (path.stem().string() + "." + std::to_string(index)
                      + path.extension().string());
    return rotated;
}

void rotate(QtLogState &state) {
    state.output.close();
    std::error_code error;
    if (state.maxFileCount <= 1) {
        std::filesystem::remove(state.filePath, error);
    }
    else {
        std::filesystem::remove(
            rotatedPath(state.filePath, state.maxFileCount - 1), error);
        for (std::size_t index = state.maxFileCount - 1; index > 1;
             --index) {
            error.clear();
            std::filesystem::rename(rotatedPath(state.filePath, index - 1),
                                    rotatedPath(state.filePath, index), error);
        }
        error.clear();
        std::filesystem::rename(state.filePath,
                                rotatedPath(state.filePath, 1), error);
    }
    state.output.open(state.filePath, std::ios::binary | std::ios::trunc);
    state.bytesWritten = 0;
}

void qtFileMessageHandler(QtMsgType type, const QMessageLogContext &context,
                          const QString &message) {
    auto &state = logState();
    const QString formatted = qFormatLogMessage(type, context, message);
    const QByteArray encoded = formatted.toUtf8();

    if (state.previousHandler != nullptr
        && state.previousHandler != qtFileMessageHandler) {
        state.previousHandler(type, context, message);
    }
    else {
        std::fwrite(encoded.constData(), sizeof(char),
                    static_cast<std::size_t>(encoded.size()), stderr);
        std::fputc('\n', stderr);
        std::fflush(stderr);
    }

    std::scoped_lock lock(state.mutex);
    if (!state.output.is_open()) {
        return;
    }
    const auto lineSize = static_cast<std::uint64_t>(encoded.size()) + 1U;
    if (state.bytesWritten > 0
        && state.bytesWritten + lineSize > state.maxFileSize) {
        rotate(state);
    }
    state.output.write(encoded.constData(), encoded.size());
    state.output.put('\n');
    state.output.flush();
    if (state.output) {
        state.bytesWritten += lineSize;
    }
}

#endif

} // namespace

auto configureLogging(std::string_view level, const LogFileOptions &options)
    -> LogConfigurationResult {
    if (!setLogLevel(level)) {
        return {.success = false,
                .filePath = {},
                .errorMessage = "invalid log level: " + std::string(level)};
    }
    if (auto error = validateOptions(options)) {
        return {.success = false,
                .filePath = {},
                .errorMessage = std::move(*error)};
    }

    std::error_code error;
    std::filesystem::create_directories(options.directory, error);
    if (error) {
        return {.success = false,
                .filePath = {},
                .errorMessage = "cannot create log directory: "
                                + error.message()};
    }
    const std::filesystem::path filePath = makeLogFilePath(options.directory);

#ifdef ANIME_LAND_USE_SPDLOG
    try {
        auto consoleSink =
            std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto fileSink =
            std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                filePath, static_cast<std::size_t>(options.maxFileSize),
                options.maxFileCount);
        std::vector<spdlog::sink_ptr> sinks {consoleSink, fileSink};
        auto logger = std::make_shared<spdlog::logger>(
            "anime-land", sinks.begin(), sinks.end());
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        logger->flush_on(spdlog::level::warn);

        auto &state = logState();
        std::scoped_lock lock(state.mutex);
        if (!state.previousLogger) {
            state.previousLogger = spdlog::default_logger();
        }
        state.filePath = filePath;
        spdlog::set_default_logger(std::move(logger));
        static_cast<void>(setLogLevel(level));
    }
    catch (const spdlog::spdlog_ex &exception) {
        return {.success = false,
                .filePath = {},
                .errorMessage = exception.what()};
    }
#else
    std::ofstream output(filePath, std::ios::binary | std::ios::app);
    if (!output.is_open()) {
        return {.success = false,
                .filePath = {},
                .errorMessage = "cannot open log file: "
                                + filePath.string()};
    }
    const auto existingSize = std::filesystem::file_size(filePath, error);

    auto &state = logState();
    std::scoped_lock lock(state.mutex);
    state.output = std::move(output);
    state.filePath = filePath;
    state.bytesWritten = error ? 0 : existingSize;
    state.maxFileSize = options.maxFileSize;
    state.maxFileCount = options.maxFileCount;
    if (!state.installed) {
        state.previousHandler = qInstallMessageHandler(qtFileMessageHandler);
        state.installed = true;
    }
#endif

    return {.success = true, .filePath = filePath, .errorMessage = {}};
}

void shutdownLogging() noexcept {
#ifdef ANIME_LAND_USE_SPDLOG
    auto &state = logState();
    std::scoped_lock lock(state.mutex);
    if (state.previousLogger) {
        spdlog::set_default_logger(std::move(state.previousLogger));
    }
    state.filePath.clear();
#else
    auto &state = logState();
    QtMessageHandler previous = nullptr;
    {
        std::scoped_lock lock(state.mutex);
        state.output.close();
        state.filePath.clear();
        if (!state.installed) {
            return;
        }
        previous = state.previousHandler;
        state.previousHandler = nullptr;
        state.installed = false;
    }
    qInstallMessageHandler(previous);
#endif
}

auto currentLogFilePath() -> std::filesystem::path {
    auto &state = logState();
    std::scoped_lock lock(state.mutex);
    return state.filePath;
}

} // namespace anime_land
