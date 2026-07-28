#include "model/playback/playback_session.hpp"

#include <ilias/io/error.hpp>

#include <algorithm>
#include <cassert>
#include <system_error>
#include <type_traits>
#include <utility>

namespace anime_land {
namespace {

constexpr std::size_t kPlaybackCommandCapacity = 32;

auto canceledError() -> std::error_code {
    return ilias::make_error_code(ilias::IoError::Canceled);
}

} // namespace

PlaybackSession::PlaybackSession()
    : PlaybackSession([] { return makeNekoavPlaybackPipeline(); }) {}

PlaybackSession::PlaybackSession(PipelineFactory factory)
    : mFactory(std::move(factory)) {
    auto channel = ilias::mpsc::channel<QueuedCommand>(
        kPlaybackCommandCapacity);
    mCommandSender = std::move(channel.sender);
    mCommandReceiver = std::move(channel.receiver);
}

PlaybackSession::~PlaybackSession() {
    assert((!mRunStarted.load() || mClosed.load())
           && "PlaybackSession::close() must complete before destruction");
    assert(!mPipeline
           && "PlaybackSession must release its pipeline before destruction");
}

auto PlaybackSession::run() -> ilias::Task<void> {
    if (mRunStarted.exchange(true)) {
        co_return;
    }

    std::optional<QueuedCommand> pending;
    while (true) {
        std::optional<QueuedCommand> queued;
        if (pending) {
            queued = std::exchange(pending, std::nullopt);
        }
        else {
            queued = co_await mCommandReceiver.recv();
        }
        if (!queued || std::holds_alternative<ShutdownCommand>(*queued)) {
            break;
        }

        auto command = std::get<PlaybackCommand>(std::move(*queued));
        if (std::holds_alternative<Seek>(command)) {
            while (auto next = mCommandReceiver.tryRecv()) {
                if (std::holds_alternative<PlaybackCommand>(*next)
                    && std::holds_alternative<Seek>(
                        std::get<PlaybackCommand>(*next))) {
                    command = std::get<PlaybackCommand>(std::move(*next));
                    continue;
                }
                pending = std::move(*next);
                break;
            }
        }
        co_await handle(std::move(command));
    }

    co_await stopCurrent(false);
    mClosed.store(true);
    mClosedEvent.set();
}

auto PlaybackSession::close() -> ilias::Task<void> {
    if (!mRunStarted.load()) {
        mClosing.store(true);
        mClosed.store(true);
        mClosedEvent.set();
        co_return;
    }

    if (!mClosing.exchange(true)) {
        auto sent = co_await mCommandSender.send(QueuedCommand {
            ShutdownCommand {},
        });
        if (!sent && !mClosed.load()) {
            mClosed.store(true);
            mClosedEvent.set();
        }
    }
    co_await mClosedEvent;
}

auto PlaybackSession::send(PlaybackCommand command) -> ilias::IoTask<void> {
    if (mClosing.load() || mClosed.load()) {
        co_return ilias::Err(canceledError());
    }
    auto sent = co_await mCommandSender.send(
        QueuedCommand {std::move(command)});
    if (!sent) {
        co_return ilias::Err(canceledError());
    }
    co_return {};
}

auto PlaybackSession::snapshot() const -> PlaybackSnapshot {
    const auto lock = std::lock_guard {mSnapshotMutex};
    return mSnapshot;
}

auto PlaybackSession::handle(PlaybackCommand command) -> ilias::Task<void> {
    co_await std::visit(
        [this](auto value) -> ilias::Task<void> {
            using Command = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Command, OpenMedia>) {
                co_await open(std::move(value));
            }
            else if constexpr (std::same_as<Command, Stop>) {
                co_await stopCurrent(true);
            }
            else if constexpr (std::same_as<Command, Play>) {
                if (!mPipeline) {
                    publishFailure(PlaybackErrorCode::InvalidState,
                                   QStringLiteral("没有已打开的媒体"));
                    co_return;
                }
                auto result = co_await mPipeline->play();
                if (!result) {
                    publishFailure(PlaybackErrorCode::PipelineFailure,
                                   QStringLiteral("开始播放失败"),
                                   result.error());
                    co_return;
                }
                const auto lock = std::lock_guard {mSnapshotMutex};
                mSnapshot.state = PlaybackState::Playing;
                mSnapshot.error.reset();
            }
            else if constexpr (std::same_as<Command, Pause>) {
                if (!mPipeline) {
                    publishFailure(PlaybackErrorCode::InvalidState,
                                   QStringLiteral("没有可暂停的媒体"));
                    co_return;
                }
                auto result = co_await mPipeline->pause();
                if (!result) {
                    publishFailure(PlaybackErrorCode::PipelineFailure,
                                   QStringLiteral("暂停播放失败"),
                                   result.error());
                    co_return;
                }
                const auto lock = std::lock_guard {mSnapshotMutex};
                mSnapshot.state = PlaybackState::Paused;
                mSnapshot.error.reset();
            }
            else if constexpr (std::same_as<Command, Seek>) {
                if (!mPipeline || value.position < std::chrono::nanoseconds {}) {
                    publishFailure(PlaybackErrorCode::InvalidState,
                                   QStringLiteral("当前媒体不能跳转到该位置"));
                    co_return;
                }
                {
                    const auto lock = std::lock_guard {mSnapshotMutex};
                    mStateBeforeSeek =
                        mSnapshot.state == PlaybackState::Playing
                            ? PlaybackState::Playing
                            : PlaybackState::Paused;
                    mSnapshot.state = PlaybackState::Seeking;
                    mSnapshot.position =
                        mSnapshot.duration > std::chrono::nanoseconds {}
                            ? std::min(value.position, mSnapshot.duration)
                            : value.position;
                    mSnapshot.error.reset();
                }
                auto result = co_await mPipeline->seek(value.position);
                if (!result) {
                    publishFailure(PlaybackErrorCode::PipelineFailure,
                                   QStringLiteral("跳转播放位置失败"),
                                   result.error());
                }
            }
            else if constexpr (std::same_as<Command, SetVolume>) {
                const auto lock = std::lock_guard {mSnapshotMutex};
                mSnapshot.volume = std::clamp(value.volume, 0.0, 1.0);
            }
            else if constexpr (std::same_as<Command, SetRate>) {
                const auto lock = std::lock_guard {mSnapshotMutex};
                mSnapshot.rate = std::clamp(value.rate, 0.25, 4.0);
            }
            else if constexpr (std::same_as<Command, SelectAudioTrack>) {
                const auto lock = std::lock_guard {mSnapshotMutex};
                mSnapshot.selectedAudioTrack = value.index;
            }
            else if constexpr (std::same_as<Command,
                                            SelectSubtitleTrack>) {
                const auto lock = std::lock_guard {mSnapshotMutex};
                mSnapshot.selectedSubtitleTrack = value.index;
            }
            co_return;
        },
        std::move(command));
}

auto PlaybackSession::open(OpenMedia command) -> ilias::Task<void> {
    if (!command.url.isValid() || command.url.isEmpty()) {
        publishFailure(PlaybackErrorCode::InvalidSource,
                       QStringLiteral("媒体地址无效"));
        co_return;
    }

    co_await stopCurrent(false);
    {
        const auto lock = std::lock_guard {mSnapshotMutex};
        ++mSnapshot.generation;
        mSnapshot.state = PlaybackState::Opening;
        mSnapshot.source = command.url;
        mSnapshot.position = {};
        mSnapshot.duration = {};
        mSnapshot.error.reset();
    }

    mPipeline = mFactory ? mFactory() : nullptr;
    if (!mPipeline) {
        publishFailure(PlaybackErrorCode::PipelineFailure,
                       QStringLiteral("无法创建播放管线"));
        co_return;
    }

    const auto generation = snapshot().generation;
    mMessageWatcher = ilias::spawn(
        watchMessages(mPipeline.get(), generation));
    auto opened = co_await mPipeline->open(command.url);
    if (!opened) {
        mMessageWatcher.stop();
        co_await std::exchange(mMessageWatcher, {});
        auto cleanup = co_await mPipeline->shutdown();
        (void) cleanup;
        mPipeline.reset();
        publishFailure(PlaybackErrorCode::PipelineFailure,
                       QStringLiteral("打开媒体失败"),
                       opened.error());
        co_return;
    }

    const auto lock = std::lock_guard {mSnapshotMutex};
    if (mSnapshot.generation == generation
        && mSnapshot.state == PlaybackState::Opening) {
        mSnapshot.state = PlaybackState::Ready;
    }
}

auto PlaybackSession::stopCurrent(bool publishIdle) -> ilias::Task<void> {
    if (!mPipeline) {
        const auto lock = std::lock_guard {mSnapshotMutex};
        const auto generation =
            mSnapshot.generation + static_cast<std::uint64_t>(publishIdle);
        mSnapshot = PlaybackSnapshot {.generation = generation};
        co_return;
    }

    {
        const auto lock = std::lock_guard {mSnapshotMutex};
        mSnapshot.state = PlaybackState::Stopping;
        ++mSnapshot.generation;
    }
    if (mMessageWatcher) {
        mMessageWatcher.stop();
        co_await std::exchange(mMessageWatcher, {});
    }
    auto stopped = co_await mPipeline->shutdown();
    mPipeline.reset();

    const auto lock = std::lock_guard {mSnapshotMutex};
    const auto generation = mSnapshot.generation;
    if (!stopped) {
        mSnapshot.state = PlaybackState::Error;
        mSnapshot.error = PlaybackError {
            .code = PlaybackErrorCode::PipelineFailure,
            .message = QStringLiteral("停止播放管线失败"),
            .detail = QString::fromStdString(stopped.error().message()),
        };
        co_return;
    }
    mSnapshot = PlaybackSnapshot {.generation = generation};
    if (!publishIdle) {
        mSnapshot.state = PlaybackState::Idle;
    }
}

auto PlaybackSession::watchMessages(PlaybackPipeline *pipeline,
                                    std::uint64_t generation)
    -> ilias::Task<void> {
    while (true) {
        auto message = co_await pipeline->readMessage();
        applyMessage(generation, message);
    }
}

auto PlaybackSession::applyMessage(
    std::uint64_t generation,
    const PlaybackPipelineMessage &message) -> void {
    const auto lock = std::lock_guard {mSnapshotMutex};
    if (generation != mSnapshot.generation) {
        return;
    }

    std::visit(
        [this](const auto &value) {
            using Message = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Message,
                                       PlaybackPipelineClockUpdate>) {
                mSnapshot.position = std::max(
                    value.position, std::chrono::nanoseconds {});
            }
            else if constexpr (std::same_as<Message,
                                                PlaybackPipelineMediaLoaded>) {
                mSnapshot.position = std::max(
                    value.startTime, std::chrono::nanoseconds {});
                mSnapshot.duration = std::max(
                    value.duration, std::chrono::nanoseconds {});
                if (mSnapshot.state == PlaybackState::Opening) {
                    mSnapshot.state = PlaybackState::Ready;
                }
            }
            else if constexpr (std::same_as<Message,
                                                PlaybackPipelineSeekBegin>) {
                if (mSnapshot.state != PlaybackState::Seeking) {
                    mStateBeforeSeek =
                        mSnapshot.state == PlaybackState::Playing
                            ? PlaybackState::Playing
                            : PlaybackState::Paused;
                    mSnapshot.state = PlaybackState::Seeking;
                }
            }
            else if constexpr (std::same_as<Message,
                                                PlaybackPipelineSeekEnd>) {
                if (mSnapshot.state == PlaybackState::Seeking) {
                    mSnapshot.state = mStateBeforeSeek;
                }
            }
            else if constexpr (std::same_as<Message,
                                                PlaybackPipelineEndOfStream>) {
                mSnapshot.state = PlaybackState::Ended;
                mSnapshot.position = mSnapshot.duration;
            }
            else {
                mSnapshot.state = PlaybackState::Error;
                mSnapshot.error = PlaybackError {
                    .code = PlaybackErrorCode::PipelineFailure,
                    .message = QStringLiteral("播放管线发生错误"),
                    .detail = QString::fromStdString(
                        value.message.empty() ? value.error.message()
                                              : value.message),
                };
            }
        },
        message);
}

auto PlaybackSession::publishFailure(PlaybackErrorCode code,
                                     QString message,
                                     std::error_code detail) -> void {
    const auto lock = std::lock_guard {mSnapshotMutex};
    mSnapshot.state = PlaybackState::Error;
    mSnapshot.error = PlaybackError {
        .code = code,
        .message = std::move(message),
        .detail = detail ? QString::fromStdString(detail.message()) : QString {},
    };
}

} // namespace anime_land
