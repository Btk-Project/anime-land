#include "pch.hpp"

#include "model/episode_resource/types.hpp"

#include <QJsonArray>

#include <cmath>
#include <utility>

namespace anime_land {
namespace {

auto invalidResult(QString message) -> EpisodeProviderResult<OnlinePlayable> {
    return ilias::Err(episodeProviderError(
        EpisodeProviderErrorCode::InvalidScriptResult, std::move(message)));
}

auto optionalString(const QJsonObject &object, QStringView key)
    -> std::optional<QString> {
    const auto value = object.value(key);
    if (!value.isString() || value.toString().isEmpty()) {
        return std::nullopt;
    }
    return value.toString();
}

} // namespace

auto episodeAssetKindName(EpisodeAssetKind kind) -> QString {
    switch (kind) {
        case EpisodeAssetKind::Video:
            return QStringLiteral("video");
        case EpisodeAssetKind::Subtitle:
            return QStringLiteral("subtitle");
        case EpisodeAssetKind::Audio:
            return QStringLiteral("audio");
        case EpisodeAssetKind::Danmaku:
            return QStringLiteral("danmaku");
    }
    return {};
}

auto mediaStreamTypeName(MediaStreamType type) -> QString {
    switch (type) {
        case MediaStreamType::Unknown:
            return QStringLiteral("unknown");
        case MediaStreamType::Progressive:
            return QStringLiteral("progressive");
        case MediaStreamType::Hls:
            return QStringLiteral("hls");
        case MediaStreamType::Dash:
            return QStringLiteral("dash");
    }
    return {};
}

auto episodeAssetKindFromName(QStringView name)
    -> std::optional<EpisodeAssetKind> {
    if (name == QStringLiteral("video")) {
        return EpisodeAssetKind::Video;
    }
    if (name == QStringLiteral("subtitle")) {
        return EpisodeAssetKind::Subtitle;
    }
    if (name == QStringLiteral("audio")) {
        return EpisodeAssetKind::Audio;
    }
    if (name == QStringLiteral("danmaku")) {
        return EpisodeAssetKind::Danmaku;
    }
    return std::nullopt;
}

auto mediaStreamTypeFromName(QStringView name)
    -> std::optional<MediaStreamType> {
    if (name == QStringLiteral("unknown")) {
        return MediaStreamType::Unknown;
    }
    if (name == QStringLiteral("progressive")) {
        return MediaStreamType::Progressive;
    }
    if (name == QStringLiteral("hls")) {
        return MediaStreamType::Hls;
    }
    if (name == QStringLiteral("dash")) {
        return MediaStreamType::Dash;
    }
    return std::nullopt;
}

auto validate(const EpisodeQuery &query) -> EpisodeProviderResult<void> {
    if (query.subjectName.trimmed().isEmpty() && query.subjectAliases.empty()) {
        return ilias::Err(episodeProviderError(
            EpisodeProviderErrorCode::InvalidQuery,
            QStringLiteral("番剧名和别名不能同时为空")));
    }
    if (query.episodeName.trimmed().isEmpty() && !query.episodeNumber) {
        return ilias::Err(episodeProviderError(
            EpisodeProviderErrorCode::InvalidQuery,
            QStringLiteral("章节名和章节序号不能同时为空")));
    }
    if (query.episodeNumber &&
        (!std::isfinite(*query.episodeNumber) || *query.episodeNumber < 0.0)) {
        return ilias::Err(episodeProviderError(
            EpisodeProviderErrorCode::InvalidQuery,
            QStringLiteral("章节序号必须是非负有限数")));
    }
    return {};
}

auto validate(const OnlinePlayable &playable, bool requireResolvedVideo)
    -> EpisodeProviderResult<void> {
    if (playable.stableKey.trimmed().isEmpty() ||
        playable.displayName.trimmed().isEmpty()) {
        return ilias::Err(episodeProviderError(
            EpisodeProviderErrorCode::InvalidScriptResult,
            QStringLiteral("在线结果缺少稳定键或显示名")));
    }

    int videoCount = 0;
    bool resolvedVideo = false;
    for (const auto &asset : playable.assets) {
        if (asset.displayName.trimmed().isEmpty()) {
            return ilias::Err(episodeProviderError(
                EpisodeProviderErrorCode::InvalidScriptResult,
                QStringLiteral("在线资源缺少显示名")));
        }
        if (asset.kind == EpisodeAssetKind::Video) {
            ++videoCount;
            const auto url = QUrl(asset.data.value(QStringLiteral("url")).toString(),
                                  QUrl::StrictMode);
            resolvedVideo = url.isValid() && !url.scheme().isEmpty() &&
                            !url.host().isEmpty();
        }
    }
    if (videoCount != 1) {
        return ilias::Err(episodeProviderError(
            EpisodeProviderErrorCode::InvalidScriptResult,
            QStringLiteral("在线结果必须包含且只包含一个主视频")));
    }
    if (requireResolvedVideo && !resolvedVideo) {
        return ilias::Err(episodeProviderError(
            EpisodeProviderErrorCode::InvalidScriptResult,
            QStringLiteral("解析后的在线结果缺少可播放 URL")));
    }
    return {};
}

auto isResolved(const OnlinePlayable &playable) -> bool {
    return validate(playable, true).has_value();
}

auto episodeQueryToJson(const EpisodeQuery &query) -> QJsonObject {
    QJsonArray aliases;
    for (const auto &alias : query.subjectAliases) {
        aliases.append(alias);
    }
    QJsonObject result {
        {QStringLiteral("subjectId"), static_cast<qint64>(query.subjectId.value)},
        {QStringLiteral("episodeId"), static_cast<qint64>(query.episodeId.value)},
        {QStringLiteral("subjectName"), query.subjectName},
        {QStringLiteral("subjectAliases"), aliases},
        {QStringLiteral("episodeName"), query.episodeName},
        {QStringLiteral("episodeType"), query.episodeType},
    };
    if (query.episodeNumber) {
        result.insert(QStringLiteral("episodeNumber"), *query.episodeNumber);
    }
    return result;
}

auto onlinePlayableToJson(const OnlinePlayable &playable) -> QJsonObject {
    QJsonArray assets;
    for (const auto &asset : playable.assets) {
        QJsonObject value {
            {QStringLiteral("kind"), episodeAssetKindName(asset.kind)},
            {QStringLiteral("streamType"), mediaStreamTypeName(asset.streamType)},
            {QStringLiteral("name"), asset.displayName},
            {QStringLiteral("data"), asset.data},
        };
        if (asset.language) {
            value.insert(QStringLiteral("language"), *asset.language);
        }
        if (asset.mimeType) {
            value.insert(QStringLiteral("mimeType"), *asset.mimeType);
        }
        assets.append(value);
    }

    QJsonObject result {
        {QStringLiteral("key"), playable.stableKey},
        {QStringLiteral("name"), playable.displayName},
        {QStringLiteral("match"),
         QJsonObject {
             {QStringLiteral("key"), playable.match.stableKey},
             {QStringLiteral("title"), playable.match.title},
             {QStringLiteral("cover"), playable.match.cover.toString()},
             {QStringLiteral("detail"), playable.match.detail},
             {QStringLiteral("episodeTitle"), playable.match.episodeTitle},
             {QStringLiteral("sourceLine"), playable.match.sourceLine},
             {QStringLiteral("confidence"), playable.match.confidence},
         }},
        {QStringLiteral("assets"), assets},
    };
    if (playable.expiresAt) {
        result.insert(QStringLiteral("expiresAt"),
                      playable.expiresAt->toUTC().toString(Qt::ISODateWithMs));
    }
    return result;
}

auto onlinePlayableFromJson(const QJsonObject &object)
    -> EpisodeProviderResult<OnlinePlayable> {
    OnlinePlayable playable;
    playable.stableKey = object.value(QStringLiteral("key")).toString();
    playable.displayName = object.value(QStringLiteral("name")).toString();

    const auto match = object.value(QStringLiteral("match")).toObject();
    playable.match.stableKey = match.value(QStringLiteral("key")).toString();
    playable.match.title = match.value(QStringLiteral("title")).toString();
    playable.match.cover = QUrl(match.value(QStringLiteral("cover")).toString(),
                                QUrl::StrictMode);
    playable.match.detail = match.value(QStringLiteral("detail")).toString();
    playable.match.episodeTitle =
        match.value(QStringLiteral("episodeTitle")).toString();
    playable.match.sourceLine =
        match.value(QStringLiteral("sourceLine")).toString();
    playable.match.confidence =
        match.value(QStringLiteral("confidence")).toDouble();

    const auto assets = object.value(QStringLiteral("assets"));
    if (!assets.isArray()) {
        return invalidResult(QStringLiteral("在线结果 assets 必须是数组"));
    }
    for (const auto &value : assets.toArray()) {
        if (!value.isObject()) {
            return invalidResult(QStringLiteral("在线结果 asset 必须是对象"));
        }
        const auto assetObject = value.toObject();
        const auto kind = episodeAssetKindFromName(
            assetObject.value(QStringLiteral("kind")).toString());
        const auto streamType = mediaStreamTypeFromName(
            assetObject.value(QStringLiteral("streamType")).toString());
        if (!kind || !streamType ||
            !assetObject.value(QStringLiteral("data")).isObject()) {
            return invalidResult(QStringLiteral("在线结果 asset 类型或 data 无效"));
        }
        playable.assets.push_back({
            .kind = *kind,
            .streamType = *streamType,
            .displayName = assetObject.value(QStringLiteral("name")).toString(),
            .language = optionalString(assetObject, QStringLiteral("language")),
            .mimeType = optionalString(assetObject, QStringLiteral("mimeType")),
            .data = assetObject.value(QStringLiteral("data")).toObject(),
        });
    }

    const auto expiresAt = object.value(QStringLiteral("expiresAt"));
    if (expiresAt.isString()) {
        const auto parsed = QDateTime::fromString(expiresAt.toString(), Qt::ISODate);
        if (!parsed.isValid()) {
            return invalidResult(QStringLiteral("在线结果 expiresAt 无效"));
        }
        playable.expiresAt = parsed;
    }

    if (auto validated = validate(playable); !validated) {
        return ilias::Err(std::move(validated.error()));
    }
    return playable;
}

} // namespace anime_land
