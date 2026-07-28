#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>

#include <ilias/platform/qt.hpp>

#include "presentation/library/subject_details_view_model.hpp"

#include <algorithm>
#include <chrono>
#include <optional>
#include <vector>

using namespace anime_land;
using namespace std::chrono_literals;

namespace {

auto sampleDetails() -> SubjectLibraryDetails {
    return {
        .subject =
            {
                .summary =
                    {
                        .id = SubjectId {21},
                        .subjectType = 2,
                        .title = QStringLiteral("Sousou no Frieren"),
                        .titleCn = QStringLiteral("葬送的芙莉莲"),
                        .summary = QStringLiteral("冒险结束后的故事"),
                    },
                .airDate = QDate(2023, 9, 29),
                .tags = {{.name = QStringLiteral("治愈"),
                          .providerKey = QStringLiteral("bangumi"),
                          .weight = 1200.0}},
                .externalRefs = {{
                    .ref = {.providerKey = QStringLiteral("bangumi"),
                            .externalId = QStringLiteral("400602")},
                    .fetchedAt = QDateTime::currentDateTimeUtc(),
                    .remoteUpdatedAt = std::nullopt,
                }},
            },
        .episodes = {{
            .episode =
                {
                    .id = EpisodeId {31},
                    .subjectId = SubjectId {21},
                    .sortOrder = 0,
                    .episodeType = 0,
                    .episodeNumber = 1.0,
                    .title = QStringLiteral("The Journey's End"),
                    .titleCn = QStringLiteral("冒险的终点"),
                    .duration = 24min,
                },
            .media = {{
                .resource =
                    {
                        .id = MediaResourceId {7},
                        .providerKey = QStringLiteral("local-file"),
                        .stableKey = QStringLiteral("d:/anime/frieren"),
                        .descriptorVersion = 1,
                        .descriptor = QByteArrayLiteral(
                            R"({"directory":"D:/Anime/Frieren"})"),
                        .displayName = QStringLiteral("Frieren"),
                    },
                .item =
                    {
                        .id = SourceItemId {11},
                        .resourceId = MediaResourceId {7},
                        .stableKey = QStringLiteral("episode-01.mkv"),
                        .descriptor = QByteArrayLiteral(
                            R"({"relativePath":"episode-01.mkv"})"),
                        .displayName = QStringLiteral("episode-01.mkv"),
                        .duration = 24min,
                    },
            }},
        }},
    };
}

void processUntilSettled(SubjectDetailsViewModel &viewModel) {
    QElapsedTimer timer;
    timer.start();
    while ((viewModel.loading() || viewModel.loadingMore()
            || viewModel.playing())
           && timer.elapsed() < 1000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

} // namespace

TEST(SubjectDetailsViewModel, MapsDatabaseSubjectEpisodesAndLinkedMedia) {
    SubjectId loadedSubject;
    SubjectDetailsViewModel viewModel(
        [&loadedSubject](SubjectId subject)
            -> ilias::Task<
                LibraryResult<std::optional<SubjectLibraryDetails>>> {
            loadedSubject = subject;
            co_return std::optional<SubjectLibraryDetails> {sampleDetails()};
        });

    viewModel.openSubject(21);
    processUntilSettled(viewModel);

    EXPECT_EQ(loadedSubject, SubjectId {21});
    EXPECT_TRUE(viewModel.hasSubject());
    EXPECT_EQ(viewModel.playableEpisodeCount(), 1);
    EXPECT_TRUE(viewModel.errorMessage().isEmpty());
    const auto subject = viewModel.subject();
    EXPECT_EQ(subject.value(QStringLiteral("title")).toString(),
              QStringLiteral("葬送的芙莉莲"));
    EXPECT_EQ(subject.value(QStringLiteral("bangumiId")).toLongLong(),
              400602);
    ASSERT_EQ(viewModel.episodes().size(), 1);
    const auto episode = viewModel.episodes().front().toMap();
    EXPECT_EQ(episode.value(QStringLiteral("id")).toLongLong(), 31);
    EXPECT_EQ(episode.value(QStringLiteral("number")).toString(),
              QStringLiteral("EP1"));
    EXPECT_EQ(episode.value(QStringLiteral("title")).toString(),
              QStringLiteral("冒险的终点"));
    EXPECT_EQ(episode.value(QStringLiteral("source")).toString(),
              QStringLiteral("episode-01.mkv"));
    EXPECT_EQ(episode.value(QStringLiteral("duration")).toString(),
              QStringLiteral("24:00"));
    EXPECT_TRUE(episode.value(QStringLiteral("linked")).toBool());
}

TEST(SubjectDetailsViewModel, ResolvesBangumiIdentityBeforeDatabaseLoad) {
    std::int64_t receivedBangumiId = 0;
    SubjectDetailsViewModel viewModel(
        [](SubjectId)
            -> ilias::Task<
                LibraryResult<std::optional<SubjectLibraryDetails>>> {
            co_return std::optional<SubjectLibraryDetails> {sampleDetails()};
        },
        [&receivedBangumiId](std::int64_t bangumiId)
            -> ilias::Task<LibraryResult<SubjectId>> {
            receivedBangumiId = bangumiId;
            co_return SubjectId {21};
        });

    viewModel.openBangumiSubject(400602);
    processUntilSettled(viewModel);

    EXPECT_EQ(receivedBangumiId, 400602);
    EXPECT_TRUE(viewModel.hasSubject());
    EXPECT_EQ(viewModel.subject()
                  .value(QStringLiteral("subjectId"))
                  .toLongLong(),
              21);
}

TEST(SubjectDetailsViewModel, LoadsDatabaseBeforeRefreshingBangumi) {
    std::vector<QString> operations;
    SubjectDetailsViewModel viewModel(
        [&operations](SubjectId subject)
            -> ilias::Task<
                LibraryResult<std::optional<SubjectLibraryDetails>>> {
            operations.push_back(QStringLiteral("load:%1").arg(subject.value));
            co_return std::optional<SubjectLibraryDetails> {sampleDetails()};
        },
        [&operations](std::int64_t bangumiId)
            -> ilias::Task<LibraryResult<SubjectId>> {
            operations.push_back(
                QStringLiteral("refresh:%1").arg(bangumiId));
            co_return SubjectId {21};
        },
        {}, nullptr,
        [&operations](std::int64_t bangumiId)
            -> ilias::Task<LibraryResult<std::optional<SubjectId>>> {
            operations.push_back(QStringLiteral("find:%1").arg(bangumiId));
            co_return std::optional<SubjectId> {SubjectId {21}};
        });

    viewModel.openBangumiSubject(400602);
    processUntilSettled(viewModel);

    EXPECT_TRUE(viewModel.hasSubject());
    ASSERT_EQ(operations.size(), 4U);
    EXPECT_EQ(operations[0], QStringLiteral("find:400602"));
    EXPECT_EQ(operations[1], QStringLiteral("load:21"));
    EXPECT_EQ(operations[2], QStringLiteral("refresh:400602"));
    EXPECT_EQ(operations[3], QStringLiteral("load:21"));
}

TEST(SubjectDetailsViewModel, ReportsRemoteCatalogSyncFailure) {
    SubjectDetailsViewModel viewModel(
        SubjectDetailsViewModel::DetailsLoader {}, [](std::int64_t)
                -> ilias::Task<LibraryResult<SubjectId>> {
            co_return ilias::Err(libraryError(
                LibraryErrorCode::RemoteLookupFailure,
                QStringLiteral("无法读取 Bangumi 条目详情")));
        });

    viewModel.openBangumiSubject(400602);
    processUntilSettled(viewModel);

    EXPECT_FALSE(viewModel.hasSubject());
    EXPECT_TRUE(viewModel.errorMessage().contains(
        QStringLiteral("无法读取 Bangumi 条目详情")));
}

TEST(SubjectDetailsViewModel, LabelsEpisodeWithoutPublishedTitle) {
    SubjectDetailsViewModel viewModel(
        [](SubjectId)
            -> ilias::Task<
                LibraryResult<std::optional<SubjectLibraryDetails>>> {
            auto details = sampleDetails();
            details.episodes.front().episode.title.reset();
            details.episodes.front().episode.titleCn.reset();
            co_return std::optional<SubjectLibraryDetails> {
                std::move(details)};
        });

    viewModel.openSubject(21);
    processUntilSettled(viewModel);

    ASSERT_EQ(viewModel.episodes().size(), 1);
    const auto episode = viewModel.episodes().front().toMap();
    EXPECT_EQ(episode.value(QStringLiteral("title")).toString(),
              QStringLiteral("标题待公布"));
}

TEST(SubjectDetailsViewModel, PlaysFirstLinkedEpisodeByLocalIdentity) {
    EpisodeId playedEpisode;
    SubjectDetailsViewModel viewModel(
        [](SubjectId)
            -> ilias::Task<
                LibraryResult<std::optional<SubjectLibraryDetails>>> {
            co_return std::optional<SubjectLibraryDetails> {sampleDetails()};
        }, {},
        [&playedEpisode](EpisodeId episode)
            -> ilias::Task<LibraryResult<void>> {
            playedEpisode = episode;
            co_return LibraryResult<void> {};
        });

    viewModel.openSubject(21);
    processUntilSettled(viewModel);
    viewModel.playFirstAvailable();
    processUntilSettled(viewModel);

    EXPECT_EQ(playedEpisode, EpisodeId {31});
    EXPECT_EQ(viewModel.noticeMessage(),
              QStringLiteral("已在内置播放器中打开"));
}

TEST(SubjectDetailsViewModel,
     PagesAndReversesLargeEpisodeListsWithoutAppending) {
    std::vector<int> offsets;
    SubjectDetailsViewModel viewModel(
        [&offsets](SubjectId, int limit, int offset, bool descending)
            -> ilias::Task<
                LibraryResult<std::optional<SubjectLibraryDetails>>> {
            EXPECT_EQ(limit, 24);
            offsets.push_back(offset);
            auto details = sampleDetails();
            details.episodes.clear();
            details.totalEpisodeCount = 50;
            details.offset = offset;
            const int count = std::min(limit, 50 - offset);
            for (int index = 0; index < count; ++index) {
                auto episode = sampleDetails().episodes.front();
                const int number = descending ? 50 - offset - index
                                              : offset + index + 1;
                episode.episode.id = EpisodeId {100 + number};
                episode.episode.sortOrder = number - 1;
                episode.episode.episodeNumber = number;
                episode.media.clear();
                details.episodes.push_back(std::move(episode));
            }
            co_return std::optional<SubjectLibraryDetails> {
                std::move(details)};
        });

    viewModel.openSubject(21);
    processUntilSettled(viewModel);

    EXPECT_EQ(viewModel.episodes().size(), 24);
    EXPECT_EQ(viewModel.totalEpisodeCount(), 50);
    EXPECT_EQ(viewModel.currentEpisodePage(), 1);
    EXPECT_EQ(viewModel.episodePageCount(), 3);
    EXPECT_TRUE(viewModel.hasMoreEpisodes());

    viewModel.nextEpisodePage();
    processUntilSettled(viewModel);
    EXPECT_EQ(viewModel.episodes().size(), 24);
    EXPECT_EQ(viewModel.currentEpisodePage(), 2);
    EXPECT_TRUE(viewModel.hasMoreEpisodes());

    viewModel.goToEpisodePage(3);
    processUntilSettled(viewModel);
    EXPECT_EQ(viewModel.episodes().size(), 2);
    EXPECT_EQ(viewModel.currentEpisodePage(), 3);
    EXPECT_FALSE(viewModel.hasMoreEpisodes());

    viewModel.setEpisodeSortDescending(true);
    processUntilSettled(viewModel);
    EXPECT_TRUE(viewModel.episodeSortDescending());
    EXPECT_EQ(viewModel.currentEpisodePage(), 1);
    ASSERT_EQ(viewModel.episodes().size(), 24);
    EXPECT_EQ(viewModel.episodes().front().toMap()
                  .value(QStringLiteral("number")).toString(),
              QStringLiteral("EP50"));

    viewModel.goToEpisodePage(3);
    processUntilSettled(viewModel);
    ASSERT_EQ(viewModel.episodes().size(), 2);
    EXPECT_EQ(viewModel.episodes().front().toMap()
                  .value(QStringLiteral("number")).toString(),
              QStringLiteral("EP2"));
    EXPECT_EQ(offsets, (std::vector<int> {0, 24, 48, 0, 48}));
}

#define EXPAND_IN_MAIN_WITH_ARGS(argc, argv)                                   \
  QCoreApplication qtApplication(argc, argv);                                 \
  ilias::QIoContext ioContext;                                                 \
  ioContext.install()
#include "common/common_main.hpp.in"
