#include "presentation/episode_resource/episode_resources_view_model.hpp"

#include "common/log.hpp"

#include <QDateTime>

#include <algorithm>
#include <cmath>
#include <utility>

namespace anime_land {
namespace {

auto searchStatusName(EpisodeResourcesViewModel::SearchStatus status)
    -> QString {
    switch (status) {
        case EpisodeResourcesViewModel::SearchStatus::Idle:
            return QStringLiteral("idle");
        case EpisodeResourcesViewModel::SearchStatus::Loading:
            return QStringLiteral("loading");
        case EpisodeResourcesViewModel::SearchStatus::Empty:
            return QStringLiteral("empty");
        case EpisodeResourcesViewModel::SearchStatus::Error:
            return QStringLiteral("error");
        case EpisodeResourcesViewModel::SearchStatus::Ready:
            return QStringLiteral("ready");
    }
    return QStringLiteral("error");
}

auto episodePrefix(int type) -> QString {
    switch (type) {
        case 0:
            return QStringLiteral("EP");
        case 1:
            return QStringLiteral("SP");
        case 2:
            return QStringLiteral("OP");
        case 3:
            return QStringLiteral("ED");
        case 4:
            return QStringLiteral("PV");
        case 5:
            return QStringLiteral("MAD");
        default:
            return QStringLiteral("其他");
    }
}

auto matchLabel(double confidence) -> QString {
    if (confidence >= 0.9) {
        return QStringLiteral("高度匹配");
    }
    if (confidence >= 0.65) {
        return QStringLiteral("可能匹配");
    }
    return QStringLiteral("模糊结果");
}

} // namespace

EpisodeResourcesViewModel::EpisodeResourcesViewModel(
    EpisodeResourceService &service, EpisodeProviderRegistry &registry,
    MediaLauncher launcher, QObject *parent)
    : EpisodeResourcesViewModel(
          service, registry,
          [&service](EpisodeId episode) { return service.query(episode); },
          std::move(launcher), parent) {}

EpisodeResourcesViewModel::EpisodeResourcesViewModel(
    EpisodeResourceService &service, EpisodeProviderRegistry &registry,
    QueryLoader queryLoader, MediaLauncher launcher, QObject *parent)
    : QObject(parent), mService(service), mRegistry(registry),
      mQueryLoader(std::move(queryLoader)), mLauncher(std::move(launcher)) {
    const auto providers = mRegistry.list();
    mProviders.reserve(providers.size());
    for (const auto &provider : providers) {
        if (!provider) {
            continue;
        }
        mProviders.push_back({
            .key = provider->key(),
            .name = provider->name(),
            .icon = provider->icon(),
            .selected = true,
            .status = SearchStatus::Idle,
            .message = {},
            .results = {},
        });
    }
}

EpisodeResourcesViewModel::~EpisodeResourcesViewModel() {
    mDestroying = true;
    ++mContextGeneration;
    ++mSearchGeneration;
    cancelActiveProviders();
    mTasks.shutdown().wait();
}

auto EpisodeResourcesViewModel::episode() const -> QVariantMap {
    if (!mQuery) {
        return {};
    }
    QString displayNumber;
    if (mQuery->episodeNumber) {
        displayNumber = episodePrefix(mQuery->episodeType)
                        + QString::number(*mQuery->episodeNumber, 'g', 8);
    }
    return {
        {QStringLiteral("id"), QVariant::fromValue(mQuery->episodeId.value)},
        {QStringLiteral("subjectId"),
         QVariant::fromValue(mQuery->subjectId.value)},
        {QStringLiteral("subjectTitle"), mQuery->subjectName},
        {QStringLiteral("episodeTitle"), mQuery->episodeName},
        {QStringLiteral("number"), displayNumber},
    };
}

auto EpisodeResourcesViewModel::providers() const -> QVariantList {
    QVariantList result;
    result.reserve(static_cast<qsizetype>(mProviders.size()));
    for (const auto &provider : mProviders) {
        result.push_back(providerMap(provider));
    }
    return result;
}

auto EpisodeResourcesViewModel::selectedProviderCount() const noexcept -> int {
    return static_cast<int>(std::ranges::count_if(
        mProviders, [](const ProviderState &provider) {
            return provider.selected;
        }));
}

void EpisodeResourcesViewModel::openEpisode(qlonglong episodeId) {
    const EpisodeId episode {episodeId};
    if (!isValid(episode) || mDestroying) {
        reportError(QStringLiteral("章节 ID 无效"));
        return;
    }

    cancelActiveProviders();
    ++mSearchGeneration;
    const auto generation = ++mContextGeneration;
    mQuery.reset();
    mEpisodeLoading = true;
    mPendingSearches = 0;
    mPlayingHandle.clear();
    mErrorMessage.clear();
    mNoticeMessage.clear();
    resetSearchResults();
    emit episodeChanged();
    emit providersChanged();
    emit stateChanged();
    mTasks.spawn(loadEpisode(episode, generation));
}

void EpisodeResourcesViewModel::setProviderSelected(
    const QString &providerKey, bool selected) {
    if (busy() || mDestroying) {
        return;
    }
    auto *provider = findProvider(providerKey);
    if (provider == nullptr || provider->selected == selected) {
        return;
    }
    provider->selected = selected;
    emit providersChanged();
}

void EpisodeResourcesViewModel::searchSelectedProviders() {
    if (!mQuery || busy() || mDestroying) {
        if (!mQuery && !mEpisodeLoading) {
            reportError(QStringLiteral("请先选择一个章节"));
        }
        return;
    }
    if (selectedProviderCount() == 0) {
        reportError(QStringLiteral("请至少选择一个在线源"));
        return;
    }

    cancelActiveProviders();
    const auto searchGeneration = ++mSearchGeneration;
    const auto contextGeneration = mContextGeneration;
    mCurrentHandles.clear();
    mHandleProviders.clear();
    mErrorMessage.clear();
    mNoticeMessage.clear();
    mPendingSearches = selectedProviderCount();
    for (auto &provider : mProviders) {
        provider.results.clear();
        provider.message.clear();
        provider.status = provider.selected ? SearchStatus::Loading
                                            : SearchStatus::Idle;
    }
    emit providersChanged();
    emit stateChanged();

    for (const auto &provider : mProviders) {
        if (provider.selected) {
            mTasks.spawn(searchProvider(provider.key, *mQuery,
                                        contextGeneration,
                                        searchGeneration));
        }
    }
}

void EpisodeResourcesViewModel::playOnline(const QString &handle) {
    if (busy() || mDestroying || !mCurrentHandles.contains(handle)) {
        if (!busy()) {
            reportError(QStringLiteral("在线结果无效或已经过期，请重新搜索"));
        }
        return;
    }
    const QString providerKey = mHandleProviders.value(handle);
    if (providerKey.isEmpty()) {
        reportError(QStringLiteral("在线结果缺少来源，请重新搜索"));
        return;
    }

    mPlayingHandle = handle;
    mErrorMessage.clear();
    mNoticeMessage = QStringLiteral("正在解析临时播放地址…");
    emit stateChanged();
    mTasks.spawn(resolveAndPlay(handle, providerKey, mContextGeneration,
                                mSearchGeneration));
}

void EpisodeResourcesViewModel::clear() {
    if (mDestroying) {
        return;
    }
    cancelActiveProviders();
    ++mContextGeneration;
    ++mSearchGeneration;
    mQuery.reset();
    mEpisodeLoading = false;
    mPendingSearches = 0;
    mPlayingHandle.clear();
    mErrorMessage.clear();
    mNoticeMessage.clear();
    resetSearchResults();
    emit episodeChanged();
    emit providersChanged();
    emit stateChanged();
}

auto EpisodeResourcesViewModel::loadEpisode(EpisodeId episode,
                                             std::uint64_t generation)
    -> ilias::Task<void> {
    if (!mQueryLoader) {
        if (!mDestroying && generation == mContextGeneration) {
            mEpisodeLoading = false;
            reportError(QStringLiteral("在线资源章节查询未配置"));
        }
        co_return;
    }
    auto loaded = co_await mQueryLoader(episode);
    if (mDestroying || generation != mContextGeneration) {
        co_return;
    }
    mEpisodeLoading = false;
    if (!loaded) {
        reportError(loaded.error().message);
        co_return;
    }
    mQuery = std::move(*loaded);
    mNoticeMessage = QStringLiteral("选择一个或多个在线源后开始搜索");
    emit episodeChanged();
    emit stateChanged();
}

auto EpisodeResourcesViewModel::searchProvider(
    QString providerKey, EpisodeQuery query,
    std::uint64_t contextGeneration, std::uint64_t searchGeneration)
    -> ilias::Task<void> {
    auto searched = co_await mService.search(providerKey, std::move(query));
    if (mDestroying || contextGeneration != mContextGeneration
        || searchGeneration != mSearchGeneration) {
        co_return;
    }
    auto *provider = findProvider(providerKey);
    if (provider == nullptr) {
        finishProviderSearch();
        co_return;
    }
    if (!searched) {
        provider->status = SearchStatus::Error;
        provider->message = searched.error().message;
        AL_LOG_WARN("[presentation.episode-resource] search failed provider={} code={}",
                    providerKey.toStdString(),
                    episodeProviderErrorCodeName(searched.error().code));
    }
    else if (searched->empty()) {
        provider->status = SearchStatus::Empty;
        provider->message = QStringLiteral("没有找到匹配当前章节的结果");
    }
    else {
        provider->status = SearchStatus::Ready;
        provider->results = std::move(*searched);
        provider->message =
            QStringLiteral("找到 %1 个临时结果")
                .arg(provider->results.size());
        for (const auto &cached : provider->results) {
            mCurrentHandles.insert(cached.handle);
            mHandleProviders.insert(cached.handle, providerKey);
        }
    }
    emit providersChanged();
    finishProviderSearch();
}

auto EpisodeResourcesViewModel::resolveAndPlay(
    QString handle, QString providerKey, std::uint64_t contextGeneration,
    std::uint64_t searchGeneration) -> ilias::Task<void> {
    auto resolved = co_await mService.resolve(handle);
    if (mDestroying || contextGeneration != mContextGeneration
        || searchGeneration != mSearchGeneration
        || handle != mPlayingHandle) {
        co_return;
    }
    if (!resolved) {
        mPlayingHandle.clear();
        mErrorMessage = resolved.error().message;
        mNoticeMessage.clear();
        if (resolved.error().code == EpisodeProviderErrorCode::ResultExpired) {
            if (auto *provider = findProvider(providerKey)) {
                provider->status = SearchStatus::Error;
                provider->message = QStringLiteral("结果已过期，请重新搜索");
                for (const auto &cached : provider->results) {
                    mCurrentHandles.remove(cached.handle);
                    mHandleProviders.remove(cached.handle);
                }
                provider->results.clear();
                emit providersChanged();
            }
        }
        emit stateChanged();
        co_return;
    }

    const auto video = std::ranges::find_if(
        resolved->assets, [](const RemoteAsset &asset) {
            return asset.kind == EpisodeAssetKind::Video;
        });
    const QUrl url = video == resolved->assets.end()
                         ? QUrl {}
                         : QUrl(video->data.value(QStringLiteral("url"))
                                    .toString(),
                                QUrl::StrictMode);
    if (video == resolved->assets.end() || !url.isValid()
        || (url.scheme() != QStringLiteral("https")
            && url.scheme() != QStringLiteral("http"))
        || url.host().isEmpty()) {
        mPlayingHandle.clear();
        reportError(QStringLiteral("在线源没有返回可播放的 HTTP(S) 视频"));
        co_return;
    }
    if (!mLauncher || !mLauncher(url, resolved->displayName)) {
        mPlayingHandle.clear();
        reportError(QStringLiteral("内置播放器未能接收这个在线资源"));
        co_return;
    }

    mPlayingHandle.clear();
    mErrorMessage.clear();
    mNoticeMessage = QStringLiteral("已在内置播放器中打开临时在线资源");
    emit stateChanged();
    emit playbackOpened();
}

auto EpisodeResourcesViewModel::findProvider(QStringView key)
    -> ProviderState * {
    const auto found = std::ranges::find_if(
        mProviders, [&](const ProviderState &provider) {
            return provider.key == key;
        });
    return found == mProviders.end() ? nullptr : &*found;
}

auto EpisodeResourcesViewModel::findProvider(QStringView key) const
    -> const ProviderState * {
    const auto found = std::ranges::find_if(
        mProviders, [&](const ProviderState &provider) {
            return provider.key == key;
        });
    return found == mProviders.end() ? nullptr : &*found;
}

auto EpisodeResourcesViewModel::providerMap(
    const ProviderState &provider) const -> QVariantMap {
    QVariantList results;
    results.reserve(static_cast<qsizetype>(provider.results.size()));
    for (const auto &cached : provider.results) {
        results.push_back(resultMap(cached));
    }
    return {
        {QStringLiteral("key"), provider.key},
        {QStringLiteral("name"), provider.name},
        {QStringLiteral("iconUrl"), provider.icon.toString()},
        {QStringLiteral("selected"), provider.selected},
        {QStringLiteral("status"), searchStatusName(provider.status)},
        {QStringLiteral("message"), provider.message},
        {QStringLiteral("results"), results},
        {QStringLiteral("resultCount"),
         static_cast<int>(provider.results.size())},
    };
}

auto EpisodeResourcesViewModel::resultMap(
    const CachedOnlinePlayable &cached) const -> QVariantMap {
    const auto &playable = cached.playable;
    QVariantList assets;
    assets.reserve(static_cast<qsizetype>(playable.assets.size()));
    for (const auto &asset : playable.assets) {
        assets.push_back(QVariantMap {
            {QStringLiteral("kind"), episodeAssetKindName(asset.kind)},
            {QStringLiteral("streamType"),
             mediaStreamTypeName(asset.streamType)},
            {QStringLiteral("name"), asset.displayName},
            {QStringLiteral("language"), asset.language.value_or(QString {})},
            {QStringLiteral("mimeType"), asset.mimeType.value_or(QString {})},
        });
    }
    const double confidence = std::isfinite(playable.match.confidence)
                                  ? std::clamp(playable.match.confidence,
                                               0.0, 1.0)
                                  : 0.0;
    return {
        {QStringLiteral("handle"), cached.handle},
        {QStringLiteral("displayName"), playable.displayName},
        {QStringLiteral("subjectTitle"), playable.match.title},
        {QStringLiteral("coverUrl"), playable.match.cover.toString()},
        {QStringLiteral("detail"), playable.match.detail},
        {QStringLiteral("episodeTitle"), playable.match.episodeTitle},
        {QStringLiteral("sourceLine"), playable.match.sourceLine},
        {QStringLiteral("confidence"), confidence},
        {QStringLiteral("matchLabel"), matchLabel(confidence)},
        {QStringLiteral("assets"), assets},
        {QStringLiteral("temporary"), true},
        {QStringLiteral("expiresAt"),
         cached.expiresAt.toUTC().toString(Qt::ISODateWithMs)},
    };
}

void EpisodeResourcesViewModel::resetSearchResults() {
    mCurrentHandles.clear();
    mHandleProviders.clear();
    for (auto &provider : mProviders) {
        provider.status = SearchStatus::Idle;
        provider.message.clear();
        provider.results.clear();
    }
}

void EpisodeResourcesViewModel::cancelActiveProviders() {
    QSet<QString> active;
    for (const auto &provider : mProviders) {
        if (provider.status == SearchStatus::Loading) {
            active.insert(provider.key);
        }
    }
    if (!mPlayingHandle.isEmpty()) {
        active.insert(mHandleProviders.value(mPlayingHandle));
    }
    for (const auto &key : active) {
        if (auto provider = mRegistry.find(key)) {
            provider->cancel();
        }
    }
}

void EpisodeResourcesViewModel::finishProviderSearch() {
    if (mPendingSearches <= 0) {
        return;
    }
    --mPendingSearches;
    if (mPendingSearches > 0) {
        emit stateChanged();
        return;
    }

    int resultCount = 0;
    int errorCount = 0;
    for (const auto &provider : mProviders) {
        resultCount += static_cast<int>(provider.results.size());
        errorCount += provider.selected
                      && provider.status == SearchStatus::Error;
    }
    if (resultCount > 0) {
        mNoticeMessage =
            errorCount > 0
                ? QStringLiteral("找到 %1 个建议；%2 个在线源失败")
                      .arg(resultCount)
                      .arg(errorCount)
                : QStringLiteral("找到 %1 个建议，请核对番剧和分集后选择")
                      .arg(resultCount);
    }
    else if (errorCount > 0) {
        mNoticeMessage = QStringLiteral("在线搜索完成，但没有可用结果");
    }
    else {
        mNoticeMessage = QStringLiteral("所选在线源均未找到匹配结果");
    }
    emit stateChanged();
}

void EpisodeResourcesViewModel::reportError(QString message) {
    mErrorMessage = std::move(message);
    mNoticeMessage.clear();
    emit stateChanged();
}

} // namespace anime_land
