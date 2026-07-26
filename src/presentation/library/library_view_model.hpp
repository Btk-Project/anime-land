#pragma once

#include "model/library/local_media_import.hpp"

#include <QObject>
#include <QVariantList>

#include <ilias/task.hpp>
#include <ilias/task/scope.hpp>

#include <cstdint>
#include <functional>
#include <vector>

namespace anime_land {

/** QML-facing state for local media import, episode links and playback launch. */
class LibraryViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList mediaItems READ mediaItems NOTIFY mediaChanged)
    Q_PROPERTY(QVariantList subjectGroups READ subjectGroups NOTIFY mediaChanged)
    Q_PROPERTY(QVariantList unassociatedGroups READ unassociatedGroups
                   NOTIFY mediaChanged)
    Q_PROPERTY(QVariantList associationSubjects READ associationSubjects
                   NOTIFY associationChanged)
    Q_PROPERTY(QVariantList associationEpisodes READ associationEpisodes
                   NOTIFY associationChanged)
    Q_PROPERTY(int mediaCount READ mediaCount NOTIFY mediaChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(bool importing READ importing NOTIFY stateChanged)
    Q_PROPERTY(bool removing READ removing NOTIFY stateChanged)
    Q_PROPERTY(bool associating READ associating NOTIFY stateChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY stateChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)
    Q_PROPERTY(QString noticeMessage READ noticeMessage NOTIFY stateChanged)

public:
    using MediaLoader = std::function<
        ilias::Task<LibraryResult<std::vector<MediaEntry>>>()>;
    using LibraryLoader = std::function<
        ilias::Task<LibraryResult<std::vector<LibraryMediaEntry>>>()>;
    using MediaImporter = std::function<
        ilias::Task<LibraryResult<LocalMediaImportResult>>(QList<QUrl>)>;
    using MediaRemover =
        std::function<ilias::Task<LibraryResult<void>>(SourceItemId)>;
    using SubjectSearcher = std::function<
        ilias::Task<LibraryResult<std::vector<BangumiSearchSubject>>>(QString)>;
    using EpisodeLoader = std::function<ilias::Task<
        LibraryResult<std::vector<AssociationEpisodeOption>>>(
        const BangumiSearchSubject &)>;
    using MediaLinker = std::function<
        ilias::Task<LibraryResult<void>>(SourceItemId, EpisodeId)>;
    using MediaPlayer =
        std::function<ilias::Task<LibraryResult<void>>(SourceItemId)>;

    explicit LibraryViewModel(LocalMediaImportService &service,
                              QObject *parent = nullptr);
    explicit LibraryViewModel(LibraryLoader loader,
                              QObject *parent = nullptr);
    LibraryViewModel(MediaLoader loader, MediaImporter importer,
                     MediaRemover remover = {}, QObject *parent = nullptr);
    ~LibraryViewModel() override;

    auto mediaItems() const -> QVariantList { return mMediaItems; }
    auto subjectGroups() const -> QVariantList { return mSubjectGroups; }
    auto unassociatedGroups() const -> QVariantList {
        return mUnassociatedGroups;
    }
    auto associationSubjects() const -> QVariantList {
        return mAssociationSubjects;
    }
    auto associationEpisodes() const -> QVariantList {
        return mAssociationEpisodes;
    }
    auto mediaCount() const noexcept -> int {
        return static_cast<int>(mMediaItems.size());
    }
    auto loading() const noexcept -> bool { return mLoading; }
    auto importing() const noexcept -> bool { return mImporting; }
    auto removing() const noexcept -> bool { return mRemoving; }
    auto associating() const noexcept -> bool { return mAssociating; }
    auto playing() const noexcept -> bool { return mPlaying; }
    auto errorMessage() const -> QString { return mErrorMessage; }
    auto noticeMessage() const -> QString { return mNoticeMessage; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void importFiles(const QList<QUrl> &files);
    Q_INVOKABLE void removeMedia(qlonglong sourceItemId);
    Q_INVOKABLE void searchAssociationSubjects(const QString &query);
    Q_INVOKABLE void selectAssociationSubject(qlonglong bangumiSubjectId);
    Q_INVOKABLE void linkMedia(qlonglong sourceItemId, qlonglong episodeId);
    Q_INVOKABLE void unlinkMedia(qlonglong sourceItemId, qlonglong episodeId);
    Q_INVOKABLE void playMedia(qlonglong sourceItemId);
    Q_INVOKABLE void clearAssociationPicker();

signals:
    void mediaChanged();
    void associationChanged();
    void stateChanged();

private:
    auto isBusy() const noexcept -> bool;
    auto reloadMedia()
        -> ilias::Task<LibraryResult<std::vector<LibraryMediaEntry>>>;
    auto load(std::uint64_t generation) -> ilias::Task<void>;
    auto import(QList<QUrl> files, std::uint64_t generation)
        -> ilias::Task<void>;
    auto remove(SourceItemId item, std::uint64_t generation)
        -> ilias::Task<void>;
    auto search(QString query, std::uint64_t generation) -> ilias::Task<void>;
    auto loadEpisodes(std::int64_t bangumiSubjectId,
                      std::uint64_t generation) -> ilias::Task<void>;
    auto link(SourceItemId item, EpisodeId episode, bool remove,
              std::uint64_t generation) -> ilias::Task<void>;
    auto play(SourceItemId item, std::uint64_t generation)
        -> ilias::Task<void>;
    void applyMedia(const std::vector<LibraryMediaEntry> &entries);

    MediaLoader mLegacyLoader;
    LibraryLoader mLibraryLoader;
    MediaImporter mImporter;
    MediaRemover mRemover;
    SubjectSearcher mSubjectSearcher;
    EpisodeLoader mEpisodeLoader;
    MediaLinker mLinker;
    MediaLinker mUnlinker;
    MediaPlayer mPlayer;
    ilias::TaskScope mTasks;
    std::vector<BangumiSearchSubject> mSubjectResults;
    QVariantList mMediaItems;
    QVariantList mSubjectGroups;
    QVariantList mUnassociatedGroups;
    QVariantList mAssociationSubjects;
    QVariantList mAssociationEpisodes;
    QString mErrorMessage;
    QString mNoticeMessage;
    std::uint64_t mGeneration = 0;
    bool mLoading = false;
    bool mImporting = false;
    bool mRemoving = false;
    bool mAssociating = false;
    bool mPlaying = false;
    bool mDestroying = false;
};

} // namespace anime_land
