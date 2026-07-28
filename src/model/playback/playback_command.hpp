#pragma once

#include <QUrl>

#include <chrono>
#include <variant>

namespace anime_land {

struct OpenMedia {
    QUrl url;
};

struct Play {};
struct Pause {};
struct Stop {};

struct Seek {
    std::chrono::nanoseconds position {};
};

struct SetVolume {
    double volume = 1.0;
};

struct SetRate {
    double rate = 1.0;
};

struct SelectAudioTrack {
    int index = -1;
};

struct SelectSubtitleTrack {
    int index = -1;
};

using PlaybackCommand = std::variant<OpenMedia,
                                     Play,
                                     Pause,
                                     Stop,
                                     Seek,
                                     SetVolume,
                                     SetRate,
                                     SelectAudioTrack,
                                     SelectSubtitleTrack>;

} // namespace anime_land
