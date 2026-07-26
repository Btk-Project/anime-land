#pragma once

#include "model/library/error.hpp"
#include "model/library/media.hpp"
#include "model/bangumi/episode.hpp"
#include "model/bangumi/search.hpp"
#include "model/bangumi/subject.hpp"
#include "model/persistence/catalog_store.hpp"

#include <QUrl>
#include <QList>

#include <ilias/task.hpp>

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

namespace anime_land::persistence {
class LibraryStore;
}

namespace anime_land {

class BangumiModule;

struct LocalMediaImportResult {
    std::vector<StoredMediaDiscovery> resources;
    std::size_t persistedFileCount = 0;
    std::size_t duplicateSelectionCount = 0;
};

struct MediaAssociationSummary {
    EpisodeId episodeId;
    SubjectId subjectId;
    QString subjectTitle;
    QString episodeTitle;
    std::optional<double> episodeNumber;
    int episodeType = 0;
    int sortOrder = 0;
};

struct LibraryMediaEntry {
    MediaEntry media;
    std::vector<MediaAssociationSummary> associations;
};

struct AssociationEpisodeOption {
    EpisodeId id;
    QString title;
    QString displayNumber;
    int episodeType = 0;
};

struct EpisodeLibraryEntry {
    persistence::EpisodeDetails episode;
    std::vector<MediaEntry> media;
};

struct SubjectLibraryDetails {
    persistence::SubjectDetails subject;
    std::vector<EpisodeLibraryEntry> episodes;
};

auto resolveLocalMediaUrl(const MediaEntry &entry) -> LibraryResult<QUrl>;

/** Model use case for explicitly selected local files. */
class LocalMediaImportService final {
public:
    using MediaLauncher = std::function<bool(const QUrl &)>;
    using SubjectLookup = std::function<ilias::Task<
        BangumiResult<BangumiSubjectSearchResponse>>(
        BangumiSubjectSearchQuery)>;
    using SubjectDetailsLookup = std::function<ilias::Task<
        BangumiResult<BangumiSubjectDetailsResponse>>(std::int64_t)>;
    using EpisodeLookup = std::function<ilias::Task<
        BangumiResult<BangumiEpisodeResponse>>(std::int64_t, int, int)>;

    explicit LocalMediaImportService(persistence::LibraryStore &store)
        : mStore(store) {}
    LocalMediaImportService(persistence::LibraryStore &store,
                            persistence::CatalogStore &catalog,
                            BangumiModule &bangumi,
                            MediaLauncher launcher = {});
    LocalMediaImportService(persistence::LibraryStore &store,
                            persistence::CatalogStore &catalog,
                            SubjectLookup subjects, EpisodeLookup episodes,
                            MediaLauncher launcher = {});
    LocalMediaImportService(persistence::LibraryStore &store,
                            persistence::CatalogStore &catalog,
                            SubjectLookup subjects,
                            SubjectDetailsLookup subjectDetails,
                            EpisodeLookup episodes,
                            MediaLauncher launcher = {});

    auto importFiles(QList<QUrl> files)
        -> ilias::Task<LibraryResult<LocalMediaImportResult>>;
    auto listMedia()
        -> ilias::Task<LibraryResult<std::vector<MediaEntry>>>;
    auto listLibraryMedia()
        -> ilias::Task<LibraryResult<std::vector<LibraryMediaEntry>>>;
    auto removeMedia(SourceItemId item)
        -> ilias::Task<LibraryResult<void>>;
    auto searchAssociationSubjects(QString query)
        -> ilias::Task<LibraryResult<std::vector<BangumiSearchSubject>>>;
    auto loadAssociationEpisodes(const BangumiSearchSubject &subject)
        -> ilias::Task<LibraryResult<std::vector<AssociationEpisodeOption>>>;
    auto linkMedia(SourceItemId item, EpisodeId episode)
        -> ilias::Task<LibraryResult<void>>;
    auto unlinkMedia(SourceItemId item, EpisodeId episode)
        -> ilias::Task<LibraryResult<void>>;
    auto playMedia(SourceItemId item) -> ilias::Task<LibraryResult<void>>;
    auto findBangumiSubject(std::int64_t bangumiSubjectId)
        -> ilias::Task<LibraryResult<std::optional<SubjectId>>>;
    auto ensureBangumiSubject(std::int64_t bangumiSubjectId)
        -> ilias::Task<LibraryResult<SubjectId>>;
    auto getSubjectLibraryDetails(SubjectId subject)
        -> ilias::Task<LibraryResult<std::optional<SubjectLibraryDetails>>>;
    auto playEpisode(EpisodeId episode)
        -> ilias::Task<LibraryResult<void>>;

private:
    persistence::LibraryStore &mStore;
    persistence::CatalogStore *mCatalog = nullptr;
    SubjectLookup mSubjectLookup;
    SubjectDetailsLookup mSubjectDetailsLookup;
    EpisodeLookup mEpisodeLookup;
    MediaLauncher mLauncher;
};

} // namespace anime_land
