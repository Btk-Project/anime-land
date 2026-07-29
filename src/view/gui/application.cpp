#include "view/gui/application.hpp"

#include <QCoreApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSet>
#include <QStandardPaths>
#include <QtLogging>

#include <ilias/platform/qt.hpp>

#include "common/app_settings.hpp"
#include "common/config.h"
#include "common/log.hpp"
#include "adapters/episode_provider_js/plugin_runtime.hpp"
#include "model/bangumi/bangumi.hpp"
#include "model/bangumi/network_cache.hpp"
#include "model/episode_resource/episode_resource.hpp"
#include "model/library/local_media_import.hpp"
#include "model/library/local_metadata.hpp"
#include "model/persistence/catalog_store.hpp"
#include "model/persistence/database.hpp"
#include "model/persistence/library_store.hpp"
#include "model/playback/playback_session.hpp"
#include "presentation/bangumi/browser_view_model.hpp"
#include "presentation/bangumi/calendar_view_model.hpp"
#include "presentation/library/library_view_model.hpp"
#include "presentation/library/subject_details_view_model.hpp"
#include "presentation/episode_resource/episode_resources_view_model.hpp"
#include "presentation/playback/playback_controller.hpp"
#include "presentation/settings_view_model.hpp"
#include "view/playback/playback_video_surface.hpp"
#include "view/qml/qml_application.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

namespace anime_land::gui {
namespace {

struct StartupOptions {
    std::filesystem::path configFilePath;
    std::optional<std::string> logLevel;
    std::optional<QUrl> proxy;
};

void configureApplicationMetadata() {
    QCoreApplication::setOrganizationName(QStringLiteral("Btk-Project"));
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

auto defaultConfigPath() -> QString {
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return QDir(directory).filePath(QStringLiteral("settings.toml"));
}

auto parseStartupOptions(QGuiApplication &application) -> std::optional<StartupOptions> {
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Qt Quick desktop application for anime-land."));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption configOption(
        {QStringLiteral("c"), QStringLiteral("config")},
        QStringLiteral("Load application settings from PATH."), QStringLiteral("PATH"));
    const QCommandLineOption proxyOption(
        QStringLiteral("proxy"), QStringLiteral("Override the configured Bangumi HTTP/SOCKS5 proxy for this run."),
        QStringLiteral("URL"));
    const QCommandLineOption logLevelOption(
        QStringLiteral("log-level"), QStringLiteral("Override the configured log level for this run."),
        QStringLiteral("LEVEL"));
    parser.addOptions({configOption, proxyOption, logLevelOption});
    parser.process(application);

    StartupOptions options;
    const QString configPath = parser.value(configOption).trimmed();
    options.configFilePath = QFileInfo(configPath.isEmpty() ? defaultConfigPath() : configPath).filesystemFilePath();

    const QString logLevel = parser.value(logLevelOption).trimmed();
    if (!logLevel.isEmpty()) {
        options.logLevel = logLevel.toStdString();
        if (!setLogLevel(*options.logLevel)) {
            std::cerr << "invalid log level: " << *options.logLevel << '\n';
            return std::nullopt;
        }
    }

    const QString proxy = parser.value(proxyOption).trimmed();
    if (!proxy.isEmpty()) {
        options.proxy = QUrl(proxy, QUrl::StrictMode);
        if (!options.proxy->isValid() || options.proxy->scheme().isEmpty()) {
            std::cerr << "invalid proxy URL: " << proxy.toStdString() << '\n';
            return std::nullopt;
        }
    }
    return options;
}

auto fixtureUiRequested() -> bool {
    return qEnvironmentVariableIsSet("ANIME_LAND_UI_FIXTURE") || qEnvironmentVariableIsSet("ANIME_LAND_UI_SMOKE_TEST");
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
        AL_LOG_WARN("[app.log] file logging unavailable: {}", configured.errorMessage);
        return;
    }
    AL_LOG_INFO("[app.log] file logging ready path={}", configured.filePath.string());
}

auto resolveDatabaseSettings(SqlSettings settings) -> std::optional<SqlSettings> {
    const QString databaseType = QString::fromStdString(settings.database_type).toLower();
    if (databaseType != QStringLiteral("sqlite") && databaseType != QStringLiteral("sqlcipher")) {
        return settings;
    }

    QString databasePath = QString::fromUtf8(settings.database_path);
    if (QFileInfo(databasePath).isRelative()) {
        const QString dataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
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

class GraphicalRuntime final {
public:
    GraphicalRuntime(QGuiApplication &application, StartupOptions options)
        : mApplication(application), mOptions(std::move(options)) {}

    auto run() -> int {
        loadSettings();
        if (!initializeCredentials()) {
            return 2;
        }

        ilias::QIoContext ioContext;
        ioContext.install();

        auto sqlSettings = resolveDatabaseSettings(std::move(mSqlSettings));
        if (!sqlSettings) {
            AL_LOG_ERROR("[app.qml] cannot create local data directory");
            return 2;
        }

        auto databaseResult = persistence::LocalDatabase::open(*sqlSettings).wait();
        if (!databaseResult) {
            AL_LOG_ERROR("[app.qml] local database open failed code={}", databaseResult.error().message());
            return 2;
        }

        auto database = std::move(*databaseResult);
        const int exitCode = runWithDatabase(database);
        shutdownDatabase(database);
        return exitCode;
    }

private:
    void loadSettings() {
        const auto loaded = mGlobalSettings.loadOrCreate(mOptions.configFilePath);
        if (!loaded) {
            AL_LOG_WARN("[app.qml] settings unavailable; using defaults");
        }
        else {
            auto settings = mGlobalSettings.get();
            mBangumiSettings = settings->bangumi_settings;
            mSqlSettings = settings->sql_settings;
            mGeneralSettings = settings->general_settings;
            mPluginSettings = settings->plugin_settings;
            AL_LOG_INFO("[app.qml] settings {}", *loaded == AppSettingsFileState::Created ? "created" : "loaded");
        }
        applyLogSettings(mGeneralSettings, mOptions.logLevel);
        if (mOptions.proxy) {
            mBangumiSettings.proxy_url = *mOptions.proxy;
            AL_LOG_INFO("[app.qml] command-line Bangumi proxy override enabled");
        }
    }

    auto initializeCredentials() -> bool {
        TokenStoreOptions options;
        options.kind = TokenStoreKind::System;
        auto store = TokenStore::create(std::move(options));
        if (!store) {
            AL_LOG_WARN("[app.qml] system credential store unavailable; using non-persistent session code={}",
                        bangumiErrorCodeName(store.error().code));
            TokenStoreOptions fallbackOptions;
            fallbackOptions.kind = TokenStoreKind::Memory;
            store = TokenStore::create(std::move(fallbackOptions));
            mPersistentCredentials = false;
        }
        if (!store) {
            AL_LOG_ERROR("[app.qml] token store initialization failed");
            return false;
        }

        mTokenStore = std::move(*store);
        return true;
    }

    auto runWithDatabase(persistence::LocalDatabase &database) -> int {
        auto catalogStore = persistence::CatalogStore::open(database).wait();
        if (!catalogStore) {
            AL_LOG_ERROR("[app.qml] catalog store open failed code={}", catalogStore.error().message());
            return 2;
        }

        auto libraryStore = persistence::LibraryStore::open(database).wait();
        if (!libraryStore) {
            AL_LOG_ERROR("[app.qml] library store open failed code={}", libraryStore.error().message());
            return 2;
        }

        return runShell(std::move(*catalogStore), std::move(*libraryStore));
    }

    auto runShell(persistence::CatalogStore catalogStore, persistence::LibraryStore libraryStore) -> int {
        episode_provider_js::initializeBuiltinEpisodeProviderResources();
        EpisodeProviderRegistry episodeProviderRegistry;
        OnlinePlayableCache onlinePlayableCache;
        EpisodeResourceService episodeResourceService(episodeProviderRegistry,
                                                      onlinePlayableCache,
                                                      catalogStore);
        const QString providerConfigDirectory =
            QString::fromUtf8(mPluginSettings.provider_config_directory);
        if (mPluginSettings.enabled &&
            !QDir().mkpath(providerConfigDirectory)) {
            AL_LOG_WARN(
                "[episode-provider.runtime] cannot create provider config directory path={}",
                providerConfigDirectory.toStdString());
        }
        QSet<QString> loadedPluginIds;
        auto registerPlugin = [&](episode_provider_js::LoadedEpisodePlugin plugin,
                                  QStringView source) {
            auto registered = episodeProviderRegistry.registerProviders(
                plugin.providers);
            if (!registered) {
                AL_LOG_WARN(
                    "[episode-provider.runtime] plugin registration failed id={} source={} code={} message={}",
                    plugin.manifest.id.toStdString(), source.toString().toStdString(),
                    episodeProviderErrorCodeName(registered.error().code),
                    registered.error().message.toStdString());
                return;
            }
            loadedPluginIds.insert(plugin.manifest.id);
            AL_LOG_INFO(
                "[episode-provider.runtime] plugin loaded id={} version={} source={} providers={} network_requests=0",
                plugin.manifest.id.toStdString(),
                plugin.manifest.version.toStdString(),
                source.toString().toStdString(), plugin.providers.size());
        };

        if (mPluginSettings.enabled && mPluginSettings.load_builtin) {
            const QString providerConfig =
                QDir(providerConfigDirectory)
                    .filePath(QStringLiteral("org.anime-land.yhdmmm.json"));
            auto yhdmmmPlugin = episode_provider_js::loadEpisodeProviderPlugin(
                episode_provider_js::builtinYhdmmmPackageRoot(), providerConfig);
            if (!yhdmmmPlugin) {
                AL_LOG_WARN(
                    "[episode-provider.runtime] builtin plugin unavailable code={} message={}",
                    episodeProviderErrorCodeName(yhdmmmPlugin.error().code),
                    yhdmmmPlugin.error().message.toStdString());
            }
            else {
                registerPlugin(std::move(*yhdmmmPlugin),
                               QStringLiteral("builtin"));
            }
        }

        if (mPluginSettings.enabled && mPluginSettings.scan_on_startup) {
            const QString pluginRoot =
                QString::fromUtf8(mPluginSettings.plugins_directory);
            const QString providerDirectory =
                QDir(pluginRoot).filePath(QStringLiteral("episode-providers"));
            if (!QDir().mkpath(providerDirectory)) {
                AL_LOG_WARN(
                    "[episode-provider.runtime] cannot create plugin scan directory path={}",
                    providerDirectory.toStdString());
            }
            else {
                auto scanned = episode_provider_js::scanEpisodeProviderPlugins(
                    providerDirectory, providerConfigDirectory,
                    mPluginSettings.max_packages,
                    mPluginSettings.allow_symlinks,
                    QStringList(loadedPluginIds.cbegin(),
                                loadedPluginIds.cend()));
                for (auto &plugin : scanned.plugins) {
                    registerPlugin(std::move(plugin), QStringLiteral("user"));
                }
                for (const auto &issue : scanned.issues) {
                    AL_LOG_WARN(
                        "[episode-provider.runtime] plugin skipped path={} code={} message={}",
                        issue.packagePath.toStdString(),
                        episodeProviderErrorCodeName(issue.error.code),
                        issue.error.message.toStdString());
                }
                AL_LOG_INFO(
                    "[episode-provider.runtime] startup scan completed directory={} loaded={} skipped={} network_requests=0",
                    providerDirectory.toStdString(), scanned.plugins.size(),
                    scanned.issues.size());
            }
        }

        BangumiModuleOptions bangumiOptions;
        bangumiOptions.features.push_back(bangumiUserCollectionsFeature());
        const auto networkCacheOptions = bangumiNetworkCacheOptions(mBangumiSettings);
        BangumiModule module(std::move(mBangumiSettings), std::move(mTokenStore), std::move(bangumiOptions));

        auto playbackVideoSurface = std::make_shared<qml::PlaybackVideoSurface>();
        PlaybackSession playbackSession([playbackVideoSurface] {
            return makeNekoavPlaybackPipeline(playbackVideoSurface);
        });
        PlaybackController playbackController(playbackSession);
        EpisodeResourcesViewModel episodeResourcesViewModel(
            episodeResourceService, episodeProviderRegistry,
            [&playbackController](const QUrl &source, const QString &title) {
                return playbackController.openMedia(source, title);
            });
        LocalMediaImportService importService(libraryStore, catalogStore, module, [&playbackController](const QUrl &source) {
            return playbackController.openMedia(source);
        });
        LocalMetadataService metadataService(catalogStore, libraryStore);
        LibraryViewModel libraryViewModel(importService, metadataService);
        SubjectDetailsViewModel subjectDetailsViewModel(importService);
        BangumiCalendarViewModel calendarViewModel(module);
        BangumiBrowserViewModel bangumiBrowserViewModel(module);
        ApplicationSettingsViewModel settingsViewModel(mGlobalSettings, mOptions.configFilePath, &module,
                                                       mPersistentCredentials);

        AL_LOG_INFO("[app.qml] starting live model mode");
        const int exitCode = qml::runApplication(mApplication, &calendarViewModel, &bangumiBrowserViewModel, &libraryViewModel,
                                                 &subjectDetailsViewModel, &episodeResourcesViewModel,
                                                 &settingsViewModel, &playbackController,
                                                 playbackVideoSurface.get(), false, networkCacheOptions);
        playbackController.shutdown().wait();
        episodeProviderRegistry.clear();
        onlinePlayableCache.clear();
        return exitCode;
    }

    static void shutdownDatabase(persistence::LocalDatabase &database) {
        auto closed = database.close().wait();
        if (!closed) {
            AL_LOG_WARN("[app.qml] local database close failed code={}", closed.error().message());
        }
    }

    QGuiApplication &mApplication;
    StartupOptions mOptions;
    GlobalAppSettingGuard mGlobalSettings;
    BangumiSettings mBangumiSettings;
    SqlSettings mSqlSettings;
    GeneralSettings mGeneralSettings;
    PluginSettings mPluginSettings;
    std::unique_ptr<TokenStore> mTokenStore;
    bool mPersistentCredentials = true;
};

auto runGraphicalApplication(QGuiApplication &application, StartupOptions options) -> int {
    if (fixtureUiRequested()) {
        AL_LOG_INFO("[app.qml] starting isolated fixture mode");
        return qml::runApplication(application, nullptr, nullptr, nullptr,
                                   nullptr, nullptr, nullptr, nullptr,
                                   nullptr, true, {});
    }
    return GraphicalRuntime(application, std::move(options)).run();
}

} // namespace
} // namespace anime_land::gui

auto anime_land::gui::runApplication(int argc, char **argv) -> int {
#if defined(Q_OS_LINUX)
    // The Quick fallback dialog follows our dynamic palette, while native
    // portal/theme dialogs can independently mix light and dark controls.
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
#endif
    QGuiApplication application(argc, argv);
    configureApplicationMetadata();
    configureQtMessagePattern();
    auto options = parseStartupOptions(application);
    if (!options) {
        return 2;
    }

    AL_LOG_INFO("[app] starting QML shell version={}", ANIME_LAND_VERSION_STRING);
    const int exitCode = runGraphicalApplication(application, std::move(*options));
    AL_LOG_INFO("[app] stopped QML shell exit_code={}", exitCode);
    return exitCode;
}
