#pragma once

#include <QString>
#include <QUrl>

#include <chrono>
#include <cstdint>
#include <optional>

namespace anime_land {

enum class PlaybackState {
    Idle,
    Opening,
    Ready,
    Playing,
    Paused,
    Seeking,
    Buffering,
    Stopping,
    Ended,
    Error,
};

enum class PlaybackErrorCode {
    InvalidSource,
    InvalidState,
    PipelineFailure,
    UnsupportedCommand,
};

struct PlaybackError {
    PlaybackErrorCode code = PlaybackErrorCode::PipelineFailure;
    QString message;
    QString detail;
};

struct PlaybackSnapshot {
    std::uint64_t generation = 0;
    PlaybackState state = PlaybackState::Idle;
    std::optional<QUrl> source;
    std::chrono::nanoseconds position {};
    std::chrono::nanoseconds duration {};
    double volume = 1.0;
    double rate = 1.0;
    int selectedAudioTrack = -1;
    int selectedSubtitleTrack = -1;
    std::optional<PlaybackError> error;
};

} // namespace anime_land
