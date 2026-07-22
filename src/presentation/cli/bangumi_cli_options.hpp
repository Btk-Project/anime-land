#pragma once

#include <nekoproto/argparser/argparser.hpp>

#include <optional>
#include <string>
#include <variant>

namespace anime_land::cli {

struct CredentialStoreOptions {
  std::string tokenStore = "system";
  std::optional<std::string> tokenFile;
};

struct LoginCommand {
  std::string tokenStore = "system";
  std::optional<std::string> tokenFile;
  std::optional<std::string> config;

  struct Neko {
    constexpr static auto value = NEKO_NAMESPACE::Object(
        "tokenStore",
        NEKO_NAMESPACE::make_tags<
            NEKO_NAMESPACE::argparser::arg_long_name<"token-store">,
            NEKO_NAMESPACE::argparser::arg_choices<"memory", "file", "system">,
            NEKO_NAMESPACE::argparser::arg_help<
                "credential storage: memory, file, or system (default)">>(
            &LoginCommand::tokenStore),
        "tokenFile",
        NEKO_NAMESPACE::make_tags<
            NEKO_NAMESPACE::argparser::arg_long_name<"token-file">,
            NEKO_NAMESPACE::argparser::arg_value_name<"PATH">,
            NEKO_NAMESPACE::argparser::arg_help<
                "credential file path (file store only)">>(
            &LoginCommand::tokenFile),
        "config",
        NEKO_NAMESPACE::make_tags<
            NEKO_NAMESPACE::argparser::arg_value_name<"PATH">,
            NEKO_NAMESPACE::argparser::arg_help<
                "application settings TOML file">>(&LoginCommand::config));
  };
};

struct StatusCommand {
  std::string tokenStore = "system";
  std::optional<std::string> tokenFile;

  struct Neko {
    constexpr static auto value = NEKO_NAMESPACE::Object(
        "tokenStore",
        NEKO_NAMESPACE::make_tags<
            NEKO_NAMESPACE::argparser::arg_long_name<"token-store">,
            NEKO_NAMESPACE::argparser::arg_choices<"memory", "file", "system">,
            NEKO_NAMESPACE::argparser::arg_help<
                "credential storage: memory, file, or system (default)">>(
            &StatusCommand::tokenStore),
        "tokenFile",
        NEKO_NAMESPACE::make_tags<
            NEKO_NAMESPACE::argparser::arg_long_name<"token-file">,
            NEKO_NAMESPACE::argparser::arg_value_name<"PATH">,
            NEKO_NAMESPACE::argparser::arg_help<
                "credential file path (file store only)">>(
            &StatusCommand::tokenFile));
  };
};

struct LogoutCommand {
  std::string tokenStore = "system";
  std::optional<std::string> tokenFile;

  struct Neko {
    constexpr static auto value = NEKO_NAMESPACE::Object(
        "tokenStore",
        NEKO_NAMESPACE::make_tags<
            NEKO_NAMESPACE::argparser::arg_long_name<"token-store">,
            NEKO_NAMESPACE::argparser::arg_choices<"memory", "file", "system">,
            NEKO_NAMESPACE::argparser::arg_help<
                "credential storage: memory, file, or system (default)">>(
            &LogoutCommand::tokenStore),
        "tokenFile",
        NEKO_NAMESPACE::make_tags<
            NEKO_NAMESPACE::argparser::arg_long_name<"token-file">,
            NEKO_NAMESPACE::argparser::arg_value_name<"PATH">,
            NEKO_NAMESPACE::argparser::arg_help<
                "credential file path (file store only)">>(
            &LogoutCommand::tokenFile));
  };
};

struct CollectionsCommand {
  std::string tokenStore = "system";
  std::optional<std::string> tokenFile;
  std::optional<std::string> config;
  std::string subjectType = "all";
  std::string collectionType = "all";
  int limit = 30;
  int offset = 0;

  struct Neko {
    constexpr static auto value = NEKO_NAMESPACE::Object(
        "tokenStore",
        NEKO_NAMESPACE::make_tags<
            NEKO_NAMESPACE::argparser::arg_long_name<"token-store">,
            NEKO_NAMESPACE::argparser::arg_choices<"memory", "file", "system">,
            NEKO_NAMESPACE::argparser::arg_help<
                "credential storage: memory, file, or system (default)">>(
            &CollectionsCommand::tokenStore),
        "tokenFile",
        NEKO_NAMESPACE::make_tags<
            NEKO_NAMESPACE::argparser::arg_long_name<"token-file">,
            NEKO_NAMESPACE::argparser::arg_value_name<"PATH">,
            NEKO_NAMESPACE::argparser::arg_help<"credential file path">>(
            &CollectionsCommand::tokenFile),
        "config",
        NEKO_NAMESPACE::make_tags<
            NEKO_NAMESPACE::argparser::arg_value_name<"PATH">,
            NEKO_NAMESPACE::argparser::arg_help<
                "application settings TOML file">>(&CollectionsCommand::config),
        "subjectType",
        NEKO_NAMESPACE::make_tags<
            NEKO_NAMESPACE::argparser::arg_long_name<"subject-type">,
            NEKO_NAMESPACE::argparser::arg_choices<"all", "book", "anime",
                                                   "music", "game", "real">,
            NEKO_NAMESPACE::argparser::arg_help<"filter by subject type">>(
            &CollectionsCommand::subjectType),
        "collectionType",
        NEKO_NAMESPACE::make_tags<
            NEKO_NAMESPACE::argparser::arg_long_name<"collection-type">,
            NEKO_NAMESPACE::argparser::arg_choices<
                "all", "wish", "done", "doing", "on-hold", "dropped">,
            NEKO_NAMESPACE::argparser::arg_help<"filter by collection status">>(
            &CollectionsCommand::collectionType),
        "limit",
        NEKO_NAMESPACE::make_tags<
            NEKO_NAMESPACE::argparser::arg_value_name<"1..50">,
            NEKO_NAMESPACE::argparser::arg_help<"page size (default: 30)">>(
            &CollectionsCommand::limit),
        "offset",
        NEKO_NAMESPACE::make_tags<
            NEKO_NAMESPACE::argparser::arg_value_name<"N">,
            NEKO_NAMESPACE::argparser::arg_help<"page offset (default: 0)">>(
            &CollectionsCommand::offset));
  };
};

struct AnimeLandCommands {
  LoginCommand login;
  StatusCommand status;
  LogoutCommand logout;
  CollectionsCommand collections;

  struct Neko {
    constexpr static auto value = NEKO_NAMESPACE::Object(
        "login",
        NEKO_NAMESPACE::make_tags<
            NEKO_NAMESPACE::argparser::arg_help<
                "log in to Bangumi through the system browser">,
            NEKO_NAMESPACE::argparser::ArgTags{.command = true}>(
            &AnimeLandCommands::login),
        "status",
        NEKO_NAMESPACE::make_tags<NEKO_NAMESPACE::argparser::arg_help<
                                      "verify the selected credential store">,
                                  NEKO_NAMESPACE::argparser::ArgTags{.command =
                                                                         true}>(
            &AnimeLandCommands::status),
        "logout",
        NEKO_NAMESPACE::make_tags<NEKO_NAMESPACE::argparser::arg_help<
                                      "clear the selected credential store">,
                                  NEKO_NAMESPACE::argparser::ArgTags{.command =
                                                                         true}>(
            &AnimeLandCommands::logout),
        "collections",
        NEKO_NAMESPACE::make_tags<NEKO_NAMESPACE::argparser::arg_help<
                                      "get one page of the current user's "
                                      "Bangumi collections as JSON">,
                                  NEKO_NAMESPACE::argparser::ArgTags{.command =
                                                                         true}>(
            &AnimeLandCommands::collections));
  };
};

using Command = std::variant<LoginCommand, StatusCommand, LogoutCommand,
                             CollectionsCommand>;

} // namespace anime_land::cli
