#pragma once

#include <nekoproto/argparser/argparser.hpp>

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace anime_land::cli {
using namespace NEKO_NAMESPACE;
using namespace argparser;

struct ConfigFileOptions {
  std::optional<std::string> config;

  // clang-format off
  struct Neko {
    constexpr static auto value = Object(
        "config",
        make_tags<arg_absolute_name<"config">,
                  arg_value_name<"PATH">,
                  arg_help<"application settings TOML file">>(&ConfigFileOptions::config)
    );
  };
  // clang-format on
};

struct CommonCommandOptions {
  std::string tokenStore = "system";
  std::optional<std::string> tokenFile;
  std::optional<std::string> proxy;
  std::optional<std::string> logLevel;

  // clang-format off
  struct Neko {
    constexpr static auto value = Object(
        "tokenStore",
        make_tags<arg_absolute_name<"token-store">,
                  arg_choices<"memory", "file", "system">,
                  arg_help<"credential storage: memory, file, or system (default)">>(&CommonCommandOptions::tokenStore),
        "tokenFile",
        make_tags<arg_absolute_name<"token-file">,
                  arg_value_name<"PATH">,
                  arg_help<"credential file path (file store only)">>(&CommonCommandOptions::tokenFile),
        "proxy",
        make_tags<arg_absolute_name<"proxy">,
                  arg_value_name<"URL">,
                  arg_help<"override the Bangumi HTTP/SOCKS5 proxy">>(&CommonCommandOptions::proxy),
        "logLevel",
        make_tags<arg_absolute_name<"log-level">,
                  arg_choices<"trace", "debug", "info", "warn", "error", "critical">,
                  arg_env<"ANIME_LAND_LOG_LEVEL">,
                  arg_default<"info"_cs>,
                  arg_help<"set the application log level">>(&CommonCommandOptions::logLevel)
    );
  };
  // clang-format on
};

struct LoginCommand {
  CommonCommandOptions common;
  ConfigFileOptions settings;

  // clang-format off
  struct Neko {
    constexpr static auto value = Object(
        "common",   &LoginCommand::common,
        "settings", &LoginCommand::settings
    );
  };
  // clang-format on
};

struct StatusCommand {
  CommonCommandOptions common;

  // clang-format off
  struct Neko {
    constexpr static auto value = Object(
        "common", &StatusCommand::common
    );
  };
  // clang-format on
};

struct LogoutCommand {
  CommonCommandOptions common;

  // clang-format off
  struct Neko {
    constexpr static auto value = Object(
        "common", &LogoutCommand::common
    );
  };
  // clang-format on
};

struct CollectionsCommand {
  CommonCommandOptions common;
  ConfigFileOptions settings;
  std::string subjectType = "all";
  std::string collectionType = "all";
  int limit = 30;
  int offset = 0;

  // clang-format off
  struct Neko {
    constexpr static auto value = Object(
        "common",         &CollectionsCommand::common,
        "settings",       &CollectionsCommand::settings,
        "subjectType",
        make_tags<arg_long_name<"subject-type">,
                  arg_choices<"all", "book", "anime", "music", "game", "real">,
                  arg_default<"all"_cs>,
                  arg_help<"filter by subject type">>(&CollectionsCommand::subjectType),
        "collectionType", 
        make_tags<arg_long_name<"collection-type">,
                  arg_choices<"all", "wish", "done", "doing", "on-hold", "dropped">,
                  arg_default<"all"_cs>,
                  arg_help<"filter by collection status">>(&CollectionsCommand::collectionType),
        "limit", 
        make_tags<arg_value_name<"1..50">,
                  ArgTags{.range_min = 1, .range_max = 51},
                  arg_default<30>,
                  arg_help<"page size">>(&CollectionsCommand::limit),
        "offset",
        make_tags<arg_value_name<"N">,
                  arg_default<0>,
                  arg_help<"page offset">>(&CollectionsCommand::offset)
    );
  };
  // clang-format on
};

struct SearchCommand {
  CommonCommandOptions common;
  ConfigFileOptions settings;
  std::string keyword;
  std::string subjectType = "all";
  std::string sort = "match";
  std::vector<std::string> metaTags;
  std::vector<std::string> tags;
  int limit = 30;
  int offset = 0;

  // clang-format off
  struct Neko {
    constexpr static auto value = Object(
        "common",      &SearchCommand::common,
        "settings",    &SearchCommand::settings,
        "keyword",
        make_tags<arg_value_name<"KEYWORD">,
                  arg_help<"subject name or keyword">,
                  ArgTags{.required = true, .positional = true}>(&SearchCommand::keyword),
        "subjectType",
        make_tags<arg_long_name<"subject-type">,
                  arg_choices<"all", "book", "anime", "music", "game", "real">,
                  arg_default<"all"_cs>,
                  arg_help<"filter by subject type">>(&SearchCommand::subjectType),
        "sort",
        make_tags<arg_choices<"match", "heat", "rank", "score">,
                  arg_default<"match"_cs>,
                  arg_help<"sort by match, heat, rank, or score">>(&SearchCommand::sort),
        "metaTags",
        make_tags<arg_long_name<"meta-tag">,
                  arg_value_name<"TAG">,
                  ArgTags{.repeatable = true},
                  arg_help<"require a meta tag">>(&SearchCommand::metaTags),
        "tags",
        make_tags<arg_long_name<"tag">,
                  arg_value_name<"TAG">,
                  ArgTags{.repeatable = true},
                  arg_help<"require a subject tag">>(&SearchCommand::tags),
        "limit",
        make_tags<arg_value_name<"1..50">,
                  ArgTags{.range_min = 1, .range_max = 51},
                  arg_default<30>,
                  arg_help<"page size">>(&SearchCommand::limit),
        "offset",
        make_tags<arg_value_name<"N">,
                  arg_default<0>,
                  arg_help<"page offset">>(&SearchCommand::offset)
    );
  };
  // clang-format on
};

struct AnimeLandCommands {
  LoginCommand login;
  StatusCommand status;
  LogoutCommand logout;
  CollectionsCommand collections;
  SearchCommand search;

  // clang-format off
  struct Neko {
    constexpr static auto value = Object(
        "login",       make_tags<arg_help<"log in to Bangumi through the system browser">, ArgTags{.command = true}>(&AnimeLandCommands::login),
        "status",      make_tags<arg_help<"verify the selected credential store">, ArgTags{.command = true}>(&AnimeLandCommands::status),
        "logout",      make_tags<arg_help<"clear the selected credential store">, ArgTags{.command = true}>(&AnimeLandCommands::logout),
        "collections", make_tags<arg_help<"get one page of the current user's Bangumi collections as JSON">, ArgTags{.command = true}>(&AnimeLandCommands::collections),
        "search",      make_tags<arg_help<"search public Bangumi subjects; login is optional">, ArgTags{.command = true}>(&AnimeLandCommands::search)
    );
  };
  // clang-format on
};

using Command = std::variant<LoginCommand, StatusCommand, LogoutCommand,
                             CollectionsCommand, SearchCommand>;

} // namespace anime_land::cli
