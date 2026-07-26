#pragma once

#include <ilias/sql_orm/detail/schema_generator.hpp>

#include <nekoproto/global/string_literal.hpp>
#include <nekoproto/serialization/reflection.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
namespace anime_land::persistence::schema {
/// 应用 Schema 的规范关系名；仅用于创建或首次附加 Form。
namespace table {
using NEKO_NAMESPACE::ConstexprString;
inline constexpr ConstexprString subjectsName {"subjects"};
inline constexpr ConstexprString subjectExternalRefsName {
    "subject_external_refs"};
inline constexpr ConstexprString tagsName {"tags"};
inline constexpr ConstexprString subjectTagsName {
    "subject_tags"};
inline constexpr ConstexprString episodesName {"episodes"};
inline constexpr ConstexprString episodeExternalRefsName {
    "episode_external_refs"};
inline constexpr ConstexprString idColumnName {"id"};

inline constexpr std::string_view subjects = subjectsName.view();
inline constexpr std::string_view subjectExternalRefs =
    subjectExternalRefsName.view();
inline constexpr std::string_view tags = tagsName.view();
inline constexpr std::string_view subjectTags = subjectTagsName.view();
inline constexpr std::string_view episodes = episodesName.view();
inline constexpr std::string_view episodeExternalRefs =
    episodeExternalRefsName.view();
} // namespace table

inline constexpr auto subjectIdReference =
    ilias::sql::sql_references<table::subjectsName, table::idColumnName,
                               ilias::sql::SqlReferenceAction::Cascade>;
inline constexpr auto tagIdReference =
    ilias::sql::sql_references<table::tagsName, table::idColumnName,
                               ilias::sql::SqlReferenceAction::Cascade>;
inline constexpr auto episodeIdReference =
    ilias::sql::sql_references<table::episodesName, table::idColumnName,
                               ilias::sql::SqlReferenceAction::Cascade>;

/// subjects 表的 ORM 记录，仅描述关系字段，不包含业务行为。
struct SubjectRecord {
    std::int64_t id = 0;
    std::int64_t subjectType = 2;
    std::string title;
    std::optional<std::string> titleCn;
    std::optional<std::string> summary;
    std::optional<std::string> airDate;
    std::optional<std::string> coverUrl;
    std::string aliasesJson = "[]";
    std::int64_t createdAt = 0;
    std::int64_t updatedAt = 0;
    std::int64_t metadataLevel = 0;
    std::optional<std::int64_t> metadataRefreshedAt;
};

/// subject_external_refs 表的 ORM 记录。
struct SubjectExternalRefRecord {
    std::int64_t subjectId = 0;
    std::string providerKey;
    std::string externalId;
    std::int64_t fetchedAt = 0;
    std::optional<std::int64_t> remoteUpdatedAt;
};

/// tags 表的 ORM 记录；normalizedName 是跨来源去重和查询键。
struct TagRecord {
    std::int64_t id = 0;
    std::string normalizedName;
    std::string displayName;
};

/// subject_tags 表的 ORM 记录，同一标签可以按提供者分别保留。
struct SubjectTagRecord {
    std::int64_t subjectId = 0;
    std::int64_t tagId = 0;
    std::string providerKey;
    std::optional<double> weight;
    std::int64_t updatedAt = 0;
};

/// episodes 表的 ORM 记录。
struct EpisodeRecord {
    std::int64_t id = 0;
    std::int64_t subjectId = 0;
    std::int64_t sortOrder = 0;
    std::int64_t episodeType = 0;
    std::optional<double> episodeNumber;
    std::optional<std::string> title;
    std::optional<std::string> titleCn;
    std::optional<std::string> summary;
    std::optional<std::string> airDate;
    std::optional<std::int64_t> durationMs;
    std::int64_t createdAt = 0;
    std::int64_t updatedAt = 0;
};

/// episode_external_refs 表的 ORM 记录。
struct EpisodeExternalRefRecord {
    std::int64_t episodeId = 0;
    std::string providerKey;
    std::string externalId;
    std::int64_t fetchedAt = 0;
    std::optional<std::int64_t> remoteUpdatedAt;
};

} // namespace anime_land::persistence::schema

NEKO_BEGIN_NAMESPACE
using ilias::sql::SqlTags;
using ilias::sql::sql_default;
using namespace anime_land::persistence::schema;

// clang-format off
template <>
struct Meta<SubjectRecord, void> {
  using Record = SubjectRecord;

  constexpr static auto value = Object(
      "id",                    make_tags<SqlTags::createPrimaryKeyTags(true)>(&Record::id),
      "subject_type",          make_tags<SqlTags {.not_null = true}, sql_default<"2">>(&Record::subjectType),
      "title",                 make_tags<SqlTags {.not_null = true}>(&Record::title),
      "title_cn",              &Record::titleCn,
      "summary",               &Record::summary,
      "air_date",              &Record::airDate,
      "cover_url",             &Record::coverUrl,
      "aliases_json",          make_tags<SqlTags {.not_null = true}, sql_default<"'[]'">>(&Record::aliasesJson),
      "created_at",            make_tags<SqlTags {.not_null = true}>(&Record::createdAt),
      "updated_at",            make_tags<SqlTags {.not_null = true}>(&Record::updatedAt),
      "metadata_level",        make_tags<SqlTags {.not_null = true}, sql_default<"0">>(&Record::metadataLevel),
      "metadata_refreshed_at", &Record::metadataRefreshedAt
  );
};
// clang-format on

// clang-format off
template <>
struct Meta<SubjectExternalRefRecord, void> {
  using Record = SubjectExternalRefRecord;

  constexpr static auto value = Object(
      "subject_id",        make_tags<SqlTags {.not_null = true}, subjectIdReference>(&Record::subjectId),
      "provider_key",      make_tags<SqlTags {.not_null = true, .length = 64}>(&Record::providerKey),
      "external_id",       make_tags<SqlTags {.not_null = true, .length = 255}>(&Record::externalId),
      "fetched_at",        make_tags<SqlTags {.not_null = true}>(&Record::fetchedAt),
      "remote_updated_at", &Record::remoteUpdatedAt
  );
};
// clang-format on

// clang-format off
template <>
struct Meta<TagRecord, void> {
  using Record = TagRecord;

  constexpr static auto value = Object(
      "id",              make_tags<SqlTags::createPrimaryKeyTags(true)>(&Record::id),
      "normalized_name", make_tags<SqlTags {.not_null = true, .unique = true}>(&Record::normalizedName),
      "display_name",    make_tags<SqlTags {.not_null = true}>(&Record::displayName)
  );
};
// clang-format on

// clang-format off
template <>
struct Meta<SubjectTagRecord, void> {
  using Record = SubjectTagRecord;

  constexpr static auto value = Object(
      "subject_id",   make_tags<SqlTags {.not_null = true}, subjectIdReference>(&Record::subjectId),
      "tag_id",       make_tags<SqlTags {.not_null = true}, tagIdReference>(&Record::tagId),
      "provider_key", make_tags<SqlTags {.not_null = true, .length = 64}>(&Record::providerKey),
      "weight",       &Record::weight,
      "updated_at",   make_tags<SqlTags {.not_null = true}>(&Record::updatedAt)
  );
};
// clang-format on

// clang-format off
template <>
struct Meta<EpisodeRecord, void> {
  using Record = EpisodeRecord;

  constexpr static auto value = Object(
      "id",             make_tags<SqlTags::createPrimaryKeyTags(true)>(&Record::id),
      "subject_id",     make_tags<SqlTags {.not_null = true}, subjectIdReference>(&Record::subjectId),
      "sort_order",     make_tags<SqlTags {.not_null = true}>(&Record::sortOrder),
      "episode_type",   make_tags<SqlTags {.not_null = true}, sql_default<"0">>(&Record::episodeType),
      "episode_number", &Record::episodeNumber,
      "title",          &Record::title,
      "title_cn",       &Record::titleCn,
      "summary",        &Record::summary,
      "air_date",       &Record::airDate,
      "duration_ms",    &Record::durationMs,
      "created_at",     make_tags<SqlTags {.not_null = true}>(&Record::createdAt),
      "updated_at",     make_tags<SqlTags {.not_null = true}>(&Record::updatedAt)
  );
};
// clang-format on

// clang-format off
template <>
struct Meta<EpisodeExternalRefRecord, void> {
  using Record = EpisodeExternalRefRecord;

  constexpr static auto value = Object(
      "episode_id",        make_tags<SqlTags {.not_null = true}, episodeIdReference>(&Record::episodeId),
      "provider_key",      make_tags<SqlTags {.not_null = true, .length = 64}>(&Record::providerKey),
      "external_id",       make_tags<SqlTags {.not_null = true, .length = 255}>(&Record::externalId),
      "fetched_at",        make_tags<SqlTags {.not_null = true}>(&Record::fetchedAt),
      "remote_updated_at", &Record::remoteUpdatedAt
  );
};
// clang-format on

NEKO_END_NAMESPACE

ILIAS_SQL_NS_BEGIN
using namespace anime_land::persistence::schema;
// clang-format off
template <>
struct SqlTableMeta<SubjectRecord> {
  using Record = SubjectRecord;

  constexpr static auto value = sql_table(
      sql_index<"idx_subjects_updated", sql_desc<&Record::updatedAt>, sql_desc<&Record::id>>
  );
};
// clang-format on

// clang-format off
template <>
struct SqlTableMeta<SubjectExternalRefRecord> {
  using Record = SubjectExternalRefRecord;

  constexpr static auto value = sql_table(
      sql_primary_key<&Record::providerKey, &Record::externalId>,
      sql_index<"idx_subject_external_refs_subject", sql_asc<&Record::subjectId>>
  );
};
// clang-format on

// clang-format off
template <>
struct SqlTableMeta<SubjectTagRecord> {
  using Record = SubjectTagRecord;

  constexpr static auto value = sql_table(
      sql_primary_key<&Record::subjectId, &Record::tagId, &Record::providerKey>,
      sql_index<"idx_subject_tags_tag_subject", sql_asc<&Record::tagId>, sql_asc<&Record::subjectId>>,
      sql_index<"idx_subject_tags_subject", sql_asc<&Record::subjectId>>
  );
};
// clang-format on

// clang-format off
template <>
struct SqlTableMeta<EpisodeRecord> {
  using Record = EpisodeRecord;

  constexpr static auto value = sql_table(
      sql_index<"idx_episodes_subject_order", sql_asc<&Record::subjectId>, sql_asc<&Record::sortOrder>, sql_asc<&Record::id>>
  );
};
// clang-format on

// clang-format off
template <>
struct SqlTableMeta<EpisodeExternalRefRecord> {
  using Record = EpisodeExternalRefRecord;

  constexpr static auto value = sql_table(
      sql_primary_key<&Record::providerKey, &Record::externalId>,
      sql_index<"idx_episode_external_refs_episode", sql_asc<&Record::episodeId>>
  );
};
// clang-format on

ILIAS_SQL_NS_END
