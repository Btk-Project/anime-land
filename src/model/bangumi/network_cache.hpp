#pragma once

#include <QString>

#include <chrono>
#include <cstdint>

class QNetworkAccessManager;

namespace anime_land {

struct BangumiSettings;

enum class NetworkCachePartition {
    Api,
    Images,
};

struct BangumiNetworkCacheOptions {
    bool enabled = false;
    QString directory;
    std::uint64_t maxSizeBytes = 0;
    std::chrono::seconds timeToLive = std::chrono::seconds::zero();
};

auto bangumiNetworkCacheOptions(const BangumiSettings &settings)
    -> BangumiNetworkCacheOptions;

/** Installs a public-data-only Qt disk cache when options enable it. */
void installBangumiNetworkCache(QNetworkAccessManager &network,
                                const BangumiNetworkCacheOptions &options,
                                NetworkCachePartition partition);

} // namespace anime_land
