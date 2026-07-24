#include "persistence/catalog_store.hpp"

#include "common/log.hpp"
#include "common/qt_serialization.hpp"
#include "persistence/database_schema.hpp"

#include <ilias/sql_orm/orm_form.hpp>
#include <nekoproto/serialization/json/rapid_json_serializer.hpp>

#include <QTimeZone>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace anime_land::persistence {
namespace {

using ilias::Err;
using ilias::IoTask;
using ilias::sql::Form;
using ilias::sql::SqlDatabase;
using ilias::sql::SqlResult;
using ilias::sql::SqliteTag;
using ilias::sql::MysqlTag;

using StoredSubjectRow = schema::SubjectRecord;
using StoredEpisodeRow = schema::EpisodeRecord;
using SubjectExternalRefRecord = schema::SubjectExternalRefRecord;
using TagRecord = schema::TagRecord;
using SubjectTagRecord = schema::SubjectTagRecord;
using EpisodeRecord = schema::EpisodeRecord;
using EpisodeExternalRefRecord = schema::EpisodeExternalRefRecord;
/**
 * @brief 一个 CatalogStore 在指定后端上长期持有的关系 Form 集合。
 *
 * create() 直接在 Store 所依赖的长生命周期 SqlDatabase 上取得所有 Form；
 * State 接管后复用这些 Form，不做第二次 attach 或探测查询。
 */
template <typename BackendTag>
struct Forms {
    using Database = SqlDatabase;
    template <typename T>
    using FormT = Form<T, BackendTag, Database>;

    FormT<SubjectRecord> subjects;
    FormT<SubjectExternalRefRecord> subjectExternalRefs;
    FormT<TagRecord> tags;
    FormT<SubjectTagRecord> subjectTags;
    FormT<EpisodeRecord> episodes;
    FormT<EpisodeExternalRefRecord> episodeExternalRefs;

    static auto create(Database &db) -> IoTask<Forms> {
        ILIAS_CO_TRY(
            auto subjects,
            co_await FormT<SubjectRecord>::create_if_not_exists(db, schema::table::subjects));
        ILIAS_CO_TRY(
            auto subjectExternalRefs,
            co_await FormT<SubjectExternalRefRecord>::create_if_not_exists(db, schema::table::subjectExternalRefs));
        ILIAS_CO_TRY(
            auto tags,
            co_await FormT<TagRecord>::create_if_not_exists(db, schema::table::tags));
        ILIAS_CO_TRY(
            auto subjectTags,
            co_await FormT<SubjectTagRecord>::create_if_not_exists(db, schema::table::subjectTags));
        ILIAS_CO_TRY(
            auto episodes,
            co_await FormT<EpisodeRecord>::create_if_not_exists(db, schema::table::episodes));
        ILIAS_CO_TRY(
            auto episodeExternalRefs,
            co_await FormT<EpisodeExternalRefRecord>::create_if_not_exists(db, schema::table::episodeExternalRefs));
        co_return Forms {
            .subjects = std::move(subjects),
            .subjectExternalRefs = std::move(subjectExternalRefs),
            .tags = std::move(tags),
            .subjectTags = std::move(subjectTags),
            .episodes = std::move(episodes),
            .episodeExternalRefs = std::move(episodeExternalRefs),
        };
    }
};

/// 生成用于标签唯一键和精确查询的 Unicode 兼容规范化名称。
auto normalizeTagName(const QString &value) -> QString {
    return value.normalized(QString::NormalizationForm_KC)
        .trimmed()
        .toCaseFolded();
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

/// 使用长期 Form 按外部身份查找条目主键。
template <typename ExternalRefForm>
auto findExternal(ExternalRefForm &form, const ExternalRef &ref)
    -> IoTask<std::optional<SubjectId>> {
    using Record = schema::SubjectExternalRefRecord;
    ILIAS_CO_TRY(
        auto result,
        co_await form.select()
            .where(form.sql(&Record::providerKey) == ref.providerKey.toStdString() &&
                   form.sql(&Record::externalId) == ref.externalId.toStdString())
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
    -> IoTask<std::optional<EpisodeId>> {
    using Record = schema::EpisodeExternalRefRecord;
    ILIAS_CO_TRY(
        auto result,
        co_await form.select()
            .where(form.sql(&Record::providerKey) == ref.providerKey.toStdString() &&
                   form.sql(&Record::externalId) == ref.externalId.toStdString())
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
    -> IoTask<std::optional<StoredSubjectRow>> {
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
    -> IoTask<std::optional<StoredEpisodeRow>> {
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
    requires requires(Record row) {
        { row.providerKey } -> std::same_as<std::string &>;
        { row.externalId } -> std::same_as<std::string &>;
        { row.remoteUpdatedAt } -> std::same_as<std::optional<std::int64_t> &>;
    }
auto toExternalRef(const Record &row) -> StoredExternalRef {
    return {
        .ref = {
            .providerKey = QString::fromStdString(row.providerKey),
            .externalId = QString::fromStdString(row.externalId),
        },
        .fetchedAt = QDateTime::fromMSecsSinceEpoch(row.fetchedAt, QTimeZone::UTC),
        .remoteUpdatedAt = row.remoteUpdatedAt.transform([](std::int64_t value) {
            return QDateTime::fromMSecsSinceEpoch(value, QTimeZone::UTC);
        }),
    };
}

/// 使用长期 Form 加载条目的全部外部身份映射。
template <typename ExternalRefForm>
auto loadSubjectExternalRefs(ExternalRefForm &form, SubjectId subject)
    -> IoTask<std::vector<StoredExternalRef>> {
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
    -> IoTask<std::vector<StoredExternalRef>> {
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
        .title = QString::fromStdString(row.title),
        .titleCn = row.titleCn.transform(
            [](const auto &value) { return QString::fromStdString(value); }),
        .summary = row.summary.transform(
            [](const auto &value) { return QString::fromStdString(value); }),
        .metadataLevel =
            static_cast<SubjectMetadataLevel>(row.metadataLevel),
        .updatedAt =
            QDateTime::fromMSecsSinceEpoch(row.updatedAt, QTimeZone::UTC),
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
        .title = row.title.transform(
            [](const auto &value) { return QString::fromStdString(value); }),
        .titleCn = row.titleCn.transform(
            [](const auto &value) { return QString::fromStdString(value); }),
        .summary = row.summary.transform(
            [](const auto &value) { return QString::fromStdString(value); }),
        .airDate = std::nullopt,
        .duration = row.durationMs.transform([](std::int64_t value) {
            return std::chrono::milliseconds(value);
        }),
        .externalRefs = {},
        .updatedAt =
            QDateTime::fromMSecsSinceEpoch(row.updatedAt, QTimeZone::UTC),
    };
    if (row.airDate) {
        const QDate date = QDate::fromString(
            QString::fromStdString(*row.airDate), Qt::ISODate);
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
                         std::int64_t updatedAt) -> IoTask<void> {
    using SubjectTag = schema::SubjectTagRecord;
    using Tag = schema::TagRecord;

    const std::string providerKey = provider.toStdString();
    AL_LOG_DEBUG(
        "[database.catalog] tag replacement started subject_id={} count={}",
        subject.value, tags.size());
    ILIAS_CO_TRYV(
        co_await subjectTags.remove()
            .where(subjectTags.sql(&SubjectTag::subjectId) == subject.value &&
                   subjectTags.sql(&SubjectTag::providerKey) == providerKey)
            .execute());

    for (const auto &tag : tags) {
        const QString displayName = tag.name.normalized(QString::NormalizationForm_KC).trimmed();
        const std::string normalized = normalizeTagName(displayName).toStdString();
        const std::string displayed = displayName.toStdString();

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
                    subjectTags.sql(&SubjectTag::weight) = std::optional<double>(tag.weight),
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

/// 消费 ORM 的完整 subjects 结果，确保字段变更继续由反射映射而非手工列清单维护。
auto appendSubjectRows(SqlResult<StoredSubjectRow> result,
                       std::vector<StoredSubjectRow> &subjects)
    -> IoTask<void> {
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
    -> IoTask<std::unordered_set<std::int64_t>> {
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

} // namespace

namespace {

/// 按指定后端方言原子写入条目快照及其所有关系投影。
template <typename BackendTag>
auto upsertSubject(LocalDatabase &database, Forms<BackendTag> &forms,
                   SubjectSnapshot snapshot)
    -> IoTask<SubjectId> {
    if (snapshot.origin.providerKey.trimmed().isEmpty() ||
        snapshot.origin.externalId.trimmed().isEmpty()) {
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
                 Form<Subject, BackendTag>::bind(
                     transaction, forms.subjects.getTableName()));

    ILIAS_CO_TRY(auto externalRefs,
                 Form<SubjectExternalRef, BackendTag>::bind(
                     transaction,
                     forms.subjectExternalRefs.getTableName()));

    ILIAS_CO_TRY(auto existing,
                 co_await findExternal(externalRefs, snapshot.origin));

    SubjectId subject;
    const std::int64_t writtenAt = QDateTime::currentMSecsSinceEpoch();
    if (existing) {
        subject = *existing;
    }
    else {
        // 创建一条空数据，生成主键
        ILIAS_CO_TRYV(
            co_await subjects.insert()
                .set(subjects.sql(&Subject::subjectType) = static_cast<std::int64_t>(snapshot.subjectType),
                     subjects.sql(&Subject::title) = snapshot.title.toStdString(),
                     subjects.sql(&Subject::aliasesJson) = std::string("[]"),
                     subjects.sql(&Subject::createdAt) = writtenAt,
                     subjects.sql(&Subject::updatedAt) = writtenAt)
                .execute());
        ILIAS_CO_TRY(auto connection, transaction.connection());
        subject = SubjectId {connection->lastInsertId()};
    }

    const std::int64_t metadataLevel = static_cast<std::int64_t>(snapshot.metadataLevel);
    auto metadataRefreshedAt =
        (snapshot.metadataLevel == SubjectMetadataLevel::Details)
            ? std::optional<std::int64_t> {
                  snapshot.fetchedAt.toMSecsSinceEpoch()}
            : std::nullopt;

    auto titleCn = snapshot.titleCn.transform(
        [](const QString &value) { return value.toStdString(); });
    auto summary = snapshot.summary.transform(
        [](const QString &value) { return value.toStdString(); });
    auto airDate = snapshot.airDate.transform([](const QDate &value) {
        return value.toString(Qt::ISODate).toStdString();
    });
    auto coverUrl = snapshot.coverUrl.transform([](const QUrl &value) {
        return value.toString(QUrl::FullyEncoded).toStdString();
    });

    // COALESCE 保留摘要响应中未提供的详情字段；GREATEST 防止完整度降级。
    ILIAS_CO_TRYV(
        co_await subjects.update()
            .set(subjects.sql(&Subject::subjectType) = static_cast<std::int64_t>(snapshot.subjectType),
                 subjects.sql(&Subject::title) = snapshot.title.toStdString(),
                 subjects.assignCoalesced(subjects.sql(&Subject::titleCn), std::move(titleCn)),
                 subjects.assignCoalesced(subjects.sql(&Subject::summary), std::move(summary)),
                 subjects.assignCoalesced(subjects.sql(&Subject::airDate), std::move(airDate)),
                 subjects.assignCoalesced(subjects.sql(&Subject::coverUrl), std::move(coverUrl)),
                 subjects.assignCoalesced(subjects.sql(&Subject::aliasesJson), std::optional<std::string>(aliasesJson)),
                 subjects.sql(&Subject::updatedAt) = writtenAt,
                 subjects.assignGreatest(subjects.sql(&Subject::metadataLevel), metadataLevel),
                 subjects.assignCoalesced(subjects.sql(&Subject::metadataRefreshedAt), std::move(metadataRefreshedAt)))
            .where(subjects.sql(&Subject::id) == subject.value)
            .execute());

    ILIAS_CO_TRYV(
        co_await externalRefs.upsert()
            .values(
                externalRefs.sql(&SubjectExternalRef::subjectId) = subject.value,
                externalRefs.sql(&SubjectExternalRef::providerKey) = snapshot.origin.providerKey.toStdString(),
                externalRefs.sql(&SubjectExternalRef::externalId) = snapshot.origin.externalId.toStdString(),
                externalRefs.sql(&SubjectExternalRef::fetchedAt) = static_cast<std::int64_t>(snapshot.fetchedAt.toMSecsSinceEpoch()),
                externalRefs.sql(&SubjectExternalRef::remoteUpdatedAt) = snapshot.remoteUpdatedAt.transform(
                    [](const QDateTime &value) {
                        return static_cast<std::int64_t>(
                            value.toMSecsSinceEpoch());
                    }))
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
                     Form<SubjectTag, BackendTag>::bind(transaction, forms.subjectTags.getTableName()));
        ILIAS_CO_TRY(auto tags,
                     Form<Tag, BackendTag>::bind(transaction, forms.tags.getTableName()));
        ILIAS_CO_TRYV(co_await replaceProviderTags(
            subjectTags,
            tags,
            subject,
            snapshot.origin.providerKey,
            *snapshot.tags,
            writtenAt));
    }

    // 事务提交前任何一步失败都会回滚条目、外部映射和标签。
    ILIAS_CO_TRYV(co_await transaction.commit());
    co_return subject;
}

/// 按指定后端方言原子写入一组章节快照。
template <typename BackendTag>
auto upsertEpisodes(LocalDatabase &database, Forms<BackendTag> &forms,
                    SubjectId subject,
                    std::vector<EpisodeSnapshot> snapshots)
    -> IoTask<std::vector<EpisodeId>> {
    if (std::ranges::any_of(
            snapshots, [](const EpisodeSnapshot &snapshot) {
                return snapshot.origin.providerKey.trimmed().isEmpty() ||
                       snapshot.origin.externalId.trimmed().isEmpty();
            })) {
        co_return Err(std::make_error_code(std::errc::invalid_argument));
    }

    ILIAS_CO_TRY(auto transaction, co_await database.advancedConnection().transaction());
    using Subject = schema::SubjectRecord;
    ILIAS_CO_TRY(auto subjects,
                 Form<Subject, BackendTag>::bind(transaction, forms.subjects.getTableName()));
    ILIAS_CO_TRY(auto parent, co_await loadSubjectRow(subjects, subject));
    if (!parent) {
        co_return Err(
            std::make_error_code(std::errc::no_such_file_or_directory));
    }

    using Episode = schema::EpisodeRecord;
    using EpisodeExternalRef = schema::EpisodeExternalRefRecord;
    ILIAS_CO_TRY(auto episodes,
                 Form<Episode, BackendTag>::bind(transaction, forms.episodes.getTableName()));
    ILIAS_CO_TRY(auto externalRefs,
                 Form<EpisodeExternalRef, BackendTag>::bind(transaction, forms.episodeExternalRefs.getTableName()));

    std::vector<EpisodeId> ids;
    ids.reserve(snapshots.size());
    const std::int64_t writtenAt = QDateTime::currentMSecsSinceEpoch();
    for (const auto &snapshot : snapshots) {
        ILIAS_CO_TRY(
            auto existing,
            co_await findEpisodeExternal(externalRefs, snapshot.origin));

        EpisodeId episode;
        if (existing) {
            episode = *existing;
            ILIAS_CO_TRY(auto stored, co_await loadEpisodeRow(episodes, episode));
            if (!stored || stored->subjectId != subject.value) {
                co_return Err(std::make_error_code(std::errc::operation_not_permitted));
            }
        }
        else {
            ILIAS_CO_TRYV(
                co_await episodes.insert()
                    .set(episodes.sql(&Episode::subjectId) = subject.value,
                         episodes.sql(&Episode::sortOrder) = static_cast<std::int64_t>(snapshot.sortOrder),
                         episodes.sql(&Episode::episodeType) = static_cast<std::int64_t>(snapshot.episodeType),
                         episodes.sql(&Episode::createdAt) = writtenAt,
                         episodes.sql(&Episode::updatedAt) = writtenAt)
                    .execute());
            ILIAS_CO_TRY(auto connection, transaction.connection());
            episode = EpisodeId {connection->lastInsertId()};
        }

        auto title = snapshot.title.transform(
            [](const QString &value) { return value.toStdString(); });
        auto titleCn = snapshot.titleCn.transform(
            [](const QString &value) { return value.toStdString(); });
        auto summary = snapshot.summary.transform(
            [](const QString &value) { return value.toStdString(); });
        auto airDate = snapshot.airDate.transform(
            [](const QDate &value) {
                return value.toString(Qt::ISODate).toStdString();
            });
        auto duration = snapshot.duration.transform(
            [](std::chrono::milliseconds value) {
                return static_cast<std::int64_t>(value.count());
            });
        auto episodeNumber = snapshot.episodeNumber;

        ILIAS_CO_TRYV(
            co_await episodes.update()
                .set(
                    episodes.sql(&Episode::sortOrder) = static_cast<std::int64_t>(snapshot.sortOrder),
                    episodes.sql(&Episode::episodeType) = static_cast<std::int64_t>(snapshot.episodeType),
                    episodes.sql(&Episode::episodeNumber) = std::move(episodeNumber),
                    episodes.sql(&Episode::title) = std::move(title),
                    episodes.sql(&Episode::titleCn) = std::move(titleCn),
                    episodes.sql(&Episode::summary) = std::move(summary),
                    episodes.sql(&Episode::airDate) = std::move(airDate),
                    episodes.sql(&Episode::durationMs) = std::move(duration),
                    episodes.sql(&Episode::updatedAt) = writtenAt)
                .where(episodes.sql(&Episode::id) == episode.value)
                .execute());

        ILIAS_CO_TRYV(
            co_await externalRefs.upsert()
                .values(
                    externalRefs.sql(&EpisodeExternalRef::episodeId) = episode.value,
                    externalRefs.sql(&EpisodeExternalRef::providerKey) = snapshot.origin.providerKey.toStdString(),
                    externalRefs.sql(&EpisodeExternalRef::externalId) = snapshot.origin.externalId.toStdString(),
                    externalRefs.sql(&EpisodeExternalRef::fetchedAt) = static_cast<std::int64_t>(snapshot.fetchedAt.toMSecsSinceEpoch()),
                    externalRefs.sql(&EpisodeExternalRef::remoteUpdatedAt) = snapshot.remoteUpdatedAt.transform(
                        [](const QDateTime &value) {
                            return static_cast<std::int64_t>(
                                value.toMSecsSinceEpoch());
                        }))
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
auto replaceTags(LocalDatabase &database, Forms<BackendTag> &forms,
                 SubjectId subject, const QString &providerKey,
                 const std::vector<SubjectTagSnapshot> &tags)
    -> IoTask<void> {
    if (providerKey.trimmed().isEmpty()) {
        co_return Err(std::make_error_code(std::errc::invalid_argument));
    }

    ILIAS_CO_TRY(auto transaction,
                 co_await database.advancedConnection().transaction());
    using Subject = schema::SubjectRecord;
    using SubjectTag = schema::SubjectTagRecord;
    using Tag = schema::TagRecord;
    ILIAS_CO_TRY(auto subjects,
                 Form<Subject, BackendTag>::bind(transaction, forms.subjects.getTableName()));
    ILIAS_CO_TRY(auto subjectTags,
                 Form<SubjectTag, BackendTag>::bind(transaction, forms.subjectTags.getTableName()));
    ILIAS_CO_TRY(auto tagRecords,
                 Form<Tag, BackendTag>::bind(transaction, forms.tags.getTableName()));
    ILIAS_CO_TRY(
        auto stored, co_await loadSubjectRow(subjects, subject));
    if (!stored) {
        co_return Err(std::make_error_code(std::errc::no_such_file_or_directory));
    }
    ILIAS_CO_TRYV(co_await replaceProviderTags(subjectTags, tagRecords, subject, providerKey, tags,
                                               QDateTime::currentMSecsSinceEpoch()));
    ILIAS_CO_TRYV(co_await transaction.commit());
    co_return {};
}

/// 按后端方言查找外部身份。
template <typename BackendTag>
auto findSubject(Forms<BackendTag> &forms, const ExternalRef &ref)
    -> IoTask<std::optional<SubjectId>> {
    return findExternal(forms.subjectExternalRefs, ref);
}

/// 按后端方言查找章节外部身份。
template <typename BackendTag>
auto findEpisode(Forms<BackendTag> &forms, const ExternalRef &ref)
    -> IoTask<std::optional<EpisodeId>> {
    return findEpisodeExternal(forms.episodeExternalRefs, ref);
}

/// 按后端方言加载条目主记录与标签关系。
template <typename BackendTag>
auto subjectDetails(Forms<BackendTag> &forms, SubjectId subject)
    -> IoTask<std::optional<SubjectDetails>> {
    ILIAS_CO_TRY(auto stored, co_await loadSubjectRow(forms.subjects, subject));
    if (!stored) {
        co_return std::nullopt;
    }

    SubjectDetails details;
    details.summary = toSummary(*stored);
    if (stored->airDate) {
        const QDate date = QDate::fromString(QString::fromStdString(*stored->airDate), Qt::ISODate);
        if (date.isValid()) {
            details.airDate = date;
        }
    }
    if (stored->coverUrl) {
        const QUrl url(QString::fromStdString(*stored->coverUrl));
        if (url.isValid()) {
            details.coverUrl = url;
        }
    }
    if (!decodeAliases(stored->aliasesJson, details.aliases)) {
        co_return Err(std::make_error_code(std::errc::illegal_byte_sequence));
    }
    if (stored->metadataRefreshedAt) {
        details.metadataRefreshedAt = QDateTime::fromMSecsSinceEpoch(
            *stored->metadataRefreshedAt, QTimeZone::UTC);
    }

    using SubjectTag = schema::SubjectTagRecord;
    using Tag = schema::TagRecord;
    ILIAS_CO_TRY(
        auto result,
        co_await forms.subjectTags.join(forms.tags)
            .on(forms.subjectTags.col(&SubjectTag::tagId) == forms.tags.col(&Tag::id))
            .where(forms.subjectTags.col(&SubjectTag::subjectId) == subject.value)
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
            {.name = QString::fromStdString(tag.displayName),
             .providerKey = QString::fromStdString(relation.providerKey),
             .weight = relation.weight});
    }

    ILIAS_CO_TRY(
        details.externalRefs,
        co_await loadSubjectExternalRefs(forms.subjectExternalRefs, subject));
    co_return details;
}

/// 按后端方言加载单个章节及其外部身份映射。
template <typename BackendTag>
auto episodeDetails(Forms<BackendTag> &forms, EpisodeId episode)
    -> IoTask<std::optional<EpisodeDetails>> {
    ILIAS_CO_TRY(auto stored, co_await loadEpisodeRow(forms.episodes, episode));
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
auto loadEpisodes(Forms<BackendTag> &forms, SubjectId subject)
    -> IoTask<std::vector<EpisodeDetails>> {
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
auto loadTags(Forms<BackendTag> &forms, const LocalTagQuery &query)
    -> IoTask<std::vector<TagFacet>> {
    if (query.limit <= 0 || query.limit > 200 || query.offset < 0) {
        co_return Err(std::make_error_code(std::errc::invalid_argument));
    }

    using SubjectTag = schema::SubjectTagRecord;
    using Tag = schema::TagRecord;
    ILIAS_CO_TRY(
        auto result,
        co_await forms.tags.join(forms.subjectTags)
            .on(forms.tags.col(&Tag::id) == forms.subjectTags.col(&SubjectTag::tagId))
            .query());

    struct TagAggregate {
        Tag record;
        std::unordered_set<std::int64_t> subjectIds;
    };
    const std::string prefix = normalizeTagName(query.prefix).toStdString();
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
            {.name = QString::fromStdString(ordered[index].record.displayName),
             .subjectCount = static_cast<std::int64_t>(ordered[index].subjectIds.size())});
    }
    co_return facets;
}

/// 搜索完整 ORM 实体，并在 C++ 值对象层完成关系过滤和分页。
template <typename BackendTag>
auto search(Forms<BackendTag> &forms, const LocalSubjectQuery &query)
    -> IoTask<std::vector<SubjectSummary>> {
    if (query.limit <= 0 || query.limit > 200 || query.offset < 0) {
        co_return Err(std::make_error_code(std::errc::invalid_argument));
    }

    const QString text = query.text.trimmed();
    std::optional<std::string> tag;
    if (query.tag) {
        const QString normalized = normalizeTagName(*query.tag);
        if (normalized.isEmpty()) {
            co_return Err(std::make_error_code(std::errc::invalid_argument));
        }
        tag = normalized.toStdString();
    }

    std::optional<std::unordered_set<std::int64_t>> taggedSubjectIds;
    if (tag) {
        ILIAS_CO_TRY(
            auto ids,
            co_await loadSubjectIdsForTag(forms.tags, forms.subjectTags, *tag));
        if (ids.empty()) {
            co_return std::vector<SubjectSummary> {};
        }
        taggedSubjectIds = std::move(ids);
    }
    const auto acceptsTag = [&taggedSubjectIds](std::int64_t subjectId) {
        return !taggedSubjectIds || taggedSubjectIds->contains(subjectId);
    };

    using Subject = schema::SubjectRecord;
    std::vector<StoredSubjectRow> rows;
    if (!text.isEmpty()) {
        const std::string needle = text.toStdString();
        const auto matches =
            forms.subjects.sql(&Subject::title).contains(needle) ||
            forms.subjects.sql(&Subject::titleCn).contains(needle) ||
            forms.subjects.sql(&Subject::aliasesJson).contains(needle) ||
            forms.subjects.sql(&Subject::summary).contains(needle);
        ILIAS_CO_TRY(auto result, co_await forms.subjects.select().where(matches).query());
        ILIAS_CO_TRYV(co_await appendSubjectRows(std::move(result), rows));
    }
    else {
        ILIAS_CO_TRY(auto result, co_await forms.subjects.select().query());
        ILIAS_CO_TRYV(co_await appendSubjectRows(std::move(result), rows));
    }

    std::erase_if(rows, [&acceptsTag](const Subject &row) {
        return !acceptsTag(row.id);
    });
    // ORM 当前只有单列 ORDER BY；复合顺序在完整反射实体上稳定实现。
    std::ranges::sort(rows, [](const Subject &left, const Subject &right) {
        return std::tie(left.updatedAt, left.id) > std::tie(right.updatedAt, right.id);
    });

    const auto begin = std::min<std::size_t>(static_cast<std::size_t>(query.offset), rows.size());
    const auto end = std::min<std::size_t>(begin + static_cast<std::size_t>(query.limit), rows.size());
    std::vector<SubjectSummary> summaries;
    summaries.reserve(end - begin);
    for (auto index = begin; index < end; ++index) {
        summaries.push_back(toSummary(rows[index]));
    }
    co_return summaries;
}

} // namespace

struct CatalogStore::State {
    using Set = std::variant<Forms<SqliteTag>, Forms<MysqlTag>>;

    template <typename T>
    explicit State(T &&value) : forms(std::forward<T>(value)) {}

    Set forms;
};

CatalogStore::~CatalogStore() = default;
CatalogStore::CatalogStore(LocalDatabase &database, std::unique_ptr<State> state)
    : mDatabase(database), mState(std::move(state)) {}
CatalogStore::CatalogStore(CatalogStore &&) noexcept = default;

auto CatalogStore::open(LocalDatabase &database) -> IoTask<CatalogStore> {
    AL_LOG_DEBUG("[database.catalog] creating ORM forms backend={}", database.backendName());
    if (database.backend() == DatabaseBackend::Sqlite) {
        ILIAS_CO_TRY(
            auto forms,
            co_await Forms<SqliteTag>::create(database.advancedConnection()));
        AL_LOG_INFO("[database.catalog] ORM forms ready backend={}", database.backendName());
        co_return CatalogStore(database, std::make_unique<State>(std::move(forms)));
    }

    ILIAS_CO_TRY(
        auto forms,
        co_await Forms<MysqlTag>::create(database.advancedConnection()));
    AL_LOG_INFO("[database.catalog] ORM forms ready backend={}", database.backendName());
    co_return CatalogStore(database, std::make_unique<State>(std::move(forms)));
}

auto CatalogStore::upsertSubjectSnapshot(SubjectSnapshot snapshot)
    -> IoTask<SubjectId> {
    AL_LOG_INFO(
        "[database.catalog] upsert started backend={} provider={} "
        "metadata_level={} aliases_supplied={} tags_supplied={}",
        mDatabase.backendName(),
        snapshot.origin.providerKey.toStdString(),
        static_cast<std::int64_t>(snapshot.metadataLevel),
        snapshot.aliases.has_value(),
        snapshot.tags.has_value());
    return std::visit(
        [&](auto &forms) { return upsertSubject(mDatabase, forms, std::move(snapshot)); },
        mState->forms);
}

auto CatalogStore::upsertEpisodeSnapshots(
    SubjectId subject, std::vector<EpisodeSnapshot> snapshots)
    -> IoTask<std::vector<EpisodeId>> {
    AL_LOG_INFO("[database.catalog] episode upsert started backend={} subject_id={} count={}",
                mDatabase.backendName(), subject.value, snapshots.size());
    return std::visit(
        [&](auto &forms) { return upsertEpisodes(mDatabase, forms, subject, std::move(snapshots)); },
        mState->forms);
}

auto CatalogStore::replaceSubjectTags(
    SubjectId subject, QString providerKey,
    std::vector<SubjectTagSnapshot> tags) -> IoTask<void> {
    AL_LOG_INFO("[database.catalog] explicit tag replacement started backend={} subject_id={} count={}",
                mDatabase.backendName(), subject.value, tags.size());
    return std::visit(
        [&](auto &forms) { return replaceTags(mDatabase, forms, subject, providerKey, tags); },
        mState->forms);
}

auto CatalogStore::findSubjectByExternalRef(const ExternalRef &ref)
    -> IoTask<std::optional<SubjectId>> {
    AL_LOG_DEBUG("[database.catalog] external lookup started backend={} provider={}",
                 mDatabase.backendName(), ref.providerKey.toStdString());
    return std::visit([&](auto &forms) { return findSubject(forms, ref); }, mState->forms);
}

auto CatalogStore::findEpisodeByExternalRef(const ExternalRef &ref)
    -> IoTask<std::optional<EpisodeId>> {
    AL_LOG_DEBUG("[database.catalog] episode external lookup started backend={} provider={}",
                 mDatabase.backendName(), ref.providerKey.toStdString());
    return std::visit([&](auto &forms) { return findEpisode(forms, ref); }, mState->forms);
}

auto CatalogStore::getSubject(SubjectId subject)
    -> IoTask<std::optional<SubjectDetails>> {
    AL_LOG_DEBUG("[database.catalog] detail lookup started backend={} subject_id={}",
                 mDatabase.backendName(), subject.value);
    return std::visit([&](auto &forms) { return subjectDetails(forms, subject); }, mState->forms);
}

auto CatalogStore::getEpisode(EpisodeId episode)
    -> IoTask<std::optional<EpisodeDetails>> {
    AL_LOG_DEBUG("[database.catalog] episode detail lookup started backend={} episode_id={}",
                 mDatabase.backendName(), episode.value);
    return std::visit([&](auto &forms) { return episodeDetails(forms, episode); }, mState->forms);
}

auto CatalogStore::listEpisodes(SubjectId subject)
    -> IoTask<std::vector<EpisodeDetails>> {
    AL_LOG_INFO("[database.catalog] episode list started backend={} subject_id={}",
                mDatabase.backendName(), subject.value);
    return std::visit([&](auto &forms) { return loadEpisodes(forms, subject); }, mState->forms);
}

auto CatalogStore::searchSubjects(const LocalSubjectQuery &query)
    -> IoTask<std::vector<SubjectSummary>> {
    AL_LOG_INFO("[database.catalog] search started backend={} has_text={} has_tag={} limit={} offset={}",
                mDatabase.backendName(),
                !query.text.trimmed().isEmpty(),
                query.tag.has_value(),
                query.limit,
                query.offset);
    return std::visit([&](auto &forms) { return search(forms, query); }, mState->forms);
}

auto CatalogStore::listTags(const LocalTagQuery &query)
    -> IoTask<std::vector<TagFacet>> {
    AL_LOG_INFO(
        "[database.catalog] tag list started backend={} has_prefix={} limit={} offset={}",
        mDatabase.backendName(),
        !normalizeTagName(query.prefix).isEmpty(),
        query.limit,
        query.offset);
    return std::visit([&](auto &forms) { return loadTags(forms, query); }, mState->forms);
}

} // namespace anime_land::persistence
