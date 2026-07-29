#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QUrl>

#include "model/episode_resource/error.hpp"
#include "model/library/identity.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace anime_land {

enum class EpisodeAssetKind {
    Video,
    Subtitle,
    Audio,
    Danmaku,
};

enum class MediaStreamType {
    Unknown,
    Progressive,
    Hls,
    Dash,
};

struct EpisodeQuery {
    SubjectId subjectId;
    EpisodeId episodeId;
    QString subjectName;
    std::vector<QString> subjectAliases;
    QString episodeName;
    int episodeType = 0;
    std::optional<double> episodeNumber;
};

struct ProviderHealth {
    bool reachable = false;
    QString mirrorId;
    QString detail;
    std::int64_t latencyMilliseconds = 0;
};

struct ProviderSubjectMatch {
    QString stableKey;
    QString title;
    QUrl cover;
    QString detail;
    QString episodeTitle;
    QString sourceLine;
    double confidence = 0.0;
};

struct RemoteAsset {
    EpisodeAssetKind kind = EpisodeAssetKind::Video;
    MediaStreamType streamType = MediaStreamType::Unknown;
    QString displayName;
    std::optional<QString> language;
    std::optional<QString> mimeType;
    QJsonObject data;
};

struct OnlinePlayable {
    QString stableKey;
    QString displayName;
    ProviderSubjectMatch match;
    std::vector<RemoteAsset> assets;
    std::optional<QDateTime> expiresAt;
};

auto episodeAssetKindName(EpisodeAssetKind kind) -> QString;
auto mediaStreamTypeName(MediaStreamType type) -> QString;
auto episodeAssetKindFromName(QStringView name)
    -> std::optional<EpisodeAssetKind>;
auto mediaStreamTypeFromName(QStringView name)
    -> std::optional<MediaStreamType>;

auto validate(const EpisodeQuery &query) -> EpisodeProviderResult<void>;
auto validate(const OnlinePlayable &playable, bool requireResolvedVideo = false)
    -> EpisodeProviderResult<void>;
auto isResolved(const OnlinePlayable &playable) -> bool;

auto episodeQueryToJson(const EpisodeQuery &query) -> QJsonObject;
auto onlinePlayableToJson(const OnlinePlayable &playable) -> QJsonObject;
auto onlinePlayableFromJson(const QJsonObject &object)
    -> EpisodeProviderResult<OnlinePlayable>;

} // namespace anime_land
