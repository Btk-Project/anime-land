#include <gtest/gtest.h>

#include <QDateTime>

#include "model/library/library.hpp"

#include <chrono>
#include <concepts>
#include <optional>

using namespace anime_land;
using namespace std::chrono_literals;

namespace {

auto validTimestamp() -> QDateTime {
    return QDateTime::fromMSecsSinceEpoch(1'700'000'000'000LL);
}

auto validResource() -> MediaResource {
    return {
        .id = MediaResourceId{1},
        .providerKey = QStringLiteral("local-file"),
        .stableKey = QStringLiteral("library/anime"),
        .descriptorVersion = 1,
        .descriptor = QByteArrayLiteral(R"({"root":"D:/Anime"})"),
        .displayName = QStringLiteral("Anime"),
    };
}

auto validItem() -> SourceItem {
    return {
        .id = SourceItemId{2},
        .resourceId = MediaResourceId{1},
        .stableKey = QStringLiteral("episode-01.mkv"),
        .descriptor = QByteArrayLiteral(R"({"relativePath":"episode-01.mkv"})"),
        .displayName = QStringLiteral("Episode 01"),
        .duration = 24min,
    };
}

auto validProgress() -> PlaybackProgress {
    return {
        .episodeId = EpisodeId{3},
        .lastSourceItemId = SourceItemId{2},
        .position = 12min,
        .duration = 24min,
        .completed = false,
        .updatedAt = validTimestamp(),
    };
}

} // namespace

static_assert(!std::same_as<SubjectId, EpisodeId>);
static_assert(!std::same_as<MediaResourceId, SourceItemId>);

TEST(LibraryIdentity, OnlyPositiveLocalIdsAreValid) {
    EXPECT_FALSE(isValid(SubjectId{}));
    EXPECT_FALSE(isValid(EpisodeId{-1}));
    EXPECT_TRUE(isValid(MediaResourceId{1}));
    EXPECT_TRUE(isValid(SourceItemId{9}));
}

TEST(LibraryMediaResource, ValidatesProviderIdentityAndDescriptorVersion) {
    auto resource = validResource();
    EXPECT_TRUE(validate(resource));

    resource.providerKey = QStringLiteral("  ");
    auto result = validate(resource);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, LibraryErrorCode::InvalidProviderKey);

    resource = validResource();
    resource.stableKey.clear();
    result = validate(resource);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, LibraryErrorCode::InvalidStableKey);

    resource = validResource();
    resource.descriptorVersion = 0;
    result = validate(resource);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code,
              LibraryErrorCode::InvalidDescriptorVersion);
}

TEST(LibrarySourceItem, RequiresResourceOwnershipAndNonnegativeDuration) {
    auto item = validItem();
    EXPECT_TRUE(validate(item));

    item.resourceId = {};
    auto result = validate(item);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, LibraryErrorCode::InvalidIdentity);

    item = validItem();
    item.duration = -1ms;
    result = validate(item);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, LibraryErrorCode::InvalidDuration);
}

TEST(LibraryEpisodeMediaLink, ValidatesIdsAndTimestamp) {
    EpisodeMediaLink link{
        .episodeId = EpisodeId{3},
        .sourceItemId = SourceItemId{2},
        .kind = MediaLinkKind::Filename,
        .updatedAt = validTimestamp(),
    };
    EXPECT_TRUE(validate(link));
    EXPECT_EQ(mediaLinkKindName(link.kind), "filename");

    link.sourceItemId = {};
    auto result = validate(link);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, LibraryErrorCode::InvalidIdentity);

    link.sourceItemId = SourceItemId{2};
    link.updatedAt = {};
    result = validate(link);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, LibraryErrorCode::InvalidTimestamp);
}

TEST(LibraryPlaybackProgress, ValidatesPositionDurationAndLastSource) {
    auto progress = validProgress();
    EXPECT_TRUE(validate(progress));

    progress.position = -1ms;
    auto result = validate(progress);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, LibraryErrorCode::InvalidPosition);

    progress = validProgress();
    progress.duration = -1ms;
    result = validate(progress);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, LibraryErrorCode::InvalidDuration);

    progress = validProgress();
    progress.lastSourceItemId = SourceItemId{};
    result = validate(progress);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, LibraryErrorCode::InvalidIdentity);
}

TEST(LibraryPlaybackProgress, ComputesBoundedFractionWhenDurationIsKnown) {
    auto progress = validProgress();
    ASSERT_TRUE(playbackFraction(progress));
    EXPECT_DOUBLE_EQ(*playbackFraction(progress), 0.5);

    progress.position = 30min;
    ASSERT_TRUE(playbackFraction(progress));
    EXPECT_DOUBLE_EQ(*playbackFraction(progress), 1.0);

    progress.duration = std::nullopt;
    EXPECT_FALSE(playbackFraction(progress));

    progress.duration = 0ms;
    EXPECT_FALSE(playbackFraction(progress));
}

TEST(LibraryEnums, ExposeStableDiagnosticNames) {
    EXPECT_EQ(libraryErrorCodeName(LibraryErrorCode::InvalidIdentity),
              "invalid-identity");
    EXPECT_EQ(mediaLinkKindName(MediaLinkKind::Manual), "manual");
    EXPECT_EQ(mediaLinkKindName(MediaLinkKind::Sequence), "sequence");
}

#include "common/common_main.hpp.in"
