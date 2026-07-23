#include "persistence/catalog_store.hpp"

#include "common/log.hpp"
#include "common/qt_serialization.hpp"
#include "persistence/database_schema.hpp"

#include <ilias/sql_orm/orm_form.hpp>
#include <nekoproto/serialization/json/rapid_json_serializer.hpp>

#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace anime_land::persistence {
namespace {

using ilias::Err;

using StoredSubjectRow = schema::SubjectRecord;
using StoredEpisodeRow = schema::EpisodeRecord;

/**
 * @brief 一个 CatalogStore 在指定后端上长期持有的关系 Form 集合。
 *
 * attach() 在 Store 首次使用时完成一次真实 Schema 审查。Form 随后作为表的一等
 * 对象复用，而不是在每条查询前临时 bind。
 */
template <typename BackendTag>
struct CatalogForms {
    using Database = ilias::sql::SqlDatabase;

    ilias::sql::Form<schema::SubjectRecord, BackendTag, Database> subjects;
    ilias::sql::Form<schema::SubjectExternalRefRecord, BackendTag, Database>
        subjectExternalRefs;
    ilias::sql::Form<schema::TagRecord, BackendTag, Database> tags;
    ilias::sql::Form<schema::SubjectTagRecord, BackendTag, Database>
        subjectTags;
    ilias::sql::Form<schema::EpisodeRecord, BackendTag, Database> episodes;
    ilias::sql::Form<schema::EpisodeExternalRefRecord, BackendTag, Database>
        episodeExternalRefs;

    template <typename Record>
    static auto attachOne(Database &database, std::string_view tableName)
        -> ilias::IoTask<ilias::sql::Form<Record, BackendTag, Database>> {
        auto attached =
            co_await ilias::sql::Form<Record, BackendTag>::attach(
                database, std::string(tableName));
        if (!attached) {
            AL_LOG_ERROR(
                "[database.catalog] ORM form attachment failed table={} "
                "error={}",
                tableName, attached.error().message());
            co_return Err(attached.error());
        }
        co_return std::move(*attached);
    }

    /**
     * 当前 ilias-sql attach() 尚未把 SqlTableMeta 的复合主键合并进预期
     * Schema，会把正确的复合键关系表误判为 SchemaMismatch。此处只对这些已由
     * Migration 使用同一 Record + SqlTableMeta 生成的表执行一次无 I/O bind。
     */
    template <typename Record>
    static auto bindComposite(Database &database, std::string_view tableName)
        -> ilias::IoResult<ilias::sql::Form<Record, BackendTag, Database>> {
        return ilias::sql::Form<Record, BackendTag>::bind(
            database, std::string(tableName));
    }

    static auto attach(Database &database) -> ilias::IoTask<CatalogForms> {
        ILIAS_CO_TRY(
            auto subjects,
            co_await attachOne<schema::SubjectRecord>(
                database, schema::table::subjects));
        ILIAS_CO_TRY(
            auto subjectExternalRefs,
            bindComposite<schema::SubjectExternalRefRecord>(
                database, schema::table::subjectExternalRefs));
        ILIAS_CO_TRY(
            auto tags,
            co_await attachOne<schema::TagRecord>(database,
                                                  schema::table::tags));
        ILIAS_CO_TRY(
            auto subjectTags,
            bindComposite<schema::SubjectTagRecord>(
                database, schema::table::subjectTags));
        ILIAS_CO_TRY(
            auto episodes,
            co_await attachOne<schema::EpisodeRecord>(
                database, schema::table::episodes));
        ILIAS_CO_TRY(
            auto episodeExternalRefs,
            bindComposite<schema::EpisodeExternalRefRecord>(
                database, schema::table::episodeExternalRefs));
        co_return CatalogForms {
            .subjects = std::move(subjects),
            .subjectExternalRefs = std::move(subjectExternalRefs),
            .tags = std::move(tags),
            .subjectTags = std::move(subjectTags),
            .episodes = std::move(episodes),
            .episodeExternalRefs = std::move(episodeExternalRefs),
        };
    }
};

/// 把 QString 无损转换为数据库和 ORM 使用的 UTF-8 字节串。
auto utf8(const QString &value) -> std::string {
    const QByteArray encoded = value.toUtf8();
    return {encoded.constData(), static_cast<std::size_t>(encoded.size())};
}

/// 把数据库中的 UTF-8 字节串恢复为 QString。
auto fromUtf8(const std::string &value) -> QString {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

/// 保留 optional 的“未提供”语义并转换其中的 QString。
auto optionalUtf8(const std::optional<QString> &value)
    -> std::optional<std::string> {
    if (!value) {
        return std::nullopt;
    }
    return utf8(*value);
}

/// 生成用于标签唯一键和精确查询的 Unicode 兼容规范化名称。
auto normalizedTag(const QString &value) -> QString {
    return value.normalized(QString::NormalizationForm_KC)
        .trimmed()
        .toCaseFolded();
}

/// 把可选日期编码为跨后端稳定的 ISO 8601 文本。
auto dateText(const std::optional<QDate> &value) -> std::optional<std::string> {
    if (!value) {
        return std::nullopt;
    }
    return utf8(value->toString(Qt::ISODate));
}

/// 把可选 URL 编码为不会丢失转义信息的完整 URL 文本。
auto urlText(const std::optional<QUrl> &value) -> std::optional<std::string> {
    if (!value) {
        return std::nullopt;
    }
    return utf8(value->toString(QUrl::FullyEncoded));
}

/// 把可选时间转换为 UTC Unix 毫秒时间戳。
auto dateTimeMillis(const std::optional<QDateTime> &value)
    -> std::optional<std::int64_t> {
    if (!value) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(value->toMSecsSinceEpoch());
}

/// 把可选时长转换为数据库统一使用的毫秒整数。
auto durationMillis(const std::optional<std::chrono::milliseconds> &value)
    -> std::optional<std::int64_t> {
    if (!value) {
        return std::nullopt;
    }
    return value->count();
}

/// 返回目录记录写入时使用的 UTC Unix 毫秒时间戳。
auto nowMillis() -> std::int64_t {
    return static_cast<std::int64_t>(
        QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());
}

/// 从数据库时间戳构造显式使用 UTC 时区的 QDateTime。
auto fromEpochMillis(std::int64_t value) -> QDateTime {
    return QDateTime::fromMSecsSinceEpoch(
        static_cast<qint64>(value), QTimeZone(QByteArrayLiteral("UTC")));
}

/// 把别名数组序列化为 subjects.aliases_json。
auto encodeAliases(const std::vector<QString> &aliases)
    -> std::optional<std::string> {
    std::vector<char> data;
    NEKO_NAMESPACE::RapidJsonOutputSerializer serializer(data);
    if (!serializer(aliases) || !serializer.end()) {
        return std::nullopt;
    }
    return std::string(data.begin(), data.end());
}

/// 解码 subjects.aliases_json，失败时保持调用方通过返回值处理损坏数据。
auto decodeAliases(std::string_view text,
                   std::vector<QString> &aliases) -> bool {
    NEKO_NAMESPACE::RapidJsonInputSerializer serializer(text.data(), text.size());
    std::vector<QString> decoded;
    if (!serializer(decoded)) {
        return false;
    }
    aliases = std::move(decoded);
    return true;
}

/// 生成 SQLite FTS aliases 列使用的换行分隔文本。
auto flattenedAliases(const std::vector<QString> &aliases) -> std::string {
    QString flattened;
    for (const auto &alias : aliases) {
        if (!flattened.isEmpty()) {
            flattened.append(u'\n');
        }
        flattened.append(alias);
    }
    return utf8(flattened);
}

/// 把用户文本转义成单个 FTS5 phrase，避免其被解释为查询运算符。
auto ftsPhrase(const QString &input) -> std::string {
    QString escaped =
        input.normalized(QString::NormalizationForm_KC).trimmed();
    escaped.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    escaped.prepend(u'"');
    escaped.append(u'"');
    return utf8(escaped);
}

/// 防止无效枚举值越过数据库 CHECK 约束。
auto validMetadataLevel(SubjectMetadataLevel value) -> bool {
    return value == SubjectMetadataLevel::Summary ||
           value == SubjectMetadataLevel::Details;
}

/// 校验一组标签名称和权重是否可以进入关系表。
auto validSubjectTags(const std::vector<SubjectTagSnapshot> &tags) -> bool {
    return std::ranges::all_of(tags, [](const SubjectTagSnapshot &tag) {
        return !normalizedTag(tag.name).isEmpty() &&
               (!tag.weight || std::isfinite(*tag.weight));
    });
}

/// 在启动事务前完成快照边界校验，避免部分写入和无效远端数据。
auto validateSnapshot(const SubjectSnapshot &snapshot) -> bool {
    if (snapshot.origin.providerKey.trimmed().isEmpty() ||
        snapshot.origin.externalId.trimmed().isEmpty() ||
        snapshot.title.trimmed().isEmpty() || snapshot.subjectType < 0 ||
        !validMetadataLevel(snapshot.metadataLevel) ||
        !snapshot.fetchedAt.isValid()) {
        return false;
    }
    if (snapshot.remoteUpdatedAt && !snapshot.remoteUpdatedAt->isValid()) {
        return false;
    }
    if (snapshot.airDate && !snapshot.airDate->isValid()) {
        return false;
    }
    if (snapshot.coverUrl && !snapshot.coverUrl->isValid()) {
        return false;
    }
    return !snapshot.tags || validSubjectTags(*snapshot.tags);
}

/// 在批量事务开始前校验章节快照的身份、范围和时间字段。
auto validateEpisodeSnapshot(const EpisodeSnapshot &snapshot) -> bool {
    return !snapshot.origin.providerKey.trimmed().isEmpty() &&
           !snapshot.origin.externalId.trimmed().isEmpty() &&
           snapshot.sortOrder >= 0 && snapshot.episodeType >= 0 &&
           (!snapshot.episodeNumber ||
            std::isfinite(*snapshot.episodeNumber)) &&
           (!snapshot.airDate || snapshot.airDate->isValid()) &&
           (!snapshot.duration || snapshot.duration->count() >= 0) &&
           snapshot.fetchedAt.isValid() &&
           (!snapshot.remoteUpdatedAt ||
            snapshot.remoteUpdatedAt->isValid());
}

/// 准备、绑定并执行带参数 SQL；只记录 SQL 模板，不记录参数内容。
template <typename SqlApi, typename... Args>
auto executeWith(SqlApi &database, std::string sql, Args &&...args)
    -> ilias::IoTask<std::size_t> {
    ILIAS_CO_TRY(auto statement, co_await database.prepare(sql));
    ILIAS_CO_TRYV(statement.bind(std::forward<Args>(args)...));
    co_return co_await statement.execute();
}

/// 使用长期 Form 按外部身份查找条目主键。
template <typename ExternalRefForm>
auto findExternal(ExternalRefForm &form, const ExternalRef &ref)
    -> ilias::IoTask<std::optional<SubjectId>> {
    using Record = schema::SubjectExternalRefRecord;
    ILIAS_CO_TRY(
        auto result,
        co_await form.select()
            .where(form.sql(&Record::providerKey) == utf8(ref.providerKey) &&
                   form.sql(&Record::externalId) == utf8(ref.externalId))
            .query());
    ilias_for_await(auto rowResult, result.rangeResult()) {
        ILIAS_CO_TRY(auto row, rowResult);
        co_return SubjectId {row.subjectId};
    }
    co_return std::nullopt;
}

/// 使用长期 Form 按外部身份查找章节主键。
template <typename ExternalRefForm>
auto findEpisodeExternal(ExternalRefForm &form, const ExternalRef &ref)
    -> ilias::IoTask<std::optional<EpisodeId>> {
    using Record = schema::EpisodeExternalRefRecord;
    ILIAS_CO_TRY(
        auto result,
        co_await form.select()
            .where(form.sql(&Record::providerKey) == utf8(ref.providerKey) &&
                   form.sql(&Record::externalId) == utf8(ref.externalId))
            .query());
    ilias_for_await(auto rowResult, result.rangeResult()) {
        ILIAS_CO_TRY(auto row, rowResult);
        co_return EpisodeId {row.episodeId};
    }
    co_return std::nullopt;
}

/// 使用长期 Form 加载一行 subjects 记录。
template <typename SubjectForm>
auto loadSubjectRow(SubjectForm &form, SubjectId subject)
    -> ilias::IoTask<std::optional<StoredSubjectRow>> {
    using Record = schema::SubjectRecord;
    ILIAS_CO_TRY(auto result,
                 co_await form.select()
                     .where(form.sql(&Record::id) == subject.value)
                     .query());
    ilias_for_await(auto rowResult, result.rangeResult()) {
        ILIAS_CO_TRY(auto row, rowResult);
        co_return row;
    }
    co_return std::nullopt;
}

/// 使用长期 Form 加载一行 episodes 记录。
template <typename EpisodeForm>
auto loadEpisodeRow(EpisodeForm &form, EpisodeId episode)
    -> ilias::IoTask<std::optional<StoredEpisodeRow>> {
    using Record = schema::EpisodeRecord;
    ILIAS_CO_TRY(auto result,
                 co_await form.select()
                     .where(form.sql(&Record::id) == episode.value)
                     .query());
    ilias_for_await(auto rowResult, result.rangeResult()) {
        ILIAS_CO_TRY(auto row, rowResult);
        co_return row;
    }
    co_return std::nullopt;
}

/// 把外部身份 ORM 记录转换为公共值对象。
template <typename Record>
auto toExternalRef(const Record &row) -> StoredExternalRef {
    return {
        .ref =
            {
                .providerKey = fromUtf8(row.providerKey),
                .externalId = fromUtf8(row.externalId),
            },
        .fetchedAt = fromEpochMillis(row.fetchedAt),
        .remoteUpdatedAt = row.remoteUpdatedAt.transform(fromEpochMillis),
    };
}

/// 使用长期 Form 加载条目的全部外部身份映射。
template <typename ExternalRefForm>
auto loadSubjectExternalRefs(ExternalRefForm &form, SubjectId subject)
    -> ilias::IoTask<std::vector<StoredExternalRef>> {
    using Record = schema::SubjectExternalRefRecord;
    ILIAS_CO_TRY(auto result,
                 co_await form.select()
                     .where(form.sql(&Record::subjectId) == subject.value)
                     .query());

    std::vector<StoredExternalRef> refs;
    ilias_for_await(auto rowResult, result.rangeResult()) {
        ILIAS_CO_TRY(auto row, rowResult);
        refs.push_back(toExternalRef(row));
    }
    co_return refs;
}

/// 使用长期 Form 加载章节的全部外部身份映射。
template <typename ExternalRefForm>
auto loadEpisodeExternalRefs(ExternalRefForm &form, EpisodeId episode)
    -> ilias::IoTask<std::vector<StoredExternalRef>> {
    using Record = schema::EpisodeExternalRefRecord;
    ILIAS_CO_TRY(auto result,
                 co_await form.select()
                     .where(form.sql(&Record::episodeId) == episode.value)
                     .query());

    std::vector<StoredExternalRef> refs;
    ilias_for_await(auto rowResult, result.rangeResult()) {
        ILIAS_CO_TRY(auto row, rowResult);
        refs.push_back(toExternalRef(row));
    }
    co_return refs;
}

/// 把关系表记录转换成不包含关联数据的业务摘要。
auto toSummary(const StoredSubjectRow &row) -> SubjectSummary {
    return {
        .id = SubjectId {row.id},
        .subjectType = static_cast<int>(row.subjectType),
        .title = fromUtf8(row.title),
        .titleCn =
            row.titleCn.transform([](const auto &value) { return fromUtf8(value); }),
        .summary =
            row.summary.transform([](const auto &value) { return fromUtf8(value); }),
        .metadataLevel =
            static_cast<SubjectMetadataLevel>(row.metadataLevel),
        .updatedAt = fromEpochMillis(row.updatedAt),
    };
}

/// 把章节关系记录转换成公共详情值对象。
auto toEpisodeDetails(const StoredEpisodeRow &row) -> EpisodeDetails {
    EpisodeDetails details {
        .id = EpisodeId {row.id},
        .subjectId = SubjectId {row.subjectId},
        .sortOrder = static_cast<int>(row.sortOrder),
        .episodeType = static_cast<int>(row.episodeType),
        .episodeNumber = row.episodeNumber,
        .title =
            row.title.transform([](const auto &value) { return fromUtf8(value); }),
        .titleCn = row.titleCn.transform(
            [](const auto &value) { return fromUtf8(value); }),
        .summary = row.summary.transform(
            [](const auto &value) { return fromUtf8(value); }),
        .airDate = std::nullopt,
        .duration = row.durationMs.transform([](std::int64_t value) {
            return std::chrono::milliseconds(value);
        }),
        .externalRefs = {},
        .updatedAt = fromEpochMillis(row.updatedAt),
    };
    if (row.airDate) {
        const QDate date = QDate::fromString(fromUtf8(*row.airDate), Qt::ISODate);
        if (date.isValid()) {
            details.airDate = date;
        }
    }
    return details;
}

/**
 * @brief 用提供者本次返回的完整标签集合替换该提供者的旧关系。
 *
 * tags 表本身按规范化名称复用；这里只删除指定 provider 的 subject_tags，
 * 不影响手工标签或其他远端提供者的标签。
 */
template <typename SubjectTagForm, typename TagForm>
auto replaceProviderTags(SubjectTagForm &subjectTags, TagForm &tagRecords,
                         SubjectId subject,
                         const QString &provider,
                         const std::vector<SubjectTagSnapshot> &tags,
                         std::int64_t updatedAt) -> ilias::IoTask<void> {
    using SubjectTag = schema::SubjectTagRecord;
    using Tag = schema::TagRecord;

    const std::string providerKey = utf8(provider);
    AL_LOG_DEBUG(
        "[database.catalog] tag replacement started subject_id={} count={}",
        subject.value, tags.size());
    ILIAS_CO_TRYV(
        co_await subjectTags.remove()
            .where(subjectTags.sql(&SubjectTag::subjectId) == subject.value &&
                   subjectTags.sql(&SubjectTag::providerKey) == providerKey)
            .execute());

    for (const auto &tag : tags) {
        const QString displayName =
            tag.name.normalized(QString::NormalizationForm_KC).trimmed();
        const QString normalizedName = normalizedTag(displayName);
        const std::string normalized = utf8(normalizedName);
        const std::string displayed = utf8(displayName);

        ILIAS_CO_TRYV(
            co_await tagRecords.upsert()
                .values(tagRecords.sql(&Tag::normalizedName) = normalized,
                        tagRecords.sql(&Tag::displayName) = displayed)
                .onConflict(tagRecords.sql(&Tag::normalizedName))
                .updateExcluded(tagRecords.sql(&Tag::displayName))
                .execute());

        ILIAS_CO_TRY(
            auto result,
            co_await tagRecords.select()
                .where(tagRecords.sql(&Tag::normalizedName) == normalized)
                .query());

        std::int64_t tagId = 0;
        ilias_for_await(auto rowResult, result.rangeResult()) {
            ILIAS_CO_TRY(auto row, rowResult);
            tagId = row.id;
        }
        if (tagId <= 0) {
            co_return Err(std::make_error_code(std::errc::protocol_error));
        }

        ILIAS_CO_TRYV(
            co_await subjectTags.upsert()
                .values(
                    subjectTags.sql(&SubjectTag::subjectId) = subject.value,
                    subjectTags.sql(&SubjectTag::tagId) = tagId,
                    subjectTags.sql(&SubjectTag::providerKey) = providerKey,
                    subjectTags.sql(&SubjectTag::weight) =
                        std::optional<double>(tag.weight),
                    subjectTags.sql(&SubjectTag::updatedAt) = updatedAt)
                .onConflict(subjectTags.sql(&SubjectTag::subjectId),
                            subjectTags.sql(&SubjectTag::tagId),
                            subjectTags.sql(&SubjectTag::providerKey))
                .updateExcluded(subjectTags.sql(&SubjectTag::weight),
                                subjectTags.sql(&SubjectTag::updatedAt))
                .execute());
    }
    AL_LOG_DEBUG(
        "[database.catalog] tag replacement completed subject_id={} count={}",
        subject.value, tags.size());
    co_return {};
}

/// 在写事务内重建单个条目的 SQLite FTS 投影。
template <typename SqlApi>
auto syncFts(SqlApi &database, const StoredSubjectRow &row)
    -> ilias::IoTask<void> {
    std::vector<QString> aliases;
    if (!decodeAliases(row.aliasesJson, aliases)) {
        co_return Err(std::make_error_code(std::errc::illegal_byte_sequence));
    }

    // FTS 虚拟表没有关系表的 UPSERT 约束，使用 delete+insert 保证单行投影。
    const std::string ftsTable(schema::table::subjectFts);
    ILIAS_CO_TRYV(co_await executeWith(
        database, "DELETE FROM " + ftsTable + " WHERE subject_id = ?", row.id));
    ILIAS_CO_TRYV(co_await executeWith(
        database,
        "INSERT INTO " + ftsTable +
            "("
            "subject_id, title, title_cn, aliases, summary"
            ") VALUES (?, ?, ?, ?, ?)",
        row.id, row.title, row.titleCn, flattenedAliases(aliases), row.summary));
    co_return {};
}

/// 消费 ORM 的完整 subjects 结果，确保字段变更继续由反射映射而非手工列清单维护。
auto appendSubjectRows(ilias::sql::SqlResult<StoredSubjectRow> result,
                       std::vector<StoredSubjectRow> &subjects)
    -> ilias::IoTask<void> {
    ilias_for_await(auto rowResult, result.rangeResult()) {
        ILIAS_CO_TRY(auto row, rowResult);
        subjects.push_back(std::move(row));
    }
    co_return {};
}

/**
 * @brief 通过 ORM 解析精确标签，并返回当前关联的条目主键集合。
 *
 * subject_tags 可能因不同 provider 对同一条目产生重复关系，因此在 C++ 层去重。
 */
template <typename TagForm, typename SubjectTagForm>
auto loadSubjectIdsForTag(TagForm &tags, SubjectTagForm &subjectTags,
                          const std::string &normalizedName)
    -> ilias::IoTask<std::unordered_set<std::int64_t>> {
    using SubjectTag = schema::SubjectTagRecord;
    using Tag = schema::TagRecord;

    ILIAS_CO_TRY(auto tagRows,
                 co_await tags.select()
                     .where(tags.sql(&Tag::normalizedName) == normalizedName)
                     .query());

    std::optional<std::int64_t> tagId;
    ilias_for_await(auto rowResult, tagRows.rangeResult()) {
        ILIAS_CO_TRY(auto row, rowResult);
        tagId = row.id;
    }
    if (!tagId) {
        co_return std::unordered_set<std::int64_t> {};
    }

    ILIAS_CO_TRY(auto relations,
                 co_await subjectTags.select()
                     .where(subjectTags.sql(&SubjectTag::tagId) == *tagId)
                     .query());

    std::unordered_set<std::int64_t> subjectIds;
    ilias_for_await(auto rowResult, relations.rangeResult()) {
        ILIAS_CO_TRY(auto row, rowResult);
        subjectIds.insert(row.subjectId);
    }
    co_return subjectIds;
}

/**
 * @brief 执行 SQLite FTS5 排名查询。
 *
 * FTS5 虚拟表及 bm25() 不属于关系 ORM 能表达的模型。这个后端扩展查询被刻意
 * 限制为只返回 subject_id；关系实体仍统一交给 Form<SubjectRecord> 加载。
 */
template <typename SqlApi>
auto searchFtsSubjectIds(SqlApi &database, const QString &text)
    -> ilias::IoTask<std::vector<std::int64_t>> {
    const std::string ftsTable(schema::table::subjectFts);
    const std::string sql =
        "SELECT subject_id FROM " + ftsTable + " WHERE " + ftsTable +
        " MATCH ? ORDER BY bm25(" + ftsTable + "), subject_id DESC";
    ILIAS_CO_TRY(
        auto statement,
        co_await database.prepare(sql));
    ILIAS_CO_TRYV(statement.bind(ftsPhrase(text)));
    ILIAS_CO_TRY(auto result, co_await statement.query());

    std::vector<std::int64_t> subjectIds;
    std::int64_t subjectId = 0;
    ilias_for_await(auto rowResult, result.range(subjectId)) {
        ILIAS_CO_TRYV(rowResult);
        subjectIds.push_back(subjectId);
    }
    co_return subjectIds;
}

} // namespace

namespace {

/// 按指定后端方言原子写入条目快照及其所有关系投影。
template <typename BackendTag>
auto upsertSubjectSnapshotFor(LocalDatabase &database,
                              CatalogForms<BackendTag> &catalog,
                              SubjectSnapshot snapshot)
    -> ilias::IoTask<SubjectId> {
    if (!validateSnapshot(snapshot)) {
        co_return Err(std::make_error_code(std::errc::invalid_argument));
    }

    std::optional<std::string> aliasesJson;
    if (snapshot.aliases) {
        aliasesJson = encodeAliases(*snapshot.aliases);
        if (!aliasesJson) {
            co_return Err(
                std::make_error_code(std::errc::illegal_byte_sequence));
        }
    }

    ILIAS_CO_TRY(auto transaction,
                 co_await database.advancedConnection().transaction());

    using Subject = schema::SubjectRecord;
    using SubjectExternalRef = schema::SubjectExternalRefRecord;

    ILIAS_CO_TRY(auto subjects,
                 ilias::sql::Form<Subject, BackendTag>::bind(
                     transaction, catalog.subjects.getTableName()));

    ILIAS_CO_TRY(auto externalRefs,
                 ilias::sql::Form<SubjectExternalRef, BackendTag>::bind(
                     transaction,
                     catalog.subjectExternalRefs.getTableName()));

    ILIAS_CO_TRY(auto existing,
                 co_await findExternal(externalRefs, snapshot.origin));

    SubjectId subject;
    const std::int64_t writtenAt = nowMillis();
    if (existing) {
        subject = *existing;
    }
    else {
        // 先创建仅含必填字段的壳记录，再由统一 update 路径应用完整快照。
        ILIAS_CO_TRYV(
            co_await subjects.insert()
                .set(subjects.sql(&Subject::subjectType) =
                         static_cast<std::int64_t>(snapshot.subjectType),
                     subjects.sql(&Subject::title) = utf8(snapshot.title),
                     subjects.sql(&Subject::aliasesJson) = std::string("[]"),
                     subjects.sql(&Subject::createdAt) = writtenAt,
                     subjects.sql(&Subject::updatedAt) = writtenAt)
                .execute());
        ILIAS_CO_TRY(auto connection, transaction.connection());
        subject = SubjectId {connection->lastInsertId()};
    }

    const std::int64_t metadataLevel =
        static_cast<std::int64_t>(snapshot.metadataLevel);
    const std::optional<std::int64_t> metadataRefreshedAt =
        snapshot.metadataLevel == SubjectMetadataLevel::Details
            ? std::optional<std::int64_t> {
                  static_cast<std::int64_t>(
                      snapshot.fetchedAt.toMSecsSinceEpoch())}
            : std::nullopt;

    // COALESCE 保留摘要响应中未提供的详情字段；GREATEST 防止完整度降级。
    ILIAS_CO_TRYV(
        co_await subjects.update()
            .set(
                subjects.sql(&Subject::subjectType) =
                    static_cast<std::int64_t>(snapshot.subjectType),
                subjects.sql(&Subject::title) = utf8(snapshot.title),
                subjects.assignCoalesced(subjects.sql(&Subject::titleCn),
                                         optionalUtf8(snapshot.titleCn)),
                subjects.assignCoalesced(subjects.sql(&Subject::summary),
                                         optionalUtf8(snapshot.summary)),
                subjects.assignCoalesced(subjects.sql(&Subject::airDate),
                                         dateText(snapshot.airDate)),
                subjects.assignCoalesced(subjects.sql(&Subject::coverUrl),
                                         urlText(snapshot.coverUrl)),
                subjects.assignCoalesced(subjects.sql(&Subject::aliasesJson),
                                         std::optional<std::string>(
                                             aliasesJson)),
                subjects.sql(&Subject::updatedAt) = writtenAt,
                subjects.assignGreatest(
                    subjects.sql(&Subject::metadataLevel), metadataLevel),
                subjects.assignCoalesced(
                    subjects.sql(&Subject::metadataRefreshedAt),
                    std::optional<std::int64_t>(metadataRefreshedAt)))
            .where(subjects.sql(&Subject::id) == subject.value)
            .execute());

    ILIAS_CO_TRYV(
        co_await externalRefs.upsert()
            .values(
                externalRefs.sql(&SubjectExternalRef::subjectId) =
                    subject.value,
                externalRefs.sql(&SubjectExternalRef::providerKey) =
                    utf8(snapshot.origin.providerKey),
                externalRefs.sql(&SubjectExternalRef::externalId) =
                    utf8(snapshot.origin.externalId),
                externalRefs.sql(&SubjectExternalRef::fetchedAt) =
                    static_cast<std::int64_t>(
                        snapshot.fetchedAt.toMSecsSinceEpoch()),
                externalRefs.sql(&SubjectExternalRef::remoteUpdatedAt) =
                    dateTimeMillis(snapshot.remoteUpdatedAt))
            .onConflict(
                externalRefs.sql(&SubjectExternalRef::providerKey),
                externalRefs.sql(&SubjectExternalRef::externalId))
            .updateExcluded(
                externalRefs.sql(&SubjectExternalRef::subjectId),
                externalRefs.sql(&SubjectExternalRef::fetchedAt),
                externalRefs.sql(&SubjectExternalRef::remoteUpdatedAt))
            .execute());

    if (snapshot.tags) {
        // nullopt 表示远端未返回标签；空 vector 才表示明确清空该来源标签。
        using SubjectTag = schema::SubjectTagRecord;
        using Tag = schema::TagRecord;
        ILIAS_CO_TRY(auto subjectTags,
                     ilias::sql::Form<SubjectTag, BackendTag>::bind(
                         transaction, catalog.subjectTags.getTableName()));
        ILIAS_CO_TRY(auto tags,
                     ilias::sql::Form<Tag, BackendTag>::bind(
                         transaction, catalog.tags.getTableName()));
        ILIAS_CO_TRYV(co_await replaceProviderTags(
            subjectTags, tags, subject, snapshot.origin.providerKey,
            *snapshot.tags, writtenAt));
    }

    ILIAS_CO_TRY(auto stored,
                 co_await loadSubjectRow(subjects, subject));
    if (!stored) {
        co_return Err(std::make_error_code(std::errc::protocol_error));
    }
    if constexpr (std::is_same_v<BackendTag, ilias::sql::SqliteTag>) {
        // FTS 必须与关系数据共用事务，提交后不能出现索引与正文不一致。
        ILIAS_CO_TRYV(co_await syncFts(transaction, *stored));
    }

    // 事务提交前任何一步失败都会回滚条目、外部映射、标签和 FTS。
    ILIAS_CO_TRYV(co_await transaction.commit());
    co_return subject;
}

/// 按指定后端方言原子写入一组章节快照。
template <typename BackendTag>
auto upsertEpisodeSnapshotsFor(LocalDatabase &database,
                               CatalogForms<BackendTag> &catalog,
                               SubjectId subject,
                               std::vector<EpisodeSnapshot> snapshots)
    -> ilias::IoTask<std::vector<EpisodeId>> {
    if (subject.value <= 0 ||
        std::ranges::any_of(snapshots, [](const EpisodeSnapshot &snapshot) {
            return !validateEpisodeSnapshot(snapshot);
        })) {
        co_return Err(std::make_error_code(std::errc::invalid_argument));
    }

    ILIAS_CO_TRY(auto transaction,
                 co_await database.advancedConnection().transaction());
    using Subject = schema::SubjectRecord;
    ILIAS_CO_TRY(auto subjects,
                 ilias::sql::Form<Subject, BackendTag>::bind(
                     transaction, catalog.subjects.getTableName()));
    ILIAS_CO_TRY(
        auto parent, co_await loadSubjectRow(subjects, subject));
    if (!parent) {
        co_return Err(
            std::make_error_code(std::errc::no_such_file_or_directory));
    }

    using Episode = schema::EpisodeRecord;
    using EpisodeExternalRef = schema::EpisodeExternalRefRecord;
    ILIAS_CO_TRY(auto episodes,
                 ilias::sql::Form<Episode, BackendTag>::bind(
                     transaction, catalog.episodes.getTableName()));
    ILIAS_CO_TRY(auto externalRefs,
                 ilias::sql::Form<EpisodeExternalRef, BackendTag>::bind(
                     transaction,
                     catalog.episodeExternalRefs.getTableName()));

    std::vector<EpisodeId> ids;
    ids.reserve(snapshots.size());
    const std::int64_t writtenAt = nowMillis();
    for (const auto &snapshot : snapshots) {
        ILIAS_CO_TRY(
            auto existing,
            co_await findEpisodeExternal(externalRefs, snapshot.origin));

        EpisodeId episode;
        if (existing) {
            episode = *existing;
            ILIAS_CO_TRY(
                auto stored, co_await loadEpisodeRow(episodes, episode));
            if (!stored || stored->subjectId != subject.value) {
                co_return Err(
                    std::make_error_code(std::errc::operation_not_permitted));
            }
        }
        else {
            ILIAS_CO_TRYV(
                co_await episodes.insert()
                    .set(episodes.sql(&Episode::subjectId) = subject.value,
                         episodes.sql(&Episode::sortOrder) =
                             static_cast<std::int64_t>(snapshot.sortOrder),
                         episodes.sql(&Episode::episodeType) =
                             static_cast<std::int64_t>(snapshot.episodeType),
                         episodes.sql(&Episode::createdAt) = writtenAt,
                         episodes.sql(&Episode::updatedAt) = writtenAt)
                    .execute());
            ILIAS_CO_TRY(auto connection, transaction.connection());
            episode = EpisodeId {connection->lastInsertId()};
        }

        ILIAS_CO_TRYV(
            co_await episodes.update()
                .set(
                    episodes.sql(&Episode::sortOrder) =
                        static_cast<std::int64_t>(snapshot.sortOrder),
                    episodes.sql(&Episode::episodeType) =
                        static_cast<std::int64_t>(snapshot.episodeType),
                    episodes.sql(&Episode::episodeNumber) =
                        std::optional<double>(snapshot.episodeNumber),
                    episodes.sql(&Episode::title) =
                        optionalUtf8(snapshot.title),
                    episodes.sql(&Episode::titleCn) =
                        optionalUtf8(snapshot.titleCn),
                    episodes.sql(&Episode::summary) =
                        optionalUtf8(snapshot.summary),
                    episodes.sql(&Episode::airDate) =
                        dateText(snapshot.airDate),
                    episodes.sql(&Episode::durationMs) =
                        durationMillis(snapshot.duration),
                    episodes.sql(&Episode::updatedAt) = writtenAt)
                .where(episodes.sql(&Episode::id) == episode.value)
                .execute());

        ILIAS_CO_TRYV(
            co_await externalRefs.upsert()
                .values(
                    externalRefs.sql(&EpisodeExternalRef::episodeId) =
                        episode.value,
                    externalRefs.sql(&EpisodeExternalRef::providerKey) =
                        utf8(snapshot.origin.providerKey),
                    externalRefs.sql(&EpisodeExternalRef::externalId) =
                        utf8(snapshot.origin.externalId),
                    externalRefs.sql(&EpisodeExternalRef::fetchedAt) =
                        static_cast<std::int64_t>(
                            snapshot.fetchedAt.toMSecsSinceEpoch()),
                    externalRefs.sql(&EpisodeExternalRef::remoteUpdatedAt) =
                        dateTimeMillis(snapshot.remoteUpdatedAt))
                .onConflict(
                    externalRefs.sql(&EpisodeExternalRef::providerKey),
                    externalRefs.sql(&EpisodeExternalRef::externalId))
                .updateExcluded(
                    externalRefs.sql(&EpisodeExternalRef::episodeId),
                    externalRefs.sql(&EpisodeExternalRef::fetchedAt),
                    externalRefs.sql(&EpisodeExternalRef::remoteUpdatedAt))
                .execute());
        ids.push_back(episode);
    }

    ILIAS_CO_TRYV(co_await transaction.commit());
    co_return ids;
}

/// 按指定后端方言替换一个来源的条目标签。
template <typename BackendTag>
auto replaceSubjectTagsFor(LocalDatabase &database,
                           CatalogForms<BackendTag> &catalog,
                           SubjectId subject,
                           const QString &providerKey,
                           const std::vector<SubjectTagSnapshot> &tags)
    -> ilias::IoTask<void> {
    if (subject.value <= 0 || providerKey.trimmed().isEmpty() ||
        !validSubjectTags(tags)) {
        co_return Err(std::make_error_code(std::errc::invalid_argument));
    }

    ILIAS_CO_TRY(auto transaction,
                 co_await database.advancedConnection().transaction());
    using Subject = schema::SubjectRecord;
    using SubjectTag = schema::SubjectTagRecord;
    using Tag = schema::TagRecord;
    ILIAS_CO_TRY(auto subjects,
                 ilias::sql::Form<Subject, BackendTag>::bind(
                     transaction, catalog.subjects.getTableName()));
    ILIAS_CO_TRY(auto subjectTags,
                 ilias::sql::Form<SubjectTag, BackendTag>::bind(
                     transaction, catalog.subjectTags.getTableName()));
    ILIAS_CO_TRY(auto tagRecords,
                 ilias::sql::Form<Tag, BackendTag>::bind(
                     transaction, catalog.tags.getTableName()));
    ILIAS_CO_TRY(
        auto stored, co_await loadSubjectRow(subjects, subject));
    if (!stored) {
        co_return Err(
            std::make_error_code(std::errc::no_such_file_or_directory));
    }
    ILIAS_CO_TRYV(co_await replaceProviderTags(
        subjectTags, tagRecords, subject, providerKey, tags, nowMillis()));
    ILIAS_CO_TRYV(co_await transaction.commit());
    co_return {};
}

/// 按后端方言校验并查找外部身份。
template <typename BackendTag>
auto findSubjectByExternalRefFor(CatalogForms<BackendTag> &forms,
                                 const ExternalRef &ref)
    -> ilias::IoTask<std::optional<SubjectId>> {
    if (ref.providerKey.trimmed().isEmpty() ||
        ref.externalId.trimmed().isEmpty()) {
        co_return Err(std::make_error_code(std::errc::invalid_argument));
    }
    co_return co_await findExternal(forms.subjectExternalRefs, ref);
}

/// 按后端方言校验并查找章节外部身份。
template <typename BackendTag>
auto findEpisodeByExternalRefFor(CatalogForms<BackendTag> &forms,
                                 const ExternalRef &ref)
    -> ilias::IoTask<std::optional<EpisodeId>> {
    if (ref.providerKey.trimmed().isEmpty() ||
        ref.externalId.trimmed().isEmpty()) {
        co_return Err(std::make_error_code(std::errc::invalid_argument));
    }
    co_return co_await findEpisodeExternal(forms.episodeExternalRefs, ref);
}

/// 按后端方言加载条目主记录与标签关系。
template <typename BackendTag>
auto getSubjectFor(CatalogForms<BackendTag> &forms, SubjectId subject)
    -> ilias::IoTask<std::optional<SubjectDetails>> {
    if (subject.value <= 0) {
        co_return Err(std::make_error_code(std::errc::invalid_argument));
    }

    ILIAS_CO_TRY(auto stored,
                 co_await loadSubjectRow(forms.subjects, subject));
    if (!stored) {
        co_return std::nullopt;
    }

    SubjectDetails details;
    details.summary = toSummary(*stored);
    if (stored->airDate) {
        const QDate date =
            QDate::fromString(fromUtf8(*stored->airDate), Qt::ISODate);
        if (date.isValid()) {
            details.airDate = date;
        }
    }
    if (stored->coverUrl) {
        const QUrl url(fromUtf8(*stored->coverUrl));
        if (url.isValid()) {
            details.coverUrl = url;
        }
    }
    if (!decodeAliases(stored->aliasesJson, details.aliases)) {
        co_return Err(
            std::make_error_code(std::errc::illegal_byte_sequence));
    }
    if (stored->metadataRefreshedAt) {
        details.metadataRefreshedAt =
            fromEpochMillis(*stored->metadataRefreshedAt);
    }

    using SubjectTag = schema::SubjectTagRecord;
    using Tag = schema::TagRecord;
    ILIAS_CO_TRY(
        auto result,
        co_await forms.subjectTags.join(forms.tags)
            .on(forms.subjectTags.col(&SubjectTag::tagId) ==
                forms.tags.col(&Tag::id))
            .where(forms.subjectTags.col(&SubjectTag::subjectId) ==
                   subject.value)
            .query());

    std::vector<std::tuple<SubjectTag, Tag>> rows;
    ilias_for_await(auto rowResult, result.rangeResult()) {
        ILIAS_CO_TRY(auto row, rowResult);
        rows.push_back(std::move(row));
    }
    std::ranges::sort(rows, [](const auto &left, const auto &right) {
        const auto &[leftRelation, leftTag] = left;
        const auto &[rightRelation, rightTag] = right;
        return std::tie(leftTag.normalizedName, leftRelation.providerKey) <
               std::tie(rightTag.normalizedName, rightRelation.providerKey);
    });
    for (const auto &[relation, tag] : rows) {
        details.tags.push_back(
            {.name = fromUtf8(tag.displayName),
             .providerKey = fromUtf8(relation.providerKey),
             .weight = relation.weight});
    }

    ILIAS_CO_TRY(
        details.externalRefs,
        co_await loadSubjectExternalRefs(forms.subjectExternalRefs, subject));
    co_return details;
}

/// 按后端方言加载单个章节及其外部身份映射。
template <typename BackendTag>
auto getEpisodeFor(CatalogForms<BackendTag> &forms, EpisodeId episode)
    -> ilias::IoTask<std::optional<EpisodeDetails>> {
    if (episode.value <= 0) {
        co_return Err(std::make_error_code(std::errc::invalid_argument));
    }

    ILIAS_CO_TRY(auto stored,
                 co_await loadEpisodeRow(forms.episodes, episode));
    if (!stored) {
        co_return std::nullopt;
    }
    EpisodeDetails details = toEpisodeDetails(*stored);
    ILIAS_CO_TRY(
        details.externalRefs,
        co_await loadEpisodeExternalRefs(forms.episodeExternalRefs, episode));
    co_return details;
}

/// 按后端方言列出条目的章节并补齐外部身份映射。
template <typename BackendTag>
auto listEpisodesFor(CatalogForms<BackendTag> &forms, SubjectId subject)
    -> ilias::IoTask<std::vector<EpisodeDetails>> {
    if (subject.value <= 0) {
        co_return Err(std::make_error_code(std::errc::invalid_argument));
    }

    using Episode = schema::EpisodeRecord;
    ILIAS_CO_TRY(
        auto result,
        co_await forms.episodes.select()
            .where(forms.episodes.sql(&Episode::subjectId) == subject.value)
            .query());

    std::vector<StoredEpisodeRow> rows;
    ilias_for_await(auto rowResult, result.rangeResult()) {
        ILIAS_CO_TRY(auto row, rowResult);
        rows.push_back(std::move(row));
    }
    // ORM 当前仅保存一个 ORDER BY 表达式，复合稳定排序在完整实体上完成。
    std::ranges::sort(rows, [](const Episode &left, const Episode &right) {
        return std::tie(left.sortOrder, left.id) <
               std::tie(right.sortOrder, right.id);
    });

    // 先消费完章节结果集，再在同一连接上读取外部映射，兼容单活跃结果集驱动。
    std::vector<EpisodeDetails> episodes;
    episodes.reserve(rows.size());
    for (const auto &stored : rows) {
        EpisodeDetails details = toEpisodeDetails(stored);
        ILIAS_CO_TRY(
            details.externalRefs,
            co_await loadEpisodeExternalRefs(forms.episodeExternalRefs,
                                             details.id));
        episodes.push_back(std::move(details));
    }
    co_return episodes;
}

/// 按后端方言列出当前仍有关联关系的标签 Facet。
template <typename BackendTag>
auto listTagsFor(CatalogForms<BackendTag> &forms, const LocalTagQuery &query)
    -> ilias::IoTask<std::vector<TagFacet>> {
    if (query.limit <= 0 || query.limit > 200 || query.offset < 0) {
        co_return Err(std::make_error_code(std::errc::invalid_argument));
    }

    using SubjectTag = schema::SubjectTagRecord;
    using Tag = schema::TagRecord;
    ILIAS_CO_TRY(
        auto result,
        co_await forms.tags.join(forms.subjectTags)
            .on(forms.tags.col(&Tag::id) ==
                forms.subjectTags.col(&SubjectTag::tagId))
            .query());

    struct TagAggregate {
        Tag record;
        std::unordered_set<std::int64_t> subjectIds;
    };
    const std::string prefix = utf8(normalizedTag(query.prefix));
    std::unordered_map<std::int64_t, TagAggregate> aggregates;
    ilias_for_await(auto rowResult, result.rangeResult()) {
        ILIAS_CO_TRY(auto row, rowResult);
        auto &[tag, relation] = row;
        if (!tag.normalizedName.starts_with(prefix)) {
            continue;
        }
        auto [entry, inserted] = aggregates.try_emplace(
            tag.id, TagAggregate {.record = tag, .subjectIds = {}});
        entry->second.subjectIds.insert(relation.subjectId);
    }

    std::vector<TagAggregate> ordered;
    ordered.reserve(aggregates.size());
    for (auto &[id, aggregate] : aggregates) {
        ordered.push_back(std::move(aggregate));
    }
    std::ranges::sort(ordered, [](const TagAggregate &left,
                                  const TagAggregate &right) {
        if (left.subjectIds.size() != right.subjectIds.size()) {
            return left.subjectIds.size() > right.subjectIds.size();
        }
        return left.record.normalizedName < right.record.normalizedName;
    });

    const auto begin = std::min<std::size_t>(
        static_cast<std::size_t>(query.offset), ordered.size());
    const auto end = std::min<std::size_t>(
        begin + static_cast<std::size_t>(query.limit), ordered.size());
    std::vector<TagFacet> facets;
    facets.reserve(end - begin);
    for (auto index = begin; index < end; ++index) {
        facets.push_back(
            {.name = fromUtf8(ordered[index].record.displayName),
             .subjectCount = static_cast<std::int64_t>(
                 ordered[index].subjectIds.size())});
    }
    co_return facets;
}

/// 按后端能力搜索完整 ORM 实体，并在 C++ 值对象层完成关系过滤和分页。
template <typename BackendTag>
auto searchSubjectsFor(LocalDatabase &database,
                       CatalogForms<BackendTag> &forms,
                       const LocalSubjectQuery &query)
    -> ilias::IoTask<std::vector<SubjectSummary>> {
    if (query.limit <= 0 || query.limit > 200 || query.offset < 0) {
        co_return Err(std::make_error_code(std::errc::invalid_argument));
    }

    const QString text = query.text.trimmed();
    std::optional<std::string> tag;
    if (query.tag) {
        const QString normalized = normalizedTag(*query.tag);
        if (normalized.isEmpty()) {
            co_return Err(std::make_error_code(std::errc::invalid_argument));
        }
        tag = utf8(normalized);
    }

    std::optional<std::unordered_set<std::int64_t>> taggedSubjectIds;
    if (tag) {
        ILIAS_CO_TRY(
            auto ids,
            co_await loadSubjectIdsForTag(forms.tags, forms.subjectTags,
                                          *tag));
        if (ids.empty()) {
            co_return std::vector<SubjectSummary> {};
        }
        taggedSubjectIds = std::move(ids);
    }
    const auto acceptsTag = [&taggedSubjectIds](std::int64_t subjectId) {
        return !taggedSubjectIds ||
               taggedSubjectIds->contains(subjectId);
    };

    if constexpr (std::is_same_v<BackendTag, ilias::sql::SqliteTag>) {
        if (!text.isEmpty()) {
            ILIAS_CO_TRY(
                auto rankedIds,
                co_await searchFtsSubjectIds(database.advancedConnection(),
                                             text));

            std::vector<SubjectSummary> matches;
            matches.reserve(static_cast<std::size_t>(query.limit));
            std::size_t skipped = 0;
            for (const auto subjectId : rankedIds) {
                if (!acceptsTag(subjectId)) {
                    continue;
                }
                if (skipped < static_cast<std::size_t>(query.offset)) {
                    ++skipped;
                    continue;
                }
                ILIAS_CO_TRY(
                    auto row,
                    co_await loadSubjectRow(forms.subjects,
                                            SubjectId {subjectId}));
                if (row) {
                    matches.push_back(toSummary(*row));
                }
                if (matches.size() == static_cast<std::size_t>(query.limit)) {
                    break;
                }
            }
            co_return matches;
        }
    }

    using Subject = schema::SubjectRecord;
    std::vector<StoredSubjectRow> rows;
    if (!text.isEmpty()) {
        const std::string needle = utf8(text);
        const auto matches =
            forms.subjects.sql(&Subject::title).contains(needle) ||
            forms.subjects.sql(&Subject::titleCn).contains(needle) ||
            forms.subjects.sql(&Subject::aliasesJson).contains(needle) ||
            forms.subjects.sql(&Subject::summary).contains(needle);
        ILIAS_CO_TRY(auto result,
                     co_await forms.subjects.select().where(matches).query());
        ILIAS_CO_TRYV(
            co_await appendSubjectRows(std::move(result), rows));
    }
    else {
        ILIAS_CO_TRY(auto result, co_await forms.subjects.select().query());
        ILIAS_CO_TRYV(
            co_await appendSubjectRows(std::move(result), rows));
    }

    std::erase_if(rows, [&acceptsTag](const Subject &row) {
        return !acceptsTag(row.id);
    });
    // ORM 当前只有单列 ORDER BY；复合顺序在完整反射实体上稳定实现。
    std::ranges::sort(rows, [](const Subject &left, const Subject &right) {
        return std::tie(left.updatedAt, left.id) >
               std::tie(right.updatedAt, right.id);
    });

    const auto begin = std::min<std::size_t>(
        static_cast<std::size_t>(query.offset), rows.size());
    const auto end = std::min<std::size_t>(
        begin + static_cast<std::size_t>(query.limit), rows.size());
    std::vector<SubjectSummary> summaries;
    summaries.reserve(end - begin);
    for (auto index = begin; index < end; ++index) {
        summaries.push_back(toSummary(rows[index]));
    }
    co_return summaries;
}

} // namespace

struct CatalogStore::State {
    std::variant<std::monostate, CatalogForms<ilias::sql::SqliteTag>,
                 CatalogForms<ilias::sql::MysqlTag>>
        forms;
};

CatalogStore::~CatalogStore() = default;
CatalogStore::CatalogStore(LocalDatabase &database) : mDatabase(database) {}
CatalogStore::CatalogStore(CatalogStore &&) noexcept = default;

auto CatalogStore::ensureForms() -> ilias::IoTask<void> {
    if (mState && !std::holds_alternative<std::monostate>(mState->forms)) {
        co_return {};
    }

    AL_LOG_DEBUG("[database.catalog] attaching ORM forms backend={}",
                 mDatabase.backendName());
    auto state = std::make_unique<State>();
    if (mDatabase.backend() == DatabaseBackend::Sqlite) {
        ILIAS_CO_TRY(
            auto forms,
            co_await CatalogForms<ilias::sql::SqliteTag>::attach(
                mDatabase.advancedConnection()));
        state->forms.template emplace<
            CatalogForms<ilias::sql::SqliteTag>>(std::move(forms));
    }
    else {
        ILIAS_CO_TRY(
            auto forms,
            co_await CatalogForms<ilias::sql::MysqlTag>::attach(
                mDatabase.advancedConnection()));
        state->forms.template emplace<CatalogForms<ilias::sql::MysqlTag>>(
            std::move(forms));
    }
    mState = std::move(state);
    AL_LOG_INFO("[database.catalog] ORM forms attached backend={}",
                mDatabase.backendName());
    co_return {};
}

auto CatalogStore::upsertSubjectSnapshot(SubjectSnapshot snapshot)
    -> ilias::IoTask<SubjectId> {
    ILIAS_CO_TRYV(co_await ensureForms());
    AL_LOG_INFO(
        "[database.catalog] upsert started backend={} provider={} "
        "metadata_level={} aliases_supplied={} tags_supplied={}",
        mDatabase.backendName(), utf8(snapshot.origin.providerKey),
        static_cast<std::int64_t>(snapshot.metadataLevel),
        snapshot.aliases.has_value(), snapshot.tags.has_value());

    ilias::IoResult<SubjectId> result;
    if (mDatabase.backend() == DatabaseBackend::Sqlite) {
        auto &forms =
            std::get<CatalogForms<ilias::sql::SqliteTag>>(mState->forms);
        result = co_await upsertSubjectSnapshotFor<ilias::sql::SqliteTag>(
            mDatabase, forms, std::move(snapshot));
    }
    else {
        auto &forms =
            std::get<CatalogForms<ilias::sql::MysqlTag>>(mState->forms);
        result = co_await upsertSubjectSnapshotFor<ilias::sql::MysqlTag>(
            mDatabase, forms, std::move(snapshot));
    }
    if (!result) {
        AL_LOG_ERROR("[database.catalog] upsert failed backend={} error={}",
                     mDatabase.backendName(), result.error().message());
        co_return Err(result.error());
    }
    AL_LOG_INFO(
        "[database.catalog] upsert completed backend={} subject_id={}",
        mDatabase.backendName(), result->value);
    co_return *result;
}

auto CatalogStore::upsertEpisodeSnapshots(
    SubjectId subject, std::vector<EpisodeSnapshot> snapshots)
    -> ilias::IoTask<std::vector<EpisodeId>> {
    ILIAS_CO_TRYV(co_await ensureForms());
    AL_LOG_INFO(
        "[database.catalog] episode upsert started backend={} subject_id={} "
        "count={}",
        mDatabase.backendName(), subject.value, snapshots.size());

    ilias::IoResult<std::vector<EpisodeId>> result;
    if (mDatabase.backend() == DatabaseBackend::Sqlite) {
        auto &forms =
            std::get<CatalogForms<ilias::sql::SqliteTag>>(mState->forms);
        result = co_await upsertEpisodeSnapshotsFor<ilias::sql::SqliteTag>(
            mDatabase, forms, subject, std::move(snapshots));
    }
    else {
        auto &forms =
            std::get<CatalogForms<ilias::sql::MysqlTag>>(mState->forms);
        result = co_await upsertEpisodeSnapshotsFor<ilias::sql::MysqlTag>(
            mDatabase, forms, subject, std::move(snapshots));
    }
    if (!result) {
        AL_LOG_ERROR(
            "[database.catalog] episode upsert failed backend={} "
            "subject_id={} error={}",
            mDatabase.backendName(), subject.value, result.error().message());
        co_return Err(result.error());
    }
    AL_LOG_INFO(
        "[database.catalog] episode upsert completed backend={} subject_id={} "
        "count={}",
        mDatabase.backendName(), subject.value, result->size());
    co_return std::move(*result);
}

auto CatalogStore::replaceSubjectTags(
    SubjectId subject, QString providerKey,
    std::vector<SubjectTagSnapshot> tags) -> ilias::IoTask<void> {
    ILIAS_CO_TRYV(co_await ensureForms());
    AL_LOG_INFO(
        "[database.catalog] explicit tag replacement started backend={} "
        "subject_id={} count={}",
        mDatabase.backendName(), subject.value, tags.size());

    ilias::IoResult<void> result;
    if (mDatabase.backend() == DatabaseBackend::Sqlite) {
        auto &forms =
            std::get<CatalogForms<ilias::sql::SqliteTag>>(mState->forms);
        result = co_await replaceSubjectTagsFor<ilias::sql::SqliteTag>(
            mDatabase, forms, subject, providerKey, tags);
    }
    else {
        auto &forms =
            std::get<CatalogForms<ilias::sql::MysqlTag>>(mState->forms);
        result = co_await replaceSubjectTagsFor<ilias::sql::MysqlTag>(
            mDatabase, forms, subject, providerKey, tags);
    }
    if (!result) {
        AL_LOG_ERROR(
            "[database.catalog] explicit tag replacement failed backend={} "
            "subject_id={} error={}",
            mDatabase.backendName(), subject.value, result.error().message());
        co_return Err(result.error());
    }
    AL_LOG_INFO(
        "[database.catalog] explicit tag replacement completed backend={} "
        "subject_id={} count={}",
        mDatabase.backendName(), subject.value, tags.size());
    co_return {};
}

auto CatalogStore::findSubjectByExternalRef(const ExternalRef &ref)
    -> ilias::IoTask<std::optional<SubjectId>> {
    ILIAS_CO_TRYV(co_await ensureForms());
    AL_LOG_DEBUG(
        "[database.catalog] external lookup started backend={} provider={}",
        mDatabase.backendName(), utf8(ref.providerKey));

    ilias::IoResult<std::optional<SubjectId>> result;
    if (mDatabase.backend() == DatabaseBackend::Sqlite) {
        auto &forms =
            std::get<CatalogForms<ilias::sql::SqliteTag>>(mState->forms);
        result = co_await findSubjectByExternalRefFor(forms, ref);
    }
    else {
        auto &forms =
            std::get<CatalogForms<ilias::sql::MysqlTag>>(mState->forms);
        result = co_await findSubjectByExternalRefFor(forms, ref);
    }
    if (!result) {
        AL_LOG_WARN(
            "[database.catalog] external lookup failed backend={} error={}",
            mDatabase.backendName(), result.error().message());
        co_return Err(result.error());
    }
    AL_LOG_DEBUG(
        "[database.catalog] external lookup completed backend={} found={}",
        mDatabase.backendName(), result->has_value());
    co_return std::move(*result);
}

auto CatalogStore::findEpisodeByExternalRef(const ExternalRef &ref)
    -> ilias::IoTask<std::optional<EpisodeId>> {
    ILIAS_CO_TRYV(co_await ensureForms());
    AL_LOG_DEBUG(
        "[database.catalog] episode external lookup started backend={} "
        "provider={}",
        mDatabase.backendName(), utf8(ref.providerKey));

    ilias::IoResult<std::optional<EpisodeId>> result;
    if (mDatabase.backend() == DatabaseBackend::Sqlite) {
        auto &forms =
            std::get<CatalogForms<ilias::sql::SqliteTag>>(mState->forms);
        result = co_await findEpisodeByExternalRefFor(forms, ref);
    }
    else {
        auto &forms =
            std::get<CatalogForms<ilias::sql::MysqlTag>>(mState->forms);
        result = co_await findEpisodeByExternalRefFor(forms, ref);
    }
    if (!result) {
        AL_LOG_WARN(
            "[database.catalog] episode external lookup failed backend={} "
            "error={}",
            mDatabase.backendName(), result.error().message());
        co_return Err(result.error());
    }
    AL_LOG_DEBUG(
        "[database.catalog] episode external lookup completed backend={} "
        "found={}",
        mDatabase.backendName(), result->has_value());
    co_return std::move(*result);
}

auto CatalogStore::getSubject(SubjectId subject)
    -> ilias::IoTask<std::optional<SubjectDetails>> {
    ILIAS_CO_TRYV(co_await ensureForms());
    AL_LOG_DEBUG(
        "[database.catalog] detail lookup started backend={} subject_id={}",
        mDatabase.backendName(), subject.value);

    ilias::IoResult<std::optional<SubjectDetails>> result;
    if (mDatabase.backend() == DatabaseBackend::Sqlite) {
        auto &forms =
            std::get<CatalogForms<ilias::sql::SqliteTag>>(mState->forms);
        result = co_await getSubjectFor(forms, subject);
    }
    else {
        auto &forms =
            std::get<CatalogForms<ilias::sql::MysqlTag>>(mState->forms);
        result = co_await getSubjectFor(forms, subject);
    }
    if (!result) {
        AL_LOG_WARN(
            "[database.catalog] detail lookup failed backend={} subject_id={} "
            "error={}",
            mDatabase.backendName(), subject.value, result.error().message());
        co_return Err(result.error());
    }
    AL_LOG_DEBUG(
        "[database.catalog] detail lookup completed backend={} subject_id={} "
        "found={}",
        mDatabase.backendName(), subject.value, result->has_value());
    co_return std::move(*result);
}

auto CatalogStore::getEpisode(EpisodeId episode)
    -> ilias::IoTask<std::optional<EpisodeDetails>> {
    ILIAS_CO_TRYV(co_await ensureForms());
    AL_LOG_DEBUG(
        "[database.catalog] episode detail lookup started backend={} "
        "episode_id={}",
        mDatabase.backendName(), episode.value);

    ilias::IoResult<std::optional<EpisodeDetails>> result;
    if (mDatabase.backend() == DatabaseBackend::Sqlite) {
        auto &forms =
            std::get<CatalogForms<ilias::sql::SqliteTag>>(mState->forms);
        result = co_await getEpisodeFor(forms, episode);
    }
    else {
        auto &forms =
            std::get<CatalogForms<ilias::sql::MysqlTag>>(mState->forms);
        result = co_await getEpisodeFor(forms, episode);
    }
    if (!result) {
        AL_LOG_WARN(
            "[database.catalog] episode detail lookup failed backend={} "
            "episode_id={} error={}",
            mDatabase.backendName(), episode.value, result.error().message());
        co_return Err(result.error());
    }
    AL_LOG_DEBUG(
        "[database.catalog] episode detail lookup completed backend={} "
        "episode_id={} found={}",
        mDatabase.backendName(), episode.value, result->has_value());
    co_return std::move(*result);
}

auto CatalogStore::listEpisodes(SubjectId subject)
    -> ilias::IoTask<std::vector<EpisodeDetails>> {
    ILIAS_CO_TRYV(co_await ensureForms());
    AL_LOG_INFO(
        "[database.catalog] episode list started backend={} subject_id={}",
        mDatabase.backendName(), subject.value);

    ilias::IoResult<std::vector<EpisodeDetails>> result;
    if (mDatabase.backend() == DatabaseBackend::Sqlite) {
        auto &forms =
            std::get<CatalogForms<ilias::sql::SqliteTag>>(mState->forms);
        result = co_await listEpisodesFor(forms, subject);
    }
    else {
        auto &forms =
            std::get<CatalogForms<ilias::sql::MysqlTag>>(mState->forms);
        result = co_await listEpisodesFor(forms, subject);
    }
    if (!result) {
        AL_LOG_WARN(
            "[database.catalog] episode list failed backend={} subject_id={} "
            "error={}",
            mDatabase.backendName(), subject.value, result.error().message());
        co_return Err(result.error());
    }
    AL_LOG_INFO(
        "[database.catalog] episode list completed backend={} subject_id={} "
        "returned={}",
        mDatabase.backendName(), subject.value, result->size());
    co_return std::move(*result);
}

auto CatalogStore::searchSubjects(const LocalSubjectQuery &query)
    -> ilias::IoTask<std::vector<SubjectSummary>> {
    ILIAS_CO_TRYV(co_await ensureForms());
    AL_LOG_INFO(
        "[database.catalog] search started backend={} has_text={} has_tag={} "
        "limit={} offset={}",
        mDatabase.backendName(), !query.text.trimmed().isEmpty(),
        query.tag.has_value(), query.limit, query.offset);

    ilias::IoResult<std::vector<SubjectSummary>> result;
    if (mDatabase.backend() == DatabaseBackend::Sqlite) {
        auto &forms =
            std::get<CatalogForms<ilias::sql::SqliteTag>>(mState->forms);
        result = co_await searchSubjectsFor(mDatabase, forms, query);
    }
    else {
        auto &forms =
            std::get<CatalogForms<ilias::sql::MysqlTag>>(mState->forms);
        result = co_await searchSubjectsFor(mDatabase, forms, query);
    }
    if (!result) {
        AL_LOG_WARN("[database.catalog] search failed backend={} error={}",
                    mDatabase.backendName(), result.error().message());
        co_return Err(result.error());
    }
    AL_LOG_INFO(
        "[database.catalog] search completed backend={} returned={} limit={} "
        "offset={}",
        mDatabase.backendName(), result->size(), query.limit, query.offset);
    co_return std::move(*result);
}

auto CatalogStore::listTags(const LocalTagQuery &query)
    -> ilias::IoTask<std::vector<TagFacet>> {
    ILIAS_CO_TRYV(co_await ensureForms());
    AL_LOG_INFO(
        "[database.catalog] tag list started backend={} has_prefix={} "
        "limit={} offset={}",
        mDatabase.backendName(), !normalizedTag(query.prefix).isEmpty(),
        query.limit, query.offset);

    ilias::IoResult<std::vector<TagFacet>> result;
    if (mDatabase.backend() == DatabaseBackend::Sqlite) {
        auto &forms =
            std::get<CatalogForms<ilias::sql::SqliteTag>>(mState->forms);
        result = co_await listTagsFor(forms, query);
    }
    else {
        auto &forms =
            std::get<CatalogForms<ilias::sql::MysqlTag>>(mState->forms);
        result = co_await listTagsFor(forms, query);
    }
    if (!result) {
        AL_LOG_WARN("[database.catalog] tag list failed backend={} error={}",
                    mDatabase.backendName(), result.error().message());
        co_return Err(result.error());
    }
    AL_LOG_INFO(
        "[database.catalog] tag list completed backend={} returned={} "
        "limit={} offset={}",
        mDatabase.backendName(), result->size(), query.limit, query.offset);
    co_return std::move(*result);
}

} // namespace anime_land::persistence
