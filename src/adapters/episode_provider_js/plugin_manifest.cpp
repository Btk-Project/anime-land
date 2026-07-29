#include "adapters/episode_provider_js/plugin_manifest.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace anime_land::episode_provider_js {
namespace {

constexpr qsizetype kMaximumManifestBytes = 64 * 1024;
constexpr qsizetype kMaximumConfigurationBytes = 256 * 1024;

auto invalidManifest(QString message)
    -> EpisodeProviderResult<EpisodePluginManifest> {
    return ilias::Err(episodeProviderError(
        EpisodeProviderErrorCode::InvalidManifest, std::move(message)));
}

auto invalidConfiguration(QString message)
    -> EpisodeProviderResult<EpisodePluginConfiguration> {
    return ilias::Err(episodeProviderError(
        EpisodeProviderErrorCode::InvalidConfiguration, std::move(message)));
}

auto isSafeRelativePath(QStringView path) -> bool {
    if (path.isEmpty() || QDir::isAbsolutePath(path.toString()) ||
        path.startsWith(QLatin1Char(':')) || path.contains(QLatin1Char('\\'))) {
        return false;
    }
    const QString clean = QDir::cleanPath(path.toString());
    return clean == path && clean != QStringLiteral(".") &&
           !clean.startsWith(QStringLiteral("../")) &&
           !clean.contains(QStringLiteral("/../"));
}

auto parseJsonObject(const QByteArray &bytes, EpisodeProviderErrorCode code,
                     QStringView name) -> EpisodeProviderResult<QJsonObject> {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return ilias::Err(episodeProviderError(
            code, QStringLiteral("%1 不是有效 JSON 对象：%2")
                      .arg(name, parseError.errorString())));
    }
    return document.object();
}

auto parseOrigin(QStringView text) -> std::optional<OriginPermission> {
    QString value = text.trimmed().toString();
    bool subdomains = false;
    const QString wildcardMarker = QStringLiteral("://*.");
    const qsizetype wildcard = value.indexOf(wildcardMarker);
    if (wildcard >= 0) {
        value.remove(wildcard + 3, 2);
        subdomains = true;
    }
    const QUrl url(value, QUrl::StrictMode);
    if (!url.isValid() || url.scheme() != QStringLiteral("https") ||
        url.host().isEmpty() || !url.userInfo().isEmpty() ||
        (!url.path().isEmpty() && url.path() != QStringLiteral("/")) ||
        url.hasQuery() || url.hasFragment()) {
        return std::nullopt;
    }
    const QString host = url.host(QUrl::FullyDecoded).toLower();
    if (host == QStringLiteral("localhost") || host.contains(QLatin1Char(':')) ||
        QRegularExpression(QStringLiteral(R"(^\d{1,3}(?:\.\d{1,3}){3}$)"))
            .match(host).hasMatch()) {
        return std::nullopt;
    }
    return OriginPermission {
        .scheme = QStringLiteral("https"),
        .host = host,
        .port = url.port(443),
        .subdomains = subdomains,
    };
}

auto parseOrigins(const QJsonValue &value, QStringView field)
    -> EpisodeProviderResult<std::vector<OriginPermission>> {
    if (!value.isArray()) {
        return ilias::Err(episodeProviderError(
            EpisodeProviderErrorCode::InvalidManifest,
            QStringLiteral("manifest permissions.%1 必须是数组").arg(field)));
    }
    std::vector<OriginPermission> result;
    for (const auto &item : value.toArray()) {
        if (!item.isString()) {
            return ilias::Err(episodeProviderError(
                EpisodeProviderErrorCode::InvalidManifest,
                QStringLiteral("manifest permissions.%1 只能包含 HTTPS origin")
                    .arg(field)));
        }
        auto origin = parseOrigin(item.toString());
        if (!origin) {
            return ilias::Err(episodeProviderError(
                EpisodeProviderErrorCode::InvalidManifest,
                QStringLiteral("manifest permissions.%1 包含无效 origin")
                    .arg(field)));
        }
        result.push_back(std::move(*origin));
    }
    return result;
}

auto mergeObjects(QJsonObject base, const QJsonObject &overrides)
    -> QJsonObject {
    for (auto iterator = overrides.constBegin(); iterator != overrides.constEnd();
         ++iterator) {
        if (iterator.value().isObject() && base.value(iterator.key()).isObject()) {
            base.insert(iterator.key(),
                        mergeObjects(base.value(iterator.key()).toObject(),
                                     iterator.value().toObject()));
        }
        else {
            base.insert(iterator.key(), iterator.value());
        }
    }
    return base;
}

auto boundedInteger(const QJsonObject &object, QStringView key, int fallback,
                    int minimum, int maximum) -> std::optional<int> {
    const auto value = object.value(key);
    if (value.isUndefined()) {
        return fallback;
    }
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const int integer = value.toInt(std::numeric_limits<int>::min());
    if (integer < minimum || integer > maximum) {
        return std::nullopt;
    }
    return integer;
}

} // namespace

auto OriginPermission::allows(const QUrl &url) const -> bool {
    if (!url.isValid() || url.scheme().toLower() != scheme ||
        url.port(443) != port || !url.userInfo().isEmpty()) {
        return false;
    }
    const QString candidate = url.host(QUrl::FullyDecoded).toLower();
    if (candidate == host) {
        return !subdomains;
    }
    return subdomains && candidate.endsWith(QLatin1Char('.') + host) &&
           candidate.size() > host.size() + 1;
}

auto EpisodePluginManifest::allowsNetwork(const QUrl &url) const -> bool {
    return std::ranges::any_of(networkOrigins,
                               [&](const auto &origin) { return origin.allows(url); });
}

auto EpisodePluginManifest::allowsMedia(const QUrl &url) const -> bool {
    return std::ranges::any_of(mediaOrigins,
                               [&](const auto &origin) { return origin.allows(url); });
}

auto readPackageFile(QStringView packageRoot, QStringView relativePath,
                     qsizetype maximumBytes)
    -> EpisodeProviderResult<QByteArray> {
    if (!isSafeRelativePath(relativePath)) {
        return ilias::Err(episodeProviderError(
            EpisodeProviderErrorCode::InvalidManifest,
            QStringLiteral("插件包路径无效：%1").arg(relativePath)));
    }
    const QString root = packageRoot.toString();
    const QString path = QDir(root).filePath(relativePath.toString());
    if (!root.startsWith(QStringLiteral(":/"))) {
        const QFileInfo rootInfo(root);
        const QFileInfo fileInfo(path);
        const QString canonicalRoot = rootInfo.canonicalFilePath();
        const QString canonicalFile = fileInfo.canonicalFilePath();
        if (canonicalRoot.isEmpty() || canonicalFile.isEmpty() ||
            (canonicalFile != canonicalRoot &&
             !canonicalFile.startsWith(canonicalRoot + QDir::separator()))) {
            return ilias::Err(episodeProviderError(
                EpisodeProviderErrorCode::InvalidManifest,
                QStringLiteral("插件包文件越过工作空间边界：%1")
                    .arg(relativePath)));
        }
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return ilias::Err(episodeProviderError(
            EpisodeProviderErrorCode::InvalidManifest,
            QStringLiteral("无法读取插件包文件：%1").arg(relativePath)));
    }
    if (file.size() < 0 || file.size() > maximumBytes) {
        return ilias::Err(episodeProviderError(
            EpisodeProviderErrorCode::InvalidManifest,
            QStringLiteral("插件包文件超过大小限制：%1").arg(relativePath)));
    }
    return file.readAll();
}

auto loadManifest(QStringView packageRoot)
    -> EpisodeProviderResult<EpisodePluginManifest> {
    auto bytes = readPackageFile(packageRoot, QStringLiteral("manifest.json"),
                                 kMaximumManifestBytes);
    if (!bytes) {
        return ilias::Err(std::move(bytes.error()));
    }
    auto object = parseJsonObject(*bytes, EpisodeProviderErrorCode::InvalidManifest,
                                  QStringLiteral("manifest.json"));
    if (!object) {
        return ilias::Err(std::move(object.error()));
    }

    EpisodePluginManifest manifest;
    manifest.manifestVersion = object->value(QStringLiteral("manifestVersion")).toInt();
    manifest.id = object->value(QStringLiteral("id")).toString();
    manifest.name = object->value(QStringLiteral("name")).toString();
    manifest.version = object->value(QStringLiteral("version")).toString();
    manifest.entry = object->value(QStringLiteral("entry")).toString();
    const auto runtime = object->value(QStringLiteral("runtime")).toObject();
    manifest.runtimeApi = runtime.value(QStringLiteral("api")).toString();
    manifest.runtimeApiVersion = runtime.value(QStringLiteral("version")).toInt();
    const auto config = object->value(QStringLiteral("config")).toObject();
    manifest.configSchema = config.value(QStringLiteral("schema")).toString();
    manifest.configDefaults = config.value(QStringLiteral("defaults")).toString();

    static const QRegularExpression idPattern(
        QStringLiteral(R"(^[a-z0-9]+(?:[.-][a-z0-9]+)+$)"));
    if (manifest.manifestVersion != 1 || !idPattern.match(manifest.id).hasMatch() ||
        manifest.name.trimmed().isEmpty() || manifest.version.trimmed().isEmpty() ||
        manifest.runtimeApi != QStringLiteral("episode-provider") ||
        manifest.runtimeApiVersion != 1 || !isSafeRelativePath(manifest.entry) ||
        !isSafeRelativePath(manifest.configSchema) ||
        !isSafeRelativePath(manifest.configDefaults)) {
        return invalidManifest(QStringLiteral("manifest 必填字段或 runtime 版本无效"));
    }

    const auto permissions = object->value(QStringLiteral("permissions")).toObject();
    auto network = parseOrigins(permissions.value(QStringLiteral("network")),
                                QStringLiteral("network"));
    auto media = parseOrigins(permissions.value(QStringLiteral("media")),
                              QStringLiteral("media"));
    if (!network) {
        return ilias::Err(std::move(network.error()));
    }
    if (!media) {
        return ilias::Err(std::move(media.error()));
    }
    if (network->empty() || media->empty()) {
        return invalidManifest(QStringLiteral("插件必须声明 network 与 media origin"));
    }
    manifest.networkOrigins = std::move(*network);
    manifest.mediaOrigins = std::move(*media);
    return manifest;
}

auto loadConfiguration(QStringView packageRoot,
                       const EpisodePluginManifest &manifest,
                       const std::optional<QString> &overridePath)
    -> EpisodeProviderResult<EpisodePluginConfiguration> {
    auto schemaBytes = readPackageFile(packageRoot, manifest.configSchema,
                                       kMaximumConfigurationBytes);
    if (!schemaBytes) {
        return ilias::Err(std::move(schemaBytes.error()));
    }
    auto schema = parseJsonObject(*schemaBytes,
                                  EpisodeProviderErrorCode::InvalidConfiguration,
                                  QStringLiteral("config schema"));
    if (!schema) {
        return ilias::Err(std::move(schema.error()));
    }
    auto defaultsBytes = readPackageFile(packageRoot, manifest.configDefaults,
                                         kMaximumConfigurationBytes);
    if (!defaultsBytes) {
        return ilias::Err(std::move(defaultsBytes.error()));
    }
    auto defaults = parseJsonObject(*defaultsBytes,
                                    EpisodeProviderErrorCode::InvalidConfiguration,
                                    QStringLiteral("config defaults"));
    if (!defaults) {
        return ilias::Err(std::move(defaults.error()));
    }
    QJsonObject values = std::move(*defaults);
    if (overridePath && QFileInfo::exists(*overridePath)) {
        QFile file(*overridePath);
        if (!file.open(QIODevice::ReadOnly) || file.size() > kMaximumConfigurationBytes) {
            return invalidConfiguration(QStringLiteral("无法读取插件用户配置或配置过大"));
        }
        auto overrides = parseJsonObject(
            file.readAll(), EpisodeProviderErrorCode::InvalidConfiguration,
            QStringLiteral("provider user config"));
        if (!overrides) {
            return ilias::Err(std::move(overrides.error()));
        }
        values = mergeObjects(std::move(values), *overrides);
    }

    EpisodePluginConfiguration configuration;
    configuration.values = values;
    const auto mirrors = values.value(QStringLiteral("mirrors"));
    if (!mirrors.isArray()) {
        return invalidConfiguration(QStringLiteral("mirrors 必须是数组"));
    }
    for (const auto &value : mirrors.toArray()) {
        const auto object = value.toObject();
        MirrorConfiguration mirror {
            .id = object.value(QStringLiteral("id")).toString(),
            .baseUrl = QUrl(object.value(QStringLiteral("baseUrl")).toString(),
                            QUrl::StrictMode),
            .enabled = object.value(QStringLiteral("enabled")).toBool(true),
            .priority = object.value(QStringLiteral("priority")).toInt(),
        };
        if (mirror.id.trimmed().isEmpty() || !manifest.allowsNetwork(mirror.baseUrl) ||
            (!mirror.baseUrl.path().isEmpty() &&
             mirror.baseUrl.path() != QStringLiteral("/")) ||
            mirror.baseUrl.hasQuery() || mirror.baseUrl.hasFragment()) {
            return invalidConfiguration(QStringLiteral("mirrors 包含无效或未授权的 baseUrl"));
        }
        configuration.mirrors.push_back(std::move(mirror));
    }
    std::ranges::stable_sort(configuration.mirrors, std::greater {},
                             &MirrorConfiguration::priority);
    if (!std::ranges::any_of(configuration.mirrors,
                             &MirrorConfiguration::enabled)) {
        return invalidConfiguration(QStringLiteral("至少需要启用一个镜像"));
    }

    const auto timeout = boundedInteger(values, QStringLiteral("requestTimeoutMs"),
                                        10'000, 1'000, 60'000);
    const auto interval = boundedInteger(
        values, QStringLiteral("minimumRequestIntervalMs"), 750, 250, 10'000);
    const auto requests = boundedInteger(
        values, QStringLiteral("maximumRequestsPerOperation"), 12, 1, 32);
    const auto responseBytes = boundedInteger(
        values, QStringLiteral("maximumResponseBytes"), 4 * 1024 * 1024,
        64 * 1024, 8 * 1024 * 1024);
    if (!timeout || !interval || !requests || !responseBytes) {
        return invalidConfiguration(QStringLiteral("运行时限制字段超出允许范围"));
    }
    configuration.requestTimeoutMilliseconds = *timeout;
    configuration.minimumRequestIntervalMilliseconds = *interval;
    configuration.maximumRequestsPerOperation = *requests;
    configuration.maximumResponseBytes = *responseBytes;

    const QByteArray digest = QCryptographicHash::hash(
        QJsonDocument(values).toJson(QJsonDocument::Compact) + manifest.version.toUtf8(),
        QCryptographicHash::Sha256);
    static_assert(sizeof(configuration.generation) <= 32);
    std::memcpy(&configuration.generation, digest.constData(),
                sizeof(configuration.generation));
    return configuration;
}

} // namespace anime_land::episode_provider_js
