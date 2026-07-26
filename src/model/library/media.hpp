#pragma once

#include "model/library/error.hpp"
#include "model/library/identity.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QString>

#include <chrono>
#include <optional>
#include <string_view>
#include <vector>

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

/** Provider discovery value before local database IDs are assigned. */
struct MediaResourceSnapshot {
    QString providerKey;
    QString stableKey;
    int descriptorVersion = 1;
    QByteArray descriptor;
    QString displayName;
};

/** One discovered playable item before its local ID is assigned. */
struct SourceItemSnapshot {
    QString stableKey;
    QByteArray descriptor;
    QString displayName;
    std::optional<std::chrono::milliseconds> duration;
};

/** Atomic provider discovery batch for one resource root. */
struct MediaDiscovery {
    MediaResourceSnapshot resource;
    std::vector<SourceItemSnapshot> items;
    QDateTime observedAt;
};

/** Persisted result of one discovery batch. */
struct StoredMediaDiscovery {
    MediaResource resource;
    std::vector<SourceItem> items;
};

/** Flat projection used by media-library list and import presentation. */
struct MediaEntry {
    MediaResource resource;
    SourceItem item;
};

enum class MediaLinkKind : std::int64_t {
    Manual = 0,
    Filename = 1,
    Sequence = 2,
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
auto validate(const MediaResourceSnapshot &resource) -> LibraryResult<void>;
auto validate(const SourceItemSnapshot &item) -> LibraryResult<void>;
auto validate(const MediaDiscovery &discovery) -> LibraryResult<void>;
auto validate(const EpisodeMediaLink &link) -> LibraryResult<void>;

} // namespace anime_land
