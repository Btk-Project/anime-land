#pragma once

#include "model/playback/playback_command.hpp"
#include "model/playback/playback_pipeline.hpp"
#include "model/playback/playback_snapshot.hpp"

#include <ilias/io/error.hpp>
#include <ilias/sync/event.hpp>
#include <ilias/sync/mpsc.hpp>
#include <ilias/task.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

namespace anime_land {

class PlaybackSession final {
public:
    using PipelineFactory =
        std::function<std::unique_ptr<PlaybackPipeline>()>;

    PlaybackSession();
    explicit PlaybackSession(PipelineFactory factory);
    ~PlaybackSession();

    PlaybackSession(const PlaybackSession &) = delete;
    PlaybackSession(PlaybackSession &&) = delete;

    auto run() -> ilias::Task<void>;
    auto close() -> ilias::Task<void>;
    auto send(PlaybackCommand command) -> ilias::IoTask<void>;
    auto snapshot() const -> PlaybackSnapshot;

private:
    struct ShutdownCommand {};
    using QueuedCommand = std::variant<PlaybackCommand, ShutdownCommand>;

    auto handle(PlaybackCommand command) -> ilias::Task<void>;
    auto open(OpenMedia command) -> ilias::Task<void>;
    auto stopCurrent(bool publishIdle) -> ilias::Task<void>;
    auto watchMessages(PlaybackPipeline *pipeline, std::uint64_t generation)
        -> ilias::Task<void>;
    auto applyMessage(std::uint64_t generation,
                      const PlaybackPipelineMessage &message) -> void;
    auto publishFailure(PlaybackErrorCode code,
                        QString message,
                        std::error_code detail = {}) -> void;

    PipelineFactory mFactory;
    ilias::mpsc::Sender<QueuedCommand> mCommandSender;
    ilias::mpsc::Receiver<QueuedCommand> mCommandReceiver;
    ilias::WaitHandle<void> mMessageWatcher;
    std::unique_ptr<PlaybackPipeline> mPipeline;

    mutable std::mutex mSnapshotMutex;
    PlaybackSnapshot mSnapshot;
    PlaybackState mStateBeforeSeek = PlaybackState::Ready;

    std::atomic_bool mRunStarted {false};
    std::atomic_bool mClosing {false};
    std::atomic_bool mClosed {false};
    ilias::Event mClosedEvent;
};

} // namespace anime_land
