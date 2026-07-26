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
    Q_PROPERTY(bool playing READ playing NOTIFY stateChanged)
    Q_PROPERTY(bool hasSubject READ hasSubject NOTIFY detailsChanged)
    Q_PROPERTY(int playableEpisodeCount READ playableEpisodeCount
                   NOTIFY detailsChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)
    Q_PROPERTY(QString noticeMessage READ noticeMessage NOTIFY stateChanged)

public:
    using DetailsLoader = std::function<ilias::Task<
        LibraryResult<std::optional<SubjectLibraryDetails>>>(SubjectId)>;
    using BangumiResolver = std::function<
        ilias::Task<LibraryResult<SubjectId>>(std::int64_t)>;
    using EpisodePlayer = std::function<
        ilias::Task<LibraryResult<void>>(EpisodeId)>;

    explicit SubjectDetailsViewModel(LocalMediaImportService &service,
                                     QObject *parent = nullptr);
    SubjectDetailsViewModel(DetailsLoader loader,
                            BangumiResolver resolver = {},
                            EpisodePlayer player = {},
                            QObject *parent = nullptr);
    ~SubjectDetailsViewModel() override;

    auto subject() const -> QVariantMap { return mSubject; }
    auto episodes() const -> QVariantList { return mEpisodes; }
    auto loading() const noexcept -> bool { return mLoading; }
    auto playing() const noexcept -> bool { return mPlaying; }
    auto hasSubject() const noexcept -> bool { return !mSubject.isEmpty(); }
    auto playableEpisodeCount() const noexcept -> int {
        return mPlayableEpisodeCount;
    }
    auto errorMessage() const -> QString { return mErrorMessage; }
    auto noticeMessage() const -> QString { return mNoticeMessage; }

    Q_INVOKABLE void openSubject(qlonglong subjectId);
    Q_INVOKABLE void openBangumiSubject(qlonglong bangumiSubjectId);
    Q_INVOKABLE void playEpisode(qlonglong episodeId);
    Q_INVOKABLE void playFirstAvailable();
    Q_INVOKABLE void clear();

signals:
    void detailsChanged();
    void stateChanged();

private:
    auto load(SubjectId subject, std::uint64_t generation)
        -> ilias::Task<void>;
    auto resolveAndLoad(std::int64_t bangumiSubjectId,
                        std::uint64_t generation) -> ilias::Task<void>;
    auto play(EpisodeId episode, std::uint64_t generation)
        -> ilias::Task<void>;
    void applyDetails(const SubjectLibraryDetails &details);
    void reportInvalid(QString message);

    DetailsLoader mLoader;
    BangumiResolver mResolver;
    EpisodePlayer mPlayer;
    ilias::TaskScope mTasks;
    QVariantMap mSubject;
    QVariantList mEpisodes;
    QString mErrorMessage;
    QString mNoticeMessage;
    std::optional<EpisodeId> mFirstPlayableEpisode;
    std::uint64_t mGeneration = 0;
    int mPlayableEpisodeCount = 0;
    bool mLoading = false;
    bool mPlaying = false;
    bool mDestroying = false;
};

} // namespace anime_land
