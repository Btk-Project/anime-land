#pragma once

#include "model/playback/playback_video_sink.hpp"

#include <QObject>
#include <QSize>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>

namespace anime_land::qml {

struct PlaybackVideoSurfaceFrame {
    QImage image;
    std::chrono::nanoseconds presentationTime {};
    std::uint64_t serial = 0;
};

/** Thread-safe capacity-one mailbox between decoder and Qt Quick. */
class PlaybackVideoSurface final : public QObject,
                                   public PlaybackVideoSink {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY frameAvailable)
    Q_PROPERTY(QSize frameSize READ frameSize NOTIFY frameAvailable)

public:
    explicit PlaybackVideoSurface(QObject *parent = nullptr);

    auto activate() -> void override;
    auto submit(PlaybackVideoFrame frame) -> void override;
    auto deactivate() -> void override;

    auto active() const -> bool;
    auto hasFrame() const -> bool;
    auto frameSize() const -> QSize;
    auto latestFrame() const -> std::optional<PlaybackVideoSurfaceFrame>;

signals:
    void frameAvailable();
    void stateChanged();

private:
    auto scheduleNotification() -> void;

    mutable std::mutex mMutex;
    std::optional<PlaybackVideoSurfaceFrame> mLatest;
    std::uint64_t mSerial = 0;
    bool mActive = false;
    std::atomic_bool mNotificationPending {false};
};

} // namespace anime_land::qml
