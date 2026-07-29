#pragma once

#include <QDateTime>
#include <QHash>

#include "model/episode_resource/types.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

namespace anime_land {

struct OnlinePlayableCacheKey {
    QString providerKey;
    std::uint64_t providerGeneration = 0;
    SubjectId subjectId;
    EpisodeId episodeId;
    QString normalizedSubjectName;
    QString normalizedEpisodeName;
};

struct CachedOnlinePlayable {
    QString handle;
    QString providerKey;
    std::uint64_t providerGeneration = 0;
    OnlinePlayable playable;
    QDateTime expiresAt;
};

class OnlinePlayableCache final {
public:
    auto store(const OnlinePlayableCacheKey &key,
               std::vector<OnlinePlayable> playables,
               std::chrono::milliseconds ttl,
               QDateTime now = QDateTime::currentDateTimeUtc())
        -> std::vector<CachedOnlinePlayable>;
    auto find(const OnlinePlayableCacheKey &key,
              QDateTime now = QDateTime::currentDateTimeUtc())
        -> std::optional<std::vector<CachedOnlinePlayable>>;
    auto resolve(QStringView handle,
                 QDateTime now = QDateTime::currentDateTimeUtc())
        -> EpisodeProviderResult<CachedOnlinePlayable>;
    auto replace(QStringView handle, OnlinePlayable playable,
                 QDateTime expiresAt) -> EpisodeProviderResult<void>;
    void invalidateProvider(QStringView providerKey);
    void clear();
    auto size() const noexcept -> qsizetype;

    static auto keyString(const OnlinePlayableCacheKey &key) -> QString;

private:
    struct SearchEntry {
        QString providerKey;
        QDateTime expiresAt;
        std::vector<QString> handles;
    };

    void removeSearch(QStringView key);

    QHash<QString, SearchEntry> mSearches;
    QHash<QString, CachedOnlinePlayable> mPlayables;
};

} // namespace anime_land
