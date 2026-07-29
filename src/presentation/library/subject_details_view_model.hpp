#pragma once

#include "model/library/local_media_import.hpp"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include <ilias/task.hpp>
#include <ilias/task/scope.hpp>

#include <cstdint>
#include <functional>
#include <optional>

namespace anime_land {

/** Database-backed subject, episode and linked-media state for QML details. */
class SubjectDetailsViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap subject READ subject NOTIFY detailsChanged)
    Q_PROPERTY(QVariantList episodes READ episodes NOTIFY detailsChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(bool refreshing READ refreshing NOTIFY stateChanged)
    Q_PROPERTY(bool loadingMore READ loadingMore NOTIFY stateChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY stateChanged)
    Q_PROPERTY(bool hasSubject READ hasSubject NOTIFY detailsChanged)
    Q_PROPERTY(int playableEpisodeCount READ playableEpisodeCount
                   NOTIFY detailsChanged)
    Q_PROPERTY(int totalEpisodeCount READ totalEpisodeCount
                   NOTIFY detailsChanged)
    Q_PROPERTY(int currentEpisodePage READ currentEpisodePage
                   NOTIFY detailsChanged)
    Q_PROPERTY(int episodePageCount READ episodePageCount
                   NOTIFY detailsChanged)
    Q_PROPERTY(int episodePageSize READ episodePageSize CONSTANT)
    Q_PROPERTY(bool episodeSortDescending READ episodeSortDescending
                   NOTIFY detailsChanged)
    Q_PROPERTY(bool hasMoreEpisodes READ hasMoreEpisodes
                   NOTIFY detailsChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)
    Q_PROPERTY(QString noticeMessage READ noticeMessage NOTIFY stateChanged)

public:
    using DetailsLoader = std::function<ilias::Task<
        LibraryResult<std::optional<SubjectLibraryDetails>>>(SubjectId)>;
    using PagedDetailsLoader = std::function<ilias::Task<
        LibraryResult<std::optional<SubjectLibraryDetails>>>(SubjectId, int,
                                                              int, bool)>;
    using BangumiResolver = std::function<
        ilias::Task<LibraryResult<SubjectId>>(std::int64_t)>;
    using BangumiFinder = std::function<ilias::Task<
        LibraryResult<std::optional<SubjectId>>>(std::int64_t)>;
    using EpisodePlayer = std::function<
        ilias::Task<LibraryResult<void>>(EpisodeId)>;

    explicit SubjectDetailsViewModel(LocalMediaImportService &service,
                                     QObject *parent = nullptr);
    SubjectDetailsViewModel(DetailsLoader loader,
                            BangumiResolver resolver = {},
                            EpisodePlayer player = {},
                            QObject *parent = nullptr,
                            BangumiFinder finder = {});
    SubjectDetailsViewModel(PagedDetailsLoader loader,
                            BangumiResolver resolver = {},
                            EpisodePlayer player = {},
                            QObject *parent = nullptr,
                            BangumiFinder finder = {});
    ~SubjectDetailsViewModel() override;

    auto subject() const -> QVariantMap { return mSubject; }
    auto episodes() const -> QVariantList { return mEpisodes; }
    auto loading() const noexcept -> bool { return mLoading; }
    auto refreshing() const noexcept -> bool { return mRefreshing; }
    auto loadingMore() const noexcept -> bool { return mLoadingMore; }
    auto playing() const noexcept -> bool { return mPlaying; }
    auto hasSubject() const noexcept -> bool { return !mSubject.isEmpty(); }
    auto playableEpisodeCount() const noexcept -> int {
        return mPlayableEpisodeCount;
    }
    auto totalEpisodeCount() const noexcept -> int {
        return mTotalEpisodeCount;
    }
    auto currentEpisodePage() const noexcept -> int {
        return mCurrentEpisodePage;
    }
    auto episodePageCount() const noexcept -> int {
        return mTotalEpisodeCount == 0
                   ? 0
                   : (mTotalEpisodeCount + kEpisodePageSize - 1)
                         / kEpisodePageSize;
    }
    auto episodePageSize() const noexcept -> int {
        return kEpisodePageSize;
    }
    auto episodeSortDescending() const noexcept -> bool {
        return mEpisodeSortDescending;
    }
    auto hasMoreEpisodes() const noexcept -> bool {
        return mCurrentEpisodePage < episodePageCount();
    }
    auto errorMessage() const -> QString { return mErrorMessage; }
    auto noticeMessage() const -> QString { return mNoticeMessage; }

    Q_INVOKABLE void openSubject(qlonglong subjectId);
    Q_INVOKABLE void openBangumiSubject(qlonglong bangumiSubjectId);
    Q_INVOKABLE void playEpisode(qlonglong episodeId);
    Q_INVOKABLE void playFirstAvailable();
    Q_INVOKABLE void loadMoreEpisodes();
    Q_INVOKABLE void goToEpisodePage(int page);
    Q_INVOKABLE void previousEpisodePage();
    Q_INVOKABLE void nextEpisodePage();
    Q_INVOKABLE void setEpisodeSortDescending(bool descending);
    Q_INVOKABLE void clear();

signals:
    void detailsChanged();
    void stateChanged();

private:
    static constexpr int kEpisodePageSize = 24;

    auto loadPage(SubjectId subject, int page, bool descending,
                  std::uint64_t generation)
        -> ilias::Task<void>;
    auto resolveAndLoad(std::int64_t bangumiSubjectId,
                        std::uint64_t generation) -> ilias::Task<void>;
    auto play(EpisodeId episode, std::uint64_t generation)
        -> ilias::Task<void>;
    void applyDetails(const SubjectLibraryDetails &details, bool append);
    void reportInvalid(QString message);

    PagedDetailsLoader mLoader;
    BangumiResolver mResolver;
    BangumiFinder mFinder;
    EpisodePlayer mPlayer;
    ilias::TaskScope mTasks;
    QVariantMap mSubject;
    QVariantList mEpisodes;
    QString mErrorMessage;
    QString mNoticeMessage;
    std::optional<EpisodeId> mFirstPlayableEpisode;
    std::optional<SubjectId> mCurrentSubject;
    std::optional<std::int64_t> mBangumiId;
    std::uint64_t mGeneration = 0;
    int mPlayableEpisodeCount = 0;
    int mTotalEpisodeCount = 0;
    int mCurrentEpisodePage = 1;
    bool mLoading = false;
    bool mRefreshing = false;
    bool mLoadingMore = false;
    bool mPlaying = false;
    bool mEpisodeSortDescending = false;
    bool mDestroying = false;
};

} // namespace anime_land
