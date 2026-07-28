#pragma once

#include <QImage>

#include <chrono>

namespace anime_land {

struct PlaybackVideoFrame {
    QImage image;
    std::chrono::nanoseconds presentationTime {};
};

/** Application-owned video boundary used by the nekoav adapter. */
class PlaybackVideoSink {
public:
    virtual ~PlaybackVideoSink() = default;

    virtual auto activate() -> void = 0;
    virtual auto submit(PlaybackVideoFrame frame) -> void = 0;
    virtual auto deactivate() -> void = 0;
};

} // namespace anime_land
