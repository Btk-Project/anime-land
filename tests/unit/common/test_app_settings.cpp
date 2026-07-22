#include <gtest/gtest.h>

#include "common/app_settings.hpp"
#include "common/log.hpp"

#include <nekoproto/serialization/toml_serializer.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace anime_land;

namespace {

struct QtSerializationProbe {
  QString text;
  QUrl url;

  struct Neko {
    constexpr static auto value = NEKO_NAMESPACE::Object(
        "text", &QtSerializationProbe::text, "url", &QtSerializationProbe::url);
  };
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
    EXPECT_EQ(settings->sql_settings.database_type, "sqlcipher");
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

  const auto loaded = settingsGuard.loadOrCreate(path);
  ASSERT_TRUE(loaded);
  EXPECT_EQ(*loaded, AppSettingsFileState::Loaded);
  removeSettingsFiles(path);
}

TEST(AppSettings, EncryptsBangumiClientSecretAndDecryptsOnLoad) {
  GlobalAppSettingGuard settingsGuard;
  const auto path = temporarySettingsPath("encrypted-secret");
  constexpr std::string_view secret = "bangumi-client-secret-value";

  {
    auto settings = settingsGuard.get();
    *settings = AppSettings{};
    settings->bangumi_settings.client_id = QStringLiteral("bangumi-client-id");
    settings->bangumi_settings.client_secret = secret;
  }
  ASSERT_TRUE(settingsGuard.save(path));

  std::ifstream input(path);
  ASSERT_TRUE(input.is_open());
  const std::string contents((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
  EXPECT_EQ(contents.find(secret), std::string::npos);
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

#include "common_main.hpp.in"
