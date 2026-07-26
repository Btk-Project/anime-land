#include "presentation/bangumi/calendar_view_model.hpp"

#include "common/log.hpp"

#include <QDate>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <utility>

namespace anime_land {
namespace {

constexpr std::array<const char *, 8> kSubjectColors = {
    "#59636b", "#5f6859", "#685d58", "#6b5c65",
    "#5c626d", "#545d63", "#655b60", "#58656a",
};

auto subjectTypeLabel(BangumiSubjectType type) -> QString {
    switch (type) {
        case BangumiSubjectType::Book:
            return QStringLiteral("书籍");
        case BangumiSubjectType::Anime:
            return QStringLiteral("动画");
        case BangumiSubjectType::Music:
            return QStringLiteral("音乐");
        case BangumiSubjectType::Game:
            return QStringLiteral("游戏");
        case BangumiSubjectType::Real:
            return QStringLiteral("三次元");
    }
    return QStringLiteral("Bangumi");
}

auto calendarSubjectMap(const BangumiCalendarSubject &subject,
                        QStringView weekdayLabel) -> QVariantMap {
    const QString title =
        subject.nameCn.isEmpty() ? subject.name : subject.nameCn;
    const QString subtitle =
        subject.nameCn.isEmpty() ? QString {} : subject.name;
    const QString typeLabel = subjectTypeLabel(subject.type);
    const QString meta = subject.airDate.isEmpty()
                             ? typeLabel
                             : QStringLiteral("%1 · %2")
                                   .arg(subject.airDate, typeLabel);
    QString coverUrl;
    if (subject.images) {
        coverUrl = !subject.images->medium.isEmpty()
                       ? subject.images->medium
                       : subject.images->common;
    }

    QVariantMap value;
    value.insert(QStringLiteral("id"),
                 QVariant::fromValue<qlonglong>(subject.id));
    value.insert(QStringLiteral("bangumiId"),
                 QVariant::fromValue<qlonglong>(subject.id));
    value.insert(QStringLiteral("bangumiUrl"), subject.url);
    value.insert(QStringLiteral("title"), title);
    value.insert(QStringLiteral("subtitle"), subtitle);
    value.insert(QStringLiteral("meta"), meta);
    value.insert(QStringLiteral("episode"),
                 QStringLiteral("%1放送").arg(weekdayLabel));
    value.insert(QStringLiteral("progress"), 0.0);
    value.insert(QStringLiteral("score"),
                 subject.rating
                     ? QString::number(subject.rating->score, 'f', 1)
                     : QStringLiteral("—"));
    value.insert(QStringLiteral("status"),
                 QStringLiteral("%1放送").arg(weekdayLabel));
    value.insert(
        QStringLiteral("color"),
        QString::fromLatin1(kSubjectColors[static_cast<std::size_t>(
            subject.id % static_cast<std::int64_t>(kSubjectColors.size()))]));
    value.insert(QStringLiteral("summary"), subject.summary);
    value.insert(QStringLiteral("coverUrl"), coverUrl);
    value.insert(QStringLiteral("airDate"), subject.airDate);
    value.insert(QStringLiteral("doingCount"),
                 subject.collection ? subject.collection->doing : 0);
    return value;
}

} // namespace

BangumiCalendarViewModel::BangumiCalendarViewModel(BangumiModule &module,
                                                   QObject *parent)
    : BangumiCalendarViewModel(
          [&module]() { return module.getCalendar(); },
          [&module]() { module.cancelPendingOperations(); }, parent) {}

BangumiCalendarViewModel::BangumiCalendarViewModel(
    CalendarLoader loader,
    PendingOperationCanceller cancelPendingOperations, QObject *parent)
    : QObject(parent), mLoader(std::move(loader)),
      mCancelPendingOperations(std::move(cancelPendingOperations)),
      mSelectedWeekday(QDate::currentDate().dayOfWeek()) {}

BangumiCalendarViewModel::~BangumiCalendarViewModel() {
    mDestroying = true;
    ++mGeneration;
    if (mCancelPendingOperations) {
        mCancelPendingOperations();
    }
    mTasks.shutdown().wait();
}

auto BangumiCalendarViewModel::selectedWeekdayLabel() const -> QString {
    return mLabelsByWeekday[static_cast<std::size_t>(mSelectedWeekday)];
}

auto BangumiCalendarViewModel::selectedItems() const -> QVariantList {
    return mItemsByWeekday[static_cast<std::size_t>(mSelectedWeekday)];
}

auto BangumiCalendarViewModel::todayLabel() const -> QString {
    const int weekday = QDate::currentDate().dayOfWeek();
    return mLabelsByWeekday[static_cast<std::size_t>(weekday)];
}

auto BangumiCalendarViewModel::todayItemCount() const -> int {
    const int weekday = QDate::currentDate().dayOfWeek();
    return static_cast<int>(
        mItemsByWeekday[static_cast<std::size_t>(weekday)].size());
}

auto BangumiCalendarViewModel::todayItems() const -> QVariantList {
    const int weekday = QDate::currentDate().dayOfWeek();
    return mItemsByWeekday[static_cast<std::size_t>(weekday)].mid(0, 3);
}

void BangumiCalendarViewModel::refresh() {
    if (mLoading || mDestroying) {
        return;
    }
    mLoading = true;
    mErrorMessage.clear();
    const auto generation = ++mGeneration;
    emit stateChanged();
    mTasks.spawn(load(generation));
}

void BangumiCalendarViewModel::setSelectedWeekday(int weekday) {
    if (weekday < 1 || weekday > 7 || weekday == mSelectedWeekday) {
        return;
    }
    mSelectedWeekday = weekday;
    emit selectionChanged();
}

auto BangumiCalendarViewModel::load(std::uint64_t generation)
    -> ilias::Task<void> {
    if (!mLoader) {
        if (!mDestroying && generation == mGeneration) {
            mLoading = false;
            mErrorMessage = QStringLiteral("每日放送加载器未配置");
            emit stateChanged();
        }
        co_return;
    }

    auto result = co_await mLoader();
    if (mDestroying || generation != mGeneration) {
        co_return;
    }

    mLoading = false;
    if (!result) {
        mErrorMessage = result.error().message;
        AL_LOG_WARN("[presentation.calendar] load failed code={}",
                    bangumiErrorCodeName(result.error().code));
        emit stateChanged();
        co_return;
    }

    applyCalendar(result->value);
    mHasData = true;
    mErrorMessage.clear();
    AL_LOG_INFO("[presentation.calendar] state updated days={}",
                mDays.size());
    emit calendarChanged();
    emit selectionChanged();
    emit stateChanged();
}

void BangumiCalendarViewModel::applyCalendar(
    const BangumiCalendar &calendar) {
    QVariantList days;
    std::array<QVariantList, 8> itemsByWeekday;
    std::array<QString, 8> labelsByWeekday;

    for (const auto &day : calendar) {
        const auto weekday = static_cast<std::size_t>(day.weekday.id);
        labelsByWeekday[weekday] = day.weekday.cn;
        auto &items = itemsByWeekday[weekday];
        items.reserve(static_cast<qsizetype>(day.items.size()));
        for (const auto &subject : day.items) {
            items.push_back(calendarSubjectMap(subject, day.weekday.cn));
        }

        QVariantMap dayValue;
        dayValue.insert(QStringLiteral("id"), day.weekday.id);
        dayValue.insert(QStringLiteral("shortLabel"),
                        day.weekday.cn.right(1));
        dayValue.insert(QStringLiteral("label"), day.weekday.cn);
        dayValue.insert(QStringLiteral("itemCount"), items.size());
        days.push_back(dayValue);
    }

    std::sort(days.begin(), days.end(), [](const QVariant &left,
                                           const QVariant &right) {
        return left.toMap().value(QStringLiteral("id")).toInt()
               < right.toMap().value(QStringLiteral("id")).toInt();
    });
    mDays = std::move(days);
    mItemsByWeekday = std::move(itemsByWeekday);
    mLabelsByWeekday = std::move(labelsByWeekday);
}

} // namespace anime_land
