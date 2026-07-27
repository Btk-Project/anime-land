#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QtLogging>
#include <QStandardPaths>
#include <QTimer>

#include <ilias/platform/qt.hpp>
#include <ilias/task.hpp>

#include <nekoproto/argparser/argparser.hpp>

#include "model/bangumi/bangumi.hpp"
#include "model/library/local_media_import.hpp"
#include "model/persistence/catalog_store.hpp"
#include "model/persistence/database.hpp"
#include "model/persistence/library_store.hpp"
#include "common/app_settings.hpp"
#include "common/config.h"
#include "common/log.hpp"
#include "presentation/bangumi/bangumi_presenter.hpp"
#include "presentation/bangumi/calendar_view_model.hpp"
#include "presentation/bangumi/browser_view_model.hpp"
#include "presentation/library/library_view_model.hpp"
#include "presentation/library/subject_details_view_model.hpp"
#include "presentation/settings_view_model.hpp"
#include "view/cli/bangumi_cli_command.hpp"
#include "view/cli/bangumi_cli_options.hpp"
#include "view/cli/bangumi_cli_view.hpp"
#include "view/qml/qml_application.hpp"

#include <filesystem>
#include <clocale>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

namespace anime_land {
namespace {

namespace argparser = NEKO_NAMESPACE::argparser;

auto utf8(std::string_view value) -> QString {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

auto parserConfig(const char *programName) -> argparser::ArgParserConfig {
    argparser::ArgParserConfig config;
    config.programName = programName != nullptr ? programName : "anime-land";
    config.description =
        "Initial Bangumi browser-login CLI for the anime-land project.";
    config.version = ANIME_LAND_VERSION_STRING;
    return config;
}

struct ParseOutcome {
    std::optional<cli::Command> command;
    int exitCode = 0;
};

auto parseCommand(int argc, char **argv) -> ParseOutcome {
    const auto config = parserConfig(argc > 0 ? argv[0] : nullptr);
    if (argc == 1) {
        std::cout << argparser::format_help<cli::AnimeLandCommands>(config);
        return {};
    }

    auto parsed = argparser::parser<cli::AnimeLandCommands>(argc, argv, config);
    if (parsed) {
        return {.command = cli::Command {std::move(*parsed)}, .exitCode = 0};
    }

    if (parsed.error() ==
        ::make_error_code(argparser::ArgParserError::HelpRequested)) {
        std::cout << argparser::format_help<cli::AnimeLandCommands>(argc, argv,
                                                                    config);
        return {};
    }
    if (parsed.error() ==
        ::make_error_code(argparser::ArgParserError::VersionRequested)) {
        std::cout << argparser::format_version(config);
        return {};
    }

    std::cerr << "argument error: " << parsed.error().message() << '\n'
              << argparser::format_help<cli::AnimeLandCommands>(argc, argv,
                                                                config);
    return {.command = std::nullopt, .exitCode = 2};
}

auto selectedCredentials(const cli::Command &command)
    -> cli::CommonCommandOptions {
    return std::visit(
        [](const auto &value) -> cli::CommonCommandOptions {
            return value.common;
        },
        command);
}

auto selectedRuntimeOptions(const cli::Command &command) -> cli::CommonCommandOptions {
    return std::visit(
        [](const auto &value) -> cli::CommonCommandOptions {
            return value.common;
        },
        command);
}

auto selectedConfigPath(const cli::Command &command) -> std::optional<QString> {
    return std::visit(
        [](const auto &value) -> std::optional<QString> {
            if constexpr (requires { value.settings.config; }) {
                if (value.settings.config) {
                    return utf8(*value.settings.config);
                }
            }
            return std::nullopt;
        },
        command);
}

auto commandName(const cli::Command &command) -> std::string_view {
    return std::visit(
        [](const auto &value) -> std::string_view {
            using Command = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_same_v<Command, cli::LoginCommand>) {
                return "login";
            }
            else if constexpr (std::is_same_v<Command, cli::StatusCommand>) {
                return "status";
            }
            else if constexpr (std::is_same_v<Command, cli::LogoutCommand>) {
                return "logout";
            }
            else if constexpr (std::is_same_v<Command,
                                              cli::CollectionsCommand>) {
                return "collections";
            }
            else {
                return "search";
            }
        },
        command);
}

auto tokenStoreName(TokenStoreKind kind) -> std::string_view {
    switch (kind) {
        case TokenStoreKind::Memory:
            return "memory";
        case TokenStoreKind::File:
            return "file";
        case TokenStoreKind::System:
            return "system";
    }
    return "unknown";
}

auto runCommand(BangumiPresenter &presenter, BangumiView &view,
                const cli::Command &command, QCoreApplication &application)
    -> ilias::FireAndForget {
    const int exitCode =
        co_await cli::runBangumiCliCommand(presenter, view, command);
    AL_LOG_INFO("[app] command completed exit_code={}", exitCode);
    application.exit(exitCode);
}

auto defaultConfigPath() -> QString {
    return QStandardPaths::writableLocation(
               QStandardPaths::AppConfigLocation) +
           QStringLiteral("/settings.toml");
}

auto fixtureUiRequested() -> bool {
    return qEnvironmentVariableIsSet("ANIME_LAND_UI_FIXTURE") || qEnvironmentVariableIsSet("ANIME_LAND_UI_SMOKE_TEST");
}

auto resolveGraphicalDatabaseSettings(SqlSettings settings)
    -> std::optional<SqlSettings> {
    const QString databaseType =
        QString::fromStdString(settings.database_type).toLower();
    if (databaseType != QStringLiteral("sqlite") && databaseType != QStringLiteral("sqlcipher")) {
        return settings;
    }

    QString databasePath = QString::fromUtf8(settings.database_path);
    if (QFileInfo(databasePath).isRelative()) {
        const QString dataDirectory = QStandardPaths::writableLocation(
            QStandardPaths::AppLocalDataLocation);
        if (dataDirectory.isEmpty() || !QDir().mkpath(dataDirectory)) {
            return std::nullopt;
        }
        databasePath = QDir(dataDirectory).filePath(databasePath);
    }
    else if (!QDir().mkpath(QFileInfo(databasePath).absolutePath())) {
        return std::nullopt;
    }
    settings.database_path = QDir::cleanPath(databasePath).toUtf8().toStdString();
    return settings;
}

auto runGraphicalApplication(QGuiApplication &application) -> int {
    const bool fixtureMode = fixtureUiRequested();
    if (fixtureMode) {
        AL_LOG_INFO("[app.qml] starting isolated fixture mode");
        return qml::runApplication(application, nullptr, nullptr, nullptr,
                                   nullptr, nullptr, true);
    }

    GlobalAppSettingGuard globalSettings;
    BangumiSettings bangumiSettings;
    SqlSettings sqlSettings;
    const QString configPath = defaultConfigPath();
    const auto loaded = globalSettings.loadOrCreate(
        QFileInfo(configPath).filesystemFilePath());
    if (loaded) {
        auto settings = globalSettings.get();
        bangumiSettings = settings->bangumi_settings;
        sqlSettings = settings->sql_settings;
        AL_LOG_INFO("[app.qml] settings {}",
                    *loaded == AppSettingsFileState::Created ? "created"
                                                             : "loaded");
    }
    else {
        AL_LOG_WARN("[app.qml] settings unavailable; using defaults");
    }

    TokenStoreOptions storeOptions;
    storeOptions.kind = TokenStoreKind::System;
    auto store = TokenStore::create(std::move(storeOptions));
    bool persistentCredentials = true;
    if (!store) {
        AL_LOG_WARN(
            "[app.qml] system credential store unavailable; using "
            "non-persistent session code={}",
            bangumiErrorCodeName(store.error().code));
        TokenStoreOptions fallbackOptions;
        fallbackOptions.kind = TokenStoreKind::Memory;
        store = TokenStore::create(std::move(fallbackOptions));
        persistentCredentials = false;
    }
    if (!store) {
        AL_LOG_ERROR("[app.qml] token store initialization failed");
        return 2;
    }

    ilias::QIoContext ioContext;
    ioContext.install();
    auto resolvedSqlSettings =
        resolveGraphicalDatabaseSettings(std::move(sqlSettings));
    if (!resolvedSqlSettings) {
        AL_LOG_ERROR("[app.qml] cannot create local data directory");
        return 2;
    }
    auto databaseResult =
        persistence::LocalDatabase::open(*resolvedSqlSettings).wait();
    if (!databaseResult) {
        AL_LOG_ERROR("[app.qml] local database open failed code={}",
                     databaseResult.error().message());
        return 2;
    }
    auto database = std::move(*databaseResult);
    auto catalogStoreResult = persistence::CatalogStore::open(database).wait();
    if (!catalogStoreResult) {
        AL_LOG_ERROR("[app.qml] catalog store open failed code={}",
                     catalogStoreResult.error().message());
        return 2;
    }
    auto libraryStoreResult = persistence::LibraryStore::open(database).wait();
    if (!libraryStoreResult) {
        AL_LOG_ERROR("[app.qml] library store open failed code={}",
                     libraryStoreResult.error().message());
        return 2;
    }

    BangumiModuleOptions bangumiOptions;
    bangumiOptions.features.push_back(bangumiUserCollectionsFeature());
    BangumiModule module(std::move(bangumiSettings), std::move(*store),
                         std::move(bangumiOptions));
    int exitCode = 0;
    {
        auto catalogStore = std::move(*catalogStoreResult);
        auto libraryStore = std::move(*libraryStoreResult);
        LocalMediaImportService importService(libraryStore, catalogStore,
                                              module);
        LibraryViewModel libraryViewModel(importService);
        SubjectDetailsViewModel subjectDetailsViewModel(importService);
        BangumiCalendarViewModel calendarViewModel(module);
        BangumiBrowserViewModel bangumiBrowserViewModel(module);
        ApplicationSettingsViewModel settingsViewModel(
            globalSettings, QFileInfo(configPath).filesystemFilePath(),
            &module, persistentCredentials);
        AL_LOG_INFO("[app.qml] starting live model mode");
        exitCode = qml::runApplication(
            application, &calendarViewModel, &bangumiBrowserViewModel,
            &libraryViewModel, &subjectDetailsViewModel,
            &settingsViewModel, false);
    }
    auto closed = database.close().wait();
    if (!closed) {
        AL_LOG_WARN("[app.qml] local database close failed code={}",
                    closed.error().message());
    }
    return exitCode;
}

} // namespace
} // namespace anime_land

auto main(int argc, char **argv) -> int {
    using namespace anime_land;

    // Keep the process locale aligned with the desktop input method. In
    // particular, a C/POSIX LC_CTYPE prevents several Linux IM modules from
    // committing non-ASCII preedit text correctly.
    std::setlocale(LC_CTYPE, "");
    QLocale::setDefault(QLocale::system());

#if defined(_WIN32)
    ::SetConsoleCP(65001);
    ::SetConsoleOutputCP(65001);
    std::setlocale(LC_ALL, ".utf-8");
#endif

    auto parsed = parseCommand(argc, argv);
    if (argc == 1) {
        parsed.command = std::optional{cli::GuiCommand {}};
    }
    if (!parsed.command) {
        return parsed.exitCode;
    }
    auto &command = *parsed.command;
    const auto runtimeOptions = selectedRuntimeOptions(command);
    if (runtimeOptions.logLevel && !setLogLevel(*runtimeOptions.logLevel)) {
        std::cerr << "invalid log level: " << *runtimeOptions.logLevel << '\n';
        return 2;
    }
#ifndef ANIME_LAND_USE_SPDLOG
    if (!qEnvironmentVariableIsSet("QT_MESSAGE_PATTERN")) {
        qSetMessagePattern(QStringLiteral(
            "[%{time yyyy-MM-dd hh:mm:ss.zzz}] [%{type}] "
            "[%{file}:%{line}] %{message}"));
    }
#endif

    AL_LOG_INFO("[app] starting version={} command={}", ANIME_LAND_VERSION_STRING,
                commandName(command));

    const bool needsBrowser = std::holds_alternative<cli::LoginCommand>(command);
    std::unique_ptr<QCoreApplication> application;
    if (needsBrowser) {
        auto guiApplication = std::make_unique<QGuiApplication>(argc, argv);
        guiApplication->setQuitOnLastWindowClosed(false);
        application = std::move(guiApplication);
    }
    else if (std::holds_alternative<cli::GuiCommand>(command)) {
#if defined(Q_OS_LINUX)
        // The Quick fallback dialog follows our dynamic palette, while native
        // portal/theme dialogs can independently mix light and dark controls.
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
#endif
        QGuiApplication application(argc, argv);
        QCoreApplication::setOrganizationName(QStringLiteral("Btk-Project"));
        QCoreApplication::setApplicationName(QStringLiteral("anime-land"));
        QCoreApplication::setApplicationVersion(
            QStringLiteral(ANIME_LAND_VERSION_STRING));
        AL_LOG_INFO("[app] starting QML shell version={}",
                    ANIME_LAND_VERSION_STRING);
        const int exitCode = runGraphicalApplication(application);
        AL_LOG_INFO("[app] stopped QML shell exit_code={}", exitCode);
        return exitCode;
    }
    else {
        application = std::make_unique<QCoreApplication>(argc, argv);
    }
    QCoreApplication::setOrganizationName(QStringLiteral("Btk-Project"));
    QCoreApplication::setApplicationName(QStringLiteral("anime-land"));
    QCoreApplication::setApplicationVersion(
        QStringLiteral(ANIME_LAND_VERSION_STRING));

    cli::BangumiCliView view;
    GlobalAppSettingGuard globalSettings;
    const QString configPath =
        selectedConfigPath(command).value_or(defaultConfigPath());
    AL_LOG_DEBUG("[app.config] loading path={}", configPath.toStdString());

    const std::filesystem::path configFilePath =
        QFileInfo(configPath).filesystemFilePath();
    auto settingsFile = globalSettings.loadOrCreate(configFilePath);
    if (!settingsFile) {
        AL_LOG_ERROR("[app.config] load_or_create failed");
        view.showError(bangumiError(
            BangumiErrorCode::InvalidConfiguration,
            QStringLiteral("无法加载或创建配置文件：%1").arg(configPath)));
        return 2;
    }
    if (*settingsFile == AppSettingsFileState::Created) {
        AL_LOG_INFO("[app.config] settings created");
        view.showMessage(
            QStringLiteral("已生成包含全部默认字段的配置文件：%1").arg(configPath));
    }
    else {
        AL_LOG_INFO("[app.config] settings loaded");
    }

    BangumiSettings bangumiSettings;
    {
        auto settings = globalSettings.get();
        bangumiSettings = settings->bangumi_settings;
    }
    if (runtimeOptions.proxy) {
        bangumiSettings.proxy_url =
            QUrl(utf8(*runtimeOptions.proxy), QUrl::StrictMode);
        AL_LOG_INFO("[app.config] command-line Bangumi proxy override enabled");
    }

    const auto credentialOptions = selectedCredentials(command);
    auto kind = parseTokenStoreKind(credentialOptions.tokenStore);
    if (!kind) {
        AL_LOG_ERROR("[app.credentials] invalid token store selection");
        view.showError(kind.error());
        return 2;
    }

    TokenStoreOptions storeOptions;
    storeOptions.kind = *kind;
    if (credentialOptions.tokenFile) {
        storeOptions.filePath = utf8(*credentialOptions.tokenFile);
    }
    auto effectiveStoreKind = *kind;
    auto store = TokenStore::create(std::move(storeOptions));
    if (!store && std::holds_alternative<cli::SearchCommand>(command)) {
        AL_LOG_WARN("[app.credentials] selected store unavailable for public "
                    "search; falling back to memory code={}",
                    bangumiErrorCodeName(store.error().code));
        view.showMessage(QStringLiteral(
            "Bangumi 凭据存储不可用；本次搜索将继续使用匿名模式。"));
        TokenStoreOptions anonymousStoreOptions;
        anonymousStoreOptions.kind = TokenStoreKind::Memory;
        store = TokenStore::create(std::move(anonymousStoreOptions));
        effectiveStoreKind = TokenStoreKind::Memory;
    }
    if (!store) {
        AL_LOG_ERROR("[app.credentials] token store initialization failed code={}",
                     bangumiErrorCodeName(store.error().code));
        view.showError(store.error());
        return store.error().code == BangumiErrorCode::UnsupportedCredentialStore
                   ? 4
                   : 2;
    }
    AL_LOG_INFO("[app.credentials] token store ready backend={}",
                tokenStoreName(effectiveStoreKind));

    ilias::QIoContext ioContext;
    ioContext.install();
    BangumiModuleOptions bangumiOptions;
    bangumiOptions.features.push_back(bangumiUserCollectionsFeature());
    BangumiModule module(std::move(bangumiSettings), std::move(*store),
                         std::move(bangumiOptions));
    auto saveOAuthApplication = [&globalSettings, configFilePath,
                                 configPath](BangumiOAuthApplication application)
        -> ilias::Task<BangumiResult<void>> {
        co_return co_await ilias::blocking([&globalSettings, configFilePath,
                                            configPath,
                                            application =
                                                std::move(application)]() mutable {
            QString oldClientId;
            std::string oldClientSecret;
            {
                auto settings = globalSettings.get();
                oldClientId = std::move(settings->bangumi_settings.client_id);
                oldClientSecret = std::move(settings->bangumi_settings.client_secret);
                settings->bangumi_settings.client_id = application.clientId;
                settings->bangumi_settings.client_secret = application.clientSecret;
            }

            if (globalSettings.save(configFilePath)) {
                AL_LOG_INFO("[app.config] OAuth application settings saved");
                clearBangumiOAuthApplication(application);
                if (!oldClientSecret.empty()) {
                    BangumiOAuthApplication oldApplication {std::move(oldClientId),
                                                            std::move(oldClientSecret)};
                    clearBangumiOAuthApplication(oldApplication);
                }
                return BangumiResult<void> {};
            }

            // Keep the in-memory settings transactional when persistence fails.
            AL_LOG_ERROR("[app.config] failed to save OAuth application settings");
            {
                auto settings = globalSettings.get();
                settings->bangumi_settings.client_id = std::move(oldClientId);
                settings->bangumi_settings.client_secret = std::move(oldClientSecret);
            }
            clearBangumiOAuthApplication(application);
            return BangumiResult<void> {ilias::Err(
                bangumiError(BangumiErrorCode::CredentialStoreError,
                             QStringLiteral("无法保存 Bangumi OAuth 应用参数：%1")
                                 .arg(configPath)))};
        });
    };
    BangumiPresenter presenter(module, view, std::move(saveOAuthApplication));

    // Start only after exec() has entered the Qt loop. Otherwise a synchronous
    // configuration failure could call exit() before Qt can observe it.
    AL_LOG_DEBUG("[app] entering Qt event loop");
    QTimer::singleShot(0, application.get(),
                       [&]() {
                           runCommand(presenter, view, command, *application);
                       });
    const int exitCode = application->exec();
    AL_LOG_INFO("[app] stopped exit_code={}", exitCode);
    return exitCode;
}
