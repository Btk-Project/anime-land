#pragma once

#include <nekoproto/argparser/argparser.hpp>

#include <optional>
#include <string>
#include <variant>

namespace anime_land::cli {
using namespace NEKO_NAMESPACE;
using namespace argparser;

struct CredentialStoreOptions {
  std::string tokenStore = "system";
  std::optional<std::string> tokenFile;
};

struct LoginCommand {
  std::string tokenStore = "system";
  std::optional<std::string> tokenFile;
  std::optional<std::string> config;

  // clang-format off
  struct Neko {
    constexpr static auto value = Object(
        "tokenStore",
        make_tags<arg_long_name<"token-store">,
                  arg_choices<"memory", "file", "system">,
                  arg_help<"credential storage: memory, file, or system (default)">>(&LoginCommand::tokenStore),
        "tokenFile",
        make_tags<arg_long_name<"token-file">,
                  arg_value_name<"PATH">,
                  arg_help<"credential file path (file store only)">>(&LoginCommand::tokenFile),
        "config",
        make_tags<arg_value_name<"PATH">,
                  arg_help<"application settings TOML file">>(&LoginCommand::config)
    );
  };
  // clang-format on
};

struct StatusCommand {
  std::string tokenStore = "system";
  std::optional<std::string> tokenFile;

  // clang-format off
  struct Neko {
    constexpr static auto value = Object(
        "tokenStore",
        make_tags<arg_long_name<"token-store">,
                  arg_choices<"memory", "file", "system">,
                  arg_help<"credential storage: memory, file, or system (default)">>(&StatusCommand::tokenStore),
        "tokenFile",
        make_tags<arg_long_name<"token-file">,
                  arg_value_name<"PATH">,
                  arg_help<"credential file path (file store only)">>(&StatusCommand::tokenFile)
    );
  };
  // clang-format on
};

struct LogoutCommand {
  std::string tokenStore = "system";
  std::optional<std::string> tokenFile;

  // clang-format off
  struct Neko {
    constexpr static auto value = Object(
        "tokenStore",
        make_tags<arg_long_name<"token-store">,
                  arg_choices<"memory", "file", "system">,
                  arg_help<"credential storage: memory, file, or system (default)">>(&LogoutCommand::tokenStore),
        "tokenFile",
        make_tags<arg_long_name<"token-file">,
                  arg_value_name<"PATH">,
                  arg_help<"credential file path (file store only)">>(&LogoutCommand::tokenFile)
    );
  };
  // clang-format on
};

struct CollectionsCommand {
  std::string tokenStore = "system";
  std::optional<std::string> tokenFile;
  std::optional<std::string> config;
  std::string subjectType = "all";
  std::string collectionType = "all";
  int limit = 30;
  int offset = 0;

  // clang-format off
  struct Neko {
    constexpr static auto value = Object(
        "tokenStore",
        make_tags<arg_long_name<"token-store">,
                  arg_choices<"memory", "file", "system">,
                  arg_help<"credential storage: memory, file, or system (default)">>(&CollectionsCommand::tokenStore),
        "tokenFile",
        make_tags<arg_long_name<"token-file">,
                  arg_value_name<"PATH">,
                  arg_help<"credential file path">>(&CollectionsCommand::tokenFile),
        "config",
        make_tags<arg_value_name<"PATH">,
                  arg_help<"application settings TOML file">>(&CollectionsCommand::config),
        "subjectType",
        make_tags<arg_long_name<"subject-type">,
                  arg_choices<"all", "book", "anime", "music", "game", "real">,
                  arg_help<"filter by subject type">>(&CollectionsCommand::subjectType),
        "collectionType", 
        make_tags<arg_long_name<"collection-type">,
                  arg_choices<"all", "wish", "done", "doing", "on-hold", "dropped">,
                  arg_help<"filter by collection status">>(&CollectionsCommand::collectionType),
        "limit", 
        make_tags<arg_value_name<"1..50">,
                  arg_help<"page size (default: 30)">>(&CollectionsCommand::limit),
        "offset",
        make_tags<arg_value_name<"N">,
                  arg_help<"page offset (default: 0)">>(&CollectionsCommand::offset)
    );
  };
  // clang-format on
};

struct AnimeLandCommands {
  LoginCommand login;
  StatusCommand status;
  LogoutCommand logout;
  CollectionsCommand collections;

  // clang-format off
  struct Neko {
    constexpr static auto value = Object(
        "login",       make_tags<arg_help<"log in to Bangumi through the system browser">, ArgTags{.command = true}>(&AnimeLandCommands::login),
        "status",      make_tags<arg_help<"verify the selected credential store">, ArgTags{.command = true}>(&AnimeLandCommands::status),
        "logout",      make_tags<arg_help<"clear the selected credential store">, ArgTags{.command = true}>(&AnimeLandCommands::logout),
        "collections", make_tags<arg_help<"get one page of the current user's Bangumi collections as JSON">, ArgTags{.command = true}>(&AnimeLandCommands::collections)
    );
  };
  // clang-format on
};

using Command = std::variant<LoginCommand, StatusCommand, LogoutCommand,
                             CollectionsCommand>;

} // namespace anime_land::cli
