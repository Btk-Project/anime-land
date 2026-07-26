#include "pch.hpp"

#include "model/persistence/library_store.hpp"

#include "common/log.hpp"
#include "model/persistence/library_schema.hpp"

#include <ilias/sql_orm/orm_form.hpp>

#include <QSet>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace anime_land::persistence {
namespace {

using ilias::Err;
using ilias::IoTask;
using ilias::sql::Form;
using ilias::sql::MysqlTag;
using ilias::sql::SqlDatabase;
using ilias::sql::SqliteTag;
using EpisodeMediaLinkRecord = library_schema::EpisodeMediaLinkRecord;
using MediaResourceRecord = library_schema::MediaResourceRecord;
using SourceItemRecord = library_schema::SourceItemRecord;

template <typename BackendTag>
struct Forms {
    using Database = SqlDatabase;
    template <typename T>
    using FormT = Form<T, BackendTag, Database>;

    FormT<MediaResourceRecord> mediaResources;
    FormT<SourceItemRecord> sourceItems;
    FormT<EpisodeMediaLinkRecord> episodeMediaLinks;

    static auto create(Database &database) -> IoTask<Forms> {
        ILIAS_CO_TRY(
            auto mediaResources,
            co_await FormT<MediaResourceRecord>::create_if_not_exists(
                database, library_schema::table::mediaResources));
        ILIAS_CO_TRY(
            auto sourceItems,
            co_await FormT<SourceItemRecord>::create_if_not_exists(
                database, library_schema::table::sourceItems));
        ILIAS_CO_TRY(
            auto episodeMediaLinks,
            co_await FormT<EpisodeMediaLinkRecord>::create_if_not_exists(
                database, library_schema::table::episodeMediaLinks));
        co_return Forms {
            .mediaResources = std::move(mediaResources),
            .sourceItems = std::move(sourceItems),
            .episodeMediaLinks = std::move(episodeMediaLinks),
        };
    }
};

auto encodeDescriptor(QByteArrayView value) -> std::string {
    return QByteArray(value.data(), value.size())
        .toBase64(QByteArray::Base64Encoding)
        .toStdString();
}

auto decodeDescriptor(const std::string &value) -> QByteArray {
    return QByteArray::fromBase64(QByteArray::fromStdString(value),
                                  QByteArray::AbortOnBase64DecodingErrors);
}

auto toResource(const MediaResourceRecord &row) -> MediaResource {
    return {
        .id = MediaResourceId {row.id},
        .providerKey = QString::fromStdString(row.providerKey),
        .stableKey = QString::fromStdString(row.stableKey),
        .descriptorVersion = static_cast<int>(row.descriptorVersion),
        .descriptor = decodeDescriptor(row.descriptorBase64),
        .displayName = QString::fromStdString(row.displayName),
    };
}

auto toSourceItem(const SourceItemRecord &row) -> SourceItem {
    return {
        .id = SourceItemId {row.id},
        .resourceId = MediaResourceId {row.resourceId},
        .stableKey = QString::fromStdString(row.stableKey),
        .descriptor = decodeDescriptor(row.descriptorBase64),
        .displayName = QString::fromStdString(row.displayName),
        .duration = row.durationMs.transform([](std::int64_t value) {
            return std::chrono::milliseconds(value);
        }),
    };
}

auto toEpisodeMediaLink(const EpisodeMediaLinkRecord &row)
    -> std::optional<EpisodeMediaLink> {
    const auto kind = static_cast<MediaLinkKind>(row.kind);
    switch (kind) {
        case MediaLinkKind::Manual:
        case MediaLinkKind::Filename:
        case MediaLinkKind::Sequence:
            break;
        default:
            return std::nullopt;
    }
    return EpisodeMediaLink {
        .episodeId = EpisodeId {row.episodeId},
        .sourceItemId = SourceItemId {row.sourceItemId},
        .kind = kind,
        .updatedAt = QDateTime::fromMSecsSinceEpoch(row.updatedAt).toUTC(),
    };
}

template <typename ResourceForm>
auto findResourceRow(ResourceForm &resources, QStringView providerKey,
                     QStringView stableKey)
    -> IoTask<std::optional<MediaResourceRecord>> {
    ILIAS_CO_TRY(
        auto result,
        co_await resources.select()
            .where(resources.sql(&MediaResourceRecord::providerKey) == providerKey.toString().toStdString() && resources.sql(&MediaResourceRecord::stableKey) == stableKey.toString().toStdString())
            .query());
    ilias_for_await(auto rowResult, result.rangeResult()) {
        ILIAS_CO_TRY(auto row, rowResult);
        co_return row;
    }
    co_return std::nullopt;
}

template <typename SourceItemForm>
auto findSourceItemRow(SourceItemForm &items, MediaResourceId resource,
                       QStringView stableKey)
    -> IoTask<std::optional<SourceItemRecord>> {
    ILIAS_CO_TRY(
        auto result,
        co_await items.select()
            .where(items.sql(&SourceItemRecord::resourceId) == resource.value && items.sql(&SourceItemRecord::stableKey) == stableKey.toString().toStdString())
            .query());
    ilias_for_await(auto rowResult, result.rangeResult()) {
        ILIAS_CO_TRY(auto row, rowResult);
        co_return row;
    }
    co_return std::nullopt;
}

template <typename SourceItemForm>
auto findSourceItemRow(SourceItemForm &items, SourceItemId item)
    -> IoTask<std::optional<SourceItemRecord>> {
    ILIAS_CO_TRY(
        auto result,
        co_await items.select()
            .where(items.sql(&SourceItemRecord::id) == item.value)
            .query());
    ilias_for_await(auto rowResult, result.rangeResult()) {
        ILIAS_CO_TRY(auto row, rowResult);
        co_return row;
    }
    co_return std::nullopt;
}

template <typename LinkForm>
auto findEpisodeMediaLinkRow(LinkForm &links, EpisodeId episode,
                             SourceItemId item)
    -> IoTask<std::optional<EpisodeMediaLinkRecord>> {
    ILIAS_CO_TRY(
        auto result,
        co_await links.select()
            .where(links.sql(&EpisodeMediaLinkRecord::episodeId) == episode.value && links.sql(&EpisodeMediaLinkRecord::sourceItemId) == item.value)
            .query());
    ilias_for_await(auto rowResult, result.rangeResult()) {
        ILIAS_CO_TRY(auto row, rowResult);
        co_return row;
    }
    co_return std::nullopt;
}

auto validateDiscoveries(const std::vector<MediaDiscovery> &discoveries)
    -> bool {
    QSet<QString> resources;
    for (const auto &discovery : discoveries) {
        if (!validate(discovery)) {
            return false;
        }
        const QString resourceIdentity =
            discovery.resource.providerKey + QChar(0x1f) + discovery.resource.stableKey;
        if (resources.contains(resourceIdentity)) {
            return false;
        }
        resources.insert(resourceIdentity);

        QSet<QString> items;
        for (const auto &item : discovery.items) {
            if (items.contains(item.stableKey)) {
                return false;
            }
            items.insert(item.stableKey);
        }
    }
    return true;
}

template <typename BackendTag>
auto upsertDiscoveries(LocalDatabase &database, Forms<BackendTag> &forms,
                       std::vector<MediaDiscovery> discoveries)
    -> IoTask<std::vector<StoredMediaDiscovery>> {
    if (!validateDiscoveries(discoveries)) {
        co_return Err(std::make_error_code(std::errc::invalid_argument));
    }

    ILIAS_CO_TRY(auto transaction,
                 co_await database.advancedConnection().transaction());
    ILIAS_CO_TRY(
        auto resources,
        Form<MediaResourceRecord, BackendTag>::bind(
            transaction, forms.mediaResources.getTableName()));
    ILIAS_CO_TRY(
        auto items,
        Form<SourceItemRecord, BackendTag>::bind(
            transaction, forms.sourceItems.getTableName()));

    std::vector<StoredMediaDiscovery> storedDiscoveries;
    storedDiscoveries.reserve(discoveries.size());
    for (const auto &discovery : discoveries) {
        const auto &snapshot = discovery.resource;
        const std::int64_t observedAt =
            discovery.observedAt.toMSecsSinceEpoch();
        ILIAS_CO_TRY(
            auto existing,
            co_await findResourceRow(resources, snapshot.providerKey,
                                     snapshot.stableKey));

        MediaResourceId resourceId;
        if (existing) {
            resourceId = MediaResourceId {existing->id};
            ILIAS_CO_TRYV(
                co_await resources.update()
                    .set(resources.sql(&MediaResourceRecord::descriptorVersion) = static_cast<std::int64_t>(
                             snapshot.descriptorVersion),
                         resources.sql(
                             &MediaResourceRecord::descriptorBase64) = encodeDescriptor(snapshot.descriptor),
                         resources.sql(&MediaResourceRecord::displayName) = snapshot.displayName.toStdString(),
                         resources.sql(&MediaResourceRecord::updatedAt) = observedAt,
                         resources.sql(&MediaResourceRecord::lastSeenAt) = observedAt)
                    .where(resources.sql(&MediaResourceRecord::id) == resourceId.value)
                    .execute());
        }
        else {
            ILIAS_CO_TRYV(
                co_await resources.insert()
                    .set(resources.sql(&MediaResourceRecord::providerKey) = snapshot.providerKey.toStdString(),
                         resources.sql(&MediaResourceRecord::stableKey) = snapshot.stableKey.toStdString(),
                         resources.sql(&MediaResourceRecord::descriptorVersion) = static_cast<std::int64_t>(
                             snapshot.descriptorVersion),
                         resources.sql(
                             &MediaResourceRecord::descriptorBase64) = encodeDescriptor(snapshot.descriptor),
                         resources.sql(&MediaResourceRecord::displayName) = snapshot.displayName.toStdString(),
                         resources.sql(&MediaResourceRecord::createdAt) = observedAt,
                         resources.sql(&MediaResourceRecord::updatedAt) = observedAt,
                         resources.sql(&MediaResourceRecord::lastSeenAt) = observedAt)
                    .execute());
            ILIAS_CO_TRY(auto connection, transaction.connection());
            resourceId = MediaResourceId {connection->lastInsertId()};
        }

        StoredMediaDiscovery stored {
            .resource =
                {
                    .id = resourceId,
                    .providerKey = snapshot.providerKey,
                    .stableKey = snapshot.stableKey,
                    .descriptorVersion = snapshot.descriptorVersion,
                    .descriptor = snapshot.descriptor,
                    .displayName = snapshot.displayName,
                },
            .items = {},
        };
        stored.items.reserve(discovery.items.size());
        for (const auto &item : discovery.items) {
            auto duration = item.duration.transform(
                [](std::chrono::milliseconds value) {
                    return static_cast<std::int64_t>(value.count());
                });
            ILIAS_CO_TRYV(
                co_await items.upsert()
                    .values(
                        items.sql(&SourceItemRecord::resourceId) = resourceId.value,
                        items.sql(&SourceItemRecord::stableKey) = item.stableKey.toStdString(),
                        items.sql(&SourceItemRecord::descriptorBase64) = encodeDescriptor(item.descriptor),
                        items.sql(&SourceItemRecord::displayName) = item.displayName.toStdString(),
                        items.sql(&SourceItemRecord::durationMs) = std::move(duration),
                        items.sql(&SourceItemRecord::available) = std::int64_t {1},
                        items.sql(&SourceItemRecord::createdAt) = observedAt,
                        items.sql(&SourceItemRecord::updatedAt) = observedAt,
                        items.sql(&SourceItemRecord::lastSeenAt) = observedAt)
                    .onConflict(items.sql(&SourceItemRecord::resourceId),
                                items.sql(&SourceItemRecord::stableKey))
                    .updateExcluded(
                        items.sql(&SourceItemRecord::descriptorBase64),
                        items.sql(&SourceItemRecord::displayName),
                        items.sql(&SourceItemRecord::durationMs),
                        items.sql(&SourceItemRecord::available),
                        items.sql(&SourceItemRecord::updatedAt),
                        items.sql(&SourceItemRecord::lastSeenAt))
                    .execute());

            ILIAS_CO_TRY(
                auto storedItem,
                co_await findSourceItemRow(items, resourceId,
                                           item.stableKey));
            if (!storedItem) {
                co_return Err(
                    std::make_error_code(std::errc::protocol_error));
            }
            stored.items.push_back(toSourceItem(*storedItem));
        }
        storedDiscoveries.push_back(std::move(stored));
    }

    ILIAS_CO_TRYV(co_await transaction.commit());
    co_return storedDiscoveries;
}

template <typename BackendTag>
auto findResource(Forms<BackendTag> &forms, QStringView providerKey,
                  QStringView stableKey)
    -> IoTask<std::optional<MediaResource>> {
    ILIAS_CO_TRY(
        auto row,
        co_await findResourceRow(forms.mediaResources, providerKey,
                                 stableKey));
    if (!row) {
        co_return std::nullopt;
    }
    co_return toResource(*row);
}

template <typename BackendTag>
auto loadResources(Forms<BackendTag> &forms)
    -> IoTask<std::vector<MediaResource>> {
    ILIAS_CO_TRY(auto result, co_await forms.mediaResources.select().query());
    std::vector<MediaResource> resources;
    ilias_for_await(auto rowResult, result.rangeResult()) {
        ILIAS_CO_TRY(auto row, rowResult);
        resources.push_back(toResource(row));
    }
    std::ranges::sort(resources, [](const MediaResource &left,
                                    const MediaResource &right) {
        return std::tie(left.displayName, left.id.value) < std::tie(right.displayName, right.id.value);
    });
    co_return resources;
}

template <typename BackendTag>
auto loadSourceItems(Forms<BackendTag> &forms, MediaResourceId resource,
                     bool includeUnavailable)
    -> IoTask<std::vector<SourceItem>> {
    if (!isValid(resource)) {
        co_return Err(std::make_error_code(std::errc::invalid_argument));
    }
    ILIAS_CO_TRY(
        auto result,
        co_await forms.sourceItems.select()
            .where(forms.sourceItems.sql(&SourceItemRecord::resourceId) == resource.value)
            .query());
    std::vector<SourceItem> items;
    ilias_for_await(auto rowResult, result.rangeResult()) {
        ILIAS_CO_TRY(auto row, rowResult);
        if (includeUnavailable || row.available) {
            items.push_back(toSourceItem(row));
        }
    }
    std::ranges::sort(items, [](const SourceItem &left,
                                const SourceItem &right) {
        return std::tie(left.displayName, left.id.value) < std::tie(right.displayName, right.id.value);
    });
    co_return items;
}

template <typename BackendTag>
auto loadMediaEntries(Forms<BackendTag> &forms, bool includeUnavailable)
    -> IoTask<std::vector<MediaEntry>> {
    ILIAS_CO_TRY(
        auto result,
        co_await forms.sourceItems.join(forms.mediaResources)
            .on(forms.sourceItems.col(&SourceItemRecord::resourceId) == forms.mediaResources.col(&MediaResourceRecord::id))
            .query());
    std::vector<MediaEntry> entries;
    ilias_for_await(auto rowResult, result.rangeResult()) {
        ILIAS_CO_TRY(auto row, rowResult);
        auto &[item, resource] = row;
        if (includeUnavailable || item.available) {
            entries.push_back({
                .resource = toResource(resource),
                .item = toSourceItem(item),
            });
        }
    }
    std::ranges::sort(entries, [](const MediaEntry &left,
                                  const MediaEntry &right) {
        return std::tie(left.resource.displayName, left.item.displayName,
                        left.item.id.value) < std::tie(right.resource.displayName,
                                                       right.item.displayName, right.item.id.value);
    });
    co_return entries;
}

template <typename BackendTag>
auto upsertEpisodeMediaLink(LocalDatabase &database,
                            Forms<BackendTag> &forms,
                            EpisodeMediaLink link)
    -> IoTask<EpisodeMediaLink> {
    if (!validate(link)) {
        co_return Err(std::make_error_code(std::errc::invalid_argument));
    }

    ILIAS_CO_TRY(auto transaction,
                 co_await database.advancedConnection().transaction());
    ILIAS_CO_TRY(
        auto links,
        Form<EpisodeMediaLinkRecord, BackendTag>::bind(
            transaction, forms.episodeMediaLinks.getTableName()));
    ILIAS_CO_TRY(
        auto existing,
        co_await findEpisodeMediaLinkRow(
            links, link.episodeId, link.sourceItemId));
    if (existing && existing->kind == static_cast<std::int64_t>(MediaLinkKind::Manual) && link.kind != MediaLinkKind::Manual) {
        ILIAS_CO_TRYV(co_await transaction.commit());
        auto stored = toEpisodeMediaLink(*existing);
        if (!stored) {
            co_return Err(
                std::make_error_code(std::errc::illegal_byte_sequence));
        }
        co_return *stored;
    }

    ILIAS_CO_TRYV(
        co_await links.upsert()
            .values(
                links.sql(&EpisodeMediaLinkRecord::episodeId) = link.episodeId.value,
                links.sql(&EpisodeMediaLinkRecord::sourceItemId) = link.sourceItemId.value,
                links.sql(&EpisodeMediaLinkRecord::kind) = static_cast<std::int64_t>(link.kind),
                links.sql(&EpisodeMediaLinkRecord::updatedAt) = link.updatedAt.toMSecsSinceEpoch())
            .onConflict(links.sql(&EpisodeMediaLinkRecord::episodeId),
                        links.sql(&EpisodeMediaLinkRecord::sourceItemId))
            .updateExcluded(links.sql(&EpisodeMediaLinkRecord::kind),
                            links.sql(&EpisodeMediaLinkRecord::updatedAt))
            .execute());
    ILIAS_CO_TRY(
        auto storedRow,
        co_await findEpisodeMediaLinkRow(
            links, link.episodeId, link.sourceItemId));
    if (!storedRow) {
        co_return Err(std::make_error_code(std::errc::protocol_error));
    }
    auto stored = toEpisodeMediaLink(*storedRow);
    if (!stored) {
        co_return Err(
            std::make_error_code(std::errc::illegal_byte_sequence));
    }

    ILIAS_CO_TRYV(co_await transaction.commit());
    co_return *stored;
}

template <typename BackendTag>
auto loadEpisodeMediaLinks(Forms<BackendTag> &forms, EpisodeId episode)
    -> IoTask<std::vector<EpisodeMediaLink>> {
    if (!isValid(episode)) {
        co_return Err(std::make_error_code(std::errc::invalid_argument));
    }
    ILIAS_CO_TRY(
        auto result,
        co_await forms.episodeMediaLinks.select()
            .where(forms.episodeMediaLinks.sql(
                       &EpisodeMediaLinkRecord::episodeId) == episode.value)
            .query());
    std::vector<EpisodeMediaLink> links;
    ilias_for_await(auto rowResult, result.rangeResult()) {
        ILIAS_CO_TRY(auto row, rowResult);
        auto link = toEpisodeMediaLink(row);
        if (!link) {
            co_return Err(
                std::make_error_code(std::errc::illegal_byte_sequence));
        }
        links.push_back(std::move(*link));
    }
    std::ranges::sort(links, [](const EpisodeMediaLink &left,
                                const EpisodeMediaLink &right) {
        return std::tie(left.sourceItemId.value, left.updatedAt) < std::tie(right.sourceItemId.value, right.updatedAt);
    });
    co_return links;
}

template <typename BackendTag>
auto loadSourceItemMediaLinks(Forms<BackendTag> &forms, SourceItemId item)
    -> IoTask<std::vector<EpisodeMediaLink>> {
    if (!isValid(item)) {
        co_return Err(std::make_error_code(std::errc::invalid_argument));
    }
    ILIAS_CO_TRY(
        auto result,
        co_await forms.episodeMediaLinks.select()
            .where(forms.episodeMediaLinks.sql(
                       &EpisodeMediaLinkRecord::sourceItemId) == item.value)
            .query());
    std::vector<EpisodeMediaLink> links;
    ilias_for_await(auto rowResult, result.rangeResult()) {
        ILIAS_CO_TRY(auto row, rowResult);
        auto link = toEpisodeMediaLink(row);
        if (!link) {
            co_return Err(
                std::make_error_code(std::errc::illegal_byte_sequence));
        }
        links.push_back(std::move(*link));
    }
    std::ranges::sort(links, [](const EpisodeMediaLink &left,
                                const EpisodeMediaLink &right) {
        return std::tie(left.episodeId.value, left.updatedAt) < std::tie(right.episodeId.value, right.updatedAt);
    });
    co_return links;
}

template <typename BackendTag>
auto removeEpisodeMediaLink(LocalDatabase &database,
                            Forms<BackendTag> &forms,
                            EpisodeId episode, SourceItemId item)
    -> IoTask<bool> {
    if (!isValid(episode) || !isValid(item)) {
        co_return Err(std::make_error_code(std::errc::invalid_argument));
    }

    ILIAS_CO_TRY(auto transaction,
                 co_await database.advancedConnection().transaction());
    ILIAS_CO_TRY(
        auto links,
        Form<EpisodeMediaLinkRecord, BackendTag>::bind(
            transaction, forms.episodeMediaLinks.getTableName()));
    ILIAS_CO_TRY(
        auto stored,
        co_await findEpisodeMediaLinkRow(links, episode, item));
    if (!stored) {
        ILIAS_CO_TRYV(co_await transaction.commit());
        co_return false;
    }
    ILIAS_CO_TRYV(
        co_await links.remove()
            .where(links.sql(&EpisodeMediaLinkRecord::episodeId) == episode.value && links.sql(&EpisodeMediaLinkRecord::sourceItemId) == item.value)
            .execute());
    ILIAS_CO_TRYV(co_await transaction.commit());
    co_return true;
}

template <typename BackendTag>
auto removeSourceItem(LocalDatabase &database, Forms<BackendTag> &forms,
                      SourceItemId item) -> IoTask<bool> {
    if (!isValid(item)) {
        co_return Err(std::make_error_code(std::errc::invalid_argument));
    }

    ILIAS_CO_TRY(auto transaction,
                 co_await database.advancedConnection().transaction());
    ILIAS_CO_TRY(
        auto resources,
        Form<MediaResourceRecord, BackendTag>::bind(
            transaction, forms.mediaResources.getTableName()));
    ILIAS_CO_TRY(
        auto items,
        Form<SourceItemRecord, BackendTag>::bind(
            transaction, forms.sourceItems.getTableName()));

    ILIAS_CO_TRY(auto storedItem, co_await findSourceItemRow(items, item));
    if (!storedItem) {
        ILIAS_CO_TRYV(co_await transaction.commit());
        co_return false;
    }

    const MediaResourceId resourceId {storedItem->resourceId};
    ILIAS_CO_TRYV(
        co_await items.remove()
            .where(items.sql(&SourceItemRecord::id) == item.value)
            .execute());

    ILIAS_CO_TRY(
        auto remaining,
        co_await items.select()
            .where(items.sql(&SourceItemRecord::resourceId) == resourceId.value)
            .query());
    bool resourceHasItems = false;
    ilias_for_await(auto rowResult, remaining.rangeResult()) {
        ILIAS_CO_TRY(auto row, rowResult);
        static_cast<void>(row);
        resourceHasItems = true;
        break;
    }
    if (!resourceHasItems) {
        ILIAS_CO_TRYV(
            co_await resources.remove()
                .where(resources.sql(&MediaResourceRecord::id) == resourceId.value)
                .execute());
    }

    ILIAS_CO_TRYV(co_await transaction.commit());
    co_return true;
}

} // namespace

struct LibraryStore::State {
    using Set = std::variant<Forms<SqliteTag>, Forms<MysqlTag>>;

    template <typename T>
    explicit State(T &&value) : forms(std::forward<T>(value)) {}

    Set forms;
};

LibraryStore::~LibraryStore() = default;
LibraryStore::LibraryStore(LocalDatabase &database,
                           std::unique_ptr<State> state)
    : mDatabase(database), mState(std::move(state)) {}
LibraryStore::LibraryStore(LibraryStore &&) noexcept = default;

auto LibraryStore::open(LocalDatabase &database) -> IoTask<LibraryStore> {
    AL_LOG_DEBUG("[database.library] creating ORM forms backend={}",
                 database.backendName());
    if (database.backend() == DatabaseBackend::Sqlite) {
        ILIAS_CO_TRY(
            auto forms,
            co_await Forms<SqliteTag>::create(
                database.advancedConnection()));
        co_return LibraryStore(
            database, std::make_unique<State>(std::move(forms)));
    }
    ILIAS_CO_TRY(
        auto forms,
        co_await Forms<MysqlTag>::create(database.advancedConnection()));
    co_return LibraryStore(database,
                           std::make_unique<State>(std::move(forms)));
}

auto LibraryStore::upsertDiscoveredMedia(
    std::vector<MediaDiscovery> discoveries)
    -> IoTask<std::vector<StoredMediaDiscovery>> {
    AL_LOG_INFO("[database.library] media upsert started resources={}",
                discoveries.size());
    return std::visit(
        [&](auto &forms) {
            return upsertDiscoveries(mDatabase, forms,
                                     std::move(discoveries));
        },
        mState->forms);
}

auto LibraryStore::findResource(QStringView providerKey,
                                QStringView stableKey)
    -> IoTask<std::optional<MediaResource>> {
    return std::visit(
        [&](auto &forms) {
            return ::anime_land::persistence::findResource(
                forms, providerKey, stableKey);
        },
        mState->forms);
}

auto LibraryStore::listResources()
    -> IoTask<std::vector<MediaResource>> {
    return std::visit(
        [&](auto &forms) { return loadResources(forms); }, mState->forms);
}

auto LibraryStore::listSourceItems(MediaResourceId resource,
                                   bool includeUnavailable)
    -> IoTask<std::vector<SourceItem>> {
    return std::visit(
        [&](auto &forms) {
            return loadSourceItems(forms, resource, includeUnavailable);
        },
        mState->forms);
}

auto LibraryStore::listMediaEntries(bool includeUnavailable)
    -> IoTask<std::vector<MediaEntry>> {
    return std::visit(
        [&](auto &forms) {
            return loadMediaEntries(forms, includeUnavailable);
        },
        mState->forms);
}

auto LibraryStore::removeSourceItem(SourceItemId item) -> IoTask<bool> {
    AL_LOG_INFO("[database.library] media item remove requested "
                "source_item_id={}",
                item.value);
    return std::visit(
        [&](auto &forms) {
            return ::anime_land::persistence::removeSourceItem(
                mDatabase, forms, item);
        },
        mState->forms);
}

auto LibraryStore::upsertEpisodeMediaLink(EpisodeMediaLink link)
    -> IoTask<EpisodeMediaLink> {
    AL_LOG_INFO(
        "[database.library] episode media link upsert requested "
        "episode_id={} source_item_id={} kind={}",
        link.episodeId.value, link.sourceItemId.value,
        mediaLinkKindName(link.kind));
    return std::visit(
        [&](auto &forms) {
            return ::anime_land::persistence::upsertEpisodeMediaLink(
                mDatabase, forms, std::move(link));
        },
        mState->forms);
}

auto LibraryStore::listEpisodeMediaLinks(EpisodeId episode)
    -> IoTask<std::vector<EpisodeMediaLink>> {
    return std::visit(
        [&](auto &forms) {
            return loadEpisodeMediaLinks(forms, episode);
        },
        mState->forms);
}

auto LibraryStore::listSourceItemMediaLinks(SourceItemId item)
    -> IoTask<std::vector<EpisodeMediaLink>> {
    return std::visit(
        [&](auto &forms) {
            return loadSourceItemMediaLinks(forms, item);
        },
        mState->forms);
}

auto LibraryStore::removeEpisodeMediaLink(EpisodeId episode,
                                          SourceItemId item)
    -> IoTask<bool> {
    AL_LOG_INFO(
        "[database.library] episode media link remove requested "
        "episode_id={} source_item_id={}",
        episode.value, item.value);
    return std::visit(
        [&](auto &forms) {
            return ::anime_land::persistence::removeEpisodeMediaLink(
                mDatabase, forms, episode, item);
        },
        mState->forms);
}

} // namespace anime_land::persistence
