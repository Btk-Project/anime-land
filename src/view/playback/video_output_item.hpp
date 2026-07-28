#pragma once

#include "view/playback/playback_video_surface.hpp"

#include <QImage>
#include <QQuickRhiItem>
#include <QSize>

#include <cstdint>

namespace anime_land::qml {

class VideoOutputItem : public QQuickRhiItem {
    Q_OBJECT
    Q_PROPERTY(PlaybackVideoSurface *source READ source WRITE setSource
                   NOTIFY sourceChanged)
    Q_PROPERTY(QSize frameSize READ frameSize NOTIFY frameSizeChanged)

public:
    explicit VideoOutputItem(QQuickItem *parent = nullptr);
    ~VideoOutputItem() override;

    auto source() const -> PlaybackVideoSurface *;
    auto setSource(PlaybackVideoSurface *source) -> void;
    auto frameSize() const -> QSize;

signals:
    void sourceChanged();
    void frameSizeChanged();

protected:
    auto createRenderer() -> QQuickRhiItemRenderer * override;

private slots:
    void frameReady();

private:
    friend class VideoOutputRenderer;

    PlaybackVideoSurface *mSource = nullptr;
    QImage mPendingFrame;
    std::uint64_t mPendingSerial = 0;
    QSize mFrameSize;
};

} // namespace anime_land::qml
