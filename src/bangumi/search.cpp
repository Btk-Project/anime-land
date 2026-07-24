#include "bangumi/search.hpp"
#include "bangumi/protocol.hpp"

#include <QUrlQuery>

#include <algorithm>
#include <utility>

namespace anime_land {
namespace {

constexpr qsizetype kMaximumKeywordLength = 512;
constexpr qsizetype kMaximumFilterValueLength = 128;
constexpr std::size_t kMaximumFilterValues = 32;
constexpr qsizetype kMaximumSearchRequestSize = 64 * 1024;

struct BangumiSubjectSearchPayloadFilter {
    std::optional<std::vector<BangumiSubjectType>> types;
    std::optional<std::vector<QString>> metaTags;
    std::optional<std::vector<QString>> tags;
    std::optional<std::vector<QString>> airDates;
    std::optional<std::vector<QString>> ratings;
    std::optional<std::vector<QString>> ratingCounts;
    std::optional<std::vector<QString>> ranks;
    std::optional<bool> nsfw;

    // clang-format off
    struct Neko {
        static constexpr auto value = Object(
            "types",        make_tags<rename_tag<"type">>(&BangumiSubjectSearchPayloadFilter::types),
            "metaTags",     make_tags<rename_tag<"meta_tags">>(&BangumiSubjectSearchPayloadFilter::metaTags),
            "tags",         make_tags<rename_tag<"tag">>(&BangumiSubjectSearchPayloadFilter::tags),
            "airDates",     make_tags<rename_tag<"air_date">>(&BangumiSubjectSearchPayloadFilter::airDates),
            "ratings",      make_tags<rename_tag<"rating">>(&BangumiSubjectSearchPayloadFilter::ratings),
            "ratingCounts", make_tags<rename_tag<"rating_count">>(&BangumiSubjectSearchPayloadFilter::ratingCounts),
            "ranks",        make_tags<rename_tag<"rank">>(&BangumiSubjectSearchPayloadFilter::ranks),
            "nsfw",         &BangumiSubjectSearchPayloadFilter::nsfw
        );
    };
    // clang-format on
};

struct BangumiSubjectSearchPayload {
    QString keyword;
    BangumiSubjectSearchSort sort = BangumiSubjectSearchSort::Match;
    std::optional<BangumiSubjectSearchPayloadFilter> filter;

    // clang-format off
    struct Neko {
        static constexpr auto value = Object(
            "keyword", &BangumiSubjectSearchPayload::keyword,
            "sort",    &BangumiSubjectSearchPayload::sort,
            "filter",  &BangumiSubjectSearchPayload::filter
        );
    };
    // clang-format on
};

auto invalidSearchConfiguration(QString message) -> BangumiResult<BangumiHttpRequest> {
    return ilias::Err(bangumiError(BangumiErrorCode::InvalidConfiguration, std::move(message)));
}

auto invalidSearchResponse(QString message) -> BangumiResult<BangumiSubjectSearchPage> {
    return ilias::Err(bangumiError(BangumiErrorCode::InvalidResponse, std::move(message)));
}

auto validFilterValues(const std::vector<QString> &values) -> bool {
    return values.size() <= kMaximumFilterValues &&
           std::ranges::all_of(values, [](const QString &value) {
               return !value.isEmpty() &&
                      value.size() <= kMaximumFilterValueLength;
           });
}

auto validSearchQuery(const BangumiSubjectSearchQuery &query) -> bool {
    return query.keyword.size() <= kMaximumKeywordLength &&
           isValidBangumiSubjectSearchSort(query.sort) && query.limit >= 1 &&
           query.limit <= 50 && query.offset >= 0 &&
           query.filter.types.size() <= kMaximumFilterValues &&
           validFilterValues(query.filter.metaTags) &&
           validFilterValues(query.filter.tags) &&
           validFilterValues(query.filter.airDates) &&
           validFilterValues(query.filter.ratings) &&
           validFilterValues(query.filter.ratingCounts) &&
           validFilterValues(query.filter.ranks);
}

template <typename T>
auto presentVector(const std::vector<T> &values) -> std::optional<std::vector<T>> {
    if (values.empty()) {
        return std::nullopt;
    }
    return values;
}

auto makePayload(const BangumiSubjectSearchQuery &query) -> BangumiSubjectSearchPayload {
    BangumiSubjectSearchPayloadFilter filter {
        .types = presentVector(query.filter.types),
        .metaTags = presentVector(query.filter.metaTags),
        .tags = presentVector(query.filter.tags),
        .airDates = presentVector(query.filter.airDates),
        .ratings = presentVector(query.filter.ratings),
        .ratingCounts = presentVector(query.filter.ratingCounts),
        .ranks = presentVector(query.filter.ranks),
        .nsfw = query.filter.nsfw,
    };
    const bool hasFilter =
        filter.types || filter.metaTags || filter.tags || filter.airDates ||
        filter.ratings || filter.ratingCounts || filter.ranks || filter.nsfw;
    BangumiSubjectSearchPayload payload {
        .keyword = query.keyword,
        .sort = query.sort,
        .filter = std::nullopt,
    };
    if (hasFilter) {
        payload.filter = std::move(filter);
    }
    return payload;
}

} // namespace

auto bangumiSubjectSearchSortName(BangumiSubjectSearchSort sort) -> std::string_view {
    switch (sort) {
        case BangumiSubjectSearchSort::Match:
            return "match";
        case BangumiSubjectSearchSort::Heat:
            return "heat";
        case BangumiSubjectSearchSort::Rank:
            return "rank";
        case BangumiSubjectSearchSort::Score:
            return "score";
    }
    return "unknown";
}

namespace detail {

auto buildBangumiSubjectSearchRequest(const BangumiSettings &settings, const BangumiSubjectSearchQuery &query,
                                      std::optional<QString> accessToken) -> BangumiResult<BangumiHttpRequest> {
    const QUrl &base = settings.bangumi_api;
    if (!base.isValid() || base.scheme() != QStringLiteral("https") ||
        base.host().isEmpty()) {
        return invalidSearchConfiguration(QStringLiteral("bangumi_api 必须是有效的 HTTPS URL"));
    }
    if (!validSearchQuery(query)) {
        return invalidSearchConfiguration(QStringLiteral("条目搜索参数无效（limit 必须为 1..50，offset 不能为负，筛选值必须有效）"));
    }

    auto body = bangumi_protocol::encode(makePayload(query));
    if (!body || body->size() > kMaximumSearchRequestSize) {
        return invalidSearchConfiguration(QStringLiteral("无法编码 Bangumi 条目搜索请求或请求体过大"));
    }

    QUrl url = base.resolved(QUrl(QStringLiteral("/v0/search/subjects")));
    QUrlQuery parameters;
    parameters.addQueryItem(QStringLiteral("limit"), QString::number(query.limit));
    parameters.addQueryItem(QStringLiteral("offset"), QString::number(query.offset));
    url.setQuery(parameters);

    if (accessToken && accessToken->isEmpty()) {
        accessToken.reset();
    }
    return BangumiHttpRequest {
        .url = std::move(url),
        .headers = {
            .userAgent = settings.user_agent,
            .accept = QByteArrayLiteral("application/json"),
            .bearerToken = std::move(accessToken),
            .contentType = QByteArrayLiteral("application/json"),
        },
        .body = std::move(*body),
    };
}

auto parseBangumiSubjectSearchResponse(const QByteArray &data) -> BangumiResult<BangumiSubjectSearchPage> {
    BangumiSubjectSearchPage page;
    if (auto error = bangumi_protocol::decode(data, page)) {
        auto message = QStringLiteral("条目搜索响应不是有效 JSON：%1").arg(*error);
        return invalidSearchResponse(message);
    }
    return page;
}

} // namespace detail
} // namespace anime_land
