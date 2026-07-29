#pragma once

#include <QJsonObject>
#include <QStringList>
#include <QUrl>

#include "model/episode_resource/error.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace anime_land::episode_provider_js {

struct OriginPermission {
    QString scheme;
    QString host;
    int port = -1;
    bool subdomains = false;

    auto allows(const QUrl &url) const -> bool;
};

struct EpisodePluginManifest {
    int manifestVersion = 0;
    QString id;
    QString name;
    QString version;
    QString runtimeApi;
    int runtimeApiVersion = 0;
    QString entry;
    QString configSchema;
    QString configDefaults;
    std::vector<OriginPermission> networkOrigins;
    std::vector<OriginPermission> mediaOrigins;

    auto allowsNetwork(const QUrl &url) const -> bool;
    auto allowsMedia(const QUrl &url) const -> bool;
};

struct MirrorConfiguration {
    QString id;
    QUrl baseUrl;
    bool enabled = true;
    int priority = 0;
};

struct EpisodePluginConfiguration {
    QJsonObject values;
    std::vector<MirrorConfiguration> mirrors;
    int requestTimeoutMilliseconds = 10'000;
    int minimumRequestIntervalMilliseconds = 750;
    int maximumRequestsPerOperation = 12;
    int maximumResponseBytes = 4 * 1024 * 1024;
    std::uint64_t generation = 0;
};

auto readPackageFile(QStringView packageRoot, QStringView relativePath,
                     qsizetype maximumBytes)
    -> EpisodeProviderResult<QByteArray>;
auto loadManifest(QStringView packageRoot)
    -> EpisodeProviderResult<EpisodePluginManifest>;
auto loadConfiguration(QStringView packageRoot,
                       const EpisodePluginManifest &manifest,
                       const std::optional<QString> &overridePath = std::nullopt)
    -> EpisodeProviderResult<EpisodePluginConfiguration>;

} // namespace anime_land::episode_provider_js
