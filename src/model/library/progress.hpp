#pragma once

#include "model/library/error.hpp"
#include "model/library/identity.hpp"

#include <QDateTime>

#include <chrono>
#include <optional>

namespace anime_land {

struct PlaybackProgress {
    EpisodeId episodeId;
    std::optional<SourceItemId> lastSourceItemId;
    std::chrono::milliseconds position {};
    std::optional<std::chrono::milliseconds> duration;
    bool completed = false;
    QDateTime updatedAt;
};

auto validate(const PlaybackProgress &progress) -> LibraryResult<void>;

/** Returns no value when duration is unknown or zero; otherwise clamps to [0, 1]. */
auto playbackFraction(const PlaybackProgress &progress) -> std::optional<double>;

} // namespace anime_land
