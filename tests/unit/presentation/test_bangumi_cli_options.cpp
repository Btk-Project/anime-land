#include <gtest/gtest.h>

#include <nekoproto/argparser/argparser.hpp>

#include "presentation/cli/bangumi_cli_options.hpp"

#include <array>
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
  EXPECT_EQ(command.tokenStore, "file");
  ASSERT_TRUE(command.tokenFile);
  EXPECT_EQ(*command.tokenFile, "/tmp/token.json");
  ASSERT_TRUE(command.config);
  EXPECT_EQ(*command.config, "/tmp/settings.toml");
}

TEST(BangumiCliOptions, SystemIsTheSecureDefault) {
  const char *argv[] = {"anime-land", "status"};

  auto result = argparser::parser<AnimeLandCommands>(
      static_cast<int>(std::size(argv)), argv);

  ASSERT_TRUE(result) << result.error().message();
  ASSERT_TRUE(std::holds_alternative<StatusCommand>(*result));
  EXPECT_EQ(std::get<StatusCommand>(*result).tokenStore, "system");
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
  EXPECT_EQ(command.tokenStore, "file");
  ASSERT_TRUE(command.tokenFile);
  EXPECT_EQ(*command.tokenFile, "/tmp/token.json");
  ASSERT_TRUE(command.config);
  EXPECT_EQ(*command.config, "/tmp/settings.toml");
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
  EXPECT_EQ(command.tokenStore, "system");
  EXPECT_EQ(command.subjectType, "all");
  EXPECT_EQ(command.collectionType, "all");
  EXPECT_EQ(command.limit, 30);
  EXPECT_EQ(command.offset, 0);
}

#include "../common/common_main.hpp.in"
