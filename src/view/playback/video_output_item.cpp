#include "view/playback/video_output_item.hpp"

#include "view/playback/playback_video_surface.hpp"

#include <QMetaObject>

#include <rhi/qrhi.h>

#include <utility>

namespace anime_land::qml {

class VideoOutputRenderer final : public QQuickRhiItemRenderer {
public:
    auto initialize(QRhiCommandBuffer *) -> void override {}

    auto synchronize(QQuickRhiItem *item) -> void override {
        auto *output = static_cast<VideoOutputItem *>(item);
        if (output->mPendingSerial == mSerial) {
            return;
        }
        mFrame = output->mPendingFrame;
        mSerial = output->mPendingSerial;
        mUploadPending = !mFrame.isNull();
    }

    auto render(QRhiCommandBuffer *commandBuffer) -> void override {
        if (!mUploadPending || colorTexture() == nullptr || mFrame.isNull()) {
            return;
        }
        auto *updates = rhi()->nextResourceUpdateBatch();
        updates->uploadTexture(colorTexture(), mFrame);
        commandBuffer->resourceUpdate(updates);
        mUploadPending = false;
    }

private:
    QImage mFrame;
    std::uint64_t mSerial = 0;
    bool mUploadPending = false;
};

VideoOutputItem::VideoOutputItem(QQuickItem *parent)
    : QQuickRhiItem(parent) {
    setSampleCount(1);
    setColorBufferFormat(QQuickRhiItem::TextureFormat::RGBA8);
    setAlphaBlending(false);
}

VideoOutputItem::~VideoOutputItem() = default;

auto VideoOutputItem::source() const -> PlaybackVideoSurface * {
    return mSource;
}

auto VideoOutputItem::setSource(PlaybackVideoSurface *source) -> void {
    if (mSource == source) {
        return;
    }
    if (mSource != nullptr) {
        disconnect(mSource, nullptr, this, nullptr);
    }
    mSource = source;
    if (mSource != nullptr) {
        connect(mSource, &PlaybackVideoSurface::frameAvailable,
                this, &VideoOutputItem::frameReady);
        connect(mSource, &QObject::destroyed, this,
                [this] { setSource(nullptr); });
    }
    frameReady();
    emit sourceChanged();
}

auto VideoOutputItem::frameSize() const -> QSize {
    return mFrameSize;
}

auto VideoOutputItem::createRenderer() -> QQuickRhiItemRenderer * {
    return new VideoOutputRenderer;
}

void VideoOutputItem::frameReady() {
    const auto latest = mSource != nullptr ? mSource->latestFrame()
                                           : std::nullopt;
    const QSize size = latest ? latest->image.size() : QSize {};
    if (mFrameSize != size) {
        mFrameSize = size;
        setFixedColorBufferWidth(size.width());
        setFixedColorBufferHeight(size.height());
        emit frameSizeChanged();
    }
    mPendingFrame = latest ? latest->image : QImage {};
    ++mPendingSerial;
    update();
}

} // namespace anime_land::qml
