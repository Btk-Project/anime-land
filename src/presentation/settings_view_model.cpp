#include "presentation/settings_view_model.hpp"

#include "common/log.hpp"
#include "model/bangumi/bangumi.hpp"

#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>
#include <QUrl>

#include <ilias/sync.hpp>

#include <algorithm>
#include <limits>
#include <utility>

namespace anime_land {
namespace {

auto validThemeMode(QStringView mode) -> bool {
    return mode == QStringLiteral("system")
           || mode == QStringLiteral("dark")
           || mode == QStringLiteral("light");
}

auto validLogLevel(QStringView level) -> bool {
    return level == QStringLiteral("trace")
           || level == QStringLiteral("debug")
           || level == QStringLiteral("info")
           || level == QStringLiteral("warn")
           || level == QStringLiteral("error")
           || level == QStringLiteral("critical");
}

constexpr std::uint64_t kMebibyte = 1024U * 1024U;

auto darkPalette() -> QPalette {
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(QStringLiteral("#111315")));
    palette.setColor(QPalette::WindowText,
                     QColor(QStringLiteral("#eceff1")));
    palette.setColor(QPalette::Base, QColor(QStringLiteral("#191c1f")));
    palette.setColor(QPalette::AlternateBase,
                     QColor(QStringLiteral("#202428")));
    palette.setColor(QPalette::ToolTipBase,
                     QColor(QStringLiteral("#202428")));
    palette.setColor(QPalette::ToolTipText,
                     QColor(QStringLiteral("#eceff1")));
    palette.setColor(QPalette::Text, QColor(QStringLiteral("#eceff1")));
    palette.setColor(QPalette::Button, QColor(QStringLiteral("#202428")));
    palette.setColor(QPalette::ButtonText,
                     QColor(QStringLiteral("#eceff1")));
    palette.setColor(QPalette::BrightText,
                     QColor(QStringLiteral("#ffffff")));
    palette.setColor(QPalette::Highlight,
                     QColor(QStringLiteral("#7d8893")));
    palette.setColor(QPalette::HighlightedText,
                     QColor(QStringLiteral("#111315")));
    palette.setColor(QPalette::PlaceholderText,
                     QColor(QStringLiteral("#6f767d")));
    palette.setColor(QPalette::Disabled, QPalette::Text,
                     QColor(QStringLiteral("#6f767d")));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText,
                     QColor(QStringLiteral("#6f767d")));
    return palette;
}

auto lightPalette() -> QPalette {
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(QStringLiteral("#f4f6f8")));
    palette.setColor(QPalette::WindowText,
                     QColor(QStringLiteral("#202428")));
    palette.setColor(QPalette::Base, QColor(QStringLiteral("#ffffff")));
    palette.setColor(QPalette::AlternateBase,
                     QColor(QStringLiteral("#edf0f3")));
    palette.setColor(QPalette::ToolTipBase,
                     QColor(QStringLiteral("#ffffff")));
    palette.setColor(QPalette::ToolTipText,
                     QColor(QStringLiteral("#202428")));
    palette.setColor(QPalette::Text, QColor(QStringLiteral("#202428")));
    palette.setColor(QPalette::Button, QColor(QStringLiteral("#ffffff")));
    palette.setColor(QPalette::ButtonText,
                     QColor(QStringLiteral("#202428")));
    palette.setColor(QPalette::BrightText,
                     QColor(QStringLiteral("#000000")));
    palette.setColor(QPalette::Highlight,
                     QColor(QStringLiteral("#657686")));
    palette.setColor(QPalette::HighlightedText,
                     QColor(QStringLiteral("#ffffff")));
    palette.setColor(QPalette::PlaceholderText,
                     QColor(QStringLiteral("#7c858d")));
    palette.setColor(QPalette::Disabled, QPalette::Text,
                     QColor(QStringLiteral("#9aa1a8")));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText,
                     QColor(QStringLiteral("#9aa1a8")));
    return palette;
}

auto isDarkSystemScheme() -> bool {
    const auto scheme = QGuiApplication::styleHints()->colorScheme();
    if (scheme != Qt::ColorScheme::Unknown) {
        return scheme == Qt::ColorScheme::Dark;
    }
    return QGuiApplication::palette().color(QPalette::Window).lightness()
           < 128;
}

auto validHttpUrl(const QUrl &url) -> bool {
    return url.isValid() && !url.host().isEmpty()
           && (url.scheme() == QStringLiteral("http")
               || url.scheme() == QStringLiteral("https"));
}

auto validProxyUrl(const QUrl &url) -> bool {
    if (url.isEmpty()) {
        return true;
    }
    const QString scheme = url.scheme().toLower();
    const int defaultPort = scheme == QStringLiteral("http") ? 80 : 1080;
    return url.isValid() && !url.host().isEmpty()
           && (scheme == QStringLiteral("http")
               || scheme == QStringLiteral("socks5"))
           && url.userName().isEmpty() && url.password().isEmpty()
           && (url.path().isEmpty() || url.path() == QStringLiteral("/"))
           && !url.hasQuery() && !url.hasFragment()
           && url.port(defaultPort) > 0 && url.port(defaultPort) <= 65'535;
}

} // namespace

ApplicationSettingsViewModel::ApplicationSettingsViewModel(
    GlobalAppSettingGuard &settings, std::filesystem::path settingsPath,
    BangumiModule *bangumiModule, bool credentialPersistenceAvailable,
    QObject *parent)
    : QObject(parent), mSettings(settings),
      mSettingsPath(std::move(settingsPath)),
      mBangumiModule(bangumiModule),
      mSystemDark(isDarkSystemScheme()),
      mCredentialPersistenceAvailable(credentialPersistenceAvailable) {
    loadSnapshot();
    applyTheme();
    QObject::connect(
        QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
        this, [this](Qt::ColorScheme scheme) {
            if (mThemeMode != QStringLiteral("system")) {
                return;
            }
            const bool dark = scheme == Qt::ColorScheme::Unknown
                                  ? isDarkSystemScheme()
                                  : scheme == Qt::ColorScheme::Dark;
            if (dark == mSystemDark) {
                return;
            }
            mSystemDark = dark;
            applyTheme();
            emit appearanceChanged();
        });
}

ApplicationSettingsViewModel::~ApplicationSettingsViewModel() {
    mDestroying = true;
    ++mGeneration;
    mTasks.shutdown().wait();
}

auto ApplicationSettingsViewModel::effectiveDark() const noexcept -> bool {
    return mThemeMode == QStringLiteral("dark")
           || (mThemeMode == QStringLiteral("system") && mSystemDark);
}

auto ApplicationSettingsViewModel::configPath() const -> QString {
    return QString::fromStdString(mSettingsPath.string());
}

void ApplicationSettingsViewModel::loadSnapshot() {
    auto settings = mSettings.get();
    const QString storedTheme = settings->appearance_settings.theme.trimmed();
    mThemeMode = validThemeMode(storedTheme) ? storedTheme
                                             : QStringLiteral("system");
    mDatabasePath = QString::fromStdString(
        settings->sql_settings.database_path);
    mBangumiClientId = settings->bangumi_settings.client_id;
    mRedirectUri = settings->bangumi_settings.redirect_uri.toString();
    mProxyUrl = settings->bangumi_settings.proxy_url.toString();
    mBangumiCacheEnabled = settings->bangumi_settings.cache_enabled;
    mBangumiCacheDirectory = QString::fromStdString(
        settings->bangumi_settings.cache_path);
    const std::uint64_t cacheSizeMiB =
        settings->bangumi_settings.cache_max_size / kMebibyte;
    mBangumiCacheMaxSizeMiB = static_cast<int>(
        std::min<std::uint64_t>(cacheSizeMiB, 102400U));
    mBangumiCacheTtlDays =
        std::clamp(settings->bangumi_settings.cache_ttl_days, 1, 3650);
    mClientSecretConfigured =
        !settings->bangumi_settings.client_secret.empty();
    mLogLevel = QString::fromStdString(settings->general_settings.log_level);
    mLogDirectory =
        QString::fromStdString(settings->general_settings.log_file_path);
    const std::uint64_t sizeMiB =
        std::max<std::uint64_t>(1, settings->general_settings.log_file_max_size
                                      / kMebibyte);
    mLogMaxFileSizeMiB = static_cast<int>(std::min<std::uint64_t>(
        sizeMiB, static_cast<std::uint64_t>(std::numeric_limits<int>::max())));
    mLogMaxFileCount =
        std::clamp(settings->general_settings.log_file_max_count, 1, 100);
    mActiveLogFile =
        QString::fromStdString(currentLogFilePath().string());
}

void ApplicationSettingsViewModel::applyTheme() {
    auto *hints = QGuiApplication::styleHints();
    if (mThemeMode == QStringLiteral("system")) {
        hints->unsetColorScheme();
        mSystemDark = isDarkSystemScheme();
    }
    else {
        hints->setColorScheme(effectiveDark() ? Qt::ColorScheme::Dark
                                              : Qt::ColorScheme::Light);
    }
    QGuiApplication::setPalette(effectiveDark() ? darkPalette()
                                                 : lightPalette());
}

void ApplicationSettingsViewModel::reportError(QString message) {
    mErrorMessage = std::move(message);
    mNoticeMessage.clear();
    emit stateChanged();
}

void ApplicationSettingsViewModel::setThemeMode(const QString &mode) {
    const QString normalized = mode.trimmed().toLower();
    if (!validThemeMode(normalized)) {
        reportError(QStringLiteral("无效的主题模式"));
        return;
    }
    if (normalized == mThemeMode || mSaving || mDestroying) {
        return;
    }

    const QString previous = mThemeMode;
    mThemeMode = normalized;
    mSaving = true;
    mErrorMessage.clear();
    mNoticeMessage = QStringLiteral("正在保存外观设置…");
    const auto generation = ++mGeneration;
    applyTheme();
    emit appearanceChanged();
    emit stateChanged();
    mTasks.spawn(persistTheme(previous, generation));
}

auto ApplicationSettingsViewModel::persistTheme(
    QString previousMode, std::uint64_t generation) -> ilias::Task<void> {
    const QString selected = mThemeMode;
    const bool saved = co_await ilias::blocking(
        [this, selected]() {
            QString previous;
            {
                auto settings = mSettings.get();
                previous = settings->appearance_settings.theme;
                settings->appearance_settings.theme = selected;
            }
            if (mSettings.save(mSettingsPath)) {
                return true;
            }
            auto settings = mSettings.get();
            settings->appearance_settings.theme = std::move(previous);
            return false;
        });
    if (mDestroying || generation != mGeneration) {
        co_return;
    }
    mSaving = false;
    if (!saved) {
        mThemeMode = std::move(previousMode);
        applyTheme();
        emit appearanceChanged();
        reportError(QStringLiteral("无法保存主题设置"));
        co_return;
    }
    mNoticeMessage = QStringLiteral("主题设置已保存");
    emit stateChanged();
}

void ApplicationSettingsViewModel::saveBangumiSettings(
    const QString &clientId, const QString &newClientSecret,
    const QString &redirectUri, const QString &proxyUrl) {
    if (mSaving || mDestroying) {
        return;
    }

    const QUrl redirect(redirectUri.trimmed(), QUrl::StrictMode);
    const QUrl proxy(proxyUrl.trimmed(), QUrl::StrictMode);
    if (!validHttpUrl(redirect)) {
        reportError(QStringLiteral("OAuth 回调地址必须是有效的 HTTP/HTTPS URL"));
        return;
    }
    if (!validProxyUrl(proxy)) {
        reportError(QStringLiteral(
            "代理地址必须为空，或是不含路径和凭据的 http:// / socks5:// URL"));
        return;
    }

    BangumiSettings previous;
    {
        auto settings = mSettings.get();
        previous = settings->bangumi_settings;
    }
    BangumiSettings updated = previous;
    updated.client_id = clientId.trimmed();
    if (!newClientSecret.isEmpty()) {
        updated.client_secret = newClientSecret.toStdString();
    }
    updated.redirect_uri = redirect;
    updated.proxy_url = proxy;

    mSaving = true;
    mErrorMessage.clear();
    mNoticeMessage = QStringLiteral("正在保存 Bangumi 设置…");
    const auto generation = ++mGeneration;
    emit stateChanged();
    mTasks.spawn(persistBangumi(std::move(previous), std::move(updated),
                                generation));
}

auto ApplicationSettingsViewModel::persistBangumi(
    BangumiSettings previous, BangumiSettings updated,
    std::uint64_t generation) -> ilias::Task<void> {
    const bool saved = co_await ilias::blocking(
        [this, previous, updated]() mutable {
            {
                auto settings = mSettings.get();
                settings->bangumi_settings = std::move(updated);
            }
            if (mSettings.save(mSettingsPath)) {
                return true;
            }
            auto settings = mSettings.get();
            settings->bangumi_settings = std::move(previous);
            return false;
        });
    if (mDestroying || generation != mGeneration) {
        co_return;
    }
    mSaving = false;
    if (!saved) {
        reportError(QStringLiteral("无法保存 Bangumi 设置"));
        co_return;
    }

    BangumiSettings current;
    {
        auto settings = mSettings.get();
        current = settings->bangumi_settings;
    }
    mBangumiClientId = current.client_id;
    mRedirectUri = current.redirect_uri.toString();
    mProxyUrl = current.proxy_url.toString();
    mBangumiCacheEnabled = current.cache_enabled;
    mBangumiCacheDirectory = QString::fromStdString(current.cache_path);
    mBangumiCacheMaxSizeMiB = static_cast<int>(std::min<std::uint64_t>(
        current.cache_max_size / kMebibyte, 102400U));
    mBangumiCacheTtlDays = std::clamp(current.cache_ttl_days, 1, 3650);
    mClientSecretConfigured = !current.client_secret.empty();
    mRestartRequired = true;
    if (mBangumiModule) {
        BangumiOAuthApplication application {
            .clientId = current.client_id,
            .clientSecret = current.client_secret,
        };
        mBangumiModule->setOAuthApplication(application);
        clearBangumiOAuthApplication(application);
    }
    mNoticeMessage = QStringLiteral(
        "Bangumi 设置已保存；代理等网络设置将在下次启动时生效");
    emit settingsChanged();
    emit stateChanged();
}

void ApplicationSettingsViewModel::saveBangumiCacheSettings(
    bool enabled, const QString &directory, int maxSizeMiB,
    int ttlDays) {
    if (mSaving || mDestroying) {
        return;
    }
    if (maxSizeMiB < 0 || maxSizeMiB > 102400) {
        reportError(QStringLiteral("Bangumi 缓存大小必须在 0–102400 MiB 之间"));
        return;
    }
    if (ttlDays < 1 || ttlDays > 3650) {
        reportError(QStringLiteral("Bangumi 缓存有效期必须在 1–3650 天之间"));
        return;
    }
    const QString normalizedDirectory = directory.trimmed();
    if (enabled && maxSizeMiB > 0 && normalizedDirectory.isEmpty()) {
        reportError(QStringLiteral("启用 Bangumi 缓存时目录不能为空"));
        return;
    }

    BangumiSettings previous;
    {
        auto settings = mSettings.get();
        previous = settings->bangumi_settings;
    }
    BangumiSettings updated = previous;
    updated.cache_enabled = enabled;
    updated.cache_path = expandVariables(normalizedDirectory.toStdString());
    updated.cache_max_size =
        static_cast<std::uint64_t>(maxSizeMiB) * kMebibyte;
    updated.cache_ttl_days = ttlDays;

    mSaving = true;
    mErrorMessage.clear();
    mNoticeMessage = QStringLiteral("正在保存 Bangumi 缓存设置…");
    const auto generation = ++mGeneration;
    emit stateChanged();
    mTasks.spawn(persistBangumi(std::move(previous), std::move(updated),
                                generation));
}

void ApplicationSettingsViewModel::saveLogSettings(
    const QString &level, const QString &directory, int maxFileSizeMiB,
    int maxFileCount) {
    if (mSaving || mDestroying) {
        return;
    }

    const QString normalizedLevel = level.trimmed().toLower();
    if (!validLogLevel(normalizedLevel)) {
        reportError(QStringLiteral("日志级别无效"));
        return;
    }
    const QString normalizedDirectory = directory.trimmed();
    if (normalizedDirectory.isEmpty()) {
        reportError(QStringLiteral("日志目录不能为空"));
        return;
    }
    if (maxFileSizeMiB < 1 || maxFileSizeMiB > 1024) {
        reportError(QStringLiteral("单个日志文件大小必须在 1–1024 MiB 之间"));
        return;
    }
    if (maxFileCount < 1 || maxFileCount > 100) {
        reportError(QStringLiteral("日志文件数量必须在 1–100 之间"));
        return;
    }

    GeneralSettings previous;
    {
        auto settings = mSettings.get();
        previous = settings->general_settings;
    }
    GeneralSettings updated = previous;
    updated.log_level = normalizedLevel.toStdString();
    updated.log_file_path =
        expandVariables(normalizedDirectory.toStdString());
    updated.log_file_max_size =
        static_cast<std::uint64_t>(maxFileSizeMiB) * kMebibyte;
    updated.log_file_max_count = maxFileCount;

    mSaving = true;
    mErrorMessage.clear();
    mNoticeMessage = QStringLiteral("正在保存日志设置…");
    const auto generation = ++mGeneration;
    emit stateChanged();
    mTasks.spawn(persistLog(std::move(previous), std::move(updated),
                            generation));
}

auto ApplicationSettingsViewModel::persistLog(
    GeneralSettings previous, GeneralSettings updated,
    std::uint64_t generation) -> ilias::Task<void> {
    struct Result {
        bool saved = false;
        std::string error;
        std::filesystem::path filePath;
    };
    Result result = co_await ilias::blocking(
        [this, previous, updated]() mutable {
            const LogFileOptions newOptions {
                .directory = updated.log_file_path,
                .maxFileSize = updated.log_file_max_size,
                .maxFileCount =
                    static_cast<std::size_t>(updated.log_file_max_count),
            };
            auto configured = configureLogging(updated.log_level, newOptions);
            if (!configured.success) {
                return Result {
                    .saved = false,
                    .error = std::move(configured.errorMessage),
                    .filePath = {},
                };
            }

            {
                auto settings = mSettings.get();
                settings->general_settings = updated;
            }
            if (mSettings.save(mSettingsPath)) {
                return Result {.saved = true,
                               .error = {},
                               .filePath = std::move(configured.filePath)};
            }

            {
                auto settings = mSettings.get();
                settings->general_settings = previous;
            }
            static_cast<void>(configureLogging(
                previous.log_level,
                {.directory = previous.log_file_path,
                 .maxFileSize = previous.log_file_max_size,
                 .maxFileCount = static_cast<std::size_t>(
                     std::max(1, previous.log_file_max_count))}));
            return Result {
                .saved = false,
                .error = "cannot save application settings",
                .filePath = {},
            };
        });
    if (mDestroying || generation != mGeneration) {
        co_return;
    }
    mSaving = false;
    if (!result.saved) {
        reportError(QStringLiteral("无法应用日志设置：%1")
                        .arg(QString::fromStdString(result.error)));
        co_return;
    }

    loadSnapshot();
    mActiveLogFile = QString::fromStdString(result.filePath.string());
    mNoticeMessage = QStringLiteral("日志设置已保存并立即生效");
    emit settingsChanged();
    emit stateChanged();
}

void ApplicationSettingsViewModel::reload() {
    if (mSaving || mDestroying) {
        return;
    }
    loadSnapshot();
    applyTheme();
    mErrorMessage.clear();
    mNoticeMessage = QStringLiteral("已从当前运行配置重新读取");
    emit appearanceChanged();
    emit settingsChanged();
    emit stateChanged();
}

} // namespace anime_land
