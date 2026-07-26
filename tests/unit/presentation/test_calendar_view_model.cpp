#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>

#include <ilias/platform/qt.hpp>

#include "presentation/bangumi/calendar_view_model.hpp"

#include <utility>

using namespace anime_land;

namespace {

auto sampleCalendar() -> BangumiCalendar {
  BangumiCalendar calendar;
  calendar.reserve(7);
  const QStringList labels = {
      QStringLiteral("星期一"), QStringLiteral("星期二"),
      QStringLiteral("星期三"), QStringLiteral("星期四"),
      QStringLiteral("星期五"), QStringLiteral("星期六"),
      QStringLiteral("星期日"),
  };
  const QStringList english = {
      QStringLiteral("Mon"), QStringLiteral("Tue"),
      QStringLiteral("Wed"), QStringLiteral("Thu"),
      QStringLiteral("Fri"), QStringLiteral("Sat"),
      QStringLiteral("Sun"),
  };
  const QStringList japanese = {
      QStringLiteral("月曜日"), QStringLiteral("火曜日"),
      QStringLiteral("水曜日"), QStringLiteral("木曜日"),
      QStringLiteral("金曜日"), QStringLiteral("土曜日"),
      QStringLiteral("日曜日"),
  };
  for (int weekday = 1; weekday <= 7; ++weekday) {
    BangumiCalendarDay day{
        .weekday =
            {
                .en = english[weekday - 1],
                .cn = labels[weekday - 1],
                .ja = japanese[weekday - 1],
                .id = weekday,
            },
        .items = {},
    };
    if (weekday == 1) {
      day.items.push_back({
          .id = 400602,
          .url = QStringLiteral("https://bgm.tv/subject/400602"),
          .type = BangumiSubjectType::Anime,
          .name = QStringLiteral("Sousou no Frieren"),
          .nameCn = QStringLiteral("葬送的芙莉莲"),
          .summary = QStringLiteral("冒险结束后的故事"),
          .airDate = QStringLiteral("2023-09-29"),
          .airWeekday = 1,
          .rating = BangumiCalendarRating{.total = 12000, .score = 8.8},
          .rank = 10,
          .images = BangumiSubjectImages{
              .large = QStringLiteral("large"),
              .common = QStringLiteral("common"),
              .medium = QStringLiteral("medium"),
              .small = QStringLiteral("small"),
              .grid = QStringLiteral("grid"),
          },
          .collection = BangumiCalendarCollection{.doing = 300},
      });
    }
    calendar.push_back(std::move(day));
  }
  return calendar;
}

void processUntilSettled(BangumiCalendarViewModel &viewModel) {
  QElapsedTimer timer;
  timer.start();
  while (viewModel.loading() && timer.elapsed() < 1000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }
}

} // namespace

TEST(BangumiCalendarViewModel, MapsValidatedCalendarToQmlState) {
  BangumiCalendarViewModel viewModel([]()
      -> ilias::Task<BangumiResult<BangumiCalendarResponse>> {
    co_return BangumiCalendarResponse{
        .value = sampleCalendar(),
        .rawBody = {},
    };
  });

  viewModel.refresh();
  processUntilSettled(viewModel);
  viewModel.setSelectedWeekday(1);

  EXPECT_FALSE(viewModel.loading());
  EXPECT_TRUE(viewModel.hasData());
  EXPECT_TRUE(viewModel.errorMessage().isEmpty());
  ASSERT_EQ(viewModel.days().size(), 7);
  EXPECT_EQ(viewModel.selectedWeekdayLabel(), QStringLiteral("星期一"));
  ASSERT_EQ(viewModel.selectedItems().size(), 1);
  const auto subject = viewModel.selectedItems().front().toMap();
  EXPECT_EQ(subject.value(QStringLiteral("bangumiId")).toLongLong(), 400602);
  EXPECT_EQ(subject.value(QStringLiteral("title")).toString(),
            QStringLiteral("葬送的芙莉莲"));
  EXPECT_EQ(subject.value(QStringLiteral("subtitle")).toString(),
            QStringLiteral("Sousou no Frieren"));
  EXPECT_EQ(subject.value(QStringLiteral("score")).toString(),
            QStringLiteral("8.8"));
  EXPECT_EQ(subject.value(QStringLiteral("coverUrl")).toString(),
            QStringLiteral("medium"));
  EXPECT_EQ(subject.value(QStringLiteral("doingCount")).toInt(), 300);
}

TEST(BangumiCalendarViewModel, PresentsErrorsAndCanRetry) {
  int attempts = 0;
  BangumiCalendarViewModel viewModel(
      [&attempts]()
          -> ilias::Task<BangumiResult<BangumiCalendarResponse>> {
        ++attempts;
        if (attempts == 1) {
          co_return ilias::Err(bangumiError(
              BangumiErrorCode::NetworkError,
              QStringLiteral("fixture network failure")));
        }
        co_return BangumiCalendarResponse{
            .value = sampleCalendar(),
            .rawBody = {},
        };
      });

  viewModel.refresh();
  processUntilSettled(viewModel);

  EXPECT_FALSE(viewModel.hasData());
  EXPECT_EQ(viewModel.errorMessage(),
            QStringLiteral("fixture network failure"));

  viewModel.refresh();
  processUntilSettled(viewModel);

  EXPECT_EQ(attempts, 2);
  EXPECT_TRUE(viewModel.hasData());
  EXPECT_TRUE(viewModel.errorMessage().isEmpty());
}

TEST(BangumiCalendarViewModel, RejectsInvalidWeekdaySelection) {
  BangumiCalendarViewModel viewModel([]()
      -> ilias::Task<BangumiResult<BangumiCalendarResponse>> {
    co_return BangumiCalendarResponse{
        .value = sampleCalendar(),
        .rawBody = {},
    };
  });
  const int initialWeekday = viewModel.selectedWeekday();

  viewModel.setSelectedWeekday(0);
  EXPECT_EQ(viewModel.selectedWeekday(), initialWeekday);
  viewModel.setSelectedWeekday(8);
  EXPECT_EQ(viewModel.selectedWeekday(), initialWeekday);
}

#define EXPAND_IN_MAIN_WITH_ARGS(argc, argv)                                   \
  QCoreApplication qtApplication(argc, argv);                                 \
  ilias::QIoContext ioContext;                                                \
  ioContext.install()
#include "common/common_main.hpp.in"
