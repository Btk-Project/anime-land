#pragma once

#include "model/bangumi/http_request.hpp"
#include "model/bangumi/search.hpp"

#include <cstdint>
#include <optional>

namespace anime_land {

/**
 * GET /v0/subjects/{subject_id} uses the same official Subject schema as
 * each item returned by POST /v0/search/subjects.
 */
using BangumiSubjectDetails = BangumiSearchSubject;
using BangumiSubjectDetailsResponse = BangumiResponse<BangumiSubjectDetails>;

namespace detail {

auto buildBangumiSubjectDetailsRequest(
    const BangumiSettings &settings, std::int64_t subjectId,
    std::optional<QString> accessToken = std::nullopt)
    -> BangumiResult<BangumiHttpRequest>;

auto parseBangumiSubjectDetailsResponse(const QByteArray &data)
    -> BangumiResult<BangumiSubjectDetails>;

} // namespace detail
} // namespace anime_land
