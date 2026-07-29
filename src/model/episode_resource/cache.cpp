#include "pch.hpp"

#include "model/episode_resource/cache.hpp"

#include <QCryptographicHash>
#include <QUuid>

#include <algorithm>
#include <utility>

namespace anime_land {
namespace {

auto effectiveExpiry(const OnlinePlayable &playable, QDateTime cacheExpiry)
    -> QDateTime {
    if (playable.expiresAt && *playable.expiresAt < cacheExpiry) {
        return playable.expiresAt->toUTC();
    }
    return cacheExpiry;
}

} // namespace

auto OnlinePlayableCache::keyString(const OnlinePlayableCacheKey &key)
    -> QString {
    const QByteArray bytes =
        key.providerKey.toUtf8() + '\0' +
        QByteArray::number(key.providerGeneration) + '\0' +
        QByteArray::number(key.subjectId.value) + '\0' +
        QByteArray::number(key.episodeId.value) + '\0' +
        key.normalizedSubjectName.trimmed().toCaseFolded().toUtf8() + '\0' +
        key.normalizedEpisodeName.trimmed().toCaseFolded().toUtf8();
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

auto OnlinePlayableCache::store(const OnlinePlayableCacheKey &key,
                                std::vector<OnlinePlayable> playables,
                                std::chrono::milliseconds ttl,
                                QDateTime now)
    -> std::vector<CachedOnlinePlayable> {
    const QString searchKey = keyString(key);
    removeSearch(searchKey);

    const auto boundedTtl = std::clamp(
        ttl, std::chrono::milliseconds(std::chrono::seconds(1)),
        std::chrono::milliseconds(std::chrono::hours(1)));
    const QDateTime cacheExpiry = now.toUTC().addMSecs(boundedTtl.count());
    SearchEntry search {
        .providerKey = key.providerKey,
        .expiresAt = cacheExpiry,
        .handles = {},
    };
    std::vector<CachedOnlinePlayable> result;
    result.reserve(playables.size());
    search.handles.reserve(playables.size());
    for (auto &playable : playables) {
        const QString handle = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QDateTime expiresAt = effectiveExpiry(playable, cacheExpiry);
        CachedOnlinePlayable cached {
            .handle = handle,
            .providerKey = key.providerKey,
            .providerGeneration = key.providerGeneration,
            .playable = std::move(playable),
            .expiresAt = expiresAt,
        };
        search.handles.push_back(handle);
        mPlayables.insert(handle, cached);
        result.push_back(std::move(cached));
    }
    mSearches.insert(searchKey, std::move(search));
    return result;
}

auto OnlinePlayableCache::find(const OnlinePlayableCacheKey &key, QDateTime now)
    -> std::optional<std::vector<CachedOnlinePlayable>> {
    const QString searchKey = keyString(key);
    const auto found = mSearches.find(searchKey);
    if (found == mSearches.end()) {
        return std::nullopt;
    }
    if (found->expiresAt <= now.toUTC()) {
        removeSearch(searchKey);
        return std::nullopt;
    }
    std::vector<CachedOnlinePlayable> result;
    result.reserve(found->handles.size());
    for (const auto &handle : found->handles) {
        const auto playable = mPlayables.constFind(handle);
        if (playable == mPlayables.cend() || playable->expiresAt <= now.toUTC()) {
            removeSearch(searchKey);
            return std::nullopt;
        }
        result.push_back(*playable);
    }
    return result;
}

auto OnlinePlayableCache::resolve(QStringView handle, QDateTime now)
    -> EpisodeProviderResult<CachedOnlinePlayable> {
    const auto found = mPlayables.find(handle.toString());
    if (found == mPlayables.end() || found->expiresAt <= now.toUTC()) {
        if (found != mPlayables.end()) {
            mPlayables.erase(found);
        }
        return ilias::Err(episodeProviderError(
            EpisodeProviderErrorCode::ResultExpired,
            QStringLiteral("在线结果已过期，请重新搜索")));
    }
    return *found;
}

auto OnlinePlayableCache::replace(QStringView handle, OnlinePlayable playable,
                                  QDateTime expiresAt)
    -> EpisodeProviderResult<void> {
    if (auto validated = validate(playable, true); !validated) {
        return validated;
    }
    auto found = mPlayables.find(handle.toString());
    if (found == mPlayables.end()) {
        return ilias::Err(episodeProviderError(
            EpisodeProviderErrorCode::ResultExpired,
            QStringLiteral("在线结果已过期，请重新搜索")));
    }
    found->playable = std::move(playable);
    found->expiresAt = expiresAt.toUTC();
    return {};
}

void OnlinePlayableCache::invalidateProvider(QStringView providerKey) {
    std::vector<QString> handles;
    for (auto iterator = mPlayables.cbegin(); iterator != mPlayables.cend();
         ++iterator) {
        if (iterator->providerKey == providerKey) {
            handles.push_back(iterator.key());
        }
    }
    for (const auto &handle : handles) {
        mPlayables.remove(handle);
    }
    for (auto iterator = mSearches.begin(); iterator != mSearches.end();) {
        iterator = iterator->providerKey == providerKey
                       ? mSearches.erase(iterator)
                       : std::next(iterator);
    }
}

void OnlinePlayableCache::clear() {
    mSearches.clear();
    mPlayables.clear();
}

auto OnlinePlayableCache::size() const noexcept -> qsizetype {
    return mPlayables.size();
}

void OnlinePlayableCache::removeSearch(QStringView key) {
    const auto found = mSearches.find(key.toString());
    if (found == mSearches.end()) {
        return;
    }
    for (const auto &handle : found->handles) {
        mPlayables.remove(handle);
    }
    mSearches.erase(found);
}

} // namespace anime_land
