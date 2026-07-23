#pragma once

#include "bangumi/collection.hpp"
#include "bangumi/http_request.hpp"

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace anime_land {
using namespace NEKO_NAMESPACE;

enum class BangumiSubjectSearchSort {
    Match,
    Heat,
    Rank,
    Score,
};

constexpr auto isValidBangumiSubjectSearchSort(
    BangumiSubjectSearchSort sort) noexcept -> bool {
    switch (sort) {
        case BangumiSubjectSearchSort::Match:
        case BangumiSubjectSearchSort::Heat:
        case BangumiSubjectSearchSort::Rank:
        case BangumiSubjectSearchSort::Score:
            return true;
    }
    return false;
}

auto bangumiSubjectSearchSortName(BangumiSubjectSearchSort sort)
    -> std::string_view;

struct BangumiSubjectSearchFilter {
    std::vector<BangumiSubjectType> types;
    std::vector<QString> metaTags;
    std::vector<QString> tags;
    std::vector<QString> airDates;
    std::vector<QString> ratings;
    std::vector<QString> ratingCounts;
    std::vector<QString> ranks;
    std::optional<bool> nsfw;
};

/**
 * @brief Bangumi v0 条目搜索请求。
 *
 * @details limit/offset 编入 query string，其余字段编码为 JSON body。
 * 搜索是公开能力；调用方可以提供现有 access token，但 token 不是前置条件。
 */
struct BangumiSubjectSearchQuery {
    QString keyword;
    BangumiSubjectSearchSort sort = BangumiSubjectSearchSort::Match;
    BangumiSubjectSearchFilter filter;
    int limit = 30;
    int offset = 0;
};

struct BangumiSearchSubjectTag {
    QString name;
    int count = 0;

    struct Neko {
        static constexpr auto value =
            Object("name", &BangumiSearchSubjectTag::name, "count",
                   &BangumiSearchSubjectTag::count);
    };
};

struct BangumiSearchSubjectRating {
    int rank = 0;
    int total = 0;
    double score = 0;

    // The official response also contains a score histogram named "count".
    // v0.1 does not expose that histogram, so the protocol reader ignores it.
    struct Neko {
        static constexpr auto value =
            Object("rank", &BangumiSearchSubjectRating::rank, "total",
                   &BangumiSearchSubjectRating::total, "score",
                   &BangumiSearchSubjectRating::score);
    };
};

struct BangumiSearchSubjectCollection {
    int wish = 0;
    int collect = 0;
    int doing = 0;
    int onHold = 0;
    int dropped = 0;

    // clang-format off
  struct Neko {
    static constexpr auto value = Object(
        "wish",    &BangumiSearchSubjectCollection::wish,
        "collect", &BangumiSearchSubjectCollection::collect,
        "doing",   &BangumiSearchSubjectCollection::doing,
        "onHold",  make_tags<rename_tag<"on_hold">>(&BangumiSearchSubjectCollection::onHold),
        "dropped", &BangumiSearchSubjectCollection::dropped
    );
  };
    // clang-format on
};

/**
 * @brief POST /v0/search/subjects 返回的 Subject 投影。
 *
 * @details 只省略 v0.1 不消费的 rating.count 和 infobox；其余搜索页字段经过
 * Neko 类型校验和 parseBangumiSubjectSearchResponse() 业务校验。
 */
struct BangumiSearchSubject {
    std::int64_t id = 0;
    BangumiSubjectType type = BangumiSubjectType::Anime;
    QString name;
    QString nameCn;
    QString summary;
    bool series = false;
    bool nsfw = false;
    bool locked = false;
    std::optional<QString> date;
    QString platform;
    BangumiSubjectImages images;
    int volumes = 0;
    int episodes = 0;
    int totalEpisodes = 0;
    BangumiSearchSubjectRating rating;
    BangumiSearchSubjectCollection collection;
    std::vector<QString> metaTags;
    std::vector<BangumiSearchSubjectTag> tags;

    // clang-format off
  struct Neko {
    static constexpr auto value = Object(
        "id",            &BangumiSearchSubject::id,
        "type",          &BangumiSearchSubject::type,
        "name",          &BangumiSearchSubject::name,
        "nameCn",        make_tags<rename_tag<"name_cn">>(&BangumiSearchSubject::nameCn),
        "summary",       &BangumiSearchSubject::summary,
        "series",        &BangumiSearchSubject::series,
        "nsfw",          &BangumiSearchSubject::nsfw,
        "locked",        &BangumiSearchSubject::locked,
        "date",          &BangumiSearchSubject::date,
        "platform",      &BangumiSearchSubject::platform,
        "images",        &BangumiSearchSubject::images,
        "volumes",       &BangumiSearchSubject::volumes,
        "episodes",      make_tags<rename_tag<"eps">>(&BangumiSearchSubject::episodes),
        "totalEpisodes", make_tags<rename_tag<"total_episodes">>(&BangumiSearchSubject::totalEpisodes),
        "rating",        &BangumiSearchSubject::rating,
        "collection",    &BangumiSearchSubject::collection,
        "metaTags",      make_tags<rename_tag<"meta_tags">>(&BangumiSearchSubject::metaTags),
        "tags",          &BangumiSearchSubject::tags
    );
  };
    // clang-format on
};

struct BangumiSubjectSearchPage {
    int total = 0;
    int limit = 0;
    int offset = 0;
    std::vector<BangumiSearchSubject> data;

    // clang-format off
  struct Neko {
    static constexpr auto value = Object(
        "total",  &BangumiSubjectSearchPage::total,
        "limit",  &BangumiSubjectSearchPage::limit,
        "offset", &BangumiSubjectSearchPage::offset,
        "data",   &BangumiSubjectSearchPage::data
    );
  };
    // clang-format on
};

using BangumiSubjectSearchResponse = BangumiResponse<BangumiSubjectSearchPage>;

namespace detail {

/**
 * @brief 构造公开条目搜索请求。
 * @param accessToken 当前会话已有 token；空值或空字符串表示匿名请求。
 * @post 成功请求始终为 POST JSON；不会读取 TokenStore 或检查 capability。
 */
auto buildBangumiSubjectSearchRequest(
    const BangumiSettings &settings, const BangumiSubjectSearchQuery &query,
    std::optional<QString> accessToken = std::nullopt)
    -> BangumiResult<BangumiHttpRequest>;

auto parseBangumiSubjectSearchResponse(const QByteArray &data)
    -> BangumiResult<BangumiSubjectSearchPage>;

} // namespace detail
} // namespace anime_land

NEKO_BEGIN_NAMESPACE

template <>
struct CustomParser<anime_land::BangumiSubjectSearchSort> {
    template <typename W, typename Parent, typename Tags>
    static auto write(W &writer, anime_land::BangumiSubjectSearchSort value,
                      const Parent &parent, const Tags &tags) -> ParserResult {
        if (!anime_land::isValidBangumiSubjectSearchSort(value)) {
            return detail::parser_error(sa::ErrorCode::InvalidType,
                                        "Invalid Bangumi subject search sort");
        }
        const auto name = anime_land::bangumiSubjectSearchSortName(value);
        const QString encoded =
            QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size()));
        return parser_write<W>(writer, encoded, parent, tags);
    }

    template <typename R, typename Tags>
    static auto read(typename R::InputValueType input,
                     anime_land::BangumiSubjectSearchSort &value,
                     const Tags &tags) -> ParserResult {
        QString raw;
        auto result = parser_read<R>(input, raw, tags);
        if (!result) {
            return result;
        }
        if (raw == QStringLiteral("match")) {
            value = anime_land::BangumiSubjectSearchSort::Match;
        }
        else if (raw == QStringLiteral("heat")) {
            value = anime_land::BangumiSubjectSearchSort::Heat;
        }
        else if (raw == QStringLiteral("rank")) {
            value = anime_land::BangumiSubjectSearchSort::Rank;
        }
        else if (raw == QStringLiteral("score")) {
            value = anime_land::BangumiSubjectSearchSort::Score;
        }
        else {
            return detail::parser_error(sa::ErrorCode::InvalidType,
                                        "Invalid Bangumi subject search sort");
        }
        return sa::success();
    }

    static auto toSchema() -> parsing::schema::Type {
        return parser_schema<QString>();
    }
};

NEKO_END_NAMESPACE
