#pragma once

#include "model/library/media.hpp"
#include "model/persistence/database.hpp"

#include <memory>
#include <optional>
#include <vector>

namespace anime_land::persistence {

/** Transactional persistence boundary for media resources and episode links. */
class LibraryStore {
public:
    static auto open(LocalDatabase &database) -> ilias::IoTask<LibraryStore>;

    ~LibraryStore();
    LibraryStore(const LibraryStore &) = delete;
    auto operator=(const LibraryStore &) -> LibraryStore & = delete;
    LibraryStore(LibraryStore &&) noexcept;
    auto operator=(LibraryStore &&) noexcept -> LibraryStore & = delete;

    /** Atomically upserts all explicitly discovered resources and items. */
    auto upsertDiscoveredMedia(std::vector<MediaDiscovery> discoveries)
        -> ilias::IoTask<std::vector<StoredMediaDiscovery>>;

    auto findResource(QStringView providerKey, QStringView stableKey)
        -> ilias::IoTask<std::optional<MediaResource>>;
    auto listResources() -> ilias::IoTask<std::vector<MediaResource>>;
    auto listSourceItems(MediaResourceId resource,
                         bool includeUnavailable = false)
        -> ilias::IoTask<std::vector<SourceItem>>;
    auto listMediaEntries(bool includeUnavailable = false)
        -> ilias::IoTask<std::vector<MediaEntry>>;

    /** Removes one playable item and prunes its resource when it becomes empty. */
    auto removeSourceItem(SourceItemId item) -> ilias::IoTask<bool>;

    /** Upserts one episode/item relation without downgrading a manual link. */
    auto upsertEpisodeMediaLink(EpisodeMediaLink link)
        -> ilias::IoTask<EpisodeMediaLink>;
    auto listEpisodeMediaLinks(EpisodeId episode)
        -> ilias::IoTask<std::vector<EpisodeMediaLink>>;
    auto listSourceItemMediaLinks(SourceItemId item)
        -> ilias::IoTask<std::vector<EpisodeMediaLink>>;
    auto removeEpisodeMediaLink(EpisodeId episode, SourceItemId item)
        -> ilias::IoTask<bool>;

private:
    struct State;
    LibraryStore(LocalDatabase &database, std::unique_ptr<State> state);

    LocalDatabase &mDatabase;
    std::unique_ptr<State> mState;
};

} // namespace anime_land::persistence
