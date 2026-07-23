#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QtLogging>
#include <QStandardPaths>
#include <QTimer>

#include <ilias/platform/qt.hpp>
#include <ilias/task.hpp>

#include <nekoproto/argparser/argparser.hpp>

#include "bangumi/bangumi.hpp"
#include "common/app_settings.hpp"
#include "common/config.h"
#include "common/log.hpp"
#include "presentation/bangumi_presenter.hpp"
#include "presentation/cli/bangumi_cli_options.hpp"
#include "presentation/cli/bangumi_cli_view.hpp"

#include <filesystem>
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

auto runCommand(BangumiPresenter &presenter, const cli::Command &command,
                QCoreApplication &application) -> ilias::FireAndForget {
    auto task = std::visit(
        [&presenter](const auto &value) { return presenter.run(value); },
        command);
    const int exitCode = co_await std::move(task);
    AL_LOG_INFO("[app] command completed exit_code={}", exitCode);
    application.exit(exitCode);
}

} // namespace
} // namespace anime_land

auto main(int argc, char **argv) -> int {
    using namespace anime_land;

#if defined(_WIN32)
    ::SetConsoleCP(65001);
    ::SetConsoleOutputCP(65001);
    std::setlocale(LC_ALL, ".utf-8");
#endif

    auto parsed = parseCommand(argc, argv);
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
    else {
        application = std::make_unique<QCoreApplication>(argc, argv);
    }
    QCoreApplication::setOrganizationName(QStringLiteral("Btk-Project"));
    QCoreApplication::setApplicationName(QStringLiteral("anime-land"));
    QCoreApplication::setApplicationVersion(
        QStringLiteral(ANIME_LAND_VERSION_STRING));

    cli::BangumiCliView view;
    GlobalAppSettingGuard globalSettings;
    const QString defaultConfigPath =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
        QStringLiteral("/settings.toml");
    const QString configPath =
        selectedConfigPath(command).value_or(defaultConfigPath);
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
                       [&]() { runCommand(presenter, command, *application); });
    const int exitCode = application->exec();
    AL_LOG_INFO("[app] stopped exit_code={}", exitCode);
    return exitCode;
}
