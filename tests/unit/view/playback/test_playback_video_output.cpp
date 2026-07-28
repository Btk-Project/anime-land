#include "model/playback/playback_session.hpp"
#include "view/playback/playback_video_surface.hpp"

#include <gtest/gtest.h>
#include <ilias/platform.hpp>
#include <ilias/testing.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QUrl>

#include <chrono>
#include <memory>
#include <utility>

using namespace anime_land;
using namespace anime_land::qml;
using namespace std::chrono_literals;

TEST(PlaybackVideoSurface, KeepsOnlyTheLatestActiveFrame) {
    PlaybackVideoSurface surface;
    QImage first(2, 2, QImage::Format_RGBA8888);
    first.fill(Qt::red);
    QImage second(4, 3, QImage::Format_RGBA8888);
    second.fill(Qt::green);

    surface.submit({.image = first, .presentationTime = 1ms});
    EXPECT_FALSE(surface.hasFrame());

    surface.activate();
    surface.submit({.image = first, .presentationTime = 1ms});
    const auto firstSnapshot = surface.latestFrame();
    ASSERT_TRUE(firstSnapshot);
    EXPECT_EQ(firstSnapshot->image.size(), QSize(2, 2));

    surface.submit({.image = second, .presentationTime = 2ms});
    const auto secondSnapshot = surface.latestFrame();
    ASSERT_TRUE(secondSnapshot);
    EXPECT_GT(secondSnapshot->serial, firstSnapshot->serial);
    EXPECT_EQ(secondSnapshot->image.size(), QSize(4, 3));
    EXPECT_EQ(secondSnapshot->presentationTime, 2ms);

    surface.deactivate();
    EXPECT_FALSE(surface.active());
    EXPECT_FALSE(surface.hasFrame());
}

ILIAS_TEST(PlaybackVideoOutput, ReceivesRgbaFrameFromRepositoryFixture) {
    const QDir binaryDirectory(QCoreApplication::applicationDirPath());
    const QFileInfo fixture(binaryDirectory.absoluteFilePath(QStringLiteral(
        "../../../../third_party/nekoav/test/fixtures/av_1s.mkv")));
    EXPECT_TRUE(fixture.isFile());

    auto surface = std::make_shared<PlaybackVideoSurface>();
    PlaybackSession session([surface] {
        return makeNekoavPlaybackPipeline(surface);
    });
    auto runner = ilias::spawn(session.run());

    EXPECT_TRUE(co_await session.send(OpenMedia {
        .url = QUrl::fromLocalFile(fixture.absoluteFilePath()),
    }));
    EXPECT_TRUE(co_await session.send(Play {}));

    for (int attempt = 0; attempt < 300 && !surface->hasFrame(); ++attempt) {
        co_await ilias::sleep(10ms);
    }
    const auto frame = surface->latestFrame();
    EXPECT_TRUE(frame);
    if (frame) {
        EXPECT_EQ(frame->image.size(), QSize(64, 64));
        EXPECT_EQ(frame->image.format(), QImage::Format_RGBA8888);
        EXPECT_FALSE(frame->image.isNull());
    }

    co_await session.close();
    EXPECT_TRUE(co_await std::move(runner));
    EXPECT_FALSE(surface->active());
    EXPECT_FALSE(surface->hasFrame());
    co_return;
}

#define EXPAND_IN_MAIN_WITH_ARGS(argc, argv)                               \
    QCoreApplication playbackQtApplication(argc, argv);                    \
    ilias::PlatformContext playbackTestContext;                            \
    playbackTestContext.install()
#include "common/common_main.hpp.in"
