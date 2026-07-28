#pragma once

#include <QUrl>

#include <ilias/io/error.hpp>
#include <ilias/task.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <system_error>
#include <variant>

namespace anime_land {

class PlaybackVideoSink;

struct PlaybackPipelineClockUpdate {
    std::chrono::nanoseconds position {};
};

struct PlaybackPipelineMediaLoaded {
    std::chrono::nanoseconds startTime {};
    std::chrono::nanoseconds duration {};
};

struct PlaybackPipelineSeekBegin {};
struct PlaybackPipelineSeekEnd {};
struct PlaybackPipelineEndOfStream {};

struct PlaybackPipelineError {
    std::error_code error;
    std::string message;
};

using PlaybackPipelineMessage = std::variant<PlaybackPipelineClockUpdate,
                                             PlaybackPipelineMediaLoaded,
                                             PlaybackPipelineSeekBegin,
                                             PlaybackPipelineSeekEnd,
                                             PlaybackPipelineEndOfStream,
                                             PlaybackPipelineError>;

// Test seam and renderer boundary. No nekoav type crosses this interface.
class PlaybackPipeline {
public:
    virtual ~PlaybackPipeline() = default;

    virtual auto open(const QUrl &source) -> ilias::IoTask<void> = 0;
    virtual auto play() -> ilias::IoTask<void> = 0;
    virtual auto pause() -> ilias::IoTask<void> = 0;
    virtual auto seek(std::chrono::nanoseconds position)
        -> ilias::IoTask<void> = 0;
    virtual auto shutdown() -> ilias::IoTask<void> = 0;
    virtual auto readMessage() -> ilias::Task<PlaybackPipelineMessage> = 0;
};

auto makeNekoavPlaybackPipeline()
    -> std::unique_ptr<PlaybackPipeline>;
auto makeNekoavPlaybackPipeline(
    std::shared_ptr<PlaybackVideoSink> videoSink)
    -> std::unique_ptr<PlaybackPipeline>;

} // namespace anime_land
