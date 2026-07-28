#include "model/playback/playback_pipeline.hpp"
#include "model/playback/playback_video_sink.hpp"

#include <nekoav/elements/pipeline.hpp>
#include <nekoav/elements/playbin.hpp>
#include <nekoav/elements/video.hpp>
#include <nekoav/event.hpp>

#include <QDir>
#include <QImage>

#include <cstring>
#include <memory>
#include <type_traits>
#include <utility>

namespace anime_land {
namespace {

class NekoavPlaybackVideoRenderer final : public nekoav::VideoRenderer {
public:
    explicit NekoavPlaybackVideoRenderer(
        std::shared_ptr<PlaybackVideoSink> sink)
        : mSink(std::move(sink)) {}

    auto init() -> ilias::IoTask<void> override {
        mSink->activate();
        co_return {};
    }

    auto render(nekoav::VideoFrame frame) -> ilias::IoTask<void> override {
        auto converted = co_await ilias::blocking(
            [frame = std::move(frame)]() mutable -> PlaybackVideoFrame {
                if (frame.pixelFormat() != nekoav::PixelFormat::RGBA
                    || frame.width() <= 0 || frame.height() <= 0
                    || frame.data(0) == nullptr
                    || frame.linesize(0) == 0) {
                    return {};
                }

                QImage image(frame.width(), frame.height(),
                             QImage::Format_RGBA8888);
                if (image.isNull()) {
                    return {};
                }
                const auto *source = static_cast<const std::byte *>(
                    frame.data(0));
                const auto sourceStride = frame.linesize(0);
                const auto rowBytes = static_cast<std::size_t>(frame.width())
                                      * 4U;
                for (int row = 0; row < frame.height(); ++row) {
                    std::memcpy(image.scanLine(row),
                                source + row * sourceStride,
                                rowBytes);
                }
                return PlaybackVideoFrame {
                    .image = std::move(image),
                    .presentationTime = frame.pts().value_or(
                        std::chrono::nanoseconds {}),
                };
            });
        if (!converted.image.isNull()) {
            mSink->submit(std::move(converted));
        }
        co_return {};
    }

    auto shutdown() -> ilias::IoTask<void> override {
        mSink->deactivate();
        co_return {};
    }

    auto pixelFormats() const -> std::vector<nekoav::PixelFormat> override {
        return {nekoav::PixelFormat::RGBA};
    }

private:
    std::shared_ptr<PlaybackVideoSink> mSink;
};

class NekoavPlaybackPipeline final : public PlaybackPipeline {
public:
    explicit NekoavPlaybackPipeline(
        std::shared_ptr<PlaybackVideoSink> videoSink)
        : mPipeline(std::make_shared<nekoav::Pipeline>("anime-land")),
          mPlayBin(std::make_shared<nekoav::PlayBin>("playback")) {
        if (videoSink) {
            mPlayBin->setRenderer(
                std::make_shared<NekoavPlaybackVideoRenderer>(
                    std::move(videoSink)));
        }
        else {
            mPlayBin->setRenderer(
                std::make_shared<nekoav::NullVideoRenderer>());
        }
        mPipeline->addElement(mPlayBin);
    }

    auto open(const QUrl &source) -> ilias::IoTask<void> override {
        const QByteArray encoded =
            source.isLocalFile()
                ? QDir::toNativeSeparators(source.toLocalFile()).toUtf8()
                : source.toEncoded(QUrl::FullyEncoded);
        mPlayBin->setUrl(std::string_view(encoded.constData(), encoded.size()));
        co_return co_await mPipeline->setState(nekoav::State::Paused);
    }

    auto play() -> ilias::IoTask<void> override {
        co_return co_await mPipeline->setState(nekoav::State::Running);
    }

    auto pause() -> ilias::IoTask<void> override {
        co_return co_await mPipeline->setState(nekoav::State::Paused);
    }

    auto seek(std::chrono::nanoseconds position)
        -> ilias::IoTask<void> override {
        co_return co_await mPipeline->sendEvent(
            nekoav::Event::Seek {.timestamp = position});
    }

    auto shutdown() -> ilias::IoTask<void> override {
        co_return co_await mPipeline->setState(nekoav::State::Null);
    }

    auto readMessage() -> ilias::Task<PlaybackPipelineMessage> override {
        auto message = co_await mPipeline->readMessage();
        co_return message.visit([](const auto &value) -> PlaybackPipelineMessage {
            using Message = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Message, nekoav::Message::ClockUpdate>) {
                return PlaybackPipelineClockUpdate {.position = value.time};
            }
            else if constexpr (std::same_as<Message,
                                                nekoav::Message::MediaLoaded>) {
                return PlaybackPipelineMediaLoaded {
                    .startTime = value.startTime,
                    .duration = value.duration,
                };
            }
            else if constexpr (std::same_as<Message,
                                                nekoav::Message::SeekBegin>) {
                return PlaybackPipelineSeekBegin {};
            }
            else if constexpr (std::same_as<Message,
                                                nekoav::Message::SeekEnd>) {
                return PlaybackPipelineSeekEnd {};
            }
            else if constexpr (std::same_as<Message,
                                                nekoav::Message::EndOfStream>) {
                return PlaybackPipelineEndOfStream {};
            }
            else {
                return PlaybackPipelineError {
                    .error = value.error,
                    .message = value.message,
                };
            }
        });
    }

private:
    std::shared_ptr<nekoav::Pipeline> mPipeline;
    std::shared_ptr<nekoav::PlayBin> mPlayBin;
};

} // namespace

auto makeNekoavPlaybackPipeline() -> std::unique_ptr<PlaybackPipeline> {
    return makeNekoavPlaybackPipeline({});
}

auto makeNekoavPlaybackPipeline(
    std::shared_ptr<PlaybackVideoSink> videoSink)
    -> std::unique_ptr<PlaybackPipeline> {
    return std::make_unique<NekoavPlaybackPipeline>(std::move(videoSink));
}

} // namespace anime_land
