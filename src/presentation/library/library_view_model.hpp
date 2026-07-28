#pragma once

#include "model/library/local_metadata.hpp"
#include "model/library/local_media_import.hpp"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include <ilias/task.hpp>
#include <ilias/task/scope.hpp>

#include <cstdint>
#include <functional>
#include <optional>
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
    Q_PROPERTY(int associationEpisodeTotal READ associationEpisodeTotal
                   NOTIFY associationChanged)
    Q_PROPERTY(int associationEpisodePage READ associationEpisodePage
                   NOTIFY associationChanged)
    Q_PROPERTY(int associationEpisodePageCount READ associationEpisodePageCount
                   NOTIFY associationChanged)
    Q_PROPERTY(bool associationEpisodeDescending READ
                   associationEpisodeDescending NOTIFY associationChanged)
    Q_PROPERTY(int associationEpisodeFocusIndex READ
                   associationEpisodeFocusIndex NOTIFY associationChanged)
    Q_PROPERTY(int mediaCount READ mediaCount NOTIFY mediaChanged)
    Q_PROPERTY(QVariantMap localMetadataEditor READ localMetadataEditor
                   NOTIFY localMetadataEditorChanged)
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
    using EpisodeLoader = std::function<
        ilias::Task<LibraryResult<AssociationEpisodePage>>(
            const BangumiSearchSubject &, int, int)>;
    using MediaLinker = std::function<
        ilias::Task<LibraryResult<void>>(SourceItemId, EpisodeId)>;
    using MediaPlayer =
        std::function<ilias::Task<LibraryResult<void>>(SourceItemId)>;
    using CustomMetadataCreator = std::function<ilias::Task<
        LibraryResult<SubjectId>>(SourceItemId, LocalSubjectMetadata)>;
    using LocalMetadataLoader = std::function<ilias::Task<
        LibraryResult<std::optional<StoredLocalSubjectMetadata>>>(SubjectId)>;
    using LocalMetadataUpdater = std::function<
        ilias::Task<LibraryResult<void>>(SubjectId, LocalSubjectMetadata)>;
    using LocalMetadataRemover =
        std::function<ilias::Task<LibraryResult<void>>(SubjectId)>;

    explicit LibraryViewModel(LocalMediaImportService &service,
                              QObject *parent = nullptr);
    LibraryViewModel(LocalMediaImportService &service,
                     LocalMetadataService &metadataService,
                     QObject *parent = nullptr);
    explicit LibraryViewModel(LibraryLoader loader,
                              QObject *parent = nullptr);
    LibraryViewModel(SubjectSearcher subjectSearcher,
                     EpisodeLoader episodeLoader,
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
    auto associationEpisodeTotal() const noexcept -> int {
        return mAssociationEpisodeTotal;
    }
    auto associationEpisodePage() const noexcept -> int {
        return mAssociationEpisodePage;
    }
    auto associationEpisodePageCount() const noexcept -> int {
        return mAssociationEpisodeTotal == 0
                   ? 0
                   : (mAssociationEpisodeTotal + kAssociationEpisodePageSize
                      - 1)
                         / kAssociationEpisodePageSize;
    }
    auto associationEpisodeDescending() const noexcept -> bool {
        return mAssociationEpisodeDescending;
    }
    auto associationEpisodeFocusIndex() const noexcept -> int {
        return mAssociationEpisodeFocusIndex;
    }
    auto mediaCount() const noexcept -> int {
        return static_cast<int>(mMediaItems.size());
    }
    auto localMetadataEditor() const -> QVariantMap {
        return mLocalMetadataEditor;
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
    Q_INVOKABLE void goToAssociationEpisodePage(int page);
    Q_INVOKABLE void previousAssociationEpisodePage();
    Q_INVOKABLE void nextAssociationEpisodePage();
    Q_INVOKABLE void setAssociationEpisodeDescending(bool descending);
    Q_INVOKABLE void locateAssociationEpisode(const QString &number);
    Q_INVOKABLE void linkMedia(qlonglong sourceItemId, qlonglong episodeId);
    Q_INVOKABLE void unlinkMedia(qlonglong sourceItemId, qlonglong episodeId);
    Q_INVOKABLE void playMedia(qlonglong sourceItemId);
    Q_INVOKABLE void createCustomMetadata(
        qlonglong sourceItemId, const QString &displayTitle,
        const QString &originalTitle, const QString &summary,
        const QString &coverUrl, const QString &episodeTitle,
        const QString &episodeNumber);
    Q_INVOKABLE void loadLocalMetadata(qlonglong subjectId);
    Q_INVOKABLE void updateLocalMetadata(
        qlonglong subjectId, const QString &displayTitle,
        const QString &originalTitle, const QString &summary,
        const QString &coverUrl);
    Q_INVOKABLE void deleteLocalMetadata(qlonglong subjectId);
    Q_INVOKABLE void clearLocalMetadataEditor();
    Q_INVOKABLE void clearAssociationPicker();

signals:
    void mediaChanged();
    void associationChanged();
    void localMetadataEditorChanged();
    void localMetadataSaved(qlonglong subjectId);
    void localMetadataDeleted(qlonglong subjectId);
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
    auto loadEpisodes(std::int64_t bangumiSubjectId, int page,
                      bool descending,
                      std::optional<double> focusEpisodeNumber,
                      std::uint64_t generation) -> ilias::Task<void>;
    void requestAssociationEpisodePage(
        int page, bool descending,
        std::optional<double> focusEpisodeNumber = std::nullopt);
    auto link(SourceItemId item, EpisodeId episode, bool remove,
              std::uint64_t generation) -> ilias::Task<void>;
    auto play(SourceItemId item, std::uint64_t generation)
        -> ilias::Task<void>;
    auto createMetadata(SourceItemId item, LocalSubjectMetadata metadata,
                        std::uint64_t generation) -> ilias::Task<void>;
    auto loadLocalMetadataEditor(SubjectId subject,
                                 std::uint64_t generation)
        -> ilias::Task<void>;
    auto updateMetadata(SubjectId subject, LocalSubjectMetadata metadata,
                        std::uint64_t generation) -> ilias::Task<void>;
    auto removeLocalMetadata(SubjectId subject, std::uint64_t generation)
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
    CustomMetadataCreator mCustomMetadataCreator;
    LocalMetadataLoader mLocalMetadataLoader;
    LocalMetadataUpdater mLocalMetadataUpdater;
    LocalMetadataRemover mLocalMetadataRemover;
    ilias::TaskScope mTasks;
    std::vector<BangumiSearchSubject> mSubjectResults;
    QVariantList mMediaItems;
    QVariantList mSubjectGroups;
    QVariantList mUnassociatedGroups;
    QVariantList mAssociationSubjects;
    QVariantList mAssociationEpisodes;
    QVariantMap mLocalMetadataEditor;
    static constexpr int kAssociationEpisodePageSize = 24;
    std::int64_t mSelectedAssociationSubjectId = 0;
    int mAssociationEpisodeTotal = 0;
    int mAssociationEpisodePage = 0;
    int mAssociationEpisodeFocusIndex = -1;
    bool mAssociationEpisodeDescending = false;
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
