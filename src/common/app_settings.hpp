#pragma once

#include <QString>
#include <QUrl>

#include <filesystem>
#include <optional>
#include <string>

#include <ilias/defines.hpp>
#include <ilias/sync.hpp>
#include <ilias/task.hpp>

#include <nekoproto/global/reflect.hpp>
#include <nekoproto/serialization/reflection.hpp>

#include "common/log.hpp"
#include "common/qt_serialization.hpp"

namespace anime_land {
using namespace ilias;
using namespace NEKO_NAMESPACE;

/** Expand the application variables documented in docs/common/common_design.md. */
auto expandVariables(std::string_view input) -> std::string;

/**
 * @brief The tags for the config
 * encrypt: If the config should be encrypted
 */
struct ConfigTags {
    bool encrypt = false;
    bool variable = false;

    template <typename T, auto tags>
    constexpr static bool constexpr_check() {
        if constexpr (tags.encrypt || tags.variable) {
            return std::is_same_v<T, std::string>;
        }
    }
};

namespace tag_properties {
NEKO_DETAIL_DEFINE_TAG_PROPERTY(bool, encrypt, encrypt)
NEKO_DETAIL_DEFINE_TAG_PROPERTY(bool, variable, variable)
} // namespace tag_properties

// MARK: App Settings
struct SqlSettings {
    // database common settings
    std::string database_type = "sqlite"; // sqlite, mysql
    std::string database_password = "password";
    // mysql settings
    std::string database_host = "localhost";
    uint16_t database_port = 3306;
    std::string database_user = "root";
    std::string database_name = "anime_land";
    // sqlite settings
    std::string database_path = "anime_land.db";

    // clang-format off
  struct Neko {
    constexpr static auto value = Object(
        "database_type",     &SqlSettings::database_type,
        "database_password", make_tags<ParserTag{.skippable = true}>(&SqlSettings::database_password),
        "database_host",     make_tags<ParserTag{.skippable = true}>(&SqlSettings::database_host),
        "database_port",     make_tags<ParserTag{.skippable = true}>(&SqlSettings::database_port),
        "database_user",     make_tags<ParserTag{.skippable = true}>(&SqlSettings::database_user),
        "database_name",     make_tags<ParserTag{.skippable = true}>(&SqlSettings::database_name),
        "database_path",     make_tags<ParserTag{.skippable = true}, ConfigTags{.variable = true}>(&SqlSettings::database_path)
    );
  };
    // clang-format on
};

struct BangumiSettings {
    // OAuth application values are deliberately empty until a developer creates
    // an application at Bangumi; plausible placeholders fail in confusing ways.
    QString client_id;
    std::string client_secret;
    QUrl redirect_uri {QStringLiteral("http://127.0.0.1:38457/callback"),
                       QUrl::StrictMode};
    QUrl oauth_base {QStringLiteral("https://bgm.tv"), QUrl::StrictMode};
    QUrl oauth_application_page {QStringLiteral("https://bgm.tv/dev/app"),
                                 QUrl::StrictMode};
    QUrl bangumi_api {QStringLiteral("https://api.bgm.tv"), QUrl::StrictMode};
    QString user_agent =
        QStringLiteral("Btk-Project/anime-land/0.0.1 "
                       "(https://github.com/Btk-Project/anime-land)");
    // Empty keeps QNetworkAccessManager's default proxy policy. Credentials are
    // separate so the password can use the settings encryption path.
    QUrl proxy_url {QStringLiteral(""), QUrl::StrictMode};
    QString proxy_username;
    std::string proxy_password;
    bool cache_enabled = true;
    std::string cache_path = "${APP_CACHE_DIR}/bangumi";
    std::uint64_t cache_max_size = 512ULL * 1024ULL * 1024ULL;
    int cache_ttl_days = 7;
    // clang-format off
  struct Neko {
    constexpr static auto value = Object(
        "client_id",              make_tags<ParserTag{.skippable = true}>(&BangumiSettings::client_id),
        "client_secret",          make_tags<ConfigTags{.encrypt = true}, ParserTag{.skippable = true}>(&BangumiSettings::client_secret),
        "redirect_uri",           &BangumiSettings::redirect_uri,
        "oauth_base",             &BangumiSettings::oauth_base,
        "oauth_application_page", &BangumiSettings::oauth_application_page,
        "bangumi_api",            &BangumiSettings::bangumi_api,
        "user_agent",             &BangumiSettings::user_agent,
        "proxy_url",              make_tags<ParserTag{.skippable = true}>(&BangumiSettings::proxy_url),
        "proxy_username",         make_tags<ParserTag{.skippable = true}>(&BangumiSettings::proxy_username),
        "proxy_password",         make_tags<ConfigTags{.encrypt = true}, ParserTag{.skippable = true}>(&BangumiSettings::proxy_password),
        "cache_enabled",          make_tags<ParserTag{.skippable = true}>(&BangumiSettings::cache_enabled),
        "cache_path",             make_tags<ParserTag{.skippable = true}, ConfigTags{.variable = true}>(&BangumiSettings::cache_path),
        "cache_max_size",         make_tags<ParserTag{.skippable = true}>(&BangumiSettings::cache_max_size),
        "cache_ttl_days",         make_tags<ParserTag{.skippable = true}>(&BangumiSettings::cache_ttl_days)
    );
  };
    // clang-format on
};

struct AppearanceSettings {
    // "system" follows the desktop color scheme; the other values are explicit
    // user overrides. Keep this as a string so older settings files can be
    // migrated without coupling persistence to a Qt enum value.
    QString theme = QStringLiteral("system");

    // clang-format on
    struct Neko {
        constexpr static auto value = Object(
            "theme", &AppearanceSettings::theme);
    };
    // clang-format off
};

struct GeneralSettings {
    std::string log_level = "info";
    std::string log_file_path = "${APP_LOG_DIR}";
    uint64_t log_file_max_size = 10 * 1024 * 1024; // 10MB
    int log_file_max_count = 5;

    // clang-format off
    struct Neko {
        constexpr static auto value = Object(
            "log_level",          &GeneralSettings::log_level,
            "log_file_path",      make_tags<ParserTag {.skippable = true}, ConfigTags{.variable = true}>(&GeneralSettings::log_file_path),
            "log_file_max_size",  make_tags<ParserTag {.skippable = true}>(&GeneralSettings::log_file_max_size),
            "log_file_max_count", make_tags<ParserTag {.skippable = true}>(&GeneralSettings::log_file_max_count));
    };
    // clang-format on
};

struct AppSettings {
    SqlSettings sql_settings;
    BangumiSettings bangumi_settings;
    AppearanceSettings appearance_settings;
    GeneralSettings general_settings;

    // clang-format off
    struct Neko {
        constexpr static auto value = Object(
            "sql_settings",        &AppSettings::sql_settings,
            "bangumi_settings",    make_tags<ParserTag{.skippable = true}>(&AppSettings::bangumi_settings),
            "appearance_settings", make_tags<ParserTag{.skippable = true}>(&AppSettings::appearance_settings),
            "general_settings",    make_tags<ParserTag{.skippable = true}>(&AppSettings::general_settings)
        );
    };
    // clang-format on
};

enum class AppSettingsFileState {
    Loaded,
    Created,
};

// MARK: Global App Settings
// This is a singleton class to access the app settings
// It provides thread-safe access to the app settings
class GlobalAppSettingGuard {
public:
    struct AppSettingsGuard {
        // MutexGuard only adopts an already-held Ilias mutex. Configuration access
        // is synchronous, so acquire it through the matching blocking API here.
        explicit AppSettingsGuard(Mutex &mutex) : mGuard(mutex.blockingLock()) {}
        ~AppSettingsGuard() = default;
        auto operator*() -> AppSettings & {
            return GlobalAppSettingGuard::mAppSettings;
        }
        auto operator->() -> AppSettings * {
            return &GlobalAppSettingGuard::mAppSettings;
        }

        AppSettingsGuard(const AppSettingsGuard &) = delete;
        AppSettingsGuard &operator=(const AppSettingsGuard &) = delete;
        AppSettingsGuard(AppSettingsGuard &&) = default;
        AppSettingsGuard &operator=(AppSettingsGuard &&) = delete;

    private:
        MutexGuard mGuard;
    };

    void match_and_invoke(std::string_view key, const std::string &prefix,
                          auto &&value, auto func) {
        using value_type = std::decay_t<decltype(value)>;
        if (key == prefix) {
            if constexpr (requires { func(value); }) {
                func(value);
            }
            else {
                return;
            }
        }
        if constexpr (NekoProto::detail::has_names_meta<value_type>) {
            Reflect<std::decay_t<decltype(value)>>::forEach(
                value, [this, prefix, &func, &key](auto &field, std::string_view name) {
                    auto fullname = prefix.empty() ? std::string(name)
                                                   : (prefix + "." + std::string(name));
                    return match_and_invoke(key, fullname, field, func);
                });
        }
    }

public:
    GlobalAppSettingGuard() = default;
    ~GlobalAppSettingGuard() = default;

    auto load(const std::filesystem::path &path) -> bool;
    /** Load an existing complete file, or create one from all struct defaults. */
    auto loadOrCreate(const std::filesystem::path &path)
        -> std::optional<AppSettingsFileState>;
    auto save(const std::filesystem::path &path) -> bool;

    auto get() -> AppSettingsGuard { return AppSettingsGuard(mMutex); }

    template <typename T>
    auto set(std::string_view key, T &&value) -> bool;
    template <typename T>
    auto get(std::string_view key, T &value) -> bool;

private:
    static Mutex mMutex;
    static AppSettings mAppSettings;
};

} // namespace anime_land

template <typename T>
auto anime_land::GlobalAppSettingGuard::set(std::string_view key, T &&value)
    -> bool {
    auto settings = get();
    bool found = false;
    auto setter = [&value, &found](T &field) {
        field = std::forward<T>(value);
        found = true;
    };
    match_and_invoke(key, "", *settings, setter);
    return found;
}

template <typename T>
auto anime_land::GlobalAppSettingGuard::get(std::string_view key, T &value)
    -> bool {
    auto settings = get();
    bool found = false;
    auto getter = [&value, &found](const T &field) {
        value = field;
        found = true;
    };
    match_and_invoke(key, "", *settings, getter);
    return found;
}
