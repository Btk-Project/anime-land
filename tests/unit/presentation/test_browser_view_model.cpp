#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>

#include <ilias/platform/qt.hpp>

#include "presentation/bangumi/browser_view_model.hpp"

#include <algorithm>
#include <vector>

using namespace anime_land;

namespace {

void processUntilSettled(BangumiBrowserViewModel &viewModel) {
    QElapsedTimer timer;
    timer.start();
    while ((viewModel.searchLoading() || viewModel.collectionsLoading()
            || viewModel.accountBusy())
           && timer.elapsed() < 1000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

auto subjectAt(std::int64_t id) -> BangumiSearchSubject {
    return {
        .id = id,
        .type = BangumiSubjectType::Anime,
        .name = QStringLiteral("Anime %1").arg(id),
        .nameCn = QStringLiteral("动画 %1").arg(id),
        .summary = QStringLiteral("条目简介"),
        .date = QStringLiteral("2026-07-27"),
        .episodes = 12,
        .totalEpisodes = 12,
        .rating = {.rank = 100, .total = 200, .score = 8.2},
    };
}

} // namespace

TEST(BangumiBrowserViewModel, SearchesAndAppendsPublicResultsByPage) {
    std::vector<int> offsets;
    BangumiBrowserViewModel viewModel(
        [&offsets](BangumiSubjectSearchQuery query)
            -> ilias::Task<BangumiResult<BangumiSubjectSearchResponse>> {
            EXPECT_EQ(query.keyword, QStringLiteral("芙莉莲"));
            EXPECT_EQ(query.limit, 24);
            offsets.push_back(query.offset);
            BangumiSubjectSearchPage page {
                .total = 30,
                .limit = query.limit,
                .offset = query.offset,
                .data = {},
            };
            const int count = std::min(query.limit, page.total - query.offset);
            for (int index = 0; index < count; ++index) {
                page.data.push_back(subjectAt(9'007'199'254'740'000LL
                                              + query.offset + index));
            }
            co_return BangumiSubjectSearchResponse {
                .value = std::move(page),
                .rawBody = {},
            };
        });

    viewModel.search(QStringLiteral("  芙莉莲  "));
    processUntilSettled(viewModel);

    EXPECT_EQ(viewModel.searchResults().size(), 24);
    EXPECT_EQ(viewModel.searchTotal(), 30);
    EXPECT_TRUE(viewModel.hasMoreSearch());
    EXPECT_TRUE(viewModel.searchError().isEmpty());
    EXPECT_EQ(viewModel.searchResults().front()
                  .toMap()
                  .value(QStringLiteral("bangumiId"))
                  .toLongLong(),
              9'007'199'254'740'000LL);

    viewModel.loadMoreSearch();
    processUntilSettled(viewModel);

    EXPECT_EQ(viewModel.searchResults().size(), 30);
    EXPECT_FALSE(viewModel.hasMoreSearch());
    EXPECT_EQ(offsets, (std::vector<int> {0, 24}));
}

TEST(BangumiBrowserViewModel, ReportsPublicSearchErrors) {
    BangumiBrowserViewModel viewModel(
        [](BangumiSubjectSearchQuery)
            -> ilias::Task<BangumiResult<BangumiSubjectSearchResponse>> {
            co_return ilias::Err(bangumiError(
                BangumiErrorCode::NetworkError,
                QStringLiteral("网络暂时不可用")));
        });

    viewModel.search(QStringLiteral("测试"));
    processUntilSettled(viewModel);

    EXPECT_TRUE(viewModel.searchResults().isEmpty());
    EXPECT_EQ(viewModel.searchError(), QStringLiteral("网络暂时不可用"));
}

#define EXPAND_IN_MAIN_WITH_ARGS(argc, argv)                                   \
  QCoreApplication qtApplication(argc, argv);                                 \
  ilias::QIoContext ioContext;                                                 \
  ioContext.install()
#include "common/common_main.hpp.in"
