#pragma once

#include "model/episode_resource/episode_resource.hpp"

#include <QObject>
#include <QHash>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>

#include <ilias/task/scope.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace anime_land {

/** Detail-page state for explicitly requested, non-persistent online results. */
class EpisodeResourcesViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap episode READ episode NOTIFY episodeChanged)
    Q_PROPERTY(QVariantList providers READ providers NOTIFY providersChanged)
    Q_PROPERTY(bool episodeLoading READ episodeLoading NOTIFY stateChanged)
    Q_PROPERTY(bool episodeReady READ episodeReady NOTIFY episodeChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY stateChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool hasProviders READ hasProviders NOTIFY providersChanged)
    Q_PROPERTY(int selectedProviderCount READ selectedProviderCount
                   NOTIFY providersChanged)
    Q_PROPERTY(QString playingHandle READ playingHandle NOTIFY stateChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)
    Q_PROPERTY(QString noticeMessage READ noticeMessage NOTIFY stateChanged)

public:
    enum class SearchStatus {
        Idle,
        Loading,
        Empty,
        Error,
        Ready,
    };

    using QueryLoader = std::function<
        ilias::Task<EpisodeProviderResult<EpisodeQuery>>(EpisodeId)>;
    using MediaLauncher = std::function<bool(const QUrl &, const QString &)>;

    EpisodeResourcesViewModel(EpisodeResourceService &service,
                              EpisodeProviderRegistry &registry,
                              MediaLauncher launcher = {},
                              QObject *parent = nullptr);
    EpisodeResourcesViewModel(EpisodeResourceService &service,
                              EpisodeProviderRegistry &registry,
                              QueryLoader queryLoader,
                              MediaLauncher launcher = {},
                              QObject *parent = nullptr);
    ~EpisodeResourcesViewModel() override;

    auto episode() const -> QVariantMap;
    auto providers() const -> QVariantList;
    auto episodeLoading() const noexcept -> bool { return mEpisodeLoading; }
    auto episodeReady() const noexcept -> bool { return mQuery.has_value(); }
    auto searching() const noexcept -> bool { return mPendingSearches > 0; }
    auto playing() const noexcept -> bool { return !mPlayingHandle.isEmpty(); }
    auto busy() const noexcept -> bool {
        return episodeLoading() || searching() || playing();
    }
    auto hasProviders() const noexcept -> bool { return !mProviders.empty(); }
    auto selectedProviderCount() const noexcept -> int;
    auto playingHandle() const -> QString { return mPlayingHandle; }
    auto errorMessage() const -> QString { return mErrorMessage; }
    auto noticeMessage() const -> QString { return mNoticeMessage; }

    Q_INVOKABLE void openEpisode(qlonglong episodeId);
    Q_INVOKABLE void setProviderSelected(const QString &providerKey,
                                         bool selected);
    Q_INVOKABLE void searchSelectedProviders();
    Q_INVOKABLE void playOnline(const QString &handle);
    Q_INVOKABLE void clear();

signals:
    void episodeChanged();
    void providersChanged();
    void stateChanged();
    void playbackOpened();

private:
    struct ProviderState {
        QString key;
        QString name;
        QUrl icon;
        bool selected = true;
        SearchStatus status = SearchStatus::Idle;
        QString message;
        std::vector<CachedOnlinePlayable> results;
    };

    auto loadEpisode(EpisodeId episode, std::uint64_t generation)
        -> ilias::Task<void>;
    auto searchProvider(QString providerKey, EpisodeQuery query,
                        std::uint64_t contextGeneration,
                        std::uint64_t searchGeneration)
        -> ilias::Task<void>;
    auto resolveAndPlay(QString handle, QString providerKey,
                        std::uint64_t contextGeneration,
                        std::uint64_t searchGeneration)
        -> ilias::Task<void>;
    auto findProvider(QStringView key) -> ProviderState *;
    auto findProvider(QStringView key) const -> const ProviderState *;
    auto providerMap(const ProviderState &provider) const -> QVariantMap;
    auto resultMap(const CachedOnlinePlayable &cached) const -> QVariantMap;
    void resetSearchResults();
    void cancelActiveProviders();
    void finishProviderSearch();
    void reportError(QString message);

    EpisodeResourceService &mService;
    EpisodeProviderRegistry &mRegistry;
    QueryLoader mQueryLoader;
    MediaLauncher mLauncher;
    ilias::TaskScope mTasks;
    std::vector<ProviderState> mProviders;
    std::optional<EpisodeQuery> mQuery;
    QSet<QString> mCurrentHandles;
    QHash<QString, QString> mHandleProviders;
    QString mPlayingHandle;
    QString mErrorMessage;
    QString mNoticeMessage;
    std::uint64_t mContextGeneration = 0;
    std::uint64_t mSearchGeneration = 0;
    int mPendingSearches = 0;
    bool mEpisodeLoading = false;
    bool mDestroying = false;
};

} // namespace anime_land
