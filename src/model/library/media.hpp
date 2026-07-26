#pragma once

#include "model/library/error.hpp"
#include "model/library/identity.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QString>

#include <chrono>
#include <optional>
#include <string_view>

namespace anime_land {

/** Provider-owned resource root, identified locally by MediaResourceId. */
struct MediaResource {
    MediaResourceId id;
    QString providerKey;
    QString stableKey;
    int descriptorVersion = 1;
    QByteArray descriptor;
    QString displayName;
};

/** One playable item owned by a media resource. */
struct SourceItem {
    SourceItemId id;
    MediaResourceId resourceId;
    QString stableKey;
    QByteArray descriptor;
    QString displayName;
    std::optional<std::chrono::milliseconds> duration;
};

enum class MediaLinkKind {
    Manual,
    Filename,
    Sequence,
};

/** Application-owned relation between a local episode and a playable item. */
struct EpisodeMediaLink {
    EpisodeId episodeId;
    SourceItemId sourceItemId;
    MediaLinkKind kind = MediaLinkKind::Manual;
    QDateTime updatedAt;
};

auto mediaLinkKindName(MediaLinkKind kind) -> std::string_view;
auto validate(const MediaResource &resource) -> LibraryResult<void>;
auto validate(const SourceItem &item) -> LibraryResult<void>;
auto validate(const EpisodeMediaLink &link) -> LibraryResult<void>;

} // namespace anime_land
