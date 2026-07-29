#pragma once

#include <QUrl>

#include <ilias/task.hpp>

#include "model/episode_resource/types.hpp"

#include <cstdint>
#include <vector>

namespace anime_land {

class EpisodeProvider {
public:
    virtual ~EpisodeProvider() = default;

    virtual auto key() const -> QString = 0;
    virtual auto name() const -> QString = 0;
    virtual auto icon() const -> QUrl = 0;
    virtual auto generation() const -> std::uint64_t = 0;

    virtual auto ping()
        -> ilias::Task<EpisodeProviderResult<ProviderHealth>> = 0;
    virtual auto search(EpisodeQuery query)
        -> ilias::Task<EpisodeProviderResult<std::vector<OnlinePlayable>>> = 0;
    virtual auto resolve(OnlinePlayable playable)
        -> ilias::Task<EpisodeProviderResult<OnlinePlayable>> = 0;
    virtual void cancel() = 0;
};

} // namespace anime_land
