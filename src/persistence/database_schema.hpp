#pragma once

#include <ilias/sql_orm/detail/schema_generator.hpp>

#include <nekoproto/global/string_literal.hpp>
#include <nekoproto/serialization/reflection.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace anime_land::persistence::schema {

/// 应用 Schema 的规范关系名；仅用于创建或首次附加 Form。
namespace table {
inline constexpr NEKO_NAMESPACE::ConstexprString schemaMigrationsName {
    "schema_migrations"};
inline constexpr NEKO_NAMESPACE::ConstexprString subjectsName {"subjects"};
inline constexpr NEKO_NAMESPACE::ConstexprString subjectExternalRefsName {
    "subject_external_refs"};
inline constexpr NEKO_NAMESPACE::ConstexprString tagsName {"tags"};
inline constexpr NEKO_NAMESPACE::ConstexprString subjectTagsName {
    "subject_tags"};
inline constexpr NEKO_NAMESPACE::ConstexprString episodesName {"episodes"};
inline constexpr NEKO_NAMESPACE::ConstexprString episodeExternalRefsName {
    "episode_external_refs"};
inline constexpr NEKO_NAMESPACE::ConstexprString subjectFtsName {"subject_fts"};
inline constexpr NEKO_NAMESPACE::ConstexprString idColumnName {"id"};

inline constexpr std::string_view schemaMigrations =
    schemaMigrationsName.view();
inline constexpr std::string_view subjects = subjectsName.view();
inline constexpr std::string_view subjectExternalRefs =
    subjectExternalRefsName.view();
inline constexpr std::string_view tags = tagsName.view();
inline constexpr std::string_view subjectTags = subjectTagsName.view();
inline constexpr std::string_view episodes = episodesName.view();
inline constexpr std::string_view episodeExternalRefs =
    episodeExternalRefsName.view();
inline constexpr std::string_view subjectFts = subjectFtsName.view();
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

/**
 * ilias-sql 会按同名静态属性发现自定义 tag。一个业务 tag 可以同时提供普通
 * SqlTags 属性和扩展的 sql_check_expression，无需修改 ilias-sql。
 */
struct SubjectTypeConstraint {
    static constexpr bool not_null = true;
    static constexpr std::string_view sql_check_expression =
        "subject_type >= 0";
};

struct MetadataLevelConstraint {
    static constexpr bool not_null = true;
    static constexpr std::string_view sql_check_expression =
        "metadata_level BETWEEN 0 AND 1";
};

/// 已成功提交的 Migration 版本及其 UTC 毫秒时间戳。
struct SchemaMigrationRecord {
    std::int64_t version = 0;
    std::int64_t appliedAt = 0;
};

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

/**
 * @brief 为指定后端生成 schema_migrations 建表语句。
 *
 * @param backend 支持 sqlite、mysql/mariadb 和 postgres 别名。
 * @param ifNotExists 是否生成幂等的建表语句。
 */
auto migrationTableStatement(std::string_view backend,
                             bool ifNotExists = true)
    -> ilias::IoResult<std::string>;

/**
 * @brief 生成 v1 目录 Schema 的有序 DDL 语句。
 *
 * 表按外键依赖顺序生成；SQLite 末尾额外追加 subject_fts 虚拟表。
 */
auto catalogMigrationV1Statements(std::string_view backend)
    -> ilias::IoResult<std::vector<std::string>>;

} // namespace anime_land::persistence::schema

NEKO_BEGIN_NAMESPACE

// clang-format off
template <>
struct Meta<anime_land::persistence::schema::SchemaMigrationRecord, void> {
  using Record = anime_land::persistence::schema::SchemaMigrationRecord;

  constexpr static auto value = Object(
      "version",    make_tags<ilias::sql::SqlTags::createPrimaryKeyTags()>(&Record::version),
      "applied_at", make_tags<ilias::sql::SqlTags {.not_null = true}>(&Record::appliedAt)
  );
};
// clang-format on

// clang-format off
template <>
struct Meta<anime_land::persistence::schema::SubjectRecord, void> {
  using Record = anime_land::persistence::schema::SubjectRecord;
  using MetadataLevelConstraint = anime_land::persistence::schema::MetadataLevelConstraint;
  using SubjectTypeConstraint = anime_land::persistence::schema::SubjectTypeConstraint;

  constexpr static auto value = Object(
      "id",                    make_tags<ilias::sql::SqlTags::createPrimaryKeyTags(true)>(&Record::id),
      "subject_type",          make_tags<SubjectTypeConstraint {}, ilias::sql::sql_default<"2">>(&Record::subjectType),
      "title",                 make_tags<ilias::sql::SqlTags {.not_null = true}>(&Record::title),
      "title_cn",              &Record::titleCn,
      "summary",               &Record::summary,
      "air_date",              &Record::airDate,
      "cover_url",             &Record::coverUrl,
      "aliases_json",          make_tags<ilias::sql::SqlTags {.not_null = true}, ilias::sql::sql_default<"'[]'">>(&Record::aliasesJson),
      "created_at",            make_tags<ilias::sql::SqlTags {.not_null = true}>(&Record::createdAt),
      "updated_at",            make_tags<ilias::sql::SqlTags {.not_null = true}>(&Record::updatedAt),
      "metadata_level",        make_tags<MetadataLevelConstraint {}, ilias::sql::sql_default<"0">>(&Record::metadataLevel),
      "metadata_refreshed_at", &Record::metadataRefreshedAt
  );
};
// clang-format on

// clang-format off
template <>
struct Meta<anime_land::persistence::schema::SubjectExternalRefRecord, void> {
  using Record = anime_land::persistence::schema::SubjectExternalRefRecord;

  constexpr static auto value = Object(
      "subject_id",        make_tags<ilias::sql::SqlTags {.not_null = true}, anime_land::persistence::schema::subjectIdReference>(&Record::subjectId),
      "provider_key",      make_tags<ilias::sql::SqlTags {.not_null = true, .length = 64}>(&Record::providerKey),
      "external_id",       make_tags<ilias::sql::SqlTags {.not_null = true, .length = 255}>(&Record::externalId),
      "fetched_at",        make_tags<ilias::sql::SqlTags {.not_null = true}>(&Record::fetchedAt),
      "remote_updated_at", &Record::remoteUpdatedAt
  );
};
// clang-format on

// clang-format off
template <>
struct Meta<anime_land::persistence::schema::TagRecord, void> {
  using Record = anime_land::persistence::schema::TagRecord;

  constexpr static auto value = Object(
      "id",              make_tags<ilias::sql::SqlTags::createPrimaryKeyTags(true)>(&Record::id),
      "normalized_name", make_tags<ilias::sql::SqlTags {.not_null = true, .unique = true}, ilias::sql::sql_check<"normalized_name <> ''">>(&Record::normalizedName),
      "display_name",    make_tags<ilias::sql::SqlTags {.not_null = true}, ilias::sql::sql_check<"display_name <> ''">>(&Record::displayName)
  );
};
// clang-format on

// clang-format off
template <>
struct Meta<anime_land::persistence::schema::SubjectTagRecord, void> {
  using Record = anime_land::persistence::schema::SubjectTagRecord;

  constexpr static auto value = Object(
      "subject_id",   make_tags<ilias::sql::SqlTags {.not_null = true}, anime_land::persistence::schema::subjectIdReference>(&Record::subjectId),
      "tag_id",       make_tags<ilias::sql::SqlTags {.not_null = true}, anime_land::persistence::schema::tagIdReference>(&Record::tagId),
      "provider_key", make_tags<ilias::sql::SqlTags {.not_null = true, .length = 64}>(&Record::providerKey),
      "weight",       &Record::weight,
      "updated_at",   make_tags<ilias::sql::SqlTags {.not_null = true}>(&Record::updatedAt)
  );
};
// clang-format on

// clang-format off
template <>
struct Meta<anime_land::persistence::schema::EpisodeRecord, void> {
  using Record = anime_land::persistence::schema::EpisodeRecord;

  constexpr static auto value = Object(
      "id",             make_tags<ilias::sql::SqlTags::createPrimaryKeyTags(true)>(&Record::id),
      "subject_id",     make_tags<ilias::sql::SqlTags {.not_null = true}, anime_land::persistence::schema::subjectIdReference>(&Record::subjectId),
      "sort_order",     make_tags<ilias::sql::SqlTags {.not_null = true}, ilias::sql::sql_check<"sort_order >= 0">>(&Record::sortOrder),
      "episode_type",
      make_tags<ilias::sql::SqlTags {.not_null = true},
                ilias::sql::sql_default<"0">,
                ilias::sql::sql_check<"episode_type >= 0">>(&Record::episodeType),
      "episode_number", &Record::episodeNumber,
      "title",          &Record::title,
      "title_cn",       &Record::titleCn,
      "summary",        &Record::summary,
      "air_date",       &Record::airDate,
      "duration_ms",    make_tags<ilias::sql::sql_check<"duration_ms IS NULL OR duration_ms >= 0">>(&Record::durationMs),
      "created_at",     make_tags<ilias::sql::SqlTags {.not_null = true}>(&Record::createdAt),
      "updated_at",     make_tags<ilias::sql::SqlTags {.not_null = true}>(&Record::updatedAt)
  );
};
// clang-format on

// clang-format off
template <>
struct Meta<anime_land::persistence::schema::EpisodeExternalRefRecord, void> {
  using Record = anime_land::persistence::schema::EpisodeExternalRefRecord;

  constexpr static auto value = Object(
      "episode_id",        make_tags<ilias::sql::SqlTags {.not_null = true}, anime_land::persistence::schema::episodeIdReference>(&Record::episodeId),
      "provider_key",      make_tags<ilias::sql::SqlTags {.not_null = true, .length = 64}>(&Record::providerKey),
      "external_id",       make_tags<ilias::sql::SqlTags {.not_null = true, .length = 255}>(&Record::externalId),
      "fetched_at",        make_tags<ilias::sql::SqlTags {.not_null = true}>(&Record::fetchedAt),
      "remote_updated_at", &Record::remoteUpdatedAt
  );
};
// clang-format on

NEKO_END_NAMESPACE

ILIAS_SQL_NS_BEGIN

// clang-format off
template <>
struct SqlTableMeta<anime_land::persistence::schema::SubjectRecord> {
  using Record = anime_land::persistence::schema::SubjectRecord;

  constexpr static auto value = sql_table(
      sql_index<"idx_subjects_updated", sql_desc<&Record::updatedAt>, sql_desc<&Record::id>>
  );
};
// clang-format on

// clang-format off
template <>
struct SqlTableMeta<anime_land::persistence::schema::SubjectExternalRefRecord> {
  using Record = anime_land::persistence::schema::SubjectExternalRefRecord;

  constexpr static auto value = sql_table(
      sql_primary_key<&Record::providerKey, &Record::externalId>,
      sql_index<"idx_subject_external_refs_subject", sql_asc<&Record::subjectId>>
  );
};
// clang-format on

// clang-format off
template <>
struct SqlTableMeta<anime_land::persistence::schema::SubjectTagRecord> {
  using Record = anime_land::persistence::schema::SubjectTagRecord;

  constexpr static auto value = sql_table(
      sql_primary_key<&Record::subjectId, &Record::tagId, &Record::providerKey>,
      sql_index<"idx_subject_tags_tag_subject", sql_asc<&Record::tagId>, sql_asc<&Record::subjectId>>,
      sql_index<"idx_subject_tags_subject", sql_asc<&Record::subjectId>>
  );
};
// clang-format on

// clang-format off
template <>
struct SqlTableMeta<anime_land::persistence::schema::EpisodeRecord> {
  using Record = anime_land::persistence::schema::EpisodeRecord;

  constexpr static auto value = sql_table(
      sql_index<"idx_episodes_subject_order", sql_asc<&Record::subjectId>, sql_asc<&Record::sortOrder>, sql_asc<&Record::id>>
  );
};
// clang-format on

// clang-format off
template <>
struct SqlTableMeta<anime_land::persistence::schema::EpisodeExternalRefRecord> {
  using Record = anime_land::persistence::schema::EpisodeExternalRefRecord;

  constexpr static auto value = sql_table(
      sql_primary_key<&Record::providerKey, &Record::externalId>,
      sql_index<"idx_episode_external_refs_episode", sql_asc<&Record::episodeId>>
  );
};
// clang-format on

ILIAS_SQL_NS_END
