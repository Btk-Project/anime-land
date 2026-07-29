#include <gtest/gtest.h>

#include "common/app_settings.hpp"
#include "common/log.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

#include <nekoproto/serialization/toml_serializer.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace anime_land;

namespace {

#ifndef ANIME_LAND_USE_SPDLOG
struct CapturedQtLogMessage {
  QtMsgType type;
  QString message;
  QString file;
  int line;
};

std::vector<CapturedQtLogMessage> *qtLogMessages = nullptr;

void captureQtLogMessage(QtMsgType type, const QMessageLogContext &context,
                         const QString &message) {
  if (qtLogMessages != nullptr) {
    qtLogMessages->push_back({
        .type = type,
        .message = message,
        .file = QString::fromUtf8(context.file != nullptr ? context.file : ""),
        .line = context.line,
    });
  }
}
#endif

struct QtSerializationProbe {
  QString text;
  QUrl url;

  // clang-format off
  struct Neko {
    constexpr static auto value = NEKO_NAMESPACE::Object(
        "text", &QtSerializationProbe::text,
        "url",  &QtSerializationProbe::url
    );
  };
  // clang-format on
};

auto temporarySettingsPath(std::string_view name) -> std::filesystem::path {
  const auto suffix =
      std::chrono::steady_clock::now().time_since_epoch().count();
  auto directory =
      std::filesystem::temp_directory_path() /
      ("anime-land-" + std::string(name) + "-" + std::to_string(suffix));
  std::filesystem::create_directories(directory);
  return directory / "settings.toml";
}

void removeSettingsFiles(const std::filesystem::path &path) {
  std::error_code error;
  std::filesystem::remove_all(path.parent_path(), error);
}

} // namespace

#ifndef ANIME_LAND_USE_SPDLOG
TEST(AppLog, FallbackUsesQtLoggingAndStdFormatSyntax) {
  std::vector<CapturedQtLogMessage> messages;
  qtLogMessages = &messages;
  const auto previousHandler = qInstallMessageHandler(captureQtLogMessage);

  EXPECT_TRUE(setLogLevel("trace"));
  const int debugLine = __LINE__ + 1;
  AL_LOG_DEBUG("formatted {} {:02}", "value", 7);
  const int warningLine = __LINE__ + 1;
  AL_LOG_WARN("warning {}", 3);

  qInstallMessageHandler(previousHandler);
  qtLogMessages = nullptr;

  ASSERT_EQ(messages.size(), 2U);
  EXPECT_EQ(messages[0].type, QtDebugMsg);
  EXPECT_EQ(messages[0].message, QStringLiteral("formatted value 07"));
  EXPECT_TRUE(messages[0].file.endsWith(QStringLiteral("test_app_settings.cpp")));
  EXPECT_EQ(messages[0].line, debugLine);
  EXPECT_EQ(messages[1].type, QtWarningMsg);
  EXPECT_EQ(messages[1].message, QStringLiteral("warning 3"));
  EXPECT_TRUE(messages[1].file.endsWith(QStringLiteral("test_app_settings.cpp")));
  EXPECT_EQ(messages[1].line, warningLine);
  EXPECT_TRUE(setLogLevel("info"));
}
#endif

TEST(AppSettings, ExpandsDocumentedApplicationVariables) {
  EXPECT_EQ(expandVariables("plain/value"), "plain/value");
  EXPECT_EQ(expandVariables("${APP_NAME}-${APP_VERSION}"),
            "anime-land-tests-9.8.7");
  EXPECT_EQ(expandVariables("before/${UNKNOWN}/after"),
            "before/${UNKNOWN}/after");
  EXPECT_EQ(expandVariables("unfinished/${APP_NAME"),
            "unfinished/${APP_NAME");

  const auto expectedLogDirectory =
      QDir(QStandardPaths::writableLocation(
               QStandardPaths::AppLocalDataLocation))
          .filePath(QStringLiteral("logs"))
          .toStdString();
  EXPECT_EQ(expandVariables("${APP_LOG_DIR}"), expectedLogDirectory);

  constexpr std::array<std::string_view, 13> variables {
      "APP_INSTALL_DIR", "APP_EXEC_DIR",  "APP_CONFIG_DIR",
      "APP_DATA_DIR",    "APP_LOG_DIR",   "APP_CACHE_DIR",
      "APP_TEMP_DIR",    "WORK_DIR",      "USER_HOME_DIR",
      "APP_VERSION",     "APP_NAME",      "USER_NAME",
      "HOST_NAME",
  };
  for (const auto name : variables) {
    const std::string expression = "${" + std::string(name) + "}";
    EXPECT_EQ(expandVariables(expression).find("${"), std::string::npos)
        << name;
  }
}

TEST(AppLog, WritesTimestampedApplicationLogFile) {
  const auto settingsPath = temporarySettingsPath("file-log");
  const auto logDirectory = settingsPath.parent_path() / "logs";
  const auto configured = configureLogging(
      "debug", {.directory = logDirectory,
                .maxFileSize = 1024U * 1024U,
                .maxFileCount = 2});
  ASSERT_TRUE(configured.success) << configured.errorMessage;
  EXPECT_EQ(configured.filePath.parent_path(), logDirectory);
  EXPECT_TRUE(configured.filePath.filename().string().starts_with(
      "anime-land-tests-"));
  EXPECT_EQ(configured.filePath.extension(), ".log");

  AL_LOG_WARN("file logger unicode={}", "中文");
  shutdownLogging();

  std::ifstream input(configured.filePath, std::ios::binary);
  ASSERT_TRUE(input.is_open());
  const std::string contents((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
  EXPECT_NE(contents.find("file logger unicode=中文"), std::string::npos);
  removeSettingsFiles(settingsPath);
}

TEST(AppSettings, Load) {
  GlobalAppSettingGuard gasguard;

  {
    auto settings = gasguard.get();
    *settings = AppSettings{};
  }

  {
    auto settings = gasguard.get();
    EXPECT_EQ(settings->sql_settings.database_host, "localhost");
    EXPECT_EQ(settings->sql_settings.database_password, "password");
    EXPECT_EQ(settings->sql_settings.database_port, 3306);
    EXPECT_EQ(settings->sql_settings.database_type, "sqlite");
    EXPECT_EQ(settings->sql_settings.database_user, "root");
    EXPECT_EQ(settings->sql_settings.database_name, "anime_land");
    EXPECT_EQ(settings->sql_settings.database_path, "anime_land.db");
  }

  EXPECT_TRUE(
      gasguard.set("sql_settings.database_host", std::string("127.0.0.1")));
  EXPECT_TRUE(gasguard.set("sql_settings.database_password",
                           std::string("new_password")));
  EXPECT_TRUE(
      gasguard.set("sql_settings.database_port", static_cast<uint16_t>(3307)));
  EXPECT_TRUE(gasguard.set("sql_settings.database_type", std::string("mysql")));
  EXPECT_TRUE(
      gasguard.set("sql_settings.database_user", std::string("new_root")));
  EXPECT_TRUE(gasguard.set("sql_settings.database_name",
                           std::string("new_anime_land")));
  EXPECT_TRUE(gasguard.set("sql_settings.database_path",
                           std::string("new_anime_land.db")));

  std::string value;
  EXPECT_TRUE(gasguard.get("sql_settings.database_host", value));
  EXPECT_EQ(value, "127.0.0.1");
  EXPECT_TRUE(gasguard.get("sql_settings.database_password", value));
  EXPECT_EQ(value, "new_password");
  uint16_t port;
  EXPECT_TRUE(gasguard.get("sql_settings.database_port", port));
  EXPECT_EQ(port, 3307);
  EXPECT_TRUE(gasguard.get("sql_settings.database_type", value));
  EXPECT_EQ(value, "mysql");
  EXPECT_TRUE(gasguard.get("sql_settings.database_user", value));
  EXPECT_EQ(value, "new_root");
  EXPECT_TRUE(gasguard.get("sql_settings.database_name", value));
  EXPECT_EQ(value, "new_anime_land");
  EXPECT_TRUE(gasguard.get("sql_settings.database_path", value));
  EXPECT_EQ(value, "new_anime_land.db");

  SqlSettings sql_settings;
  EXPECT_TRUE(gasguard.get("sql_settings", sql_settings));
  EXPECT_EQ(sql_settings.database_host, "127.0.0.1");
  EXPECT_EQ(sql_settings.database_password, "new_password");
  EXPECT_EQ(sql_settings.database_port, 3307);
  EXPECT_EQ(sql_settings.database_type, "mysql");
  EXPECT_EQ(sql_settings.database_user, "new_root");
  EXPECT_EQ(sql_settings.database_name, "new_anime_land");
  EXPECT_EQ(sql_settings.database_path, "new_anime_land.db");

  sql_settings.database_password = "this_is_a_new_password";
  sql_settings.database_port = 3308;
  sql_settings.database_type = "sqlite";
  sql_settings.database_user = "new_root";
  sql_settings.database_name = "new_anime_land";
  sql_settings.database_path = "new_anime_land.db";

  EXPECT_TRUE(gasguard.set("sql_settings", sql_settings));

  const auto path = temporarySettingsPath("load");
  EXPECT_TRUE(gasguard.save(path));
  EXPECT_TRUE(gasguard.load(path));
  removeSettingsFiles(path);
}

TEST(AppSettings, LoadOrCreateWritesEveryDefaultField) {
  GlobalAppSettingGuard settingsGuard;
  const auto path = temporarySettingsPath("defaults");

  const auto created = settingsGuard.loadOrCreate(path);
  ASSERT_TRUE(created);
  EXPECT_EQ(*created, AppSettingsFileState::Created);

  std::ifstream input(path);
  ASSERT_TRUE(input.is_open());
  const std::string contents((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
  EXPECT_NE(contents.find("client_id"), std::string::npos);
  EXPECT_NE(contents.find("client_secret"), std::string::npos);
  EXPECT_NE(contents.find("redirect_uri"), std::string::npos);
  EXPECT_NE(contents.find("oauth_base"), std::string::npos);
  EXPECT_NE(contents.find("oauth_application_page"), std::string::npos);
  EXPECT_NE(contents.find("bangumi_api"), std::string::npos);
  EXPECT_NE(contents.find("user_agent"), std::string::npos);
  EXPECT_NE(contents.find("proxy_url"), std::string::npos);
  EXPECT_NE(contents.find("proxy_username"), std::string::npos);
  EXPECT_NE(contents.find("proxy_password"), std::string::npos);
  EXPECT_NE(contents.find("cache_enabled"), std::string::npos);
  EXPECT_NE(contents.find("cache_path"), std::string::npos);
  EXPECT_NE(contents.find("cache_max_size"), std::string::npos);
  EXPECT_NE(contents.find("cache_ttl_days"), std::string::npos);
  EXPECT_NE(contents.find("${APP_CACHE_DIR}/bangumi"), std::string::npos);
  EXPECT_NE(contents.find("appearance_settings"), std::string::npos);
  EXPECT_NE(contents.find("theme"), std::string::npos);
  EXPECT_NE(contents.find("general_settings"), std::string::npos);
  EXPECT_NE(contents.find("log_level"), std::string::npos);
  EXPECT_NE(contents.find("${APP_LOG_DIR}"), std::string::npos);
  EXPECT_NE(contents.find("plugin_settings"), std::string::npos);
  EXPECT_NE(contents.find("plugins_directory"), std::string::npos);
  EXPECT_NE(contents.find("provider_config_directory"), std::string::npos);
  EXPECT_NE(contents.find("${APP_DATA_DIR}/plugins"), std::string::npos);
  EXPECT_NE(contents.find("${APP_CONFIG_DIR}/providers"), std::string::npos);

  {
    auto settings = settingsGuard.get();
    EXPECT_EQ(settings->general_settings.log_file_path,
              expandVariables("${APP_LOG_DIR}"));
    EXPECT_TRUE(settings->bangumi_settings.cache_enabled);
    EXPECT_EQ(settings->bangumi_settings.cache_path,
              expandVariables("${APP_CACHE_DIR}/bangumi"));
    EXPECT_EQ(settings->bangumi_settings.cache_max_size,
              512ULL * 1024ULL * 1024ULL);
    EXPECT_EQ(settings->bangumi_settings.cache_ttl_days, 7);
    EXPECT_TRUE(settings->plugin_settings.enabled);
    EXPECT_TRUE(settings->plugin_settings.scan_on_startup);
    EXPECT_TRUE(settings->plugin_settings.load_builtin);
    EXPECT_FALSE(settings->plugin_settings.allow_symlinks);
    EXPECT_EQ(settings->plugin_settings.plugins_directory,
              expandVariables("${APP_DATA_DIR}/plugins"));
    EXPECT_EQ(settings->plugin_settings.provider_config_directory,
              expandVariables("${APP_CONFIG_DIR}/providers"));
    EXPECT_EQ(settings->plugin_settings.max_packages, 64);
  }

  const auto loaded = settingsGuard.loadOrCreate(path);
  ASSERT_TRUE(loaded);
  EXPECT_EQ(*loaded, AppSettingsFileState::Loaded);
  removeSettingsFiles(path);
}

TEST(AppSettings, EncryptsBangumiClientSecretAndDecryptsOnLoad) {
  GlobalAppSettingGuard settingsGuard;
  const auto path = temporarySettingsPath("encrypted-secret");
  constexpr std::string_view secret = "bangumi-client-secret-value";
  constexpr std::string_view proxyPassword = "bangumi-proxy-password";

  {
    auto settings = settingsGuard.get();
    *settings = AppSettings{};
    settings->bangumi_settings.client_id = QStringLiteral("bangumi-client-id");
    settings->bangumi_settings.client_secret = secret;
    settings->bangumi_settings.proxy_url =
        QUrl(QStringLiteral("http://127.0.0.1:7890"), QUrl::StrictMode);
    settings->bangumi_settings.proxy_username = QStringLiteral("proxy-user");
    settings->bangumi_settings.proxy_password = proxyPassword;
  }
  ASSERT_TRUE(settingsGuard.save(path));

  std::ifstream input(path);
  ASSERT_TRUE(input.is_open());
  const std::string contents((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
  EXPECT_EQ(contents.find(secret), std::string::npos);
  EXPECT_EQ(contents.find(proxyPassword), std::string::npos);
  EXPECT_NE(contents.find("encrypted:v1:"), std::string::npos);
  EXPECT_TRUE(std::filesystem::exists(path.string() + ".key"));

  {
    auto settings = settingsGuard.get();
    *settings = AppSettings{};
  }
  ASSERT_TRUE(settingsGuard.load(path));
  {
    auto settings = settingsGuard.get();
    EXPECT_EQ(settings->bangumi_settings.client_id,
              QStringLiteral("bangumi-client-id"));
    EXPECT_EQ(settings->bangumi_settings.client_secret, secret);
    EXPECT_EQ(settings->bangumi_settings.proxy_url,
              QUrl(QStringLiteral("http://127.0.0.1:7890"), QUrl::StrictMode));
    EXPECT_EQ(settings->bangumi_settings.proxy_username,
              QStringLiteral("proxy-user"));
    EXPECT_EQ(settings->bangumi_settings.proxy_password, proxyPassword);
  }
  removeSettingsFiles(path);
}

TEST(AppSettings, RoundTripsQtBangumiValues) {
  GlobalAppSettingGuard settingsGuard;
  const auto path = temporarySettingsPath("qt-values");
  const QUrl redirect(QStringLiteral("http://127.0.0.1:38457/回调"),
                      QUrl::StrictMode);
  const QUrl applicationPage(QStringLiteral("https://bgm.tv/dev/应用"),
                             QUrl::StrictMode);

  {
    auto settings = settingsGuard.get();
    *settings = AppSettings{};
    settings->bangumi_settings.client_id = QStringLiteral("客户端-id");
    settings->bangumi_settings.redirect_uri = redirect;
    settings->bangumi_settings.oauth_application_page = applicationPage;
    settings->bangumi_settings.user_agent = QStringLiteral("anime-land/测试");
  }
  ASSERT_TRUE(settingsGuard.save(path));

  {
    auto settings = settingsGuard.get();
    *settings = AppSettings{};
  }
  ASSERT_TRUE(settingsGuard.load(path));
  {
    auto settings = settingsGuard.get();
    EXPECT_EQ(settings->bangumi_settings.client_id,
              QStringLiteral("客户端-id"));
    EXPECT_EQ(settings->bangumi_settings.redirect_uri, redirect);
    EXPECT_EQ(settings->bangumi_settings.oauth_application_page,
              applicationPage);
    EXPECT_EQ(settings->bangumi_settings.user_agent,
              QStringLiteral("anime-land/测试"));
  }
  removeSettingsFiles(path);
}

TEST(AppSettings, RejectsInvalidSerializedQUrl) {
  constexpr std::string_view input = "text='valid'\nurl='http://[invalid'\n";
  QtSerializationProbe value;
  NEKO_NAMESPACE::TomlplusplusSerializer::InputSerializer serializer(
      input.data(), input.size());

  EXPECT_FALSE(serializer(value));
  ASSERT_NE(serializer.error(), nullptr);
  EXPECT_NE(serializer.error()->msg.find("Invalid QUrl"), std::string::npos);
}

TEST(AppSettings, EmptySerializedQUrlRepresentsAnUnsetUrl) {
  constexpr std::string_view input = "text='valid'\nurl=''\n";
  QtSerializationProbe value{
      .text = QStringLiteral("unchanged"),
      .url = QUrl(QStringLiteral("https://example.com"), QUrl::StrictMode),
  };
  NEKO_NAMESPACE::TomlplusplusSerializer::InputSerializer serializer(
      input.data(), input.size());

  ASSERT_TRUE(serializer(value)) << serializer.error()->msg;
  EXPECT_EQ(value.text, QStringLiteral("valid"));
  EXPECT_TRUE(value.url.isEmpty());
}

#define EXPAND_IN_MAIN_WITH_ARGS(argc, argv)                                  \
  QCoreApplication qtApplication(argc, argv);                                \
  QCoreApplication::setOrganizationName(QStringLiteral("Btk-Project"));       \
  QCoreApplication::setApplicationName(QStringLiteral("anime-land-tests"));  \
  QCoreApplication::setApplicationVersion(QStringLiteral("9.8.7"));

#include "common/common_main.hpp.in"
