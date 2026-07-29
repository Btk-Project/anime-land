#pragma once

#include <QElapsedTimer>
#include <QJSEngine>
#include <QJSValue>
#include <QNetworkAccessManager>
#include <QPointer>

#include "adapters/episode_provider_js/html_bridge.hpp"
#include "adapters/episode_provider_js/plugin_manifest.hpp"
#include "model/episode_resource/provider.hpp"

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

class QNetworkReply;

namespace anime_land::episode_provider_js {

namespace detail {

class PluginHostBridge final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *html READ html CONSTANT)

public:
    explicit PluginHostBridge(HtmlBridge &html, QObject *parent = nullptr);

    QObject *html() const;
    Q_INVOKABLE void registerEpisodeProvider(const QJSValue &provider);
    Q_INVOKABLE void log(const QString &level, const QString &message);

    void closeRegistration();
    auto registrations() const -> const std::vector<QJSValue> &;

private:
    HtmlBridge &mHtml;
    bool mRegistrationOpen = true;
    std::vector<QJSValue> mRegistrations;
};

struct ProviderDescriptor {
    QString id;
    QString name;
    QUrl icon;
};

class JsPluginRuntime final {
public:
    struct HttpResponse {
        QUrl url;
        int status = 0;
        QJsonObject headers;
        QString text;
    };

    JsPluginRuntime(QString packageRoot, EpisodePluginManifest manifest,
                    EpisodePluginConfiguration configuration,
                    std::unique_ptr<QNetworkAccessManager> network);

    auto initialize(const QByteArray &script)
        -> EpisodeProviderResult<std::vector<ProviderDescriptor>>;
    auto ping(std::size_t providerIndex)
        -> ilias::Task<EpisodeProviderResult<ProviderHealth>>;
    auto search(std::size_t providerIndex, const EpisodeQuery &query)
        -> ilias::Task<EpisodeProviderResult<std::vector<OnlinePlayable>>>;
    auto resolve(std::size_t providerIndex, const OnlinePlayable &playable)
        -> ilias::Task<EpisodeProviderResult<OnlinePlayable>>;
    void cancel();

    auto manifest() const -> const EpisodePluginManifest &;
    auto generation() const -> std::uint64_t;

private:
    auto run(std::size_t providerIndex, QString operation, QJsonValue input)
        -> ilias::Task<EpisodeProviderResult<QJsonValue>>;
    auto runOnMirror(std::size_t providerIndex, QStringView operation,
                     const QJsonValue &input,
                     const MirrorConfiguration &mirror)
        -> ilias::Task<EpisodeProviderResult<QJsonValue>>;
    auto performRequest(const QJsonObject &descriptor,
                        const MirrorConfiguration &mirror)
        -> ilias::Task<EpisodeProviderResult<HttpResponse>>;
    auto invoke(std::size_t providerIndex, QStringView method,
                const QList<QJSValue> &arguments)
        -> EpisodeProviderResult<QJsonObject>;
    auto scriptValue(const QJsonValue &value) -> QJSValue;
    auto context(std::size_t providerIndex,
                 const MirrorConfiguration &mirror) -> QJsonObject;
    auto validateMediaUrls(const OnlinePlayable &playable)
        -> EpisodeProviderResult<void>;

    QString mPackageRoot;
    EpisodePluginManifest mManifest;
    EpisodePluginConfiguration mConfiguration;
    std::unique_ptr<QNetworkAccessManager> mNetwork;
    QJSEngine mEngine;
    HtmlBridge mHtml;
    PluginHostBridge mHost;
    QJSValue mStringify;
    std::vector<ProviderDescriptor> mDescriptors;
    QPointer<QNetworkReply> mActiveReply;
    QElapsedTimer mLastRequest;
    std::atomic_bool mBusy = false;
    std::atomic_bool mCancelled = false;
};

} // namespace detail

class JsEpisodeProvider final : public EpisodeProvider {
public:
    JsEpisodeProvider(std::shared_ptr<detail::JsPluginRuntime> runtime,
                      std::size_t providerIndex,
                      detail::ProviderDescriptor descriptor);

    auto key() const -> QString override;
    auto name() const -> QString override;
    auto icon() const -> QUrl override;
    auto generation() const -> std::uint64_t override;
    auto ping() -> ilias::Task<EpisodeProviderResult<ProviderHealth>> override;
    auto search(EpisodeQuery query)
        -> ilias::Task<EpisodeProviderResult<std::vector<OnlinePlayable>>> override;
    auto resolve(OnlinePlayable playable)
        -> ilias::Task<EpisodeProviderResult<OnlinePlayable>> override;
    void cancel() override;

private:
    std::shared_ptr<detail::JsPluginRuntime> mRuntime;
    std::size_t mProviderIndex = 0;
    detail::ProviderDescriptor mDescriptor;
};

struct LoadedEpisodePlugin {
    EpisodePluginManifest manifest;
    EpisodePluginConfiguration configuration;
    std::vector<std::shared_ptr<EpisodeProvider>> providers;
};

struct EpisodePluginScanIssue {
    QString packagePath;
    EpisodeProviderError error;
};

struct EpisodePluginScanResult {
    std::vector<LoadedEpisodePlugin> plugins;
    std::vector<EpisodePluginScanIssue> issues;
};

auto loadEpisodeProviderPlugin(
    QString packageRoot,
    const std::optional<QString> &configurationOverride = std::nullopt,
    std::unique_ptr<QNetworkAccessManager> network = {})
    -> EpisodeProviderResult<LoadedEpisodePlugin>;

auto scanEpisodeProviderPlugins(QString providerDirectory,
                                QString providerConfigDirectory,
                                int maximumPackages = 64,
                                bool allowSymlinks = false,
                                const QStringList &excludedPluginIds = {})
    -> EpisodePluginScanResult;

auto builtinYhdmmmPackageRoot() -> QString;
void initializeBuiltinEpisodeProviderResources();

} // namespace anime_land::episode_provider_js
