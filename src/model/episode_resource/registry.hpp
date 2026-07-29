#pragma once

#include "model/episode_resource/provider.hpp"

#include <memory>
#include <mutex>
#include <vector>

namespace anime_land {

class EpisodeProviderRegistry final {
public:
    auto registerProvider(std::shared_ptr<EpisodeProvider> provider)
        -> EpisodeProviderResult<void>;
    auto registerProviders(
        const std::vector<std::shared_ptr<EpisodeProvider>> &providers)
        -> EpisodeProviderResult<void>;
    auto find(QStringView key) const -> std::shared_ptr<EpisodeProvider>;
    auto list() const -> std::vector<std::shared_ptr<EpisodeProvider>>;
    void removePlugin(QStringView pluginId);
    void clear();

private:
    mutable std::mutex mMutex;
    std::vector<std::shared_ptr<EpisodeProvider>> mProviders;
};

} // namespace anime_land
