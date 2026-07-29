#include "pch.hpp"

#include "model/episode_resource/service.hpp"

#include "model/persistence/catalog_store.hpp"

#include <QDateTime>

#include <algorithm>
#include <utility>

namespace anime_land {

EpisodeResourceService::EpisodeResourceService(
    EpisodeProviderRegistry &registry, OnlinePlayableCache &cache)
    : mRegistry(registry), mCache(cache) {}

EpisodeResourceService::EpisodeResourceService(
    EpisodeProviderRegistry &registry, OnlinePlayableCache &cache,
    persistence::CatalogStore &catalog)
    : mRegistry(registry), mCache(cache), mCatalog(&catalog) {}

auto EpisodeResourceService::query(EpisodeId episode)
    -> ilias::Task<EpisodeProviderResult<EpisodeQuery>> {
    if (mCatalog == nullptr || !isValid(episode)) {
        co_return ilias::Err(episodeProviderError(
            EpisodeProviderErrorCode::InvalidQuery,
            QStringLiteral("章节目录查询未配置或章节 ID 无效")));
    }

    auto storedEpisode = co_await mCatalog->getEpisode(episode);
    if (!storedEpisode) {
        co_return ilias::Err(episodeProviderError(
            EpisodeProviderErrorCode::InvalidQuery,
            QStringLiteral("无法读取章节目录")));
    }
    if (!*storedEpisode) {
        co_return ilias::Err(episodeProviderError(
            EpisodeProviderErrorCode::InvalidQuery,
            QStringLiteral("本地目录中没有这个章节")));
    }

    auto storedSubject =
        co_await mCatalog->getSubject((*storedEpisode)->subjectId);
    if (!storedSubject) {
        co_return ilias::Err(episodeProviderError(
            EpisodeProviderErrorCode::InvalidQuery,
            QStringLiteral("无法读取章节所属条目")));
    }
    if (!*storedSubject) {
        co_return ilias::Err(episodeProviderError(
            EpisodeProviderErrorCode::InvalidQuery,
            QStringLiteral("本地目录中没有章节所属条目")));
    }

    const auto &subject = **storedSubject;
    const auto &stored = **storedEpisode;
    const QString subjectName =
        subject.summary.titleCn
                && !subject.summary.titleCn->trimmed().isEmpty()
            ? *subject.summary.titleCn
            : subject.summary.title;
    std::vector<QString> aliases = subject.aliases;
    const auto appendAlias = [&](const QString &value) {
        if (!value.trimmed().isEmpty() && value != subjectName
            && std::ranges::find(aliases, value) == aliases.end()) {
            aliases.push_back(value);
        }
    };
    appendAlias(subject.summary.title);
    if (subject.summary.titleCn) {
        appendAlias(*subject.summary.titleCn);
    }

    const std::optional<double> episodeNumber =
        stored.episodeNumber
            ? stored.episodeNumber
            : std::optional<double> {
                  static_cast<double>(stored.sortOrder + 1)};
    QString episodeName;
    if (stored.titleCn && !stored.titleCn->trimmed().isEmpty()) {
        episodeName = *stored.titleCn;
    }
    else if (stored.title && !stored.title->trimmed().isEmpty()) {
        episodeName = *stored.title;
    }
    else if (episodeNumber) {
        episodeName = QStringLiteral("第 %1 集")
                          .arg(QString::number(*episodeNumber, 'g', 8));
    }

    EpisodeQuery result {
        .subjectId = stored.subjectId,
        .episodeId = stored.id,
        .subjectName = subjectName,
        .subjectAliases = std::move(aliases),
        .episodeName = episodeName,
        .episodeType = stored.episodeType,
        .episodeNumber = episodeNumber,
    };
    if (auto validated = validate(result); !validated) {
        co_return ilias::Err(std::move(validated.error()));
    }
    co_return result;
}

auto EpisodeResourceService::search(QString providerKey, EpisodeQuery query,
                                    std::chrono::milliseconds ttl)
    -> ilias::Task<
        EpisodeProviderResult<std::vector<CachedOnlinePlayable>>> {
    auto provider = mRegistry.find(providerKey);
    if (!provider) {
        co_return ilias::Err(episodeProviderError(
            EpisodeProviderErrorCode::ProviderNotFound,
            QStringLiteral("Episode Provider 不存在或已禁用")));
    }
    if (auto validated = validate(query); !validated) {
        co_return ilias::Err(std::move(validated.error()));
    }

    const OnlinePlayableCacheKey key {
        .providerKey = providerKey,
        .providerGeneration = provider->generation(),
        .subjectId = query.subjectId,
        .episodeId = query.episodeId,
        .normalizedSubjectName = query.subjectName,
        .normalizedEpisodeName = query.episodeName,
    };
    if (auto cached = mCache.find(key)) {
        co_return std::move(*cached);
    }

    auto searched = co_await provider->search(std::move(query));
    if (!searched) {
        co_return ilias::Err(std::move(searched.error()));
    }
    std::vector<QString> stableKeys;
    stableKeys.reserve(searched->size());
    for (const auto &playable : *searched) {
        if (auto validated = validate(playable); !validated) {
            co_return ilias::Err(std::move(validated.error()));
        }
        if (std::ranges::find(stableKeys, playable.stableKey) !=
            stableKeys.end()) {
            co_return ilias::Err(episodeProviderError(
                EpisodeProviderErrorCode::InvalidScriptResult,
                QStringLiteral("Provider 搜索结果 stableKey 重复")));
        }
        stableKeys.push_back(playable.stableKey);
    }
    co_return mCache.store(key, std::move(*searched), ttl);
}

auto EpisodeResourceService::resolve(QString handle)
    -> ilias::Task<EpisodeProviderResult<OnlinePlayable>> {
    auto cached = mCache.resolve(handle);
    if (!cached) {
        co_return ilias::Err(std::move(cached.error()));
    }
    auto provider = mRegistry.find(cached->providerKey);
    if (!provider || provider->generation() != cached->providerGeneration) {
        mCache.invalidateProvider(cached->providerKey);
        co_return ilias::Err(episodeProviderError(
            EpisodeProviderErrorCode::ResultExpired,
            QStringLiteral("Provider 已重载，请重新搜索")));
    }
    if (isResolved(cached->playable)) {
        co_return cached->playable;
    }

    auto resolved = co_await provider->resolve(cached->playable);
    if (!resolved) {
        co_return ilias::Err(std::move(resolved.error()));
    }
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QDateTime expiresAt =
        resolved->expiresAt.value_or(now.addSecs(2 * 60)).toUTC();
    if (auto replaced = mCache.replace(handle, *resolved, expiresAt);
        !replaced) {
        co_return ilias::Err(std::move(replaced.error()));
    }
    co_return std::move(*resolved);
}

void EpisodeResourceService::invalidateProvider(QStringView providerKey) {
    if (auto provider = mRegistry.find(providerKey)) {
        provider->cancel();
    }
    mCache.invalidateProvider(providerKey);
}

} // namespace anime_land
