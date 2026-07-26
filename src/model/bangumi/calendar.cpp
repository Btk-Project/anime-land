#include "pch.hpp"

#include "model/bangumi/calendar.hpp"
#include "model/bangumi/protocol.hpp"

#include <array>
#include <utility>

namespace anime_land {
namespace {

auto invalidCalendarResponse(QString message)
    -> BangumiResult<BangumiCalendar> {
    return ilias::Err(
        bangumiError(BangumiErrorCode::InvalidResponse, std::move(message)));
}

auto isValidCalendar(const BangumiCalendar &calendar) -> bool {
    if (calendar.size() != 7) {
        return false;
    }

    std::array<bool, 8> weekdays {};
    for (const auto &day : calendar) {
        const int weekday = day.weekday.id;
        if (weekday < 1 || weekday > 7 || weekdays[weekday]
            || day.weekday.en.isEmpty() || day.weekday.cn.isEmpty()
            || day.weekday.ja.isEmpty()) {
            return false;
        }
        weekdays[weekday] = true;

        for (const auto &subject : day.items) {
            if (subject.id <= 0 || subject.airWeekday < 1
                || subject.airWeekday > 7
                || (subject.name.isEmpty() && subject.nameCn.isEmpty())) {
                return false;
            }
            if (subject.rating
                && (subject.rating->total < 0 || subject.rating->score < 0
                    || subject.rating->score > 10)) {
                return false;
            }
            if (subject.rank && *subject.rank <= 0) {
                return false;
            }
            if (subject.collection && subject.collection->doing < 0) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

namespace detail {

auto buildBangumiCalendarRequest(const BangumiSettings &settings)
    -> BangumiResult<BangumiHttpRequest> {
    const QUrl &base = settings.bangumi_api;
    if (!base.isValid() || base.scheme() != QStringLiteral("https")
        || base.host().isEmpty()) {
        return ilias::Err(bangumiError(
            BangumiErrorCode::InvalidConfiguration,
            QStringLiteral("bangumi_api 必须是有效的 HTTPS URL")));
    }

    return BangumiHttpRequest {
        .url = base.resolved(QUrl(QStringLiteral("/calendar"))),
        .headers = {
            .userAgent = settings.user_agent,
            .accept = QByteArrayLiteral("application/json"),
            .bearerToken = std::nullopt,
            .contentType = std::nullopt,
        },
        .body = {},
    };
}

auto parseBangumiCalendarResponse(const QByteArray &data)
    -> BangumiResult<BangumiCalendar> {
    BangumiCalendar calendar;
    if (auto error = bangumi_protocol::decode(data, calendar)) {
        return invalidCalendarResponse(
            QStringLiteral("Bangumi 每日放送响应不是有效 JSON：%1")
                .arg(*error));
    }
    if (!isValidCalendar(calendar)) {
        return invalidCalendarResponse(
            QStringLiteral("Bangumi 每日放送响应缺少有效的七日或条目字段"));
    }
    return calendar;
}

} // namespace detail
} // namespace anime_land
