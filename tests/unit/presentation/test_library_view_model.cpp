#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>

#include <ilias/platform/qt.hpp>

#include "presentation/library/library_view_model.hpp"

#include <memory>

using namespace anime_land;

namespace {

auto sampleEntries(QString fileName = QStringLiteral("episode-01.mkv"))
    -> std::vector<MediaEntry> {
    return {{
        .resource =
            {
                .id = MediaResourceId {7},
                .providerKey = QStringLiteral("local-file"),
                .stableKey = QStringLiteral("d:/anime/frieren"),
                .descriptorVersion = 1,
                .descriptor = QByteArrayLiteral(R"({"directory":"D:/Anime/Frieren"})"),
                .displayName = QStringLiteral("Frieren"),
            },
        .item =
            {
                .id = SourceItemId {11},
                .resourceId = MediaResourceId {7},
                .stableKey = fileName,
                .descriptor = QByteArrayLiteral(R"({"relativePath":"episode-01.mkv"})"),
                .displayName = std::move(fileName),
                .duration = std::nullopt,
            },
    }};
}

void processUntilSettled(LibraryViewModel &viewModel) {
    QElapsedTimer timer;
    timer.start();
    while ((viewModel.loading() || viewModel.importing()
            || viewModel.removing() || viewModel.associating()
            || viewModel.playing())
           && timer.elapsed() < 1000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

TEST(LibraryViewModel, RemovesMediaThenReloadsWithoutDeletingThroughQml) {
    auto entries = sampleEntries();
    SourceItemId received;
    LibraryViewModel viewModel(
        [&entries]() -> ilias::Task<LibraryResult<std::vector<MediaEntry>>> {
            co_return entries;
        },
        {},
        [&entries, &received](SourceItemId item)
            -> ilias::Task<LibraryResult<void>> {
            received = item;
            entries.clear();
            co_return LibraryResult<void> {};
        });

    viewModel.refresh();
    processUntilSettled(viewModel);
    ASSERT_EQ(viewModel.mediaCount(), 1);

    viewModel.removeMedia(11);
    processUntilSettled(viewModel);

    EXPECT_FALSE(viewModel.removing());
    EXPECT_EQ(received, SourceItemId {11});
    EXPECT_EQ(viewModel.mediaCount(), 0);
    EXPECT_TRUE(viewModel.errorMessage().isEmpty());
    EXPECT_EQ(viewModel.noticeMessage(),
              QStringLiteral(
                  "已从媒体库移除 1 个媒体文件，磁盘文件未删除"));
}

} // namespace

TEST(LibraryViewModel, MapsLocalMediaToQmlState) {
    LibraryViewModel viewModel(
        []() -> ilias::Task<LibraryResult<std::vector<MediaEntry>>> {
            co_return sampleEntries();
        },
        {});

    viewModel.refresh();
    processUntilSettled(viewModel);

    EXPECT_FALSE(viewModel.loading());
    EXPECT_TRUE(viewModel.errorMessage().isEmpty());
    EXPECT_EQ(viewModel.mediaCount(), 1);
    ASSERT_EQ(viewModel.mediaItems().size(), 1);
    const auto media = viewModel.mediaItems().front().toMap();
    EXPECT_EQ(media.value(QStringLiteral("id")).toLongLong(), 11);
    EXPECT_EQ(media.value(QStringLiteral("resourceId")).toLongLong(), 7);
    EXPECT_EQ(media.value(QStringLiteral("title")).toString(),
              QStringLiteral("episode-01.mkv"));
    EXPECT_EQ(media.value(QStringLiteral("subtitle")).toString(),
              QStringLiteral("Frieren"));
    EXPECT_EQ(media.value(QStringLiteral("status")).toString(),
              QStringLiteral("未关联"));
    EXPECT_TRUE(viewModel.subjectGroups().isEmpty());
    ASSERT_EQ(viewModel.unassociatedGroups().size(), 1);
    EXPECT_EQ(viewModel.unassociatedGroups()
                  .front()
                  .toMap()
                  .value(QStringLiteral("items"))
                  .toList()
                  .size(),
              1);
}

TEST(LibraryViewModel, MapsPersistedEpisodeAssociationToMediaCard) {
    LibraryViewModel viewModel(
        []() -> ilias::Task<
            LibraryResult<std::vector<LibraryMediaEntry>>> {
            co_return std::vector<LibraryMediaEntry> {{
                .media = sampleEntries().front(),
                .associations = {{
                    .episodeId = EpisodeId {31},
                    .subjectId = SubjectId {21},
                    .subjectTitle = QStringLiteral("葬送的芙莉莲"),
                    .episodeTitle = QStringLiteral("冒险的终点"),
                    .episodeNumber = 1.0,
                }},
            }};
        });

    viewModel.refresh();
    processUntilSettled(viewModel);

    ASSERT_EQ(viewModel.mediaItems().size(), 1);
    const auto media = viewModel.mediaItems().front().toMap();
    EXPECT_EQ(media.value(QStringLiteral("status")).toString(),
              QStringLiteral("已关联"));
    EXPECT_EQ(media.value(QStringLiteral("associationCount")).toInt(), 1);
    EXPECT_EQ(media.value(QStringLiteral("subtitle")).toString(),
              QStringLiteral("葬送的芙莉莲 · EP1 · 冒险的终点"));
    const auto associations =
        media.value(QStringLiteral("associations")).toList();
    ASSERT_EQ(associations.size(), 1);
    EXPECT_EQ(associations.front()
                  .toMap()
                  .value(QStringLiteral("episodeId"))
                  .toLongLong(),
              31);
    EXPECT_TRUE(viewModel.unassociatedGroups().isEmpty());
    ASSERT_EQ(viewModel.subjectGroups().size(), 1);
    const auto subject = viewModel.subjectGroups().front().toMap();
    EXPECT_EQ(subject.value(QStringLiteral("subjectId")).toLongLong(), 21);
    EXPECT_EQ(subject.value(QStringLiteral("title")).toString(),
              QStringLiteral("葬送的芙莉莲"));
    EXPECT_EQ(subject.value(QStringLiteral("episodeCount")).toInt(), 1);
    EXPECT_EQ(subject.value(QStringLiteral("mediaCount")).toInt(), 1);
    const auto episodeGroups =
        subject.value(QStringLiteral("episodes")).toList();
    ASSERT_EQ(episodeGroups.size(), 1);
    const auto episode = episodeGroups.front().toMap();
    EXPECT_EQ(episode.value(QStringLiteral("episodeId")).toLongLong(), 31);
    EXPECT_EQ(episode.value(QStringLiteral("number")).toString(),
              QStringLiteral("EP1"));
    const auto episodeItems =
        episode.value(QStringLiteral("items")).toList();
    ASSERT_EQ(episodeItems.size(), 1);
    EXPECT_EQ(episodeItems.front()
                  .toMap()
                  .value(QStringLiteral("subtitle"))
                  .toString(),
              QStringLiteral("Frieren"));
    EXPECT_EQ(episodeItems.front()
                  .toMap()
                  .value(QStringLiteral("contextAssociation"))
                  .toMap()
                  .value(QStringLiteral("episodeId"))
                  .toLongLong(),
              31);
}

TEST(LibraryViewModel,
     SortsSubjectEpisodesAndKeepsUnassociatedFilesSeparate) {
    LibraryViewModel viewModel(
        []() -> ilias::Task<
            LibraryResult<std::vector<LibraryMediaEntry>>> {
            auto episodeTwo =
                sampleEntries(QStringLiteral("episode-02.mkv")).front();
            episodeTwo.item.id = SourceItemId {12};
            episodeTwo.item.stableKey = QStringLiteral("episode-02.mkv");
            episodeTwo.item.displayName = QStringLiteral("episode-02.mkv");

            auto pending =
                sampleEntries(QStringLiteral("pending-03.mkv")).front();
            pending.resource.id = MediaResourceId {8};
            pending.resource.stableKey = QStringLiteral("d:/anime/pending");
            pending.resource.displayName = QStringLiteral("Pending");
            pending.item.id = SourceItemId {13};
            pending.item.resourceId = MediaResourceId {8};
            pending.item.stableKey = QStringLiteral("pending-03.mkv");
            pending.item.displayName = QStringLiteral("pending-03.mkv");

            co_return std::vector<LibraryMediaEntry> {
                {
                    .media = std::move(episodeTwo),
                    .associations = {{
                        .episodeId = EpisodeId {32},
                        .subjectId = SubjectId {21},
                        .subjectTitle = QStringLiteral("葬送的芙莉莲"),
                        .episodeTitle = QStringLiteral("不需要魔法也能看见的世界"),
                        .episodeNumber = 2.0,
                        .episodeType = 0,
                        .sortOrder = 1,
                    }},
                },
                {
                    .media = std::move(pending),
                    .associations = {},
                },
                {
                    .media = sampleEntries().front(),
                    .associations = {{
                        .episodeId = EpisodeId {31},
                        .subjectId = SubjectId {21},
                        .subjectTitle = QStringLiteral("葬送的芙莉莲"),
                        .episodeTitle = QStringLiteral("冒险的终点"),
                        .episodeNumber = 1.0,
                        .episodeType = 0,
                        .sortOrder = 0,
                    }},
                },
            };
        });

    viewModel.refresh();
    processUntilSettled(viewModel);

    EXPECT_EQ(viewModel.mediaCount(), 3);
    ASSERT_EQ(viewModel.subjectGroups().size(), 1);
    const auto subject = viewModel.subjectGroups().front().toMap();
    EXPECT_EQ(subject.value(QStringLiteral("mediaCount")).toInt(), 2);
    const auto episodes = subject.value(QStringLiteral("episodes")).toList();
    ASSERT_EQ(episodes.size(), 2);
    EXPECT_EQ(episodes.at(0)
                  .toMap()
                  .value(QStringLiteral("number"))
                  .toString(),
              QStringLiteral("EP1"));
    EXPECT_EQ(episodes.at(1)
                  .toMap()
                  .value(QStringLiteral("number"))
                  .toString(),
              QStringLiteral("EP2"));
    ASSERT_EQ(viewModel.unassociatedGroups().size(), 1);
    const auto pendingItems = viewModel.unassociatedGroups()
                                  .front()
                                  .toMap()
                                  .value(QStringLiteral("items"))
                                  .toList();
    ASSERT_EQ(pendingItems.size(), 1);
    EXPECT_EQ(pendingItems.front()
                  .toMap()
                  .value(QStringLiteral("id"))
                  .toLongLong(),
              13);
}

TEST(LibraryViewModel, ImportsThenReloadsMediaAndReportsDuplicates) {
    int loads = 0;
    QList<QUrl> received;
    LibraryViewModel viewModel(
        [&loads]() -> ilias::Task<LibraryResult<std::vector<MediaEntry>>> {
            ++loads;
            co_return sampleEntries(QStringLiteral("episode-02.mkv"));
        },
        [&received](QList<QUrl> files)
            -> ilias::Task<LibraryResult<LocalMediaImportResult>> {
            received = std::move(files);
            co_return LocalMediaImportResult {
                .resources = {},
                .persistedFileCount = 1,
                .duplicateSelectionCount = 1,
            };
        });

    const QUrl selected = QUrl::fromLocalFile(
        QStringLiteral("D:/Anime/Frieren/episode-02.mkv"));
    viewModel.importFiles({selected});
    processUntilSettled(viewModel);

    EXPECT_FALSE(viewModel.importing());
    EXPECT_EQ(loads, 1);
    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(received.front(), selected);
    EXPECT_EQ(viewModel.mediaCount(), 1);
    EXPECT_EQ(viewModel.noticeMessage(),
              QStringLiteral("已导入 1 个媒体文件，忽略 1 个重复选择"));
}

TEST(LibraryViewModel, PresentsImportErrorsWithoutReplacingMedia) {
    LibraryViewModel viewModel(
        []() -> ilias::Task<LibraryResult<std::vector<MediaEntry>>> {
            co_return sampleEntries();
        },
        [](QList<QUrl>)
            -> ilias::Task<LibraryResult<LocalMediaImportResult>> {
            co_return ilias::Err(libraryError(
                LibraryErrorCode::MediaFileNotFound,
                QStringLiteral("fixture file missing")));
        });

    viewModel.refresh();
    processUntilSettled(viewModel);
    ASSERT_EQ(viewModel.mediaCount(), 1);

    viewModel.importFiles(
        {QUrl::fromLocalFile(QStringLiteral("missing"))});
    processUntilSettled(viewModel);

    EXPECT_FALSE(viewModel.importing());
    EXPECT_EQ(viewModel.mediaCount(), 1);
    EXPECT_EQ(viewModel.errorMessage(), QStringLiteral("fixture file missing"));
}

TEST(LibraryViewModel, AcceptsQmlListOfLocalUrlsWithoutLosingScheme) {
    QList<QUrl> received;
    LibraryViewModel viewModel(
        []() -> ilias::Task<LibraryResult<std::vector<MediaEntry>>> {
            co_return std::vector<MediaEntry> {};
        },
        [&received](QList<QUrl> files)
            -> ilias::Task<LibraryResult<LocalMediaImportResult>> {
            received = std::move(files);
            co_return LocalMediaImportResult {
                .resources = {},
                .persistedFileCount = 1,
                .duplicateSelectionCount = 0,
            };
        });

    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("libraryViewModel"),
                                             &viewModel);
    QQmlComponent component(&engine);
    component.setData(
        QByteArrayLiteral(R"(
import QtQml
QtObject {
    property list<url> selectedFiles: [
        "file:///D:/Videos/%5BLoliHouse%5D%20Episode%20-%2001.mkv"
    ]
    Component.onCompleted: libraryViewModel.importFiles(selectedFiles)
}
)"),
        QUrl(QStringLiteral("qrc:/tests/library_url_boundary.qml")));
    std::unique_ptr<QObject> object(component.create());
    ASSERT_TRUE(object) << component.errorString().toStdString();
    processUntilSettled(viewModel);

    ASSERT_EQ(received.size(), 1);
    EXPECT_TRUE(received.front().isLocalFile());
    EXPECT_EQ(received.front().toLocalFile(),
              QStringLiteral("D:/Videos/[LoliHouse] Episode - 01.mkv"));
}

#define EXPAND_IN_MAIN_WITH_ARGS(argc, argv)                                   \
  QCoreApplication qtApplication(argc, argv);                                 \
  ilias::QIoContext ioContext;                                                \
  ioContext.install()
#include "common/common_main.hpp.in"
