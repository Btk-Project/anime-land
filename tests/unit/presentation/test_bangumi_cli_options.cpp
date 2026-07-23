#include <gtest/gtest.h>

#include <nekoproto/argparser/argparser.hpp>

#include "presentation/cli/bangumi_cli_options.hpp"

#include <array>
#include <string>
#include <variant>

namespace argparser = NEKO_NAMESPACE::argparser;
using namespace anime_land::cli;

TEST(BangumiCliOptions, ParsesLoginStorageAndConfig) {
  const char *argv[] = {
      "anime-land",   "login",           "--token-store", "file",
      "--token-file", "/tmp/token.json", "--config",      "/tmp/settings.toml"};

  auto result = argparser::parser<AnimeLandCommands>(
      static_cast<int>(std::size(argv)), argv);

  ASSERT_TRUE(result) << result.error().message();
  ASSERT_TRUE(std::holds_alternative<LoginCommand>(*result));
  const auto &command = std::get<LoginCommand>(*result);
  EXPECT_EQ(command.common.tokenStore, "file");
  ASSERT_TRUE(command.common.tokenFile);
  EXPECT_EQ(*command.common.tokenFile, "/tmp/token.json");
  ASSERT_TRUE(command.settings.config);
  EXPECT_EQ(*command.settings.config, "/tmp/settings.toml");
}

TEST(BangumiCliOptions, SystemIsTheSecureDefault) {
  const char *argv[] = {"anime-land", "status"};

  auto result = argparser::parser<AnimeLandCommands>(
      static_cast<int>(std::size(argv)), argv);

  ASSERT_TRUE(result) << result.error().message();
  ASSERT_TRUE(std::holds_alternative<StatusCommand>(*result));
  EXPECT_EQ(std::get<StatusCommand>(*result).common.tokenStore, "system");
}

TEST(BangumiCliOptions, RejectsUnknownStorageChoice) {
  const char *argv[] = {"anime-land", "logout", "--token-store", "unknown"};

  auto result = argparser::parser<AnimeLandCommands>(
      static_cast<int>(std::size(argv)), argv);

  EXPECT_FALSE(result);
}

TEST(BangumiCliOptions, CommandHelpIsAvailable) {
  const char *argv[] = {"anime-land", "login", "--help"};

  auto result = argparser::parser<AnimeLandCommands>(
      static_cast<int>(std::size(argv)), argv);

  ASSERT_FALSE(result);
  EXPECT_EQ(result.error(),
            ::make_error_code(argparser::ArgParserError::HelpRequested));
}

TEST(BangumiCliOptions, ParsesCollectionsQuery) {
  const char *argv[] = {
      "anime-land",        "collections",
      "--token-store",     "file",
      "--token-file",      "/tmp/token.json",
      "--config",          "/tmp/settings.toml",
      "--subject-type",    "anime",
      "--collection-type", "doing",
      "--limit",           "50",
      "--offset",          "100",
  };

  auto result = argparser::parser<AnimeLandCommands>(
      static_cast<int>(std::size(argv)), argv);

  ASSERT_TRUE(result) << result.error().message();
  ASSERT_TRUE(std::holds_alternative<CollectionsCommand>(*result));
  const auto &command = std::get<CollectionsCommand>(*result);
  EXPECT_EQ(command.common.tokenStore, "file");
  ASSERT_TRUE(command.common.tokenFile);
  EXPECT_EQ(*command.common.tokenFile, "/tmp/token.json");
  ASSERT_TRUE(command.settings.config);
  EXPECT_EQ(*command.settings.config, "/tmp/settings.toml");
  EXPECT_EQ(command.subjectType, "anime");
  EXPECT_EQ(command.collectionType, "doing");
  EXPECT_EQ(command.limit, 50);
  EXPECT_EQ(command.offset, 100);
}

TEST(BangumiCliOptions, CollectionsDefaultsToSystemStoreAndFirstPage) {
  const char *argv[] = {"anime-land", "collections"};

  auto result = argparser::parser<AnimeLandCommands>(
      static_cast<int>(std::size(argv)), argv);

  ASSERT_TRUE(result) << result.error().message();
  ASSERT_TRUE(std::holds_alternative<CollectionsCommand>(*result));
  const auto &command = std::get<CollectionsCommand>(*result);
  EXPECT_EQ(command.common.tokenStore, "system");
  EXPECT_EQ(command.subjectType, "all");
  EXPECT_EQ(command.collectionType, "all");
  EXPECT_EQ(command.limit, 30);
  EXPECT_EQ(command.offset, 0);
}

TEST(BangumiCliOptions, CommonNestedFieldsKeepAbsoluteCliNames) {
  const char *argv[] = {"anime-land", "status",
                        "--common.credentials.tokenStore", "file"};

  auto nested = argparser::parser<AnimeLandCommands>(
      static_cast<int>(std::size(argv)), argv);

  EXPECT_FALSE(nested);

  argparser::ArgParserConfig config;
  config.programName = "anime-land";
  const std::string help =
      argparser::format_help<AnimeLandCommands>(2, argv, config);
  EXPECT_NE(help.find("--token-store"), std::string::npos);
  EXPECT_NE(help.find("--token-file"), std::string::npos);
  EXPECT_NE(help.find("--proxy"), std::string::npos);
  EXPECT_NE(help.find("--log-level"), std::string::npos);
  EXPECT_EQ(help.find("--common."), std::string::npos);
}

TEST(BangumiCliOptions, ParsesProxyAndLogLevelForEveryCommand) {
  const char *argv[] = {"anime-land", "status", "--proxy",
                        "socks5://127.0.0.1:1080", "--log-level", "debug"};

  auto result = argparser::parser<AnimeLandCommands>(
      static_cast<int>(std::size(argv)), argv);

  ASSERT_TRUE(result) << result.error().message();
  ASSERT_TRUE(std::holds_alternative<StatusCommand>(*result));
  const auto &common = std::get<StatusCommand>(*result).common;
  ASSERT_TRUE(common.proxy);
  EXPECT_EQ(*common.proxy, "socks5://127.0.0.1:1080");
  ASSERT_TRUE(common.logLevel);
  EXPECT_EQ(*common.logLevel, "debug");
}

TEST(BangumiCliOptions, RejectsUnknownLogLevel) {
  const char *argv[] = {"anime-land", "login", "--log-level", "verbose"};

  auto result = argparser::parser<AnimeLandCommands>(
      static_cast<int>(std::size(argv)), argv);

  EXPECT_FALSE(result);
}

TEST(BangumiCliOptions, ParsesPublicSearchQuery) {
  const char *argv[] = {
      "anime-land",     "search",    "葬送的芙莉莲",
      "--subject-type", "anime",     "--sort",
      "score",          "--tag",     "治愈",
      "--tag",          "冒险",      "--meta-tag",
      "日本",           "--limit",   "20",
      "--offset",       "40",
  };

  auto result = argparser::parser<AnimeLandCommands>(
      static_cast<int>(std::size(argv)), argv);

  ASSERT_TRUE(result) << result.error().message();
  ASSERT_TRUE(std::holds_alternative<SearchCommand>(*result));
  const auto &command = std::get<SearchCommand>(*result);
  EXPECT_EQ(command.keyword, "葬送的芙莉莲");
  EXPECT_EQ(command.subjectType, "anime");
  EXPECT_EQ(command.sort, "score");
  EXPECT_EQ(command.tags, (std::vector<std::string>{"治愈", "冒险"}));
  EXPECT_EQ(command.metaTags, (std::vector<std::string>{"日本"}));
  EXPECT_EQ(command.limit, 20);
  EXPECT_EQ(command.offset, 40);
}

TEST(BangumiCliOptions, SearchDefaultsToOptionalSystemSession) {
  const char *argv[] = {"anime-land", "search", "Frieren"};

  auto result = argparser::parser<AnimeLandCommands>(
      static_cast<int>(std::size(argv)), argv);

  ASSERT_TRUE(result) << result.error().message();
  ASSERT_TRUE(std::holds_alternative<SearchCommand>(*result));
  const auto &command = std::get<SearchCommand>(*result);
  EXPECT_EQ(command.common.tokenStore, "system");
  EXPECT_EQ(command.subjectType, "all");
  EXPECT_EQ(command.sort, "match");
  EXPECT_TRUE(command.tags.empty());
  EXPECT_TRUE(command.metaTags.empty());
  EXPECT_EQ(command.limit, 30);
  EXPECT_EQ(command.offset, 0);
}

TEST(BangumiCliOptions, SearchRequiresKeywordArgument) {
  const char *argv[] = {"anime-land", "search"};

  auto result = argparser::parser<AnimeLandCommands>(
      static_cast<int>(std::size(argv)), argv);

  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(),
            ::make_error_code(argparser::ArgParserError::MissingRequired));
}

#include "../common/common_main.hpp.in"
