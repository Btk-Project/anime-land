#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QUrl>

#include <ilias/platform/qt.hpp>

#include "model/library/local_media_import.hpp"
#include "model/persistence/catalog_store.hpp"
#include "model/persistence/database.hpp"
#include "model/persistence/library_store.hpp"

#include <chrono>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace anime_land;
using namespace anime_land::persistence;
using namespace std::chrono_literals;

namespace {

auto openMemoryDatabase() -> LocalDatabase {
    SqlSettings settings;
    settings.database_type = "sqlite";
    settings.database_path = ":memory:";
    settings.database_password.clear();
    auto opened = LocalDatabase::open(settings).wait();
    if (!opened) {
        throw std::runtime_error(opened.error().message());
    }
    return std::move(*opened);
}

auto openCatalog(LocalDatabase &database) -> CatalogStore {
    auto opened = CatalogStore::open(database).wait();
    if (!opened) {
        throw std::runtime_error(opened.error().message());
    }
    return std::move(*opened);
}

auto openLibrary(LocalDatabase &database) -> LibraryStore {
    auto catalog = openCatalog(database);
    auto opened = LibraryStore::open(database).wait();
    if (!opened) {
        throw std::runtime_error(opened.error().message());
    }
    return std::move(*opened);
}

auto sampleDiscovery() -> MediaDiscovery {
    return {
        .resource =
            {
                .providerKey = QStringLiteral("local-file"),
                .stableKey = QStringLiteral("d:/anime/frieren"),
                .descriptorVersion = 1,
                .descriptor = QByteArray("a\0b", 3),
                .displayName = QStringLiteral("Frieren"),
            },
        .items =
            {
                {
                    .stableKey = QStringLiteral("episode-01.mkv"),
                    .descriptor = QByteArrayLiteral(
                        R"({"relativePath":"episode-01.mkv"})"),
                    .displayName = QStringLiteral("Episode 01.mkv"),
                    .duration = 24min,
                },
            },
        .observedAt =
            QDateTime::fromMSecsSinceEpoch(1'700'000'000'000LL),
    };
}

void createFile(const QString &path) {
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write(QByteArrayLiteral("fixture media")), 13);
}

} // namespace

TEST(LibraryStore, CreatesRelationsAndEnforcesResourceOwnership) {
    auto database = openMemoryDatabase();
    auto catalog = CatalogStore::open(database).wait();
    ASSERT_TRUE(catalog) << catalog.error().message();
    auto first = LibraryStore::open(database).wait();
    ASSERT_TRUE(first) << first.error().message();
    auto second = LibraryStore::open(database).wait();
    ASSERT_TRUE(second) << second.error().message();

    auto orphan = database.advancedConnection()
                      .execute(
                          "INSERT INTO source_items("
                          "resource_id, stable_key, descriptor, display_name, "
                          "created_at, updated_at, last_seen_at"
                          ") VALUES (999, 'orphan', X'00', 'orphan', 1, 1, 1)")
                      .wait();
    EXPECT_FALSE(orphan);
}

TEST(LibraryStore, UpsertsResourceAndItemsIdempotently) {
    auto database = openMemoryDatabase();
    auto store = openLibrary(database);

    auto first =
        store.upsertDiscoveredMedia({sampleDiscovery()}).wait();
    ASSERT_TRUE(first) << first.error().message();
    ASSERT_EQ(first->size(), 1U);
    ASSERT_EQ(first->front().items.size(), 1U);
    const auto resourceId = first->front().resource.id;
    const auto sourceItemId = first->front().items.front().id;
    EXPECT_TRUE(isValid(resourceId));
    EXPECT_TRUE(isValid(sourceItemId));
    EXPECT_EQ(first->front().resource.descriptor, QByteArray("a\0b", 3));

    auto updated = sampleDiscovery();
    updated.resource.displayName = QStringLiteral("Frieren Updated");
    updated.items.front().displayName = QStringLiteral("01 - Updated.mkv");
    updated.observedAt = updated.observedAt.addSecs(60);
    auto second =
        store.upsertDiscoveredMedia({std::move(updated)}).wait();

    ASSERT_TRUE(second) << second.error().message();
    ASSERT_EQ(second->size(), 1U);
    ASSERT_EQ(second->front().items.size(), 1U);
    EXPECT_EQ(second->front().resource.id, resourceId);
    EXPECT_EQ(second->front().items.front().id, sourceItemId);

    auto resources = store.listResources().wait();
    ASSERT_TRUE(resources) << resources.error().message();
    ASSERT_EQ(resources->size(), 1U);
    EXPECT_EQ(resources->front().displayName,
              QStringLiteral("Frieren Updated"));

    auto items = store.listSourceItems(resourceId).wait();
    ASSERT_TRUE(items) << items.error().message();
    ASSERT_EQ(items->size(), 1U);
    EXPECT_EQ(items->front().displayName,
              QStringLiteral("01 - Updated.mkv"));
    ASSERT_TRUE(items->front().duration);
    EXPECT_EQ(*items->front().duration, 24min);
}

TEST(LibraryStore, RejectsWholeInvalidBatchBeforeWriting) {
    auto database = openMemoryDatabase();
    auto store = openLibrary(database);
    auto invalid = sampleDiscovery();
    invalid.resource.stableKey = QStringLiteral("second-resource");
    invalid.observedAt = {};

    auto result = store.upsertDiscoveredMedia(
                           {sampleDiscovery(), std::move(invalid)})
                      .wait();

    EXPECT_FALSE(result);
    auto resources = store.listResources().wait();
    ASSERT_TRUE(resources) << resources.error().message();
    EXPECT_TRUE(resources->empty());
}

TEST(LibraryStore, RemovesSourceItemsAndPrunesEmptyResources) {
    auto database = openMemoryDatabase();
    auto store = openLibrary(database);
    auto discovery = sampleDiscovery();
    discovery.items.push_back({
        .stableKey = QStringLiteral("episode-02.mkv"),
        .descriptor = QByteArrayLiteral(
            R"({"relativePath":"episode-02.mkv"})"),
        .displayName = QStringLiteral("Episode 02.mkv"),
        .duration = 24min,
    });

    auto stored = store.upsertDiscoveredMedia({std::move(discovery)}).wait();
    ASSERT_TRUE(stored) << stored.error().message();
    ASSERT_EQ(stored->front().items.size(), 2U);
    const auto resourceId = stored->front().resource.id;
    const auto firstItemId = stored->front().items[0].id;
    const auto secondItemId = stored->front().items[1].id;

    auto removedFirst = store.removeSourceItem(firstItemId).wait();
    ASSERT_TRUE(removedFirst) << removedFirst.error().message();
    EXPECT_TRUE(*removedFirst);

    auto resources = store.listResources().wait();
    ASSERT_TRUE(resources) << resources.error().message();
    ASSERT_EQ(resources->size(), 1U);
    auto remaining = store.listSourceItems(resourceId).wait();
    ASSERT_TRUE(remaining) << remaining.error().message();
    ASSERT_EQ(remaining->size(), 1U);
    EXPECT_EQ(remaining->front().id, secondItemId);

    auto removedSecond = store.removeSourceItem(secondItemId).wait();
    ASSERT_TRUE(removedSecond) << removedSecond.error().message();
    EXPECT_TRUE(*removedSecond);
    resources = store.listResources().wait();
    ASSERT_TRUE(resources) << resources.error().message();
    EXPECT_TRUE(resources->empty());

    auto removedAgain = store.removeSourceItem(secondItemId).wait();
    ASSERT_TRUE(removedAgain) << removedAgain.error().message();
    EXPECT_FALSE(*removedAgain);
    EXPECT_FALSE(store.removeSourceItem(SourceItemId {}).wait());
}

TEST(LibraryStore, PersistsEpisodeLinksAndProtectsManualRelations) {
    auto database = openMemoryDatabase();
    auto catalog = openCatalog(database);
    const QDateTime fetchedAt =
        QDateTime::fromMSecsSinceEpoch(1'700'000'000'000LL).toUTC();
    auto subject = catalog
                       .upsertSubjectSnapshot({
                           .origin =
                               {
                                   .providerKey = QStringLiteral("bangumi"),
                                   .externalId = QStringLiteral("subject-1"),
                               },
                           .title = QStringLiteral("Frieren"),
                           .fetchedAt = fetchedAt,
                       })
                       .wait();
    ASSERT_TRUE(subject) << subject.error().message();
    auto episodes = catalog
                        .upsertEpisodeSnapshots(
                            *subject,
                            {
                                {
                                    .origin =
                                        {
                                            .providerKey = QStringLiteral("bangumi"),
                                            .externalId = QStringLiteral("episode-1"),
                                        },
                                    .sortOrder = 1,
                                    .episodeNumber = 1.0,
                                    .fetchedAt = fetchedAt,
                                },
                                {
                                    .origin =
                                        {
                                            .providerKey = QStringLiteral("bangumi"),
                                            .externalId = QStringLiteral("episode-2"),
                                        },
                                    .sortOrder = 2,
                                    .episodeNumber = 2.0,
                                    .fetchedAt = fetchedAt,
                                },
                            })
                        .wait();
    ASSERT_TRUE(episodes) << episodes.error().message();
    ASSERT_EQ(episodes->size(), 2U);

    auto storeResult = LibraryStore::open(database).wait();
    ASSERT_TRUE(storeResult) << storeResult.error().message();
    auto store = std::move(*storeResult);
    auto media = store.upsertDiscoveredMedia({sampleDiscovery()}).wait();
    ASSERT_TRUE(media) << media.error().message();
    const SourceItemId item = media->front().items.front().id;

    auto orphan =
        store
            .upsertEpisodeMediaLink({
                .episodeId = EpisodeId {999},
                .sourceItemId = item,
                .kind = MediaLinkKind::Manual,
                .updatedAt = fetchedAt,
            })
            .wait();
    EXPECT_FALSE(orphan);

    auto filenameStored =
        store
            .upsertEpisodeMediaLink({
                .episodeId = episodes->at(0),
                .sourceItemId = item,
                .kind = MediaLinkKind::Filename,
                .updatedAt = fetchedAt,
            })
            .wait();
    ASSERT_TRUE(filenameStored) << filenameStored.error().message();
    EXPECT_EQ(filenameStored->kind, MediaLinkKind::Filename);

    const QDateTime manualAt = fetchedAt.addSecs(60);
    auto manualStored =
        store
            .upsertEpisodeMediaLink({
                .episodeId = episodes->at(0),
                .sourceItemId = item,
                .kind = MediaLinkKind::Manual,
                .updatedAt = manualAt,
            })
            .wait();
    ASSERT_TRUE(manualStored) << manualStored.error().message();
    EXPECT_EQ(manualStored->kind, MediaLinkKind::Manual);

    auto protectedManual =
        store
            .upsertEpisodeMediaLink({
                .episodeId = episodes->at(0),
                .sourceItemId = item,
                .kind = MediaLinkKind::Sequence,
                .updatedAt = manualAt.addSecs(60),
            })
            .wait();
    ASSERT_TRUE(protectedManual) << protectedManual.error().message();
    EXPECT_EQ(protectedManual->kind, MediaLinkKind::Manual);
    EXPECT_EQ(protectedManual->updatedAt, manualAt);

    auto secondEpisode =
        store
            .upsertEpisodeMediaLink({
                .episodeId = episodes->at(1),
                .sourceItemId = item,
                .kind = MediaLinkKind::Manual,
                .updatedAt = manualAt,
            })
            .wait();
    ASSERT_TRUE(secondEpisode) << secondEpisode.error().message();
    auto itemLinks = store.listSourceItemMediaLinks(item).wait();
    ASSERT_TRUE(itemLinks) << itemLinks.error().message();
    ASSERT_EQ(itemLinks->size(), 2U);
    EXPECT_EQ(itemLinks->at(0).episodeId, episodes->at(0));
    EXPECT_EQ(itemLinks->at(1).episodeId, episodes->at(1));

    auto removed =
        store.removeEpisodeMediaLink(episodes->at(0), item).wait();
    ASSERT_TRUE(removed) << removed.error().message();
    EXPECT_TRUE(*removed);
    auto removedAgain =
        store.removeEpisodeMediaLink(episodes->at(0), item).wait();
    ASSERT_TRUE(removedAgain) << removedAgain.error().message();
    EXPECT_FALSE(*removedAgain);

    auto itemRemoved = store.removeSourceItem(item).wait();
    ASSERT_TRUE(itemRemoved) << itemRemoved.error().message();
    EXPECT_TRUE(*itemRemoved);
    auto cascaded =
        store.listEpisodeMediaLinks(episodes->at(1)).wait();
    ASSERT_TRUE(cascaded) << cascaded.error().message();
    EXPECT_TRUE(cascaded->empty());
}

TEST(LocalMediaImportService, GroupsSelectedFilesAndDeduplicates) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString firstPath = directory.filePath(QStringLiteral("Episode 01.mkv"));
    const QString secondPath = directory.filePath(QStringLiteral("第二集.mp4"));
    createFile(firstPath);
    createFile(secondPath);

    auto database = openMemoryDatabase();
    auto store = openLibrary(database);
    LocalMediaImportService importer(store);

    auto result = importer
                      .importFiles({QUrl::fromLocalFile(firstPath),
                                    QUrl::fromLocalFile(secondPath),
                                    QUrl::fromLocalFile(firstPath)})
                      .wait();

    ASSERT_TRUE(result) << result.error().message.toStdString();
    EXPECT_EQ(result->persistedFileCount, 2U);
    EXPECT_EQ(result->duplicateSelectionCount, 1U);
    ASSERT_EQ(result->resources.size(), 1U);
    ASSERT_EQ(result->resources.front().items.size(), 2U);
    const SourceItemId firstId = result->resources.front().items.front().id;

    auto repeated =
        importer.importFiles({QUrl::fromLocalFile(firstPath)}).wait();
    ASSERT_TRUE(repeated) << repeated.error().message.toStdString();
    ASSERT_EQ(repeated->resources.size(), 1U);
    ASSERT_EQ(repeated->resources.front().items.size(), 1U);
    EXPECT_EQ(repeated->resources.front().items.front().id, firstId);

    auto entries = importer.listMedia().wait();
    ASSERT_TRUE(entries) << entries.error().message.toStdString();
    ASSERT_EQ(entries->size(), 2U);
    EXPECT_EQ(entries->front().resource.providerKey,
              QStringLiteral("local-file"));
}

TEST(LocalMediaPlayback, ResolvesImportedFileAndRejectsDirectoryTraversal) {
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    const QString mediaDirectory = temporary.filePath(QStringLiteral("media"));
    ASSERT_TRUE(QDir().mkpath(mediaDirectory));
    const QString mediaPath =
        QDir(mediaDirectory).filePath(QStringLiteral("episode-01.mkv"));
    const QString outsidePath =
        temporary.filePath(QStringLiteral("outside.mkv"));
    createFile(mediaPath);
    createFile(outsidePath);

    MediaEntry entry {
        .resource =
            {
                .id = MediaResourceId {1},
                .providerKey = QStringLiteral("local-file"),
                .stableKey = mediaDirectory,
                .descriptorVersion = 1,
                .descriptor =
                    QStringLiteral(R"({"directory":"%1"})")
                        .arg(QDir::fromNativeSeparators(mediaDirectory))
                        .toUtf8(),
                .displayName = QStringLiteral("media"),
            },
        .item =
            {
                .id = SourceItemId {1},
                .resourceId = MediaResourceId {1},
                .stableKey = QStringLiteral("episode-01.mkv"),
                .descriptor = QByteArrayLiteral(
                    R"({"relativePath":"episode-01.mkv"})"),
                .displayName = QStringLiteral("episode-01.mkv"),
                .duration = std::nullopt,
            },
    };

    auto resolved = resolveLocalMediaUrl(entry);
    ASSERT_TRUE(resolved) << resolved.error().message.toStdString();
    EXPECT_EQ(QFileInfo(resolved->toLocalFile()).canonicalFilePath(),
              QFileInfo(mediaPath).canonicalFilePath());

    entry.item.descriptor =
        QByteArrayLiteral(R"({"relativePath":"../outside.mkv"})");
    auto escaped = resolveLocalMediaUrl(entry);
    ASSERT_FALSE(escaped);
    EXPECT_EQ(escaped.error().code,
              LibraryErrorCode::InvalidMediaDescriptor);
}

TEST(LocalMediaAssociation,
     PersistsBangumiSnapshotsLinksAndLaunchesBySourceItemId) {
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    const QString mediaPath =
        temporary.filePath(QStringLiteral("episode-01.mkv"));
    createFile(mediaPath);

    auto database = openMemoryDatabase();
    auto catalog = openCatalog(database);
    auto libraryResult = LibraryStore::open(database).wait();
    ASSERT_TRUE(libraryResult) << libraryResult.error().message();
    auto library = std::move(*libraryResult);

    QString searchKeyword;
    int requestedOffset = -1;
    QUrl launchedUrl;
    LocalMediaImportService service(
        library, catalog,
        [&searchKeyword](BangumiSubjectSearchQuery query)
            -> ilias::Task<BangumiResult<BangumiSubjectSearchResponse>> {
            searchKeyword = query.keyword;
            co_return BangumiSubjectSearchResponse {
                .value =
                    {
                        .total = 1,
                        .limit = 20,
                        .offset = 0,
                        .data = {{
                            .id = 400602,
                            .type = BangumiSubjectType::Anime,
                            .name = QStringLiteral("Sousou no Frieren"),
                            .nameCn = QStringLiteral("葬送的芙莉莲"),
                            .summary = QStringLiteral("冒险结束后的故事"),
                            .date = QStringLiteral("2023-09-29"),
                            .episodes = 1,
                            .totalEpisodes = 1,
                            .tags = {{.name = QStringLiteral("治愈"),
                                      .count = 1200}},
                        }},
                    },
                .rawBody = {},
            };
        },
        [&requestedOffset](std::int64_t subjectId, int limit, int offset)
            -> ilias::Task<BangumiResult<BangumiEpisodeResponse>> {
            EXPECT_EQ(subjectId, 400602);
            EXPECT_EQ(limit, 200);
            requestedOffset = offset;
            co_return BangumiEpisodeResponse {
                .value =
                    {
                        .total = 1,
                        .limit = 200,
                        .offset = offset,
                        .data = {{
                            .id = 120001,
                            .type = 0,
                            .name = QStringLiteral("The Journey's End"),
                            .nameCn = QStringLiteral("冒险的终点"),
                            .sort = 1.0,
                            .episodeNumber = 1.0,
                            .airDate = QStringLiteral("2023-09-29"),
                            .duration = QStringLiteral("00:24:00"),
                            .summary = QStringLiteral("第一集"),
                            .durationSeconds = 1440,
                        }},
                    },
                .rawBody = {},
            };
        },
        [&launchedUrl](const QUrl &url) {
            launchedUrl = url;
            return true;
        });

    auto imported =
        service.importFiles({QUrl::fromLocalFile(mediaPath)}).wait();
    ASSERT_TRUE(imported) << imported.error().message.toStdString();
    const SourceItemId item = imported->resources.front().items.front().id;

    auto subjects =
        service.searchAssociationSubjects(QStringLiteral("芙莉莲")).wait();
    ASSERT_TRUE(subjects) << subjects.error().message.toStdString();
    ASSERT_EQ(subjects->size(), 1U);
    EXPECT_EQ(searchKeyword, QStringLiteral("芙莉莲"));

    auto episodes = service.loadAssociationEpisodes(subjects->front()).wait();
    ASSERT_TRUE(episodes) << episodes.error().message.toStdString();
    ASSERT_EQ(episodes->size(), 1U);
    EXPECT_EQ(requestedOffset, 0);
    EXPECT_EQ(episodes->front().displayNumber, QStringLiteral("EP1"));

    auto linked = service.linkMedia(item, episodes->front().id).wait();
    ASSERT_TRUE(linked) << linked.error().message.toStdString();
    auto media = service.listLibraryMedia().wait();
    ASSERT_TRUE(media) << media.error().message.toStdString();
    ASSERT_EQ(media->size(), 1U);
    ASSERT_EQ(media->front().associations.size(), 1U);
    EXPECT_EQ(media->front().associations.front().subjectTitle,
              QStringLiteral("葬送的芙莉莲"));
    EXPECT_EQ(media->front().associations.front().episodeTitle,
              QStringLiteral("冒险的终点"));

    auto foundSubject = service.findBangumiSubject(400602).wait();
    ASSERT_TRUE(foundSubject) << foundSubject.error().message.toStdString();
    ASSERT_TRUE(*foundSubject);
    auto details =
        service.getSubjectLibraryDetails(**foundSubject).wait();
    ASSERT_TRUE(details) << details.error().message.toStdString();
    ASSERT_TRUE(*details);
    ASSERT_EQ((*details)->episodes.size(), 1U);
    ASSERT_EQ((*details)->episodes.front().media.size(), 1U);
    EXPECT_EQ((*details)->episodes.front().media.front().item.id, item);

    auto played = service.playEpisode(episodes->front().id).wait();
    ASSERT_TRUE(played) << played.error().message.toStdString();
    EXPECT_EQ(QFileInfo(launchedUrl.toLocalFile()).canonicalFilePath(),
              QFileInfo(mediaPath).canonicalFilePath());

    auto unlinked =
        service.unlinkMedia(item, episodes->front().id).wait();
    ASSERT_TRUE(unlinked) << unlinked.error().message.toStdString();
    auto afterUnlink = service.listLibraryMedia().wait();
    ASSERT_TRUE(afterUnlink) << afterUnlink.error().message.toStdString();
    EXPECT_TRUE(afterUnlink->front().associations.empty());
}

TEST(RemoteSubjectCatalog,
     FetchesCompleteSubjectAndEpisodesOnceThenLoadsFromDatabase) {
    auto database = openMemoryDatabase();
    auto catalog = openCatalog(database);
    auto libraryResult = LibraryStore::open(database).wait();
    ASSERT_TRUE(libraryResult) << libraryResult.error().message();
    auto library = std::move(*libraryResult);

    int subjectRequests = 0;
    int episodeRequests = 0;
    LocalMediaImportService service(
        library, catalog, LocalMediaImportService::SubjectLookup {},
        [&subjectRequests](std::int64_t subjectId)
            -> ilias::Task<BangumiResult<BangumiSubjectDetailsResponse>> {
            ++subjectRequests;
            EXPECT_EQ(subjectId, 400602);
            co_return BangumiSubjectDetailsResponse {
                .value = {
                    .id = 400602,
                    .type = BangumiSubjectType::Anime,
                    .name = QStringLiteral("Sousou no Frieren"),
                    .nameCn = QStringLiteral("葬送的芙莉莲"),
                    .summary = QStringLiteral("冒险结束后的故事"),
                    .date = QStringLiteral("2023-09-29"),
                    .images = {.large = QStringLiteral("https://example.test/large.jpg")},
                    .episodes = 1,
                    .totalEpisodes = 1,
                    .tags = {{.name = QStringLiteral("治愈"),
                              .count = 1200}},
                },
                .rawBody = {},
            };
        },
        [&episodeRequests](std::int64_t subjectId, int limit, int offset)
            -> ilias::Task<BangumiResult<BangumiEpisodeResponse>> {
            ++episodeRequests;
            EXPECT_EQ(subjectId, 400602);
            EXPECT_EQ(limit, 200);
            EXPECT_EQ(offset, 0);
            co_return BangumiEpisodeResponse {
                .value = {
                    .total = 1,
                    .limit = 200,
                    .offset = 0,
                    .data = {{
                        .id = 120001,
                        .type = 0,
                        .name = QStringLiteral("The Journey's End"),
                        .nameCn = QStringLiteral("冒险的终点"),
                        .sort = 1.0,
                        .episodeNumber = 1.0,
                        .airDate = QStringLiteral("2023-09-29"),
                        .durationSeconds = 1440,
                    }},
                },
                .rawBody = {},
            };
        });

    auto first = service.ensureBangumiSubject(400602).wait();
    ASSERT_TRUE(first) << first.error().message.toStdString();
    auto second = service.ensureBangumiSubject(400602).wait();
    ASSERT_TRUE(second) << second.error().message.toStdString();

    EXPECT_EQ(*first, *second);
    EXPECT_EQ(subjectRequests, 1);
    EXPECT_EQ(episodeRequests, 1);
    auto storedSubject = catalog.getSubject(*first).wait();
    ASSERT_TRUE(storedSubject) << storedSubject.error().message();
    ASSERT_TRUE(*storedSubject);
    EXPECT_EQ((*storedSubject)->summary.metadataLevel,
              SubjectMetadataLevel::Details);
    EXPECT_EQ((*storedSubject)->summary.titleCn,
              QStringLiteral("葬送的芙莉莲"));
    EXPECT_TRUE((*storedSubject)->metadataRefreshedAt);
    ASSERT_EQ((*storedSubject)->tags.size(), 1U);
    EXPECT_EQ((*storedSubject)->tags.front().name,
              QStringLiteral("治愈"));
    auto storedEpisodes = catalog.listEpisodes(*first).wait();
    ASSERT_TRUE(storedEpisodes) << storedEpisodes.error().message();
    ASSERT_EQ(storedEpisodes->size(), 1U);
    EXPECT_EQ(storedEpisodes->front().titleCn,
              QStringLiteral("冒险的终点"));
}

TEST(RemoteSubjectCatalog,
     RefreshesStaleDetailsAndUpdatesFutureEpisodeTitle) {
    auto database = openMemoryDatabase();
    auto catalog = openCatalog(database);
    auto libraryResult = LibraryStore::open(database).wait();
    ASSERT_TRUE(libraryResult) << libraryResult.error().message();
    auto library = std::move(*libraryResult);

    const QDateTime staleAt =
        QDateTime::currentDateTimeUtc().addSecs(-7 * 60 * 60);
    auto subject = catalog
                       .upsertSubjectSnapshot({
                           .origin =
                               {
                                   .providerKey = QStringLiteral("bangumi"),
                                   .externalId = QStringLiteral("400602"),
                               },
                           .metadataLevel = SubjectMetadataLevel::Details,
                           .subjectType = 2,
                           .title = QStringLiteral("Ongoing Anime"),
                           .fetchedAt = staleAt,
                       })
                       .wait();
    ASSERT_TRUE(subject) << subject.error().message();
    auto episodes = catalog
                        .upsertEpisodeSnapshots(
                            *subject,
                            {{
                                .origin =
                                    {
                                        .providerKey = QStringLiteral("bangumi"),
                                        .externalId = QStringLiteral("120004"),
                                    },
                                .sortOrder = 4,
                                .episodeType = 0,
                                .episodeNumber = 4.0,
                                .fetchedAt = staleAt,
                            }})
                        .wait();
    ASSERT_TRUE(episodes) << episodes.error().message();
    ASSERT_EQ(episodes->size(), 1U);
    const EpisodeId originalEpisode = episodes->front();

    int subjectRequests = 0;
    int episodeRequests = 0;
    LocalMediaImportService service(
        library, catalog, LocalMediaImportService::SubjectLookup {},
        [&subjectRequests](std::int64_t subjectId)
            -> ilias::Task<BangumiResult<BangumiSubjectDetailsResponse>> {
            ++subjectRequests;
            EXPECT_EQ(subjectId, 400602);
            co_return BangumiSubjectDetailsResponse {
                .value = {
                    .id = 400602,
                    .type = BangumiSubjectType::Anime,
                    .name = QStringLiteral("Ongoing Anime"),
                    .nameCn = QStringLiteral("连载中动画"),
                    .summary = QStringLiteral("连载中"),
                    .episodes = 4,
                    .totalEpisodes = 12,
                },
                .rawBody = {},
            };
        },
        [&episodeRequests](std::int64_t subjectId, int limit, int offset)
            -> ilias::Task<BangumiResult<BangumiEpisodeResponse>> {
            ++episodeRequests;
            EXPECT_EQ(subjectId, 400602);
            EXPECT_EQ(limit, 200);
            EXPECT_EQ(offset, 0);
            co_return BangumiEpisodeResponse {
                .value = {
                    .total = 1,
                    .limit = 200,
                    .offset = 0,
                    .data = {{
                        .id = 120004,
                        .type = 0,
                        .nameCn = QStringLiteral("新公布的标题"),
                        .sort = 4.0,
                        .episodeNumber = 4.0,
                    }},
                },
                .rawBody = {},
            };
        });

    auto refreshed = service.ensureBangumiSubject(400602).wait();

    ASSERT_TRUE(refreshed) << refreshed.error().message.toStdString();
    EXPECT_EQ(*refreshed, *subject);
    EXPECT_EQ(subjectRequests, 1);
    EXPECT_EQ(episodeRequests, 1);
    auto storedSubject = catalog.getSubject(*refreshed).wait();
    ASSERT_TRUE(storedSubject) << storedSubject.error().message();
    ASSERT_TRUE(*storedSubject);
    ASSERT_TRUE((*storedSubject)->metadataRefreshedAt);
    EXPECT_GT((*storedSubject)->metadataRefreshedAt->toMSecsSinceEpoch(),
              staleAt.toMSecsSinceEpoch());
    auto storedEpisodes = catalog.listEpisodes(*refreshed).wait();
    ASSERT_TRUE(storedEpisodes) << storedEpisodes.error().message();
    ASSERT_EQ(storedEpisodes->size(), 1U);
    EXPECT_EQ(storedEpisodes->front().id, originalEpisode);
    EXPECT_EQ(storedEpisodes->front().titleCn,
              QStringLiteral("新公布的标题"));
}

TEST(RemoteSubjectCatalog, KeepsExistingSummaryAvailableWhenRefreshFails) {
    auto database = openMemoryDatabase();
    auto catalog = openCatalog(database);
    auto libraryResult = LibraryStore::open(database).wait();
    ASSERT_TRUE(libraryResult) << libraryResult.error().message();
    auto library = std::move(*libraryResult);

    auto summary = catalog.upsertSubjectSnapshot({
        .origin = {.providerKey = QStringLiteral("bangumi"),
                   .externalId = QStringLiteral("400602")},
        .metadataLevel = SubjectMetadataLevel::Summary,
        .subjectType = 2,
        .title = QStringLiteral("Sousou no Frieren"),
        .titleCn = QStringLiteral("葬送的芙莉莲"),
        .fetchedAt = QDateTime::currentDateTimeUtc(),
    }).wait();
    ASSERT_TRUE(summary) << summary.error().message();

    int episodeRequests = 0;
    LocalMediaImportService service(
        library, catalog, LocalMediaImportService::SubjectLookup {},
        [](std::int64_t)
            -> ilias::Task<BangumiResult<BangumiSubjectDetailsResponse>> {
            co_return ilias::Err(bangumiError(
                BangumiErrorCode::NetworkError,
                QStringLiteral("fixture network unavailable")));
        },
        [&episodeRequests](std::int64_t, int, int)
            -> ilias::Task<BangumiResult<BangumiEpisodeResponse>> {
            ++episodeRequests;
            co_return ilias::Err(bangumiError(
                BangumiErrorCode::NetworkError,
                QStringLiteral("should not fetch episodes")));
        });

    auto resolved = service.ensureBangumiSubject(400602).wait();

    ASSERT_TRUE(resolved) << resolved.error().message.toStdString();
    EXPECT_EQ(*resolved, *summary);
    EXPECT_EQ(episodeRequests, 0);
    auto stored = catalog.getSubject(*resolved).wait();
    ASSERT_TRUE(stored) << stored.error().message();
    ASSERT_TRUE(*stored);
    EXPECT_EQ((*stored)->summary.metadataLevel,
              SubjectMetadataLevel::Summary);
}

TEST(LocalMediaImportService, RemovesLibraryEntryWithoutDeletingMediaFile) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString filePath = directory.filePath(QStringLiteral("Episode 01.mkv"));
    createFile(filePath);

    auto database = openMemoryDatabase();
    auto store = openLibrary(database);
    LocalMediaImportService importer(store);
    auto imported =
        importer.importFiles({QUrl::fromLocalFile(filePath)}).wait();
    ASSERT_TRUE(imported) << imported.error().message.toStdString();
    const auto itemId = imported->resources.front().items.front().id;

    auto removed = importer.removeMedia(itemId).wait();
    ASSERT_TRUE(removed) << removed.error().message.toStdString();
    EXPECT_TRUE(QFileInfo::exists(filePath));
    auto entries = importer.listMedia().wait();
    ASSERT_TRUE(entries) << entries.error().message.toStdString();
    EXPECT_TRUE(entries->empty());

    auto missing = importer.removeMedia(itemId).wait();
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code, LibraryErrorCode::MediaItemNotFound);
}

TEST(LocalMediaImportService, RejectsRemoteAndMissingFilesWithoutWriting) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto database = openMemoryDatabase();
    auto store = openLibrary(database);
    LocalMediaImportService importer(store);

    auto remote = importer
                      .importFiles(
                          {QUrl(QStringLiteral("https://example.test/a.mkv"))})
                      .wait();
    ASSERT_FALSE(remote);
    EXPECT_EQ(remote.error().code, LibraryErrorCode::InvalidMediaUrl);

    auto missing = importer
                       .importFiles({QUrl::fromLocalFile(
                           directory.filePath(QStringLiteral("missing.mkv")))})
                       .wait();
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code, LibraryErrorCode::MediaFileNotFound);

    auto entries = store.listMediaEntries().wait();
    ASSERT_TRUE(entries) << entries.error().message();
    EXPECT_TRUE(entries->empty());
}

#define EXPAND_IN_MAIN_WITH_ARGS(argc, argv)                                   \
  QCoreApplication qtApplication(argc, argv);                                 \
  ilias::QIoContext ioContext;                                                \
  ioContext.install()
#include "common/common_main.hpp.in"
