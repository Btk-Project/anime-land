#include "view/playback/playback_video_surface.hpp"

#include <QMetaObject>

#include <utility>

namespace anime_land::qml {

PlaybackVideoSurface::PlaybackVideoSurface(QObject *parent)
    : QObject(parent) {}

auto PlaybackVideoSurface::activate() -> void {
    {
        const auto lock = std::lock_guard {mMutex};
        mActive = true;
        mLatest.reset();
        ++mSerial;
    }
    scheduleNotification();
}

auto PlaybackVideoSurface::submit(PlaybackVideoFrame frame) -> void {
    if (frame.image.isNull()) {
        return;
    }
    {
        const auto lock = std::lock_guard {mMutex};
        if (!mActive) {
            return;
        }
        mLatest = PlaybackVideoSurfaceFrame {
            .image = std::move(frame.image),
            .presentationTime = frame.presentationTime,
            .serial = ++mSerial,
        };
    }
    scheduleNotification();
}

auto PlaybackVideoSurface::deactivate() -> void {
    {
        const auto lock = std::lock_guard {mMutex};
        mActive = false;
        mLatest.reset();
        ++mSerial;
    }
    scheduleNotification();
}

auto PlaybackVideoSurface::active() const -> bool {
    const auto lock = std::lock_guard {mMutex};
    return mActive;
}

auto PlaybackVideoSurface::hasFrame() const -> bool {
    const auto lock = std::lock_guard {mMutex};
    return mLatest.has_value();
}

auto PlaybackVideoSurface::frameSize() const -> QSize {
    const auto lock = std::lock_guard {mMutex};
    return mLatest ? mLatest->image.size() : QSize {};
}

auto PlaybackVideoSurface::latestFrame() const
    -> std::optional<PlaybackVideoSurfaceFrame> {
    const auto lock = std::lock_guard {mMutex};
    return mLatest;
}

auto PlaybackVideoSurface::scheduleNotification() -> void {
    if (mNotificationPending.exchange(true)) {
        return;
    }
    QMetaObject::invokeMethod(
        this,
        [this] {
            mNotificationPending.store(false);
            emit frameAvailable();
            emit stateChanged();
        },
        Qt::QueuedConnection);
}

} // namespace anime_land::qml
