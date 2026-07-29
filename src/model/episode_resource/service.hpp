#pragma once

#include "model/episode_resource/cache.hpp"
#include "model/episode_resource/registry.hpp"

#include <chrono>
#include <vector>

namespace anime_land {

namespace persistence {
class CatalogStore;
}

class EpisodeResourceService final {
public:
    EpisodeResourceService(EpisodeProviderRegistry &registry,
                           OnlinePlayableCache &cache);
    EpisodeResourceService(EpisodeProviderRegistry &registry,
                           OnlinePlayableCache &cache,
                           persistence::CatalogStore &catalog);

    auto query(EpisodeId episode)
        -> ilias::Task<EpisodeProviderResult<EpisodeQuery>>;
    auto search(QString providerKey, EpisodeQuery query,
                std::chrono::milliseconds ttl = std::chrono::minutes(5))
        -> ilias::Task<
            EpisodeProviderResult<std::vector<CachedOnlinePlayable>>>;
    auto resolve(QString handle)
        -> ilias::Task<EpisodeProviderResult<OnlinePlayable>>;
    void invalidateProvider(QStringView providerKey);

private:
    EpisodeProviderRegistry &mRegistry;
    OnlinePlayableCache &mCache;
    persistence::CatalogStore *mCatalog = nullptr;
};

} // namespace anime_land
