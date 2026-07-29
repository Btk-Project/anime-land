#include "presentation/playback/playback_controller.hpp"

#include <QFileInfo>

#include <algorithm>
#include <chrono>
#include <utility>

namespace anime_land {
namespace {

auto stateName(PlaybackState state) -> QString {
    switch (state) {
        case PlaybackState::Idle:
            return QStringLiteral("idle");
        case PlaybackState::Opening:
            return QStringLiteral("opening");
        case PlaybackState::Ready:
            return QStringLiteral("ready");
        case PlaybackState::Playing:
            return QStringLiteral("playing");
        case PlaybackState::Paused:
            return QStringLiteral("paused");
        case PlaybackState::Seeking:
            return QStringLiteral("seeking");
        case PlaybackState::Buffering:
            return QStringLiteral("buffering");
        case PlaybackState::Stopping:
            return QStringLiteral("stopping");
        case PlaybackState::Ended:
            return QStringLiteral("ended");
        case PlaybackState::Error:
            return QStringLiteral("error");
    }
    return QStringLiteral("error");
}

auto sameSnapshot(const PlaybackSnapshot &left,
                  const PlaybackSnapshot &right) -> bool {
    return left.generation == right.generation && left.state == right.state
           && left.source == right.source && left.position == right.position
           && left.duration == right.duration && left.volume == right.volume
           && left.rate == right.rate
           && left.selectedAudioTrack == right.selectedAudioTrack
           && left.selectedSubtitleTrack == right.selectedSubtitleTrack
           && left.error.has_value() == right.error.has_value()
           && (!left.error
               || (left.error->code == right.error->code
                   && left.error->message == right.error->message
                   && left.error->detail == right.error->detail));
}

} // namespace

PlaybackController::PlaybackController(PlaybackSession &session,
                                       QObject *parent)
    : QObject(parent), mSession(session), mSnapshot(session.snapshot()),
      mRunner(ilias::spawn(session.run())) {
    mRefreshTimer.setInterval(50);
    mRefreshTimer.setTimerType(Qt::PreciseTimer);
    connect(&mRefreshTimer, &QTimer::timeout,
            this, &PlaybackController::refreshSnapshot);
    mRefreshTimer.start();
}

PlaybackController::~PlaybackController() {
    if (!mClosed) {
        shutdown().wait();
    }
}

auto PlaybackController::stateName() const -> QString {
    return anime_land::stateName(mSnapshot.state);
}

auto PlaybackController::playing() const -> bool {
    return mSnapshot.state == PlaybackState::Playing;
}

auto PlaybackController::busy() const -> bool {
    return mSnapshot.state == PlaybackState::Opening
           || mSnapshot.state == PlaybackState::Seeking
           || mSnapshot.state == PlaybackState::Stopping
           || mSnapshot.state == PlaybackState::Buffering;
}

auto PlaybackController::positionMs() const -> qint64 {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               mSnapshot.position)
        .count();
}

auto PlaybackController::durationMs() const -> qint64 {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               mSnapshot.duration)
        .count();
}

auto PlaybackController::errorMessage() const -> QString {
    if (!mSnapshot.error) {
        return {};
    }
    return mSnapshot.error->detail.isEmpty()
               ? mSnapshot.error->message
               : QStringLiteral("%1：%2")
                     .arg(mSnapshot.error->message,
                          mSnapshot.error->detail);
}

auto PlaybackController::mediaTitle() const -> QString {
    return mMediaTitle;
}

auto PlaybackController::openMedia(const QUrl &source) -> bool {
    const QString title = source.isLocalFile()
                              ? QFileInfo(source.toLocalFile()).completeBaseName()
                              : source.fileName();
    return openMedia(source, title.isEmpty() ? QStringLiteral("媒体") : title);
}

auto PlaybackController::openMedia(const QUrl &source, QString displayTitle)
    -> bool {
    if (mClosed || !source.isValid() || source.isEmpty()) {
        return false;
    }
    displayTitle = displayTitle.trimmed();
    mMediaTitle = displayTitle.isEmpty() ? QStringLiteral("媒体")
                                         : std::move(displayTitle);
    emit mediaChanged();
    emit openRequested(mMediaTitle);
    mTasks.spawn(openAndPlay(source));
    return true;
}

auto PlaybackController::shutdown() -> ilias::Task<void> {
    if (mClosed) {
        co_return;
    }
    mRefreshTimer.stop();
    co_await mTasks.shutdown();
    co_await mSession.close();
    if (mRunner) {
        co_await std::exchange(mRunner, {});
    }
    mClosed = true;
    refreshSnapshot();
}

void PlaybackController::play() {
    if (!mClosed) {
        mTasks.spawn(send(Play {}));
    }
}

void PlaybackController::pause() {
    if (!mClosed) {
        mTasks.spawn(send(Pause {}));
    }
}

void PlaybackController::togglePlayback() {
    if (playing()) {
        pause();
    }
    else if (mSnapshot.state == PlaybackState::Ended) {
        mTasks.spawn([this]() -> ilias::Task<void> {
            auto seekResult = co_await mSession.send(Seek {
                .position = std::chrono::nanoseconds {},
            });
            if (seekResult) {
                auto playResult = co_await mSession.send(Play {});
                (void) playResult;
            }
        }());
    }
    else {
        play();
    }
}

void PlaybackController::seek(qint64 positionMs) {
    if (mClosed) {
        return;
    }
    const auto bounded = std::clamp<qint64>(positionMs, 0, durationMs());
    mTasks.spawn(send(Seek {
        .position = std::chrono::milliseconds {bounded},
    }));
}

void PlaybackController::stop() {
    if (!mClosed) {
        mTasks.spawn(send(Stop {}));
    }
}

auto PlaybackController::openAndPlay(QUrl source) -> ilias::Task<void> {
    auto opened = co_await mSession.send(OpenMedia {.url = std::move(source)});
    if (opened) {
        auto playingResult = co_await mSession.send(Play {});
        (void) playingResult;
    }
}

auto PlaybackController::send(PlaybackCommand command) -> ilias::Task<void> {
    auto result = co_await mSession.send(std::move(command));
    (void) result;
}

void PlaybackController::refreshSnapshot() {
    auto current = mSession.snapshot();
    if (sameSnapshot(mSnapshot, current)) {
        return;
    }
    mSnapshot = std::move(current);
    emit snapshotChanged();
}

} // namespace anime_land
