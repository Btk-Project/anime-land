#include "model/playback/playback_session.hpp"

#include <gtest/gtest.h>
#include <ilias/platform.hpp>
#include <ilias/sync/mpsc.hpp>
#include <ilias/testing.hpp>

#include <QUrl>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDir>

#include <chrono>
#include <memory>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

using namespace anime_land;
using namespace std::chrono_literals;

namespace {

struct FakePipelineState {
    std::vector<std::string> calls;
    ilias::mpsc::Sender<PlaybackPipelineMessage> messageSender;
    std::error_code openError;
    int shutdownCount = 0;
};

class FakePlaybackPipeline final : public PlaybackPipeline {
public:
    explicit FakePlaybackPipeline(std::shared_ptr<FakePipelineState> state)
        : mState(std::move(state)) {
        auto channel = ilias::mpsc::channel<PlaybackPipelineMessage>();
        mState->messageSender = channel.sender;
        mMessageReceiver = std::move(channel.receiver);
    }

    auto open(const QUrl &source) -> ilias::IoTask<void> override {
        mState->calls.push_back(
            "open:" + source.toString().toStdString());
        if (mState->openError) {
            co_return ilias::Err(mState->openError);
        }
        EXPECT_TRUE(mState->messageSender.trySend(
            PlaybackPipelineMediaLoaded {
                .startTime = 0s,
                .duration = 90s,
            }));
        co_return {};
    }

    auto play() -> ilias::IoTask<void> override {
        mState->calls.emplace_back("play");
        co_return {};
    }

    auto pause() -> ilias::IoTask<void> override {
        mState->calls.emplace_back("pause");
        co_return {};
    }

    auto seek(std::chrono::nanoseconds position)
        -> ilias::IoTask<void> override {
        mState->calls.push_back(
            "seek:" + std::to_string(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    position)
                    .count()));
        EXPECT_TRUE(mState->messageSender.trySend(
            PlaybackPipelineSeekBegin {}));
        EXPECT_TRUE(mState->messageSender.trySend(
            PlaybackPipelineClockUpdate {.position = position}));
        EXPECT_TRUE(mState->messageSender.trySend(
            PlaybackPipelineSeekEnd {}));
        co_return {};
    }

    auto shutdown() -> ilias::IoTask<void> override {
        mState->calls.emplace_back("shutdown");
        ++mState->shutdownCount;
        co_return {};
    }

    auto readMessage() -> ilias::Task<PlaybackPipelineMessage> override {
        co_return (co_await mMessageReceiver.recv()).value();
    }

private:
    std::shared_ptr<FakePipelineState> mState;
    ilias::mpsc::Receiver<PlaybackPipelineMessage> mMessageReceiver;
};

auto settle() -> ilias::Task<void> {
    co_await ilias::sleep(5ms);
}

} // namespace

static_assert(std::variant_size_v<PlaybackCommand> == 9);

ILIAS_TEST(PlaybackSession, MapsCommandsAndPipelineMessagesIntoSnapshot) {
    auto fake = std::make_shared<FakePipelineState>();
    PlaybackSession session([fake] {
        return std::make_unique<FakePlaybackPipeline>(fake);
    });
    auto runner = ilias::spawn(session.run());

    EXPECT_TRUE(co_await session.send(OpenMedia {
        .url = QUrl(QStringLiteral("file:///D:/Anime/episode-01.mkv")),
    }));
    co_await settle();
    auto snapshot = session.snapshot();
    EXPECT_EQ(snapshot.state, PlaybackState::Ready);
    EXPECT_EQ(snapshot.duration, 90s);
    EXPECT_EQ(snapshot.generation, 1U);
    EXPECT_TRUE(snapshot.source);

    EXPECT_TRUE(co_await session.send(Play {}));
    EXPECT_TRUE(co_await session.send(Seek {.position = 37s}));
    co_await settle();
    snapshot = session.snapshot();
    EXPECT_EQ(snapshot.state, PlaybackState::Playing);
    EXPECT_EQ(snapshot.position, 37s);

    EXPECT_TRUE(co_await session.send(Pause {}));
    EXPECT_TRUE(co_await session.send(SetVolume {.volume = 1.5}));
    EXPECT_TRUE(co_await session.send(SetRate {.rate = 2.0}));
    co_await settle();
    snapshot = session.snapshot();
    EXPECT_EQ(snapshot.state, PlaybackState::Paused);
    EXPECT_DOUBLE_EQ(snapshot.volume, 1.0);
    EXPECT_DOUBLE_EQ(snapshot.rate, 2.0);

    co_await session.close();
    EXPECT_TRUE(co_await std::move(runner));
    EXPECT_EQ(fake->shutdownCount, 1);
    EXPECT_EQ(session.snapshot().state, PlaybackState::Idle);
    co_return;
}

ILIAS_TEST(PlaybackSession, OpeningAnotherSourceTearsDownTheOldPipeline) {
    auto first = std::make_shared<FakePipelineState>();
    auto second = std::make_shared<FakePipelineState>();
    auto states = std::vector {first, second};
    std::size_t next = 0;
    PlaybackSession session([&states, &next] {
        return std::make_unique<FakePlaybackPipeline>(states.at(next++));
    });
    auto runner = ilias::spawn(session.run());

    EXPECT_TRUE(co_await session.send(OpenMedia {
        .url = QUrl(QStringLiteral("file:///D:/Anime/episode-01.mkv")),
    }));
    co_await settle();
    EXPECT_TRUE(co_await session.send(OpenMedia {
        .url = QUrl(QStringLiteral("file:///D:/Anime/episode-02.mkv")),
    }));
    co_await settle();

    const auto snapshot = session.snapshot();
    EXPECT_EQ(first->shutdownCount, 1);
    EXPECT_EQ(second->shutdownCount, 0);
    EXPECT_EQ(snapshot.state, PlaybackState::Ready);
    EXPECT_EQ(snapshot.generation, 3U);
    EXPECT_TRUE(snapshot.source);
    if (snapshot.source) {
        EXPECT_TRUE(snapshot.source->toString().endsWith(
            QStringLiteral("episode-02.mkv")));
    }

    co_await session.close();
    EXPECT_TRUE(co_await std::move(runner));
    EXPECT_EQ(second->shutdownCount, 1);
    co_return;
}

ILIAS_TEST(PlaybackSession, CoalescesQueuedSeeksToTheLatestPosition) {
    auto fake = std::make_shared<FakePipelineState>();
    PlaybackSession session([fake] {
        return std::make_unique<FakePlaybackPipeline>(fake);
    });

    EXPECT_TRUE(co_await session.send(OpenMedia {
        .url = QUrl(QStringLiteral("file:///D:/Anime/episode-01.mkv")),
    }));
    EXPECT_TRUE(co_await session.send(Seek {.position = 1s}));
    EXPECT_TRUE(co_await session.send(Seek {.position = 2s}));
    EXPECT_TRUE(co_await session.send(Seek {.position = 3s}));
    auto runner = ilias::spawn(session.run());
    co_await settle();

    const auto seekCalls = std::ranges::count_if(
        fake->calls, [](const std::string &call) {
            return call.starts_with("seek:");
        });
    EXPECT_EQ(seekCalls, 1);
    EXPECT_EQ(fake->calls.back(), "seek:3000");
    EXPECT_EQ(session.snapshot().position, 3s);

    co_await session.close();
    EXPECT_TRUE(co_await std::move(runner));
    co_return;
}

ILIAS_TEST(PlaybackSession, MapsOpenFailureAndRejectsCommandsAfterClose) {
    auto fake = std::make_shared<FakePipelineState>();
    fake->openError = ilias::make_error_code(ilias::IoError::InvalidArgument);
    PlaybackSession session([fake] {
        return std::make_unique<FakePlaybackPipeline>(fake);
    });
    auto runner = ilias::spawn(session.run());

    EXPECT_TRUE(co_await session.send(OpenMedia {
        .url = QUrl(QStringLiteral("file:///D:/Anime/broken.mkv")),
    }));
    co_await settle();
    const auto failed = session.snapshot();
    EXPECT_EQ(failed.state, PlaybackState::Error);
    EXPECT_TRUE(failed.error);
    if (failed.error) {
        EXPECT_EQ(failed.error->code, PlaybackErrorCode::PipelineFailure);
    }
    EXPECT_EQ(fake->shutdownCount, 1);

    co_await session.close();
    EXPECT_TRUE(co_await std::move(runner));
    const auto rejected = co_await session.send(Play {});
    EXPECT_FALSE(rejected);
    EXPECT_EQ(rejected.error(),
              ilias::make_error_code(ilias::IoError::Canceled));
    co_return;
}

ILIAS_TEST(NekoavPlaybackPipeline, ConstructsAndShutsDownInNullState) {
    auto pipeline = makeNekoavPlaybackPipeline();
    EXPECT_NE(pipeline, nullptr);
    if (pipeline) {
        EXPECT_TRUE(co_await pipeline->shutdown());
    }
    co_return;
}

ILIAS_TEST(NekoavPlaybackPipeline, OpensRepositoryMediaFixtureAndStops) {
    const QDir binaryDirectory(QCoreApplication::applicationDirPath());
    const QFileInfo fixture(binaryDirectory.absoluteFilePath(QStringLiteral(
        "../../../../third_party/nekoav/test/fixtures/av_1s.mkv")));
    EXPECT_TRUE(fixture.isFile());

    auto pipeline = makeNekoavPlaybackPipeline();
    auto opened = co_await pipeline->open(
        QUrl::fromLocalFile(fixture.absoluteFilePath()));
    EXPECT_TRUE(opened) << opened.error().message();
    if (opened) {
        auto message = co_await ilias::timeout(pipeline->readMessage(), 2s);
        EXPECT_TRUE(message);
        if (message) {
            EXPECT_TRUE(std::holds_alternative<
                        PlaybackPipelineMediaLoaded>(*message));
        }
    }
    EXPECT_TRUE(co_await pipeline->shutdown());
    co_return;
}

#define EXPAND_IN_MAIN                                                      \
    ilias::PlatformContext playbackTestContext;                             \
    playbackTestContext.install()
#include "common/common_main.hpp.in"
