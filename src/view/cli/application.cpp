#include "view/cli/application.hpp"

#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QTimer>
#include <QtLogging>

#include <ilias/platform/qt.hpp>
#include <ilias/task.hpp>

#include <nekoproto/argparser/argparser.hpp>

#include "common/app_settings.hpp"
#include "common/config.h"
#include "common/log.hpp"
#include "model/bangumi/bangumi.hpp"
#include "presentation/bangumi/bangumi_presenter.hpp"
#include "view/cli/bangumi_cli_command.hpp"
#include "view/cli/bangumi_cli_options.hpp"
#include "view/cli/bangumi_cli_view.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

namespace anime_land::cli {
namespace {

namespace argparser = NEKO_NAMESPACE::argparser;

auto utf8(std::string_view value) -> QString {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

auto parserConfig(const char *programName) -> argparser::ArgParserConfig {
    argparser::ArgParserConfig config;
    config.programName = programName != nullptr ? programName : "anime-land-cli";
    config.description = "Bangumi command-line client for anime-land.";
    config.version = ANIME_LAND_VERSION_STRING;
    return config;
}

struct ParseOutcome {
    std::optional<Command> command;
    int exitCode = 0;
};

auto parseCommand(int argc, char **argv) -> ParseOutcome {
    const auto config = parserConfig(argc > 0 ? argv[0] : nullptr);
    if (argc <= 1) {
        std::cout << argparser::format_help<AnimeLandCommands>(config);
        return {};
    }

    auto parsed = argparser::parser<AnimeLandCommands>(argc, argv, config);
    if (parsed) {
        return {.command = Command {std::move(*parsed)}, .exitCode = 0};
    }

    if (parsed.error() == ::make_error_code(argparser::ArgParserError::HelpRequested)) {
        std::cout << argparser::format_help<AnimeLandCommands>(argc, argv, config);
        return {};
    }
    if (parsed.error() == ::make_error_code(argparser::ArgParserError::VersionRequested)) {
        std::cout << argparser::format_version(config);
        return {};
    }

    std::cerr << "argument error: " << parsed.error().message() << '\n'
              << argparser::format_help<AnimeLandCommands>(argc, argv, config);
    return {.command = std::nullopt, .exitCode = 2};
}

auto commonOptions(const Command &command) -> const CommonCommandOptions & {
    return std::visit([](const auto &value) -> const CommonCommandOptions & { return value.common; }, command);
}

auto selectedConfigPath(const Command &command) -> std::optional<QString> {
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

auto commandName(const Command &command) -> std::string_view {
    return std::visit(
        [](const auto &value) -> std::string_view {
            using SelectedCommand = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_same_v<SelectedCommand, LoginCommand>) {
                return "login";
            }
            else if constexpr (std::is_same_v<SelectedCommand, StatusCommand>) {
                return "status";
            }
            else if constexpr (std::is_same_v<SelectedCommand, LogoutCommand>) {
                return "logout";
            }
            else if constexpr (std::is_same_v<SelectedCommand, CollectionsCommand>) {
                return "collections";
            }
            else {
                static_assert(std::is_same_v<SelectedCommand, SearchCommand>);
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

void configureApplicationMetadata() {
    QCoreApplication::setOrganizationName(QStringLiteral("Btk-Project"));
    // Keep the same application identity so both binaries share settings,
    // caches and the platform credential entry.
    QCoreApplication::setApplicationName(QStringLiteral("anime-land"));
    QCoreApplication::setApplicationVersion(QStringLiteral(ANIME_LAND_VERSION_STRING));
}

void configureQtMessagePattern() {
#ifndef ANIME_LAND_USE_SPDLOG
    if (!qEnvironmentVariableIsSet("QT_MESSAGE_PATTERN")) {
        qSetMessagePattern(QStringLiteral(
            "[%{time yyyy-MM-dd hh:mm:ss.zzz}] [%{type}] "
            "[%{file}:%{line}] %{message}"));
    }
#endif
}

auto runCommand(BangumiPresenter &presenter, BangumiView &view, const Command &command,
                QCoreApplication &application) -> ilias::FireAndForget {
    const int exitCode = co_await runBangumiCliCommand(presenter, view, command);
    AL_LOG_INFO("[cli] command completed exit_code={}", exitCode);
    application.exit(exitCode);
}

auto defaultConfigPath() -> QString {
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/settings.toml");
}

void applyLogSettings(const GeneralSettings &settings, const std::optional<std::string> &levelOverride) {
    const std::string_view level = levelOverride ? *levelOverride : settings.log_level;
    const auto maxFileCount = settings.log_file_max_count > 0 ? static_cast<std::size_t>(settings.log_file_max_count) : 1U;
    const auto configured = configureLogging(
        level,
        {.directory = expandVariables(settings.log_file_path),
         .maxFileSize = settings.log_file_max_size,
         .maxFileCount = maxFileCount});
    if (!configured.success) {
        AL_LOG_WARN("[cli.log] file logging unavailable: {}", configured.errorMessage);
        return;
    }
    AL_LOG_INFO("[cli.log] file logging ready path={}", configured.filePath.string());
}

struct CliSettings {
    std::filesystem::path filePath;
    BangumiSettings bangumi;
    GeneralSettings general;
};

auto loadSettings(GlobalAppSettingGuard &globalSettings, BangumiView &view, const QString &configPath)
    -> std::optional<CliSettings> {
    AL_LOG_DEBUG("[cli.config] loading path={}", configPath.toStdString());
    const auto filePath = QFileInfo(configPath).filesystemFilePath();
    auto state = globalSettings.loadOrCreate(filePath);
    if (!state) {
        AL_LOG_ERROR("[cli.config] load_or_create failed");
        view.showError(bangumiError(BangumiErrorCode::InvalidConfiguration,
                                    QStringLiteral("无法加载或创建配置文件：%1").arg(configPath)));
        return std::nullopt;
    }

    if (*state == AppSettingsFileState::Created) {
        AL_LOG_INFO("[cli.config] settings created");
        view.showMessage(QStringLiteral("已生成包含全部默认字段的配置文件：%1").arg(configPath));
    }
    else {
        AL_LOG_INFO("[cli.config] settings loaded");
    }

    auto settings = globalSettings.get();
    return CliSettings {
        .filePath = filePath,
        .bangumi = settings->bangumi_settings,
        .general = settings->general_settings,
    };
}

auto createCredentials(const Command &command, const CommonCommandOptions &options, BangumiView &view)
    -> BangumiResult<std::unique_ptr<TokenStore>> {
    auto requestedKind = parseTokenStoreKind(options.tokenStore);
    if (!requestedKind) {
        return ilias::Err(std::move(requestedKind.error()));
    }

    TokenStoreOptions storeOptions;
    storeOptions.kind = *requestedKind;
    if (options.tokenFile) {
        storeOptions.filePath = utf8(*options.tokenFile);
    }

    auto effectiveKind = *requestedKind;
    auto store = TokenStore::create(std::move(storeOptions));
    if (!store && std::holds_alternative<SearchCommand>(command)) {
        AL_LOG_WARN("[cli.credentials] selected store unavailable for public search; falling back to memory code={}",
                    bangumiErrorCodeName(store.error().code));
        view.showMessage(QStringLiteral("Bangumi 凭据存储不可用；本次搜索将继续使用匿名模式。"));
        TokenStoreOptions anonymousStoreOptions;
        anonymousStoreOptions.kind = TokenStoreKind::Memory;
        store = TokenStore::create(std::move(anonymousStoreOptions));
        effectiveKind = TokenStoreKind::Memory;
    }
    if (!store) {
        return ilias::Err(std::move(store.error()));
    }

    AL_LOG_INFO("[cli.credentials] token store ready backend={}", tokenStoreName(effectiveKind));
    return std::move(*store);
}

auto saveOAuthApplication(GlobalAppSettingGuard &globalSettings, std::filesystem::path configFilePath, QString configPath,
                          BangumiOAuthApplication application) -> ilias::Task<BangumiResult<void>> {
    co_return co_await ilias::blocking([&globalSettings, configFilePath = std::move(configFilePath),
                                        configPath = std::move(configPath), application = std::move(application)]() mutable {
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
            AL_LOG_INFO("[cli.config] OAuth application settings saved");
            clearBangumiOAuthApplication(application);
            if (!oldClientSecret.empty()) {
                BangumiOAuthApplication oldApplication {std::move(oldClientId), std::move(oldClientSecret)};
                clearBangumiOAuthApplication(oldApplication);
            }
            return BangumiResult<void> {};
        }

        AL_LOG_ERROR("[cli.config] failed to save OAuth application settings");
        {
            auto settings = globalSettings.get();
            settings->bangumi_settings.client_id = std::move(oldClientId);
            settings->bangumi_settings.client_secret = std::move(oldClientSecret);
        }
        clearBangumiOAuthApplication(application);
        return BangumiResult<void> {ilias::Err(bangumiError(
            BangumiErrorCode::CredentialStoreError,
            QStringLiteral("无法保存 Bangumi OAuth 应用参数：%1").arg(configPath)))};
    });
}

auto createApplication(int argc, char **argv, const Command &command) -> std::unique_ptr<QCoreApplication> {
    if (!std::holds_alternative<LoginCommand>(command)) {
        return std::make_unique<QCoreApplication>(argc, argv);
    }

    auto application = std::make_unique<QGuiApplication>(argc, argv);
    application->setQuitOnLastWindowClosed(false);
    return application;
}

auto executeCommand(int argc, char **argv, const Command &command, const CommonCommandOptions &options) -> int {
    auto application = createApplication(argc, argv, command);
    configureApplicationMetadata();

    BangumiCliView view;
    GlobalAppSettingGuard globalSettings;
    const QString configPath = selectedConfigPath(command).value_or(defaultConfigPath());
    auto settings = loadSettings(globalSettings, view, configPath);
    if (!settings) {
        return 2;
    }

    applyLogSettings(settings->general, options.logLevel);
    if (options.proxy) {
        settings->bangumi.proxy_url = QUrl(utf8(*options.proxy), QUrl::StrictMode);
        AL_LOG_INFO("[cli.config] command-line Bangumi proxy override enabled");
    }

    auto credentials = createCredentials(command, options, view);
    if (!credentials) {
        AL_LOG_ERROR("[cli.credentials] token store initialization failed code={}",
                     bangumiErrorCodeName(credentials.error().code));
        view.showError(credentials.error());
        return credentials.error().code == BangumiErrorCode::UnsupportedCredentialStore ? 4 : 2;
    }

    ilias::QIoContext ioContext;
    ioContext.install();
    BangumiModuleOptions bangumiOptions;
    bangumiOptions.features.push_back(bangumiUserCollectionsFeature());
    BangumiModule module(std::move(settings->bangumi), std::move(*credentials), std::move(bangumiOptions));
    auto applicationSaver = [&globalSettings, configFilePath = settings->filePath, configPath](
                                BangumiOAuthApplication application) {
        return saveOAuthApplication(globalSettings, configFilePath, configPath, std::move(application));
    };
    BangumiPresenter presenter(module, view, std::move(applicationSaver));

    // Start only after exec() has entered the Qt loop. Otherwise a synchronous
    // configuration failure could call exit() before Qt can observe it.
    AL_LOG_DEBUG("[cli] entering Qt event loop");
    QTimer::singleShot(0, application.get(), [&] {
        runCommand(presenter, view, command, *application);
    });
    const int exitCode = application->exec();
    AL_LOG_INFO("[cli] stopped exit_code={}", exitCode);
    return exitCode;
}

} // namespace

auto runApplication(int argc, char **argv) -> int {
    auto parsed = parseCommand(argc, argv);
    if (!parsed.command) {
        return parsed.exitCode;
    }

    const auto &command = *parsed.command;
    const auto &options = commonOptions(command);
    if (options.logLevel && !setLogLevel(*options.logLevel)) {
        std::cerr << "invalid log level: " << *options.logLevel << '\n';
        return 2;
    }

    configureQtMessagePattern();
    AL_LOG_INFO("[cli] starting version={} command={}", ANIME_LAND_VERSION_STRING, commandName(command));
    return executeCommand(argc, argv, command, options);
}

} // namespace anime_land::cli
