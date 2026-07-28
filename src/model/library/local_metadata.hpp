#pragma once

#include "model/library/error.hpp"
#include "model/library/identity.hpp"

#include <QString>
#include <QUrl>

#include <ilias/task.hpp>

#include <optional>

namespace anime_land::persistence {
class CatalogStore;
class LibraryStore;
}

namespace anime_land {

/** User-authored metadata persisted only in the local catalog database. */
struct LocalSubjectMetadata {
    QString displayTitle;
    QString originalTitle;
    QString summary;
    std::optional<QUrl> coverUrl;
    QString episodeTitle;
    std::optional<double> episodeNumber;
};

struct StoredLocalSubjectMetadata {
    SubjectId subjectId;
    int subjectType = 2;
    LocalSubjectMetadata metadata;
};

/** Creates a standalone local subject and links one selected media item. */
class LocalMetadataService final {
public:
    LocalMetadataService(persistence::CatalogStore &catalog,
                         persistence::LibraryStore &library)
        : mCatalog(catalog), mLibrary(library) {}

    auto createAndLink(SourceItemId item, LocalSubjectMetadata metadata)
        -> ilias::Task<LibraryResult<SubjectId>>;
    auto load(SubjectId subject)
        -> ilias::Task<LibraryResult<std::optional<StoredLocalSubjectMetadata>>>;
    auto update(SubjectId subject, LocalSubjectMetadata metadata)
        -> ilias::Task<LibraryResult<void>>;
    auto remove(SubjectId subject) -> ilias::Task<LibraryResult<void>>;

private:
    persistence::CatalogStore &mCatalog;
    persistence::LibraryStore &mLibrary;
};

} // namespace anime_land
