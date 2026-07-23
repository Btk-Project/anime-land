#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimeZone>

#include <ilias/platform/qt.hpp>

#include "persistence/catalog_store.hpp"
#include "persistence/database.hpp"
#include "persistence/database_schema.hpp"

#include <algorithm>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using anime_land::persistence::CatalogStore;
using anime_land::persistence::DatabaseBackend;
using anime_land::persistence::EpisodeSnapshot;
using anime_land::persistence::LocalDatabase;
using anime_land::persistence::LocalTagQuery;
using anime_land::persistence::SubjectId;
using anime_land::persistence::SubjectMetadataLevel;
using anime_land::persistence::SubjectSnapshot;
using anime_land::persistence::SubjectTagSnapshot;

namespace {

auto fromEpochMillis(qint64 value) -> QDateTime {
    return QDateTime::fromMSecsSinceEpoch(
        value, QTimeZone(QByteArrayLiteral("UTC")));
}

auto sampleSnapshot(QString externalId = QStringLiteral("400602"))
    -> SubjectSnapshot {
    return {
        .origin =
            {
                .providerKey = QStringLiteral("bangumi"),
                .externalId = std::move(externalId),
            },
        .metadataLevel = SubjectMetadataLevel::Details,
        .subjectType = 2,
        .title = QStringLiteral("Sousou no Frieren"),
        .titleCn = QStringLiteral("葬送的芙莉莲"),
        .summary = QStringLiteral("冒险结束后的故事"),
        .airDate = QDate(2023, 9, 29),
        .coverUrl = QUrl(QStringLiteral("https://example.test/frieren.jpg")),
        .aliases =
            std::vector<QString> {QStringLiteral("Frieren"),
                                  QStringLiteral("芙莉莲")},
        .tags =
            std::vector<SubjectTagSnapshot> {
                {.name = QStringLiteral("治愈"), .weight = 1200.0},
                {.name = QStringLiteral("奇幻"), .weight = 900.0},
            },
        .fetchedAt = fromEpochMillis(1'720'000'000'000),
        .remoteUpdatedAt = fromEpochMillis(1'719'000'000'000),
    };
}

auto sampleEpisode(QString externalId, int sortOrder,
                   QString title) -> EpisodeSnapshot {
    return {
        .origin =
            {
                .providerKey = QStringLiteral("bangumi"),
                .externalId = std::move(externalId),
            },
        .sortOrder = sortOrder,
        .episodeType = 0,
        .episodeNumber = static_cast<double>(sortOrder + 1),
        .title = std::move(title),
        .titleCn = QStringLiteral("章节标题"),
        .summary = QStringLiteral("章节简介"),
        .airDate = QDate(2023, 9, 29).addDays(sortOrder),
        .duration = std::chrono::minutes(24),
        .fetchedAt = fromEpochMillis(1'720'000'000'000 + sortOrder),
        .remoteUpdatedAt =
            fromEpochMillis(1'719'000'000'000 + sortOrder),
    };
}

auto openMemoryDatabase() -> LocalDatabase {
    anime_land::SqlSettings settings;
    settings.database_type = "sqlite";
    settings.database_path = ":memory:";
    settings.database_password.clear();
    auto opened = LocalDatabase::open(settings).wait();
    if (!opened) {
        throw std::runtime_error(opened.error().message());
    }
    return std::move(*opened);
}

} // namespace

TEST(LocalDatabaseMigration, CreatesAndValidatesCatalogSchemaIdempotently) {
    auto database = openMemoryDatabase();
    EXPECT_EQ(database.backend(), DatabaseBackend::Sqlite);

    auto version = database.schemaVersion().wait();
    ASSERT_TRUE(version) << version.error().message();
    EXPECT_EQ(*version, anime_land::persistence::kCurrentSchemaVersion);

    auto foreignKeys = database.foreignKeysEnabled().wait();
    ASSERT_TRUE(foreignKeys) << foreignKeys.error().message();
    EXPECT_TRUE(*foreignKeys);

    auto migratedAgain = database.migrate().wait();
    ASSERT_TRUE(migratedAgain) << migratedAgain.error().message();

    auto validated = database.validateSchema().wait();
    EXPECT_TRUE(validated) << validated.error().message();
}

TEST(LocalDatabaseMigration, EnforcesForeignKeys) {
    auto database = openMemoryDatabase();

    auto inserted = database.advancedConnection()
                        .execute(
                            "INSERT INTO subject_external_refs("
                            "subject_id, provider_key, external_id, fetched_at"
                            ") VALUES (999, 'bangumi', 'missing', 1)")
                        .wait();
    EXPECT_FALSE(inserted);
}

TEST(LocalDatabaseMigration, GeneratesSchemaFromIliasSqlDescriptions) {
    const auto metadata = ilias::sql::detail::extractSqlColumnMetadata(
        anime_land::persistence::schema::SubjectTypeConstraint {});
    EXPECT_TRUE(metadata.tags.not_null);
    EXPECT_EQ(metadata.check_expression, "subject_type >= 0");

    auto statements =
        anime_land::persistence::schema::catalogMigrationV1Statements(
            "sqlite");
    ASSERT_TRUE(statements) << statements.error().message();

    const auto findTable = [&statements](std::string_view table) {
        return std::ranges::find_if(
            *statements, [table](const std::string &statement) {
                return statement.starts_with("CREATE TABLE \"" +
                                             std::string(table) + "\" ");
            });
    };

    const auto subjects = findTable("subjects");
    ASSERT_NE(subjects, statements->end());
    EXPECT_NE(subjects->find("CHECK (subject_type >= 0)"), std::string::npos);
    EXPECT_NE(subjects->find("CHECK (metadata_level BETWEEN 0 AND 1)"),
              std::string::npos);
    EXPECT_NE(subjects->find("DEFAULT '[]'"), std::string::npos);

    const auto subjectRefs = findTable("subject_external_refs");
    ASSERT_NE(subjectRefs, statements->end());
    EXPECT_NE(subjectRefs->find(
                  "PRIMARY KEY (\"provider_key\", \"external_id\")"),
              std::string::npos);
    EXPECT_NE(subjectRefs->find(
                  "FOREIGN KEY (\"subject_id\") REFERENCES \"subjects\" "
                  "(\"id\") ON DELETE CASCADE"),
              std::string::npos);
}

TEST(LocalDatabaseMigration, GeneratesMysqlSchemaWithoutSqliteFts) {
    auto statements =
        anime_land::persistence::schema::catalogMigrationV1Statements(
            "mysql");
    ASSERT_TRUE(statements) << statements.error().message();

    const auto subjects = std::ranges::find_if(
        *statements, [](const std::string &statement) {
            return statement.starts_with("CREATE TABLE `subjects` ");
        });
    ASSERT_NE(subjects, statements->end());
    EXPECT_NE(subjects->find("CHECK (subject_type >= 0)"), std::string::npos);

    const auto subjectRefs = std::ranges::find_if(
        *statements, [](const std::string &statement) {
            return statement.starts_with(
                "CREATE TABLE `subject_external_refs` ");
        });
    ASSERT_NE(subjectRefs, statements->end());
    EXPECT_NE(subjectRefs->find("`provider_key` VARCHAR(64)"),
              std::string::npos);
    EXPECT_NE(subjectRefs->find("`external_id` VARCHAR(255)"),
              std::string::npos);
    EXPECT_NE(subjectRefs->find(
                  "PRIMARY KEY (`provider_key`, `external_id`)"),
              std::string::npos);

    EXPECT_EQ(std::ranges::find_if(
                  *statements, [](const std::string &statement) {
                      return statement.contains("subject_fts") ||
                             statement.contains("fts5");
                  }),
              statements->end());
}

TEST(LocalDatabaseMigration, EnforcesGeneratedChecksAndCompositeKeys) {
    auto database = openMemoryDatabase();

    auto invalidSubject = database.advancedConnection()
                              .execute(
                                  "INSERT INTO subjects("
                                  "subject_type, title, created_at, updated_at"
                                  ") VALUES (-1, 'invalid', 1, 1)")
                              .wait();
    EXPECT_FALSE(invalidSubject);

    auto invalidTag = database.advancedConnection()
                          .execute(
                              "INSERT INTO tags(normalized_name, display_name) "
                              "VALUES ('', 'invalid')")
                          .wait();
    EXPECT_FALSE(invalidTag);

    auto subject = database.advancedConnection()
                       .execute(
                           "INSERT INTO subjects("
                           "title, created_at, updated_at"
                           ") VALUES ('valid', 1, 1)")
                       .wait();
    ASSERT_TRUE(subject) << subject.error().message();

    auto firstRef = database.advancedConnection()
                        .execute(
                            "INSERT INTO subject_external_refs("
                            "subject_id, provider_key, external_id, fetched_at"
                            ") VALUES (1, 'bangumi', '1', 1)")
                        .wait();
    ASSERT_TRUE(firstRef) << firstRef.error().message();
    auto duplicateRef = database.advancedConnection()
                            .execute(
                                "INSERT INTO subject_external_refs("
                                "subject_id, provider_key, external_id, "
                                "fetched_at"
                                ") VALUES (1, 'bangumi', '1', 2)")
                            .wait();
    EXPECT_FALSE(duplicateRef);
}

TEST(LocalDatabaseMigration, ReopensMigratedFileDatabase) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto path =
        QFileInfo(directory.filePath(QStringLiteral("anime-land.sqlite3")))
            .filesystemFilePath();

    {
        anime_land::SqlSettings settings;
        settings.database_type = "sqlite";
        settings.database_path = path.string();
        settings.database_password.clear();
        auto opened = LocalDatabase::open(settings).wait();
        ASSERT_TRUE(opened) << opened.error().message();
        auto closed = opened->close().wait();
        ASSERT_TRUE(closed) << closed.error().message();
    }

    anime_land::SqlSettings settings;
    settings.database_type = "sqlite";
    settings.database_path = path.string();
    settings.database_password.clear();
    auto reopened = LocalDatabase::open(settings).wait();
    ASSERT_TRUE(reopened) << reopened.error().message();
    auto version = reopened->schemaVersion().wait();
    ASSERT_TRUE(version) << version.error().message();
    EXPECT_EQ(*version, anime_land::persistence::kCurrentSchemaVersion);
}

TEST(LocalDatabaseConfiguration, RejectsUnknownBackend) {
    anime_land::SqlSettings settings;
    settings.database_type = "unknown";

    auto opened = LocalDatabase::open(settings).wait();
    EXPECT_FALSE(opened);
}

TEST(LocalDatabaseConfiguration, OpensEncryptedDatabaseFromSqlSettings) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());

    anime_land::SqlSettings settings;
    settings.database_type = "sqlcipher";
    settings.database_password = "correct horse battery staple";
    settings.database_path =
        QFileInfo(directory.filePath(QStringLiteral("encrypted.sqlite3")))
            .filesystemFilePath()
            .string();

    {
        auto opened = LocalDatabase::open(settings).wait();
        ASSERT_TRUE(opened) << opened.error().message();
        EXPECT_EQ(opened->backend(), DatabaseBackend::Sqlite);
        auto closed = opened->close().wait();
        ASSERT_TRUE(closed) << closed.error().message();
    }

    settings.database_password = "wrong password";
    auto wrongPassword = LocalDatabase::open(settings).wait();
    EXPECT_FALSE(wrongPassword);
}

TEST(CatalogStore, UpsertsByExternalIdentityWithoutDowngradingDetails) {
    auto database = openMemoryDatabase();
    CatalogStore store(database);

    auto detailId = store.upsertSubjectSnapshot(sampleSnapshot()).wait();
    ASSERT_TRUE(detailId) << detailId.error().message();

    auto summary = sampleSnapshot();
    summary.metadataLevel = SubjectMetadataLevel::Summary;
    summary.title = QStringLiteral("Sousou no Frieren (search)");
    summary.titleCn.reset();
    summary.summary.reset();
    summary.airDate.reset();
    summary.coverUrl.reset();
    summary.aliases.reset();
    summary.tags.reset();
    summary.fetchedAt = fromEpochMillis(1'730'000'000'000);

    auto summaryId = store.upsertSubjectSnapshot(std::move(summary)).wait();
    ASSERT_TRUE(summaryId) << summaryId.error().message();
    EXPECT_EQ(*summaryId, *detailId);

    auto loaded = store.getSubject(*detailId).wait();
    ASSERT_TRUE(loaded) << loaded.error().message();
    ASSERT_TRUE(*loaded);
    EXPECT_EQ((*loaded)->summary.metadataLevel, SubjectMetadataLevel::Details);
    EXPECT_EQ((*loaded)->summary.titleCn,
              std::optional<QString>(QStringLiteral("葬送的芙莉莲")));
    EXPECT_EQ((*loaded)->summary.summary,
              std::optional<QString>(QStringLiteral("冒险结束后的故事")));
    EXPECT_EQ((*loaded)->aliases.size(), 2U);
    EXPECT_EQ((*loaded)->tags.size(), 2U);
    ASSERT_EQ((*loaded)->externalRefs.size(), 1U);
    EXPECT_EQ((*loaded)->externalRefs.front().ref.providerKey,
              QStringLiteral("bangumi"));
    EXPECT_EQ((*loaded)->externalRefs.front().ref.externalId,
              QStringLiteral("400602"));
    ASSERT_TRUE((*loaded)->metadataRefreshedAt);
    EXPECT_EQ((*loaded)->metadataRefreshedAt->toMSecsSinceEpoch(),
              1'720'000'000'000);
}

TEST(CatalogStore, KeepsSameTitlesWithDifferentExternalIdsSeparate) {
    auto database = openMemoryDatabase();
    CatalogStore store(database);

    auto first = store.upsertSubjectSnapshot(sampleSnapshot("1")).wait();
    auto second = store.upsertSubjectSnapshot(sampleSnapshot("2")).wait();
    ASSERT_TRUE(first) << first.error().message();
    ASSERT_TRUE(second) << second.error().message();
    EXPECT_NE(*first, *second);
}

TEST(CatalogStore, SearchesFtsAliasesAndExactNormalizedTags) {
    auto database = openMemoryDatabase();
    CatalogStore store(database);

    auto inserted = store.upsertSubjectSnapshot(sampleSnapshot()).wait();
    ASSERT_TRUE(inserted) << inserted.error().message();

    auto byAlias =
        store.searchSubjects(
                 {.text = QStringLiteral("Frieren"),
                  .tag = std::nullopt,
                  .limit = 10,
                  .offset = 0})
            .wait();
    ASSERT_TRUE(byAlias) << byAlias.error().message();
    ASSERT_EQ(byAlias->size(), 1U);
    EXPECT_EQ(byAlias->front().id, *inserted);

    auto byTag =
        store.searchSubjects({.text = {},
                              .tag = QStringLiteral(" 治愈 "),
                              .limit = 10,
                              .offset = 0})
            .wait();
    ASSERT_TRUE(byTag) << byTag.error().message();
    ASSERT_EQ(byTag->size(), 1U);
    EXPECT_EQ(byTag->front().id, *inserted);

    auto replaced = sampleSnapshot();
    replaced.tags = std::vector<SubjectTagSnapshot> {};
    auto replacedId = store.upsertSubjectSnapshot(std::move(replaced)).wait();
    ASSERT_TRUE(replacedId) << replacedId.error().message();

    byTag = store.searchSubjects({.text = {},
                                  .tag = QStringLiteral("治愈"),
                                  .limit = 10,
                                  .offset = 0})
                .wait();
    ASSERT_TRUE(byTag) << byTag.error().message();
    EXPECT_TRUE(byTag->empty());
}

TEST(CatalogStore, UpsertsFindsAndListsTypedEpisodeSnapshots) {
    auto database = openMemoryDatabase();
    CatalogStore store(database);

    auto subject = store.upsertSubjectSnapshot(sampleSnapshot()).wait();
    ASSERT_TRUE(subject) << subject.error().message();

    std::vector<EpisodeSnapshot> snapshots;
    snapshots.push_back(
        sampleEpisode(QStringLiteral("ep-2"), 1, QStringLiteral("Episode 2")));
    snapshots.push_back(
        sampleEpisode(QStringLiteral("ep-1"), 0, QStringLiteral("Episode 1")));
    auto inserted =
        store.upsertEpisodeSnapshots(*subject, std::move(snapshots)).wait();
    ASSERT_TRUE(inserted) << inserted.error().message();
    ASSERT_EQ(inserted->size(), 2U);

    auto found =
        store.findEpisodeByExternalRef(
                 {.providerKey = QStringLiteral("bangumi"),
                  .externalId = QStringLiteral("ep-1")})
            .wait();
    ASSERT_TRUE(found) << found.error().message();
    ASSERT_TRUE(*found);
    EXPECT_EQ(**found, inserted->at(1));

    auto episode = store.getEpisode(**found).wait();
    ASSERT_TRUE(episode) << episode.error().message();
    ASSERT_TRUE(*episode);
    EXPECT_EQ((*episode)->title,
              std::optional<QString>(QStringLiteral("Episode 1")));

    auto episodes = store.listEpisodes(*subject).wait();
    ASSERT_TRUE(episodes) << episodes.error().message();
    ASSERT_EQ(episodes->size(), 2U);
    EXPECT_EQ(episodes->at(0).id, inserted->at(1));
    EXPECT_EQ(episodes->at(0).sortOrder, 0);
    EXPECT_EQ(episodes->at(0).title,
              std::optional<QString>(QStringLiteral("Episode 1")));
    EXPECT_EQ(episodes->at(0).duration,
              std::optional<std::chrono::milliseconds>(
                  std::chrono::minutes(24)));
    ASSERT_EQ(episodes->at(0).externalRefs.size(), 1U);
    EXPECT_EQ(episodes->at(0).externalRefs.front().ref.externalId,
              QStringLiteral("ep-1"));

    std::vector<EpisodeSnapshot> update;
    update.push_back(sampleEpisode(QStringLiteral("ep-1"), 0,
                                   QStringLiteral("Episode 1 updated")));
    auto updated =
        store.upsertEpisodeSnapshots(*subject, std::move(update)).wait();
    ASSERT_TRUE(updated) << updated.error().message();
    ASSERT_EQ(updated->size(), 1U);
    EXPECT_EQ(updated->front(), inserted->at(1));

    episodes = store.listEpisodes(*subject).wait();
    ASSERT_TRUE(episodes) << episodes.error().message();
    EXPECT_EQ(episodes->front().title,
              std::optional<QString>(QStringLiteral("Episode 1 updated")));
}

TEST(CatalogStore, RejectsEpisodeSnapshotsForUnknownSubject) {
    auto database = openMemoryDatabase();
    CatalogStore store(database);

    std::vector<EpisodeSnapshot> snapshots;
    snapshots.push_back(
        sampleEpisode(QStringLiteral("ep-1"), 0, QStringLiteral("Episode 1")));
    auto inserted =
        store.upsertEpisodeSnapshots(SubjectId {999}, std::move(snapshots))
            .wait();
    EXPECT_FALSE(inserted);
}

TEST(CatalogStore, ListsTypedTagFacetsWithLiteralPrefix) {
    auto database = openMemoryDatabase();
    CatalogStore store(database);

    auto first = store.upsertSubjectSnapshot(sampleSnapshot("1")).wait();
    ASSERT_TRUE(first) << first.error().message();

    auto secondSnapshot = sampleSnapshot("2");
    secondSnapshot.tags =
        std::vector<SubjectTagSnapshot> {
            {.name = QStringLiteral("治愈"), .weight = 500.0},
            {.name = QStringLiteral("%特殊"), .weight = std::nullopt},
        };
    auto second =
        store.upsertSubjectSnapshot(std::move(secondSnapshot)).wait();
    ASSERT_TRUE(second) << second.error().message();

    auto healing =
        store.listTags(LocalTagQuery {
                           .prefix = QStringLiteral(" 治 "),
                           .limit = 10,
                           .offset = 0,
                       })
            .wait();
    ASSERT_TRUE(healing) << healing.error().message();
    ASSERT_EQ(healing->size(), 1U);
    EXPECT_EQ(healing->front().name, QStringLiteral("治愈"));
    EXPECT_EQ(healing->front().subjectCount, 2);

    auto literalWildcard =
        store.listTags(LocalTagQuery {
                           .prefix = QStringLiteral("%"),
                           .limit = 10,
                           .offset = 0,
                       })
            .wait();
    ASSERT_TRUE(literalWildcard) << literalWildcard.error().message();
    ASSERT_EQ(literalWildcard->size(), 1U);
    EXPECT_EQ(literalWildcard->front().name, QStringLiteral("%特殊"));

    auto cleared =
        store.replaceSubjectTags(*first, QStringLiteral("bangumi"), {}).wait();
    ASSERT_TRUE(cleared) << cleared.error().message();

    healing = store.listTags(LocalTagQuery {
                                 .prefix = QStringLiteral("治"),
                                 .limit = 10,
                                 .offset = 0,
                             })
                  .wait();
    ASSERT_TRUE(healing) << healing.error().message();
    ASSERT_EQ(healing->size(), 1U);
    EXPECT_EQ(healing->front().subjectCount, 1);
}

#define EXPAND_IN_MAIN_WITH_ARGS(argc, argv)                                    \
  QCoreApplication qtApplication(argc, argv);                                   \
  ilias::QIoContext ioContext;                                                  \
  ioContext.install();                                                          \
  if (!qEnvironmentVariableIsSet("QT_MESSAGE_PATTERN")) {                       \
        qSetMessagePattern(QStringLiteral(                                      \
            "[%{time yyyy-MM-dd hh:mm:ss.zzz}] [%{type}] "                      \
            "[%{file}:%{line}] %{message}"));                                   \
    }
#include "../common/common_main.hpp.in"
