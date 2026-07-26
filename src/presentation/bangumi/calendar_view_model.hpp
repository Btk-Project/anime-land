#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include <ilias/task.hpp>
#include <ilias/task/scope.hpp>

#include "model/bangumi/bangumi.hpp"

#include <array>
#include <cstdint>
#include <functional>

namespace anime_land {

/**
 * QML-facing state for Bangumi's public weekly calendar.
 *
 * The ViewModel owns selection, loading and error state. It exposes only UI
 * maps; QML never receives Bangumi protocol DTOs or invokes the Model directly.
 */
class BangumiCalendarViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(bool hasData READ hasData NOTIFY stateChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)
    Q_PROPERTY(QVariantList days READ days NOTIFY calendarChanged)
    Q_PROPERTY(int selectedWeekday READ selectedWeekday WRITE setSelectedWeekday
                   NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedWeekdayLabel READ selectedWeekdayLabel
                   NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList selectedItems READ selectedItems
                   NOTIFY selectionChanged)
    Q_PROPERTY(QString todayLabel READ todayLabel NOTIFY calendarChanged)
    Q_PROPERTY(int todayItemCount READ todayItemCount NOTIFY calendarChanged)
    Q_PROPERTY(QVariantList todayItems READ todayItems NOTIFY calendarChanged)

public:
    using CalendarLoader = std::function<
        ilias::Task<BangumiResult<BangumiCalendarResponse>>()>;
    using PendingOperationCanceller = std::function<void()>;

    explicit BangumiCalendarViewModel(BangumiModule &module,
                                      QObject *parent = nullptr);
    explicit BangumiCalendarViewModel(
        CalendarLoader loader,
        PendingOperationCanceller cancelPendingOperations = {},
        QObject *parent = nullptr);
    ~BangumiCalendarViewModel() override;

    auto loading() const noexcept -> bool { return mLoading; }
    auto hasData() const noexcept -> bool { return mHasData; }
    auto errorMessage() const -> QString { return mErrorMessage; }
    auto days() const -> QVariantList { return mDays; }
    auto selectedWeekday() const noexcept -> int { return mSelectedWeekday; }
    auto selectedWeekdayLabel() const -> QString;
    auto selectedItems() const -> QVariantList;
    auto todayLabel() const -> QString;
    auto todayItemCount() const -> int;
    auto todayItems() const -> QVariantList;

    Q_INVOKABLE void refresh();
    void setSelectedWeekday(int weekday);

signals:
    void stateChanged();
    void calendarChanged();
    void selectionChanged();

private:
    auto load(std::uint64_t generation) -> ilias::Task<void>;
    void applyCalendar(const BangumiCalendar &calendar);

    CalendarLoader mLoader;
    PendingOperationCanceller mCancelPendingOperations;
    ilias::TaskScope mTasks;
    QVariantList mDays;
    std::array<QVariantList, 8> mItemsByWeekday;
    std::array<QString, 8> mLabelsByWeekday;
    QString mErrorMessage;
    std::uint64_t mGeneration = 0;
    int mSelectedWeekday = 1;
    bool mLoading = false;
    bool mHasData = false;
    bool mDestroying = false;
};

} // namespace anime_land
