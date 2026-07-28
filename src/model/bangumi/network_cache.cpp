#include "model/bangumi/network_cache.hpp"

#include "common/app_settings.hpp"
#include "common/log.hpp"

#include <QDateTime>
#include <QDir>
#include <QNetworkAccessManager>
#include <QNetworkCacheMetaData>
#include <QNetworkDiskCache>
#include <QUrl>

#include <algorithm>
#include <limits>

namespace anime_land {
namespace {

class PublicNetworkDiskCache final : public QNetworkDiskCache {
public:
    PublicNetworkDiskCache(NetworkCachePartition partition,
                           std::chrono::seconds timeToLive,
                           QObject *parent)
        : QNetworkDiskCache(parent), mPartition(partition),
          mTimeToLive(timeToLive) {}

    auto prepare(const QNetworkCacheMetaData &metaData)
        -> QIODevice * override {
        if (!isPublicCacheable(metaData)) {
            return nullptr;
        }
        auto bounded = metaData;
        const QDateTime maximumExpiration =
            QDateTime::currentDateTimeUtc().addSecs(mTimeToLive.count());
        if (!bounded.expirationDate().isValid()
            || bounded.expirationDate() > maximumExpiration) {
            bounded.setExpirationDate(maximumExpiration);
        }
        return QNetworkDiskCache::prepare(bounded);
    }

private:
    auto isPublicCacheable(const QNetworkCacheMetaData &metaData) const
        -> bool {
        if (mPartition == NetworkCachePartition::Images) {
            const QString host = metaData.url().host().toLower();
            const bool bangumiHost =
                host == QStringLiteral("bgm.tv")
                || host.endsWith(QStringLiteral(".bgm.tv"))
                || host == QStringLiteral("bangumi.tv")
                || host.endsWith(QStringLiteral(".bangumi.tv"));
            if (!bangumiHost) {
                return false;
            }
            for (const auto &[name, value] : metaData.rawHeaders()) {
                if (name.compare(QByteArrayLiteral("Content-Type"),
                                 Qt::CaseInsensitive)
                        == 0
                    && value.toLower().startsWith("image/")) {
                    return true;
                }
            }
            return false;
        }

        const QString path = metaData.url().path();
        return path == QStringLiteral("/calendar")
               || path.startsWith(QStringLiteral("/v0/subjects/"))
               || path == QStringLiteral("/v0/episodes");
    }

    NetworkCachePartition mPartition;
    std::chrono::seconds mTimeToLive;
};

} // namespace

auto bangumiNetworkCacheOptions(const BangumiSettings &settings)
    -> BangumiNetworkCacheOptions {
    return {
        .enabled = settings.cache_enabled,
        .directory = QString::fromStdString(
            expandVariables(settings.cache_path)),
        .maxSizeBytes = settings.cache_max_size,
        .timeToLive = std::chrono::days(
            std::max(0, settings.cache_ttl_days)),
    };
}

void installBangumiNetworkCache(
    QNetworkAccessManager &network,
    const BangumiNetworkCacheOptions &options,
    NetworkCachePartition partition) {
    if (!options.enabled || options.maxSizeBytes == 0
        || options.directory.trimmed().isEmpty()
        || options.timeToLive <= std::chrono::seconds::zero()) {
        return;
    }

    // Keep the configured value as a total budget shared by API payloads and
    // image covers. Images receive the larger partition because they dominate
    // actual disk usage.
    const std::uint64_t numerator =
        partition == NetworkCachePartition::Api ? 2U : 3U;
    const std::uint64_t partitionSize =
        options.maxSizeBytes / 5U * numerator;
    if (partitionSize == 0) {
        return;
    }
    const QString child =
        partition == NetworkCachePartition::Api
            ? QStringLiteral("api") : QStringLiteral("images");
    auto *cache = new PublicNetworkDiskCache(
        partition, options.timeToLive, &network);
    cache->setCacheDirectory(QDir(options.directory).filePath(child));
    cache->setMaximumCacheSize(static_cast<qint64>(std::min<std::uint64_t>(
        partitionSize,
        static_cast<std::uint64_t>(std::numeric_limits<qint64>::max()))));
    network.setCache(cache);
    AL_LOG_INFO("[bangumi.cache] enabled partition={} max_bytes={} ttl_hours={}",
                partition == NetworkCachePartition::Api ? "api" : "images",
                partitionSize,
                std::chrono::duration_cast<std::chrono::hours>(
                    options.timeToLive)
                    .count());
}

} // namespace anime_land
