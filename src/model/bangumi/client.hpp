#pragma once

#include <QNetworkAccessManager>
#include <QPointer>

#include <ilias/task.hpp>

#include "model/bangumi/capability.hpp"
#include "model/bangumi/calendar.hpp"
#include "model/bangumi/collection.hpp"
#include "model/bangumi/config.hpp"
#include "model/bangumi/episode.hpp"
#include "model/bangumi/search.hpp"
#include "model/bangumi/subject.hpp"
#include "common/app_settings.hpp"

class QNetworkReply;

namespace anime_land {

/** Minimal authenticated Bangumi v0 API client used by the login module. */
class BangumiClient final : public QObject {
public:
    BangumiClient(QNetworkAccessManager &network, BangumiSettings settings, QObject *parent = nullptr);

    auto getCurrentUser(const BangumiToken &token) -> ilias::Task<BangumiResult<BangumiUser>>;
    auto searchSubjects(const BangumiSubjectSearchQuery &query, std::optional<QString> accessToken = std::nullopt)
        -> ilias::Task<BangumiResult<BangumiSubjectSearchResponse>>;
    auto getSubject(std::int64_t subjectId,
                    std::optional<QString> accessToken = std::nullopt)
        -> ilias::Task<BangumiResult<BangumiSubjectDetailsResponse>>;
    auto getCalendar()
        -> ilias::Task<BangumiResult<BangumiCalendarResponse>>;
    auto getEpisodes(std::int64_t subjectId, int limit = 200, int offset = 0)
        -> ilias::Task<BangumiResult<BangumiEpisodeResponse>>;
    auto getUserCollections(const BangumiToken &token, QStringView username, const BangumiCollectionQuery &query,
                            const BangumiFeatureDeclaration &feature)
        -> ilias::Task<BangumiResult<BangumiUserCollectionsResponse>>;
    void cancel();

private:
    QNetworkAccessManager &mNetwork;
    BangumiSettings mSettings;
    QPointer<QNetworkReply> mActiveReply;
};

} // namespace anime_land
