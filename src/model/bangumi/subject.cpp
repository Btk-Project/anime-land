#include "pch.hpp"

#include "model/bangumi/subject.hpp"

#include "model/bangumi/protocol.hpp"

#include <QDate>

#include <algorithm>
#include <cmath>
#include <utility>

namespace anime_land {
namespace {

auto invalidSubjectResponse(QString message)
    -> BangumiResult<BangumiSubjectDetails> {
    return ilias::Err(
        bangumiError(BangumiErrorCode::InvalidResponse, std::move(message)));
}

auto isValidSubjectDetails(const BangumiSubjectDetails &subject) -> bool {
    const int type = static_cast<int>(subject.type);
    if (subject.id <= 0 || type <= 0
        || (subject.name.trimmed().isEmpty()
            && subject.nameCn.trimmed().isEmpty())
        || subject.volumes < 0 || subject.episodes < 0
        || subject.totalEpisodes < 0 || subject.rating.rank < 0
        || subject.rating.total < 0
        || !std::isfinite(subject.rating.score)
        || subject.rating.score < 0.0) {
        return false;
    }
    if (subject.date && !subject.date->isEmpty()
        && !QDate::fromString(*subject.date, Qt::ISODate).isValid()) {
        return false;
    }
    return std::ranges::all_of(
        subject.tags, [](const BangumiSearchSubjectTag &tag) {
            return !tag.name.trimmed().isEmpty() && tag.count >= 0;
        });
}

} // namespace

namespace detail {

auto buildBangumiSubjectDetailsRequest(
    const BangumiSettings &settings, std::int64_t subjectId,
    std::optional<QString> accessToken)
    -> BangumiResult<BangumiHttpRequest> {
    const QUrl &base = settings.bangumi_api;
    if (!base.isValid() || base.scheme() != QStringLiteral("https")
        || base.host().isEmpty()) {
        return ilias::Err(bangumiError(
            BangumiErrorCode::InvalidConfiguration,
            QStringLiteral("bangumi_api 必须是有效的 HTTPS URL")));
    }
    if (subjectId <= 0) {
        return ilias::Err(bangumiError(
            BangumiErrorCode::InvalidConfiguration,
            QStringLiteral("Bangumi 条目 ID 必须是正整数")));
    }
    if (accessToken && accessToken->isEmpty()) {
        accessToken.reset();
    }

    return BangumiHttpRequest {
        .url = base.resolved(
            QUrl(QStringLiteral("/v0/subjects/%1").arg(subjectId))),
        .headers = {
            .userAgent = settings.user_agent,
            .accept = QByteArrayLiteral("application/json"),
            .bearerToken = std::move(accessToken),
            .contentType = std::nullopt,
        },
        .body = {},
    };
}

auto parseBangumiSubjectDetailsResponse(const QByteArray &data)
    -> BangumiResult<BangumiSubjectDetails> {
    BangumiSubjectDetails subject;
    if (auto error = bangumi_protocol::decode(data, subject)) {
        return invalidSubjectResponse(
            QStringLiteral("Bangumi 条目详情响应不是有效 JSON：%1")
                .arg(*error));
    }
    if (!isValidSubjectDetails(subject)) {
        return invalidSubjectResponse(
            QStringLiteral("Bangumi 条目详情响应包含无效字段"));
    }
    return subject;
}

} // namespace detail
} // namespace anime_land
