#pragma once

#include "common/app_settings.hpp"
#include "model/bangumi/config.hpp"
#include "model/bangumi/http_request.hpp"

#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

namespace anime_land {

/** Public episode projection returned by GET /v0/episodes. */
struct BangumiEpisode {
    std::int64_t id = 0;
    int type = 0;
    QString name;
    QString nameCn;
    double sort = 0.0;
    std::optional<double> episodeNumber;
    QString airDate;
    QString duration;
    QString summary;
    std::optional<std::int64_t> durationSeconds;

    // clang-format off
    struct Neko {
        static constexpr auto value = Object(
            "id",              &BangumiEpisode::id,
            "type",            &BangumiEpisode::type,
            "name",            &BangumiEpisode::name,
            "nameCn",          make_tags<rename_tag<"name_cn">>(&BangumiEpisode::nameCn),
            "sort",            &BangumiEpisode::sort,
            "episodeNumber",   make_tags<rename_tag<"ep">>(&BangumiEpisode::episodeNumber),
            "airDate",         make_tags<rename_tag<"airdate">>(&BangumiEpisode::airDate),
            "duration",        &BangumiEpisode::duration,
            "summary",         make_tags<rename_tag<"desc">>(&BangumiEpisode::summary),
            "durationSeconds", make_tags<rename_tag<"duration_seconds">>(&BangumiEpisode::durationSeconds)
        );
    };
    // clang-format on
};

struct BangumiEpisodePage {
    int total = 0;
    int limit = 0;
    int offset = 0;
    std::vector<BangumiEpisode> data;

    struct Neko {
        static constexpr auto value =
            Object("total", &BangumiEpisodePage::total,
                   "limit", &BangumiEpisodePage::limit,
                   "offset", &BangumiEpisodePage::offset,
                   "data", &BangumiEpisodePage::data);
    };
};

using BangumiEpisodeResponse = BangumiResponse<BangumiEpisodePage>;

namespace detail {

auto buildBangumiEpisodesRequest(const BangumiSettings &settings,
                                 std::int64_t subjectId, int limit = 200,
                                 int offset = 0)
    -> BangumiResult<BangumiHttpRequest>;
auto parseBangumiEpisodesResponse(const QByteArray &data)
    -> BangumiResult<BangumiEpisodePage>;

} // namespace detail
} // namespace anime_land
