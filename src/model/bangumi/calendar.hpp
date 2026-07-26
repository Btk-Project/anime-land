#pragma once

#include "model/bangumi/collection.hpp"
#include "model/bangumi/config.hpp"
#include "model/bangumi/http_request.hpp"

#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

namespace anime_land {

struct BangumiCalendarWeekday {
    QString en;
    QString cn;
    QString ja;
    int id = 0;

    // clang-format off
    struct Neko {
        static constexpr auto value = Object(
            "en", &BangumiCalendarWeekday::en,
            "cn", &BangumiCalendarWeekday::cn,
            "ja", &BangumiCalendarWeekday::ja,
            "id", &BangumiCalendarWeekday::id
        );
    };
    // clang-format on
};

struct BangumiCalendarRating {
    int total = 0;
    double score = 0;

    // The legacy endpoint also returns a score histogram named "count".
    struct Neko {
        static constexpr auto value = Object(
            "total", &BangumiCalendarRating::total,
            "score", &BangumiCalendarRating::score);
    };
};

struct BangumiCalendarCollection {
    int doing = 0;

    struct Neko {
        static constexpr auto value =
            Object("doing", &BangumiCalendarCollection::doing);
    };
};

/** Public calendar projection returned by GET /calendar. */
struct BangumiCalendarSubject {
    std::int64_t id = 0;
    QString url;
    BangumiSubjectType type = BangumiSubjectType::Anime;
    QString name;
    QString nameCn;
    QString summary;
    QString airDate;
    int airWeekday = 0;
    std::optional<BangumiCalendarRating> rating;
    std::optional<int> rank;
    std::optional<BangumiSubjectImages> images;
    std::optional<BangumiCalendarCollection> collection;

    // clang-format off
    struct Neko {
        static constexpr auto value = Object(
            "id",         &BangumiCalendarSubject::id,
            "url",        &BangumiCalendarSubject::url,
            "type",       &BangumiCalendarSubject::type,
            "name",       &BangumiCalendarSubject::name,
            "nameCn",     make_tags<rename_tag<"name_cn">>(&BangumiCalendarSubject::nameCn),
            "summary",    &BangumiCalendarSubject::summary,
            "airDate",    make_tags<rename_tag<"air_date">>(&BangumiCalendarSubject::airDate),
            "airWeekday", make_tags<rename_tag<"air_weekday">>(&BangumiCalendarSubject::airWeekday),
            "rating",     &BangumiCalendarSubject::rating,
            "rank",       &BangumiCalendarSubject::rank,
            "images",     &BangumiCalendarSubject::images,
            "collection", &BangumiCalendarSubject::collection
        );
    };
    // clang-format on
};

struct BangumiCalendarDay {
    BangumiCalendarWeekday weekday;
    std::vector<BangumiCalendarSubject> items;

    struct Neko {
        static constexpr auto value =
            Object("weekday", &BangumiCalendarDay::weekday,
                   "items", &BangumiCalendarDay::items);
    };
};

using BangumiCalendar = std::vector<BangumiCalendarDay>;
using BangumiCalendarResponse = BangumiResponse<BangumiCalendar>;

namespace detail {

auto buildBangumiCalendarRequest(const BangumiSettings &settings)
    -> BangumiResult<BangumiHttpRequest>;
auto parseBangumiCalendarResponse(const QByteArray &data)
    -> BangumiResult<BangumiCalendar>;

} // namespace detail
} // namespace anime_land
