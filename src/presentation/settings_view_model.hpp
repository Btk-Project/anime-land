#pragma once

#include "common/app_settings.hpp"

#include <QObject>
#include <QString>

#include <ilias/task.hpp>
#include <ilias/task/scope.hpp>

#include <cstdint>
#include <filesystem>

namespace anime_land {

class BangumiModule;

/** Persisted application/Bangumi settings exposed to the QML settings page. */
class ApplicationSettingsViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString themeMode READ themeMode WRITE setThemeMode
                   NOTIFY appearanceChanged)
    Q_PROPERTY(bool systemDark READ systemDark NOTIFY appearanceChanged)
    Q_PROPERTY(bool effectiveDark READ effectiveDark
                   NOTIFY appearanceChanged)
    Q_PROPERTY(QString configPath READ configPath CONSTANT)
    Q_PROPERTY(QString databasePath READ databasePath CONSTANT)
    Q_PROPERTY(QString bangumiClientId READ bangumiClientId
                   NOTIFY settingsChanged)
    Q_PROPERTY(QString redirectUri READ redirectUri NOTIFY settingsChanged)
    Q_PROPERTY(QString proxyUrl READ proxyUrl NOTIFY settingsChanged)
    Q_PROPERTY(bool bangumiCacheEnabled READ bangumiCacheEnabled
                   NOTIFY settingsChanged)
    Q_PROPERTY(QString bangumiCacheDirectory READ bangumiCacheDirectory
                   NOTIFY settingsChanged)
    Q_PROPERTY(int bangumiCacheMaxSizeMiB READ bangumiCacheMaxSizeMiB
                   NOTIFY settingsChanged)
    Q_PROPERTY(int bangumiCacheTtlDays READ bangumiCacheTtlDays
                   NOTIFY settingsChanged)
    Q_PROPERTY(QString logLevel READ logLevel NOTIFY settingsChanged)
    Q_PROPERTY(QString logDirectory READ logDirectory NOTIFY settingsChanged)
    Q_PROPERTY(int logMaxFileSizeMiB READ logMaxFileSizeMiB
                   NOTIFY settingsChanged)
    Q_PROPERTY(int logMaxFileCount READ logMaxFileCount
                   NOTIFY settingsChanged)
    Q_PROPERTY(QString activeLogFile READ activeLogFile
                   NOTIFY settingsChanged)
    Q_PROPERTY(bool clientSecretConfigured READ clientSecretConfigured
                   NOTIFY settingsChanged)
    Q_PROPERTY(bool credentialPersistenceAvailable
                   READ credentialPersistenceAvailable CONSTANT)
    Q_PROPERTY(bool saving READ saving NOTIFY stateChanged)
    Q_PROPERTY(bool restartRequired READ restartRequired NOTIFY stateChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)
    Q_PROPERTY(QString noticeMessage READ noticeMessage NOTIFY stateChanged)

public:
    ApplicationSettingsViewModel(GlobalAppSettingGuard &settings,
                                 std::filesystem::path settingsPath,
                                 BangumiModule *bangumiModule,
                                 bool credentialPersistenceAvailable,
                                 QObject *parent = nullptr);
    ~ApplicationSettingsViewModel() override;

    auto themeMode() const -> QString { return mThemeMode; }
    auto systemDark() const noexcept -> bool { return mSystemDark; }
    auto effectiveDark() const noexcept -> bool;
    auto configPath() const -> QString;
    auto databasePath() const -> QString { return mDatabasePath; }
    auto bangumiClientId() const -> QString { return mBangumiClientId; }
    auto redirectUri() const -> QString { return mRedirectUri; }
    auto proxyUrl() const -> QString { return mProxyUrl; }
    auto bangumiCacheEnabled() const noexcept -> bool {
        return mBangumiCacheEnabled;
    }
    auto bangumiCacheDirectory() const -> QString {
        return mBangumiCacheDirectory;
    }
    auto bangumiCacheMaxSizeMiB() const noexcept -> int {
        return mBangumiCacheMaxSizeMiB;
    }
    auto bangumiCacheTtlDays() const noexcept -> int {
        return mBangumiCacheTtlDays;
    }
    auto logLevel() const -> QString { return mLogLevel; }
    auto logDirectory() const -> QString { return mLogDirectory; }
    auto logMaxFileSizeMiB() const noexcept -> int {
        return mLogMaxFileSizeMiB;
    }
    auto logMaxFileCount() const noexcept -> int { return mLogMaxFileCount; }
    auto activeLogFile() const -> QString { return mActiveLogFile; }
    auto clientSecretConfigured() const noexcept -> bool {
        return mClientSecretConfigured;
    }
    auto credentialPersistenceAvailable() const noexcept -> bool {
        return mCredentialPersistenceAvailable;
    }
    auto saving() const noexcept -> bool { return mSaving; }
    auto restartRequired() const noexcept -> bool {
        return mRestartRequired;
    }
    auto errorMessage() const -> QString { return mErrorMessage; }
    auto noticeMessage() const -> QString { return mNoticeMessage; }

    void setThemeMode(const QString &mode);

    Q_INVOKABLE void saveBangumiSettings(const QString &clientId,
                                         const QString &newClientSecret,
                                         const QString &redirectUri,
                                         const QString &proxyUrl);
    Q_INVOKABLE void saveLogSettings(const QString &level,
                                     const QString &directory,
                                     int maxFileSizeMiB,
                                     int maxFileCount);
    Q_INVOKABLE void saveBangumiCacheSettings(bool enabled,
                                              const QString &directory,
                                              int maxSizeMiB,
                                              int ttlDays);
    Q_INVOKABLE void reload();

signals:
    void appearanceChanged();
    void settingsChanged();
    void stateChanged();

private:
    auto persistTheme(QString previousMode, std::uint64_t generation)
        -> ilias::Task<void>;
    auto persistBangumi(BangumiSettings previous,
                        BangumiSettings updated,
                        std::uint64_t generation) -> ilias::Task<void>;
    auto persistLog(GeneralSettings previous, GeneralSettings updated,
                    std::uint64_t generation) -> ilias::Task<void>;
    void loadSnapshot();
    void applyTheme();
    void reportError(QString message);

    GlobalAppSettingGuard &mSettings;
    std::filesystem::path mSettingsPath;
    BangumiModule *mBangumiModule = nullptr;
    ilias::TaskScope mTasks;
    QString mThemeMode = QStringLiteral("system");
    QString mDatabasePath;
    QString mBangumiClientId;
    QString mRedirectUri;
    QString mProxyUrl;
    QString mBangumiCacheDirectory;
    QString mLogLevel;
    QString mLogDirectory;
    QString mActiveLogFile;
    QString mErrorMessage;
    QString mNoticeMessage;
    std::uint64_t mGeneration = 0;
    bool mSystemDark = false;
    bool mClientSecretConfigured = false;
    bool mBangumiCacheEnabled = true;
    bool mCredentialPersistenceAvailable = false;
    bool mSaving = false;
    bool mRestartRequired = false;
    bool mDestroying = false;
    int mLogMaxFileSizeMiB = 10;
    int mLogMaxFileCount = 5;
    int mBangumiCacheMaxSizeMiB = 512;
    int mBangumiCacheTtlDays = 7;
};

} // namespace anime_land
