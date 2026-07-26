#include "pch.hpp"

#include "model/library/progress.hpp"

#include <algorithm>

namespace anime_land {

auto validate(const PlaybackProgress &progress) -> LibraryResult<void> {
    if (!isValid(progress.episodeId) || (progress.lastSourceItemId && !isValid(*progress.lastSourceItemId))) {
        return ilias::Err(libraryError(LibraryErrorCode::InvalidIdentity, QStringLiteral("播放进度包含无效 ID")));
    }
    if (progress.position < std::chrono::milliseconds::zero()) {
        return ilias::Err(libraryError(LibraryErrorCode::InvalidPosition, QStringLiteral("播放位置不能为负数")));
    }
    if (progress.duration && *progress.duration < std::chrono::milliseconds::zero()) {
        return ilias::Err(libraryError(LibraryErrorCode::InvalidDuration, QStringLiteral("播放时长不能为负数")));
    }
    if (!progress.updatedAt.isValid()) {
        return ilias::Err(libraryError(LibraryErrorCode::InvalidTimestamp, QStringLiteral("播放进度更新时间无效")));
    }
    return {};
}

auto playbackFraction(const PlaybackProgress &progress) -> std::optional<double> {
    if (!progress.duration || *progress.duration <= std::chrono::milliseconds::zero()) {
        return std::nullopt;
    }
    const auto fraction = static_cast<double>(progress.position.count()) / static_cast<double>(progress.duration->count());
    return std::clamp(fraction, 0.0, 1.0);
}

} // namespace anime_land
