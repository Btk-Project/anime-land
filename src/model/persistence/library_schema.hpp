#pragma once

#include <ilias/sql_orm/detail/schema_generator.hpp>

#include <nekoproto/global/string_literal.hpp>
#include <nekoproto/serialization/reflection.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace anime_land::persistence::library_schema {
namespace table {
using NEKO_NAMESPACE::ConstexprString;
inline constexpr ConstexprString mediaResourcesName {"media_resources"};
inline constexpr ConstexprString sourceItemsName {"source_items"};
inline constexpr ConstexprString episodeMediaLinksName {"episode_media_links"};
inline constexpr ConstexprString episodesName {"episodes"};
inline constexpr ConstexprString idColumnName {"id"};

inline constexpr std::string_view mediaResources = mediaResourcesName.view();
inline constexpr std::string_view sourceItems = sourceItemsName.view();
inline constexpr std::string_view episodeMediaLinks =
    episodeMediaLinksName.view();
} // namespace table

inline constexpr auto mediaResourceIdReference =
    ilias::sql::sql_references<table::mediaResourcesName,
                               table::idColumnName,
                               ilias::sql::SqlReferenceAction::Cascade>;
inline constexpr auto sourceItemIdReference =
    ilias::sql::sql_references<table::sourceItemsName,
                               table::idColumnName,
                               ilias::sql::SqlReferenceAction::Cascade>;
inline constexpr auto episodeIdReference =
    ilias::sql::sql_references<table::episodesName,
                               table::idColumnName,
                               ilias::sql::SqlReferenceAction::Cascade>;

struct MediaResourceRecord {
    std::int64_t id = 0;
    std::string providerKey;
    std::string stableKey;
    std::int64_t descriptorVersion = 1;
    std::string descriptorBase64;
    std::string displayName;
    std::int64_t createdAt = 0;
    std::int64_t updatedAt = 0;
    std::int64_t lastSeenAt = 0;
};

struct SourceItemRecord {
    std::int64_t id = 0;
    std::int64_t resourceId = 0;
    std::string stableKey;
    std::string descriptorBase64;
    std::string displayName;
    std::optional<std::int64_t> durationMs;
    std::int64_t available = 1;
    std::int64_t createdAt = 0;
    std::int64_t updatedAt = 0;
    std::int64_t lastSeenAt = 0;
};

struct EpisodeMediaLinkRecord {
    std::int64_t episodeId = 0;
    std::int64_t sourceItemId = 0;
    std::int64_t kind = 0;
    std::int64_t updatedAt = 0;
};

} // namespace anime_land::persistence::library_schema

NEKO_BEGIN_NAMESPACE
using ilias::sql::SqlTags;
using ilias::sql::sql_default;
using namespace anime_land::persistence::library_schema;

// clang-format off
template <>
struct Meta<MediaResourceRecord, void> {
  using Record = MediaResourceRecord;

  constexpr static auto value = Object(
      "id",                 make_tags<SqlTags::createPrimaryKeyTags(true)>(&Record::id),
      "provider_key",       make_tags<SqlTags {.not_null = true, .length = 64}>(&Record::providerKey),
      "stable_key",         make_tags<SqlTags {.not_null = true, .length = 1024}>(&Record::stableKey),
      "descriptor_version", make_tags<SqlTags {.not_null = true}, sql_default<"1">>(&Record::descriptorVersion),
      "descriptor",         make_tags<SqlTags {.not_null = true}>(&Record::descriptorBase64),
      "display_name",       make_tags<SqlTags {.not_null = true}>(&Record::displayName),
      "created_at",         make_tags<SqlTags {.not_null = true}>(&Record::createdAt),
      "updated_at",         make_tags<SqlTags {.not_null = true}>(&Record::updatedAt),
      "last_seen_at",       make_tags<SqlTags {.not_null = true}>(&Record::lastSeenAt)
  );
};
// clang-format on

// clang-format off
template <>
struct Meta<SourceItemRecord, void> {
  using Record = SourceItemRecord;

  constexpr static auto value = Object(
      "id",           make_tags<SqlTags::createPrimaryKeyTags(true)>(&Record::id),
      "resource_id",  make_tags<SqlTags {.not_null = true}, mediaResourceIdReference>(&Record::resourceId),
      "stable_key",   make_tags<SqlTags {.not_null = true, .length = 1024}>(&Record::stableKey),
      "descriptor",   make_tags<SqlTags {.not_null = true}>(&Record::descriptorBase64),
      "display_name", make_tags<SqlTags {.not_null = true}>(&Record::displayName),
      "duration_ms",  &Record::durationMs,
      "available",    make_tags<SqlTags {.not_null = true}, sql_default<"1">>(&Record::available),
      "created_at",   make_tags<SqlTags {.not_null = true}>(&Record::createdAt),
      "updated_at",   make_tags<SqlTags {.not_null = true}>(&Record::updatedAt),
      "last_seen_at", make_tags<SqlTags {.not_null = true}>(&Record::lastSeenAt)
  );
};
// clang-format on

// clang-format off
template <>
struct Meta<EpisodeMediaLinkRecord, void> {
  using Record = EpisodeMediaLinkRecord;

  constexpr static auto value = Object(
      "episode_id",    make_tags<SqlTags {.not_null = true}, episodeIdReference>(&Record::episodeId),
      "source_item_id", make_tags<SqlTags {.not_null = true}, sourceItemIdReference>(&Record::sourceItemId),
      "kind",           make_tags<SqlTags {.not_null = true}, sql_default<"0">>(&Record::kind),
      "updated_at",     make_tags<SqlTags {.not_null = true}>(&Record::updatedAt)
  );
};
// clang-format on

NEKO_END_NAMESPACE

ILIAS_SQL_NS_BEGIN
using namespace anime_land::persistence::library_schema;

// clang-format off
template <>
struct SqlTableMeta<MediaResourceRecord> {
  using Record = MediaResourceRecord;

  constexpr static auto value = sql_table(
      sql_unique<&Record::providerKey, &Record::stableKey>,
      sql_index<"idx_media_resources_updated", sql_desc<&Record::updatedAt>, sql_desc<&Record::id>>
  );
};
// clang-format on

// clang-format off
template <>
struct SqlTableMeta<SourceItemRecord> {
  using Record = SourceItemRecord;

  constexpr static auto value = sql_table(
      sql_unique<&Record::resourceId, &Record::stableKey>,
      sql_index<"idx_source_items_resource", sql_asc<&Record::resourceId>, sql_asc<&Record::id>>,
      sql_index<"idx_source_items_available", sql_asc<&Record::available>, sql_desc<&Record::updatedAt>>
  );
};
// clang-format on

// clang-format off
template <>
struct SqlTableMeta<EpisodeMediaLinkRecord> {
  using Record = EpisodeMediaLinkRecord;

  constexpr static auto value = sql_table(
      sql_unique<&Record::episodeId, &Record::sourceItemId>,
      sql_index<"idx_episode_media_links_episode", sql_asc<&Record::episodeId>, sql_asc<&Record::sourceItemId>>,
      sql_index<"idx_episode_media_links_source", sql_asc<&Record::sourceItemId>, sql_asc<&Record::episodeId>>
  );
};
// clang-format on

ILIAS_SQL_NS_END
