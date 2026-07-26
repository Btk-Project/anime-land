#include "pch.hpp"

#include "model/bangumi/episode.hpp"
#include "model/bangumi/protocol.hpp"

#include <QUrlQuery>

#include <cmath>
#include <utility>

namespace anime_land {
namespace {

auto invalidEpisodeResponse(QString message)
    -> BangumiResult<BangumiEpisodePage> {
    return ilias::Err(
        bangumiError(BangumiErrorCode::InvalidResponse, std::move(message)));
}

auto isValidEpisodePage(const BangumiEpisodePage &page) -> bool {
    if (page.total < 0 || page.limit < 0 || page.offset < 0
        || page.data.size() > static_cast<std::size_t>(page.limit)) {
        return false;
    }
    for (const auto &episode : page.data) {
        if (episode.id <= 0 || episode.type < 0 || episode.type > 6
            || !std::isfinite(episode.sort) || episode.sort < 0
            || (episode.episodeNumber
                && (!std::isfinite(*episode.episodeNumber)
                    || *episode.episodeNumber < 0))
            || (episode.durationSeconds && *episode.durationSeconds < 0)) {
            return false;
        }
    }
    return true;
}

} // namespace

namespace detail {

auto buildBangumiEpisodesRequest(const BangumiSettings &settings,
                                 std::int64_t subjectId, int limit,
                                 int offset)
    -> BangumiResult<BangumiHttpRequest> {
    const QUrl &base = settings.bangumi_api;
    if (!base.isValid() || base.scheme() != QStringLiteral("https")
        || base.host().isEmpty()) {
        return ilias::Err(bangumiError(
            BangumiErrorCode::InvalidConfiguration,
            QStringLiteral("bangumi_api 必须是有效的 HTTPS URL")));
    }
    if (subjectId <= 0 || limit < 1 || limit > 200 || offset < 0) {
        return ilias::Err(bangumiError(
            BangumiErrorCode::InvalidConfiguration,
            QStringLiteral("Bangumi 章节请求参数无效")));
    }

    QUrl url = base.resolved(QUrl(QStringLiteral("/v0/episodes")));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("subject_id"),
                       QString::number(subjectId));
    query.addQueryItem(QStringLiteral("limit"), QString::number(limit));
    query.addQueryItem(QStringLiteral("offset"), QString::number(offset));
    url.setQuery(query);

    return BangumiHttpRequest {
        .url = std::move(url),
        .headers = {
            .userAgent = settings.user_agent,
            .accept = QByteArrayLiteral("application/json"),
            .bearerToken = std::nullopt,
            .contentType = std::nullopt,
        },
        .body = {},
    };
}

auto parseBangumiEpisodesResponse(const QByteArray &data)
    -> BangumiResult<BangumiEpisodePage> {
    BangumiEpisodePage page;
    if (auto error = bangumi_protocol::decode(data, page)) {
        return invalidEpisodeResponse(
            QStringLiteral("Bangumi 章节响应不是有效 JSON：%1").arg(*error));
    }
    if (!isValidEpisodePage(page)) {
        return invalidEpisodeResponse(
            QStringLiteral("Bangumi 章节响应包含无效分页或章节字段"));
    }
    return page;
}

} // namespace detail
} // namespace anime_land
