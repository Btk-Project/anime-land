#include "adapters/episode_provider_js/plugin_runtime.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QUrlQuery>

#include <ilias_qt/network.hpp>

#include "common/log.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

static void initializeEpisodeProviderPluginResources() {
    Q_INIT_RESOURCE(episode_provider_plugins);
}

namespace anime_land::episode_provider_js {
namespace detail {
namespace {

constexpr qsizetype kMaximumScriptResultBytes = 512 * 1024;
constexpr qsizetype kMaximumRequestBodyBytes = 64 * 1024;
constexpr int kMaximumRedirects = 5;
constexpr std::size_t kMaximumSearchResults = 64;

class BusyGuard final {
public:
    explicit BusyGuard(std::atomic_bool &busy) : mBusy(busy) {}
    ~BusyGuard() { mBusy.store(false); }

private:
    std::atomic_bool &mBusy;
};

auto invalidScript(QString message, EpisodeProviderErrorCode code =
                                       EpisodeProviderErrorCode::InvalidScriptResult)
    -> EpisodeProviderResult<QJsonObject> {
    return ilias::Err(episodeProviderError(code, std::move(message)));
}

auto httpResponseToJson(const JsPluginRuntime::HttpResponse &response)
    -> QJsonObject {
    return {
        {QStringLiteral("url"), response.url.toString(QUrl::FullyEncoded)},
        {QStringLiteral("status"), response.status},
        {QStringLiteral("headers"), response.headers},
        {QStringLiteral("text"), response.text},
    };
}

auto requestMethod(const QJsonObject &descriptor) -> QByteArray {
    return descriptor.value(QStringLiteral("method"))
        .toString(QStringLiteral("GET"))
        .trimmed()
        .toUpper()
        .toLatin1();
}

auto redactedRoute(const QUrl &url) -> QString {
    return url.path().isEmpty() ? QStringLiteral("/") : url.path();
}

auto pinnedMirrorId(QStringView operation, const QJsonValue &input) -> QString {
    if (operation != QStringLiteral("resolve") || !input.isObject()) {
        return {};
    }
    const auto assets = input.toObject().value(QStringLiteral("assets")).toArray();
    if (assets.isEmpty()) {
        return {};
    }
    return assets.at(0)
        .toObject()
        .value(QStringLiteral("data"))
        .toObject()
        .value(QStringLiteral("continuation"))
        .toObject()
        .value(QStringLiteral("mirrorId"))
        .toString();
}

} // namespace

PluginHostBridge::PluginHostBridge(HtmlBridge &html, QObject *parent)
    : QObject(parent), mHtml(html) {}

QObject *PluginHostBridge::html() const { return &mHtml; }

void PluginHostBridge::registerEpisodeProvider(const QJSValue &provider) {
    if (!mRegistrationOpen || !provider.isObject()) {
        return;
    }
    mRegistrations.push_back(provider);
}

void PluginHostBridge::log(const QString &level, const QString &message) {
    const QString safeMessage = message.left(512).replace(QRegularExpression(
        QStringLiteral(R"((https?://[^\s?#]+)[^\s]*)")), QStringLiteral("\\1"));
    if (level.compare(QStringLiteral("error"), Qt::CaseInsensitive) == 0) {
        AL_LOG_ERROR("[episode-provider.js] {}", safeMessage.toStdString());
    }
    else if (level.compare(QStringLiteral("warn"), Qt::CaseInsensitive) == 0) {
        AL_LOG_WARN("[episode-provider.js] {}", safeMessage.toStdString());
    }
    else {
        AL_LOG_INFO("[episode-provider.js] {}", safeMessage.toStdString());
    }
}

void PluginHostBridge::closeRegistration() { mRegistrationOpen = false; }

auto PluginHostBridge::registrations() const
    -> const std::vector<QJSValue> & {
    return mRegistrations;
}

JsPluginRuntime::JsPluginRuntime(
    QString packageRoot, EpisodePluginManifest manifest,
    EpisodePluginConfiguration configuration,
    std::unique_ptr<QNetworkAccessManager> network)
    : mPackageRoot(std::move(packageRoot)),
      mManifest(std::move(manifest)),
      mConfiguration(std::move(configuration)),
      mNetwork(network ? std::move(network)
                       : std::make_unique<QNetworkAccessManager>()),
      mHost(mHtml) {}

auto JsPluginRuntime::initialize(const QByteArray &script)
    -> EpisodeProviderResult<std::vector<ProviderDescriptor>> {
    mStringify = mEngine.evaluate(QStringLiteral(
        "(function(){const stringify=JSON.stringify;"
        "return function(value){return stringify(value);};})()"));
    mEngine.globalObject().setProperty(
        QStringLiteral("AnimeLand"), mEngine.newQObject(&mHost));
    const QJSValue evaluated = mEngine.evaluate(
        QString::fromUtf8(script), mManifest.entry, 1);
    mHost.closeRegistration();
    if (evaluated.isError()) {
        return ilias::Err(episodeProviderError(
            EpisodeProviderErrorCode::InvalidScript,
            QStringLiteral("插件入口执行失败：%1")
                .arg(evaluated.property(QStringLiteral("stack")).toString())));
    }
    if (mHost.registrations().empty()) {
        return ilias::Err(episodeProviderError(
            EpisodeProviderErrorCode::InvalidScript,
            QStringLiteral("插件没有注册 EpisodeProvider")));
    }

    static const QRegularExpression providerIdPattern(
        QStringLiteral(R"(^[a-z0-9]+(?:-[a-z0-9]+)*$)"));
    for (const auto &registration : mHost.registrations()) {
        ProviderDescriptor descriptor {
            .id = registration.property(QStringLiteral("id")).toString(),
            .name = registration.property(QStringLiteral("name")).toString(),
            .icon = QUrl(registration.property(QStringLiteral("icon")).toString(),
                         QUrl::StrictMode),
        };
        if (!providerIdPattern.match(descriptor.id).hasMatch() ||
            descriptor.name.trimmed().isEmpty() ||
            !registration.property(QStringLiteral("begin")).isCallable() ||
            !registration.property(QStringLiteral("resume")).isCallable()) {
            return ilias::Err(episodeProviderError(
                EpisodeProviderErrorCode::InvalidScript,
                QStringLiteral("Provider 注册对象缺少有效 id/name/begin/resume")));
        }
        if (std::ranges::any_of(mDescriptors, [&](const auto &existing) {
                return existing.id == descriptor.id;
            })) {
            return ilias::Err(episodeProviderError(
                EpisodeProviderErrorCode::InvalidScript,
                QStringLiteral("插件内 Provider id 重复：%1").arg(descriptor.id)));
        }
        mDescriptors.push_back(std::move(descriptor));
    }
    return mDescriptors;
}

auto JsPluginRuntime::manifest() const -> const EpisodePluginManifest & {
    return mManifest;
}

auto JsPluginRuntime::generation() const -> std::uint64_t {
    return mConfiguration.generation;
}

auto JsPluginRuntime::scriptValue(const QJsonValue &value) -> QJSValue {
    return mEngine.toScriptValue(value.toVariant());
}

auto JsPluginRuntime::context(std::size_t providerIndex,
                              const MirrorConfiguration &mirror) -> QJsonObject {
    return {
        {QStringLiteral("plugin"),
         QJsonObject {
             {QStringLiteral("id"), mManifest.id},
             {QStringLiteral("version"), mManifest.version},
         }},
        {QStringLiteral("provider"),
         QJsonObject {
             {QStringLiteral("id"), mDescriptors.at(providerIndex).id},
         }},
        {QStringLiteral("mirror"),
         QJsonObject {
             {QStringLiteral("id"), mirror.id},
             {QStringLiteral("baseUrl"),
              mirror.baseUrl.toString(QUrl::FullyEncoded)},
         }},
        {QStringLiteral("config"), mConfiguration.values},
    };
}

auto JsPluginRuntime::invoke(std::size_t providerIndex, QStringView method,
                             const QList<QJSValue> &arguments)
    -> EpisodeProviderResult<QJsonObject> {
    if (providerIndex >= mHost.registrations().size()) {
        return invalidScript(QStringLiteral("Provider 索引越界"));
    }
    const QJSValue &provider = mHost.registrations()[providerIndex];
    const QJSValue function = provider.property(method.toString());
    if (!function.isCallable()) {
        return invalidScript(QStringLiteral("Provider 方法不可调用：%1").arg(method));
    }
    const QJSValue result = function.callWithInstance(provider, arguments);
    if (result.isError()) {
        return invalidScript(
            QStringLiteral("Provider %1() 执行失败：%2")
                .arg(method, result.property(QStringLiteral("stack")).toString()),
            EpisodeProviderErrorCode::InvalidScript);
    }
    const QJSValue encoded = mStringify.call({result});
    if (encoded.isError() || !encoded.isString()) {
        return invalidScript(QStringLiteral("Provider 返回值不能转换为 JSON"));
    }
    const QByteArray bytes = encoded.toString().toUtf8();
    if (bytes.size() > kMaximumScriptResultBytes) {
        return invalidScript(QStringLiteral("Provider JSON 状态超过大小限制"));
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return invalidScript(QStringLiteral("Provider 必须返回 JSON step 对象"));
    }
    return document.object();
}

auto JsPluginRuntime::run(std::size_t providerIndex, QString operation,
                          QJsonValue input)
    -> ilias::Task<EpisodeProviderResult<QJsonValue>> {
    if (mBusy.exchange(true)) {
        co_return ilias::Err(episodeProviderError(
            EpisodeProviderErrorCode::Busy,
            QStringLiteral("同一个 JS 插件当前已有调用正在执行"), true));
    }
    BusyGuard busyGuard(mBusy);
    mCancelled.store(false);

    std::optional<EpisodeProviderError> lastError;
    const QString pinnedMirror = pinnedMirrorId(operation, input);
    bool foundPinnedMirror = pinnedMirror.isEmpty();
    for (const auto &mirror : mConfiguration.mirrors) {
        if (!mirror.enabled ||
            (!pinnedMirror.isEmpty() && mirror.id != pinnedMirror)) {
            continue;
        }
        foundPinnedMirror = true;
        auto result = co_await runOnMirror(providerIndex, operation, input, mirror);
        if (result) {
            co_return result;
        }
        lastError = std::move(result.error());
        if (!lastError->retryable || mCancelled.load() ||
            !pinnedMirror.isEmpty()) {
            co_return ilias::Err(std::move(*lastError));
        }
        AL_LOG_WARN(
            "[episode-provider.runtime] mirror failed provider={} mirror={} code={} retrying={}",
            (mManifest.id + QLatin1Char('.') + mDescriptors.at(providerIndex).id)
                .toStdString(),
            mirror.id.toStdString(),
            episodeProviderErrorCodeName(lastError->code), true);
    }
    if (!foundPinnedMirror) {
        co_return ilias::Err(episodeProviderError(
            EpisodeProviderErrorCode::ResultExpired,
            QStringLiteral("生成该结果的镜像已禁用，请重新搜索")));
    }
    if (lastError) {
        co_return ilias::Err(std::move(*lastError));
    }
    co_return ilias::Err(episodeProviderError(
        EpisodeProviderErrorCode::InvalidConfiguration,
        QStringLiteral("没有可用镜像")));
}

auto JsPluginRuntime::runOnMirror(std::size_t providerIndex,
                                  QStringView operation,
                                  const QJsonValue &input,
                                  const MirrorConfiguration &mirror)
    -> ilias::Task<EpisodeProviderResult<QJsonValue>> {
    auto step = invoke(providerIndex, QStringLiteral("begin"),
                       {QJSValue(operation.toString()), scriptValue(input),
                        scriptValue(context(providerIndex, mirror))});
    if (!step) {
        co_return ilias::Err(std::move(step.error()));
    }

    int requestCount = 0;
    while (true) {
        if (mCancelled.load()) {
            co_return ilias::Err(episodeProviderError(
                EpisodeProviderErrorCode::Cancelled,
                QStringLiteral("Provider 调用已取消")));
        }
        const QString type = step->value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("complete")) {
            co_return step->value(QStringLiteral("value"));
        }
        if (type == QStringLiteral("fail")) {
            const auto error = step->value(QStringLiteral("error")).toObject();
            co_return ilias::Err(episodeProviderError(
                EpisodeProviderErrorCode::InvalidScriptResult,
                error.value(QStringLiteral("message"))
                    .toString(QStringLiteral("Provider 报告解析失败")),
                error.value(QStringLiteral("retryable")).toBool(false)));
        }
        if (type != QStringLiteral("request") ||
            !step->value(QStringLiteral("request")).isObject() ||
            !step->value(QStringLiteral("state")).isObject()) {
            co_return ilias::Err(episodeProviderError(
                EpisodeProviderErrorCode::InvalidScriptResult,
                QStringLiteral("Provider step 必须是 request/complete/fail")));
        }
        if (++requestCount > mConfiguration.maximumRequestsPerOperation) {
            co_return ilias::Err(episodeProviderError(
                EpisodeProviderErrorCode::RequestLimitExceeded,
                QStringLiteral("Provider 单次操作请求数超过限制")));
        }

        auto response = co_await performRequest(
            step->value(QStringLiteral("request")).toObject(), mirror);
        if (!response) {
            co_return ilias::Err(std::move(response.error()));
        }
        step = invoke(
            providerIndex, QStringLiteral("resume"),
            {scriptValue(step->value(QStringLiteral("state"))),
             scriptValue(httpResponseToJson(*response)),
             scriptValue(context(providerIndex, mirror))});
        if (!step) {
            co_return ilias::Err(std::move(step.error()));
        }
    }
}

auto JsPluginRuntime::performRequest(const QJsonObject &descriptor,
                                     const MirrorConfiguration &mirror)
    -> ilias::Task<EpisodeProviderResult<HttpResponse>> {
    QUrl url(descriptor.value(QStringLiteral("url")).toString(), QUrl::StrictMode);
    if (url.isRelative()) {
        url = mirror.baseUrl.resolved(url);
    }
    if (!mManifest.allowsNetwork(url)) {
        co_return ilias::Err(episodeProviderError(
            EpisodeProviderErrorCode::PermissionDenied,
            QStringLiteral("Provider 请求了未授权的网络 origin")));
    }
    const QByteArray method = requestMethod(descriptor);
    if (method != QByteArrayLiteral("GET") &&
        method != QByteArrayLiteral("POST") &&
        method != QByteArrayLiteral("HEAD")) {
        co_return ilias::Err(episodeProviderError(
            EpisodeProviderErrorCode::PermissionDenied,
            QStringLiteral("Provider 只允许 GET、POST 和 HEAD")));
    }
    const QByteArray body = descriptor.value(QStringLiteral("body")).toString().toUtf8();
    if (body.size() > kMaximumRequestBodyBytes ||
        (method != QByteArrayLiteral("POST") && !body.isEmpty())) {
        co_return ilias::Err(episodeProviderError(
            EpisodeProviderErrorCode::PermissionDenied,
            QStringLiteral("Provider 请求体无效或过大")));
    }

    QHash<QByteArray, QByteArray> headers;
    const auto headerObject = descriptor.value(QStringLiteral("headers")).toObject();
    for (auto iterator = headerObject.constBegin(); iterator != headerObject.constEnd();
         ++iterator) {
        const QByteArray name = iterator.key().trimmed().toLatin1();
        const QByteArray lower = name.toLower();
        if ((lower != QByteArrayLiteral("accept") &&
             lower != QByteArrayLiteral("content-type") &&
             lower != QByteArrayLiteral("referer")) ||
            !iterator.value().isString()) {
            co_return ilias::Err(episodeProviderError(
                EpisodeProviderErrorCode::PermissionDenied,
                QStringLiteral("Provider 请求头不在允许列表内")));
        }
        const QByteArray value = iterator.value().toString().toUtf8();
        if (value.contains('\r') || value.contains('\n') || value.size() > 1024) {
            co_return ilias::Err(episodeProviderError(
                EpisodeProviderErrorCode::PermissionDenied,
                QStringLiteral("Provider 请求头值无效")));
        }
        if (lower == QByteArrayLiteral("referer") &&
            !mManifest.allowsNetwork(QUrl(QString::fromUtf8(value), QUrl::StrictMode))) {
            co_return ilias::Err(episodeProviderError(
                EpisodeProviderErrorCode::PermissionDenied,
                QStringLiteral("Provider Referer 不在网络权限内")));
        }
        headers.insert(name, value);
    }

    for (int redirectCount = 0; redirectCount <= kMaximumRedirects;
         ++redirectCount) {
        if (mCancelled.load()) {
            co_return ilias::Err(episodeProviderError(
                EpisodeProviderErrorCode::Cancelled,
                QStringLiteral("Provider 请求已取消")));
        }
        if (mLastRequest.isValid()) {
            const qint64 remaining =
                mConfiguration.minimumRequestIntervalMilliseconds -
                mLastRequest.elapsed();
            if (remaining > 0) {
                co_await ilias::sleep(std::chrono::milliseconds(remaining));
            }
        }

        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::ManualRedirectPolicy);
        request.setTransferTimeout(mConfiguration.requestTimeoutMilliseconds);
        request.setRawHeader(
            QByteArrayLiteral("User-Agent"),
            QByteArrayLiteral("anime-land EpisodeProvider/1 (+desktop client)"));
        for (auto iterator = headers.constBegin(); iterator != headers.constEnd();
             ++iterator) {
            request.setRawHeader(iterator.key(), iterator.value());
        }

        AL_LOG_INFO(
            "[episode-provider.http] request provider={} method={} route={} mirror={}",
            mManifest.id.toStdString(), method.toStdString(),
            redactedRoute(url).toStdString(), mirror.id.toStdString());
        QNetworkReply *rawReply = nullptr;
        if (method == QByteArrayLiteral("GET")) {
            rawReply = mNetwork->get(request);
        }
        else if (method == QByteArrayLiteral("HEAD")) {
            rawReply = mNetwork->head(request);
        }
        else {
            rawReply = mNetwork->post(request, body);
        }
        struct ResponseAccumulator {
            QByteArray body;
            qsizetype maximumBytes = 0;
            bool tooLarge = false;
        };
        auto accumulator = std::make_shared<ResponseAccumulator>();
        accumulator->maximumBytes = mConfiguration.maximumResponseBytes;
        QObject::connect(rawReply, &QIODevice::readyRead, rawReply,
                         [rawReply, accumulator] {
            const QByteArray chunk = rawReply->readAll();
            if (accumulator->tooLarge) {
                return;
            }
            if (accumulator->body.size() + chunk.size() >
                accumulator->maximumBytes) {
                accumulator->tooLarge = true;
                rawReply->abort();
                return;
            }
            accumulator->body.append(chunk);
        });
        mLastRequest.restart();
        mActiveReply = rawReply;
        auto reply = co_await rawReply;
        mActiveReply.clear();
        if (!reply) {
            co_return ilias::Err(episodeProviderError(
                EpisodeProviderErrorCode::NetworkError,
                QStringLiteral("无法创建 Provider 网络请求"), true));
        }
        const int status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto error = reply->error();
        const QByteArray remainingBody = reply->readAll();
        if (!accumulator->tooLarge &&
            accumulator->body.size() + remainingBody.size() >
                accumulator->maximumBytes) {
            accumulator->tooLarge = true;
        }
        if (!accumulator->tooLarge) {
            accumulator->body.append(remainingBody);
        }
        const QByteArray &responseBody = accumulator->body;
        AL_LOG_INFO(
            "[episode-provider.http] response provider={} status={} bytes={} route={}",
            mManifest.id.toStdString(), status, responseBody.size(),
            redactedRoute(url).toStdString());

        const QUrl redirect = reply->attribute(
            QNetworkRequest::RedirectionTargetAttribute).toUrl();
        if (status >= 300 && status < 400 && !redirect.isEmpty()) {
            if (redirectCount == kMaximumRedirects) {
                co_return ilias::Err(episodeProviderError(
                    EpisodeProviderErrorCode::NetworkError,
                    QStringLiteral("Provider 网络重定向次数过多"), true));
            }
            const QUrl next = url.resolved(redirect);
            if (!mManifest.allowsNetwork(next)) {
                co_return ilias::Err(episodeProviderError(
                    EpisodeProviderErrorCode::PermissionDenied,
                    QStringLiteral("Provider 重定向越过网络权限")));
            }
            url = next;
            continue;
        }
        if (accumulator->tooLarge) {
            co_return ilias::Err(episodeProviderError(
                EpisodeProviderErrorCode::ResponseTooLarge,
                QStringLiteral("Provider 响应超过大小限制")));
        }
        if (error == QNetworkReply::OperationCanceledError || mCancelled.load()) {
            co_return ilias::Err(episodeProviderError(
                EpisodeProviderErrorCode::Cancelled,
                QStringLiteral("Provider 网络请求已取消")));
        }
        if (error != QNetworkReply::NoError) {
            co_return ilias::Err(episodeProviderError(
                EpisodeProviderErrorCode::NetworkError,
                QStringLiteral("Provider 网络请求失败：%1")
                    .arg(reply->errorString()), true));
        }
        if (status >= 500) {
            co_return ilias::Err(episodeProviderError(
                EpisodeProviderErrorCode::HttpError,
                QStringLiteral("Provider 站点返回 HTTP %1").arg(status), true));
        }

        QJsonObject responseHeaders;
        const QByteArray contentType = reply->rawHeader(QByteArrayLiteral("Content-Type"));
        if (!contentType.isEmpty()) {
            responseHeaders.insert(QStringLiteral("content-type"),
                                   QString::fromLatin1(contentType));
        }
        co_return HttpResponse {
            .url = url,
            .status = status,
            .headers = std::move(responseHeaders),
            .text = QString::fromUtf8(responseBody),
        };
    }

    co_return ilias::Err(episodeProviderError(
        EpisodeProviderErrorCode::NetworkError,
        QStringLiteral("Provider 网络请求未完成"), true));
}

auto JsPluginRuntime::ping(std::size_t providerIndex)
    -> ilias::Task<EpisodeProviderResult<ProviderHealth>> {
    QElapsedTimer timer;
    timer.start();
    auto value = co_await run(providerIndex, QStringLiteral("ping"), QJsonObject {});
    if (!value) {
        co_return ilias::Err(std::move(value.error()));
    }
    if (!value->isObject()) {
        co_return ilias::Err(episodeProviderError(
            EpisodeProviderErrorCode::InvalidScriptResult,
            QStringLiteral("ping 必须返回对象")));
    }
    const auto object = value->toObject();
    co_return ProviderHealth {
        .reachable = object.value(QStringLiteral("reachable")).toBool(false),
        .mirrorId = object.value(QStringLiteral("mirrorId")).toString(),
        .detail = object.value(QStringLiteral("detail")).toString(),
        .latencyMilliseconds = timer.elapsed(),
    };
}

auto JsPluginRuntime::search(std::size_t providerIndex,
                             const EpisodeQuery &query)
    -> ilias::Task<EpisodeProviderResult<std::vector<OnlinePlayable>>> {
    if (auto validated = validate(query); !validated) {
        co_return ilias::Err(std::move(validated.error()));
    }
    auto value = co_await run(providerIndex, QStringLiteral("search"),
                              episodeQueryToJson(query));
    if (!value) {
        co_return ilias::Err(std::move(value.error()));
    }
    if (!value->isArray() ||
        value->toArray().size() > static_cast<qsizetype>(kMaximumSearchResults)) {
        co_return ilias::Err(episodeProviderError(
            EpisodeProviderErrorCode::InvalidScriptResult,
            QStringLiteral("search 必须返回大小受限的数组")));
    }
    std::vector<OnlinePlayable> playables;
    playables.reserve(value->toArray().size());
    for (const auto &item : value->toArray()) {
        if (!item.isObject()) {
            co_return ilias::Err(episodeProviderError(
                EpisodeProviderErrorCode::InvalidScriptResult,
                QStringLiteral("search 结果项必须是对象")));
        }
        auto playable = onlinePlayableFromJson(item.toObject());
        if (!playable) {
            co_return ilias::Err(std::move(playable.error()));
        }
        playables.push_back(std::move(*playable));
    }
    co_return playables;
}

auto JsPluginRuntime::validateMediaUrls(const OnlinePlayable &playable)
    -> EpisodeProviderResult<void> {
    for (const auto &asset : playable.assets) {
        const QString text = asset.data.value(QStringLiteral("url")).toString();
        if (text.isEmpty()) {
            continue;
        }
        const QUrl url(text, QUrl::StrictMode);
        if (!mManifest.allowsMedia(url)) {
            return ilias::Err(episodeProviderError(
                EpisodeProviderErrorCode::PermissionDenied,
                QStringLiteral("Provider 返回了未授权的媒体 origin")));
        }
        if (asset.kind == EpisodeAssetKind::Video &&
            asset.streamType == MediaStreamType::Hls &&
            !url.path().endsWith(QStringLiteral(".m3u8"), Qt::CaseInsensitive)) {
            return ilias::Err(episodeProviderError(
                EpisodeProviderErrorCode::InvalidScriptResult,
                QStringLiteral("HLS 资源 URL 不是 m3u8")));
        }
    }
    return {};
}

auto JsPluginRuntime::resolve(std::size_t providerIndex,
                              const OnlinePlayable &playable)
    -> ilias::Task<EpisodeProviderResult<OnlinePlayable>> {
    if (isResolved(playable)) {
        if (auto permitted = validateMediaUrls(playable); !permitted) {
            co_return ilias::Err(std::move(permitted.error()));
        }
        co_return playable;
    }
    auto value = co_await run(providerIndex, QStringLiteral("resolve"),
                              onlinePlayableToJson(playable));
    if (!value) {
        co_return ilias::Err(std::move(value.error()));
    }
    if (!value->isObject()) {
        co_return ilias::Err(episodeProviderError(
            EpisodeProviderErrorCode::InvalidScriptResult,
            QStringLiteral("resolve 必须返回对象")));
    }
    auto resolved = onlinePlayableFromJson(value->toObject());
    if (!resolved) {
        co_return ilias::Err(std::move(resolved.error()));
    }
    if (resolved->stableKey != playable.stableKey) {
        co_return ilias::Err(episodeProviderError(
            EpisodeProviderErrorCode::InvalidScriptResult,
            QStringLiteral("resolve 不能替换结果稳定键")));
    }
    if (auto validated = validate(*resolved, true); !validated) {
        co_return ilias::Err(std::move(validated.error()));
    }
    if (auto permitted = validateMediaUrls(*resolved); !permitted) {
        co_return ilias::Err(std::move(permitted.error()));
    }
    co_return std::move(*resolved);
}

void JsPluginRuntime::cancel() {
    mCancelled.store(true);
    if (mActiveReply) {
        mActiveReply->abort();
    }
}

} // namespace detail

JsEpisodeProvider::JsEpisodeProvider(
    std::shared_ptr<detail::JsPluginRuntime> runtime,
    std::size_t providerIndex, detail::ProviderDescriptor descriptor)
    : mRuntime(std::move(runtime)),
      mProviderIndex(providerIndex),
      mDescriptor(std::move(descriptor)) {}

auto JsEpisodeProvider::key() const -> QString {
    return mRuntime->manifest().id + QLatin1Char('.') + mDescriptor.id;
}

auto JsEpisodeProvider::name() const -> QString { return mDescriptor.name; }

auto JsEpisodeProvider::icon() const -> QUrl { return mDescriptor.icon; }

auto JsEpisodeProvider::generation() const -> std::uint64_t {
    return mRuntime->generation();
}

auto JsEpisodeProvider::ping()
    -> ilias::Task<EpisodeProviderResult<ProviderHealth>> {
    co_return co_await mRuntime->ping(mProviderIndex);
}

auto JsEpisodeProvider::search(EpisodeQuery query)
    -> ilias::Task<EpisodeProviderResult<std::vector<OnlinePlayable>>> {
    co_return co_await mRuntime->search(mProviderIndex, query);
}

auto JsEpisodeProvider::resolve(OnlinePlayable playable)
    -> ilias::Task<EpisodeProviderResult<OnlinePlayable>> {
    co_return co_await mRuntime->resolve(mProviderIndex, playable);
}

void JsEpisodeProvider::cancel() { mRuntime->cancel(); }

auto loadEpisodeProviderPlugin(
    QString packageRoot,
    const std::optional<QString> &configurationOverride,
    std::unique_ptr<QNetworkAccessManager> network)
    -> EpisodeProviderResult<LoadedEpisodePlugin> {
    auto manifest = loadManifest(packageRoot);
    if (!manifest) {
        return ilias::Err(std::move(manifest.error()));
    }
    auto configuration = loadConfiguration(packageRoot, *manifest,
                                           configurationOverride);
    if (!configuration) {
        return ilias::Err(std::move(configuration.error()));
    }
    auto script = readPackageFile(packageRoot, manifest->entry, 2 * 1024 * 1024);
    if (!script) {
        return ilias::Err(std::move(script.error()));
    }

    auto runtime = std::make_shared<detail::JsPluginRuntime>(
        packageRoot, *manifest, *configuration, std::move(network));
    auto descriptors = runtime->initialize(*script);
    if (!descriptors) {
        return ilias::Err(std::move(descriptors.error()));
    }

    std::vector<std::shared_ptr<EpisodeProvider>> providers;
    providers.reserve(descriptors->size());
    for (std::size_t index = 0; index < descriptors->size(); ++index) {
        providers.push_back(std::make_shared<JsEpisodeProvider>(
            runtime, index, descriptors->at(index)));
    }
    return LoadedEpisodePlugin {
        .manifest = std::move(*manifest),
        .configuration = std::move(*configuration),
        .providers = std::move(providers),
    };
}

auto scanEpisodeProviderPlugins(QString providerDirectory,
                                QString providerConfigDirectory,
                                int maximumPackages, bool allowSymlinks,
                                const QStringList &excludedPluginIds)
    -> EpisodePluginScanResult {
    EpisodePluginScanResult result;
    QDir directory(providerDirectory);
    if (!directory.exists()) {
        return result;
    }
    const int limit = std::clamp(maximumPackages, 1, 256);
    const QFileInfoList entries = directory.entryInfoList(
        QDir::Dirs | QDir::Readable | QDir::NoDotAndDotDot,
        QDir::Name | QDir::IgnoreCase);
    QSet<QString> pluginIds(excludedPluginIds.cbegin(),
                            excludedPluginIds.cend());
    int visited = 0;
    for (const QFileInfo &entry : entries) {
        if (++visited > limit) {
            result.issues.push_back({
                .packagePath = directory.absolutePath(),
                .error = episodeProviderError(
                    EpisodeProviderErrorCode::InvalidConfiguration,
                    QStringLiteral("插件目录中的包数量超过 max_packages")),
            });
            break;
        }
        if (entry.isSymLink() && !allowSymlinks) {
            result.issues.push_back({
                .packagePath = entry.absoluteFilePath(),
                .error = episodeProviderError(
                    EpisodeProviderErrorCode::PermissionDenied,
                    QStringLiteral("默认不加载符号链接插件目录")),
            });
            continue;
        }
        const QString packageRoot = entry.canonicalFilePath();
        if (packageRoot.isEmpty()) {
            result.issues.push_back({
                .packagePath = entry.absoluteFilePath(),
                .error = episodeProviderError(
                    EpisodeProviderErrorCode::InvalidManifest,
                    QStringLiteral("无法解析插件包目录")),
            });
            continue;
        }
        auto manifest = loadManifest(packageRoot);
        if (!manifest) {
            result.issues.push_back({
                .packagePath = packageRoot,
                .error = std::move(manifest.error()),
            });
            continue;
        }
        if (pluginIds.contains(manifest->id)) {
            result.issues.push_back({
                .packagePath = packageRoot,
                .error = episodeProviderError(
                    EpisodeProviderErrorCode::DuplicateProvider,
                    QStringLiteral("插件目录包含重复 plugin id：%1")
                        .arg(manifest->id)),
            });
            continue;
        }
        const QString overridePath =
            QDir(providerConfigDirectory)
                .filePath(manifest->id + QStringLiteral(".json"));
        auto loaded = loadEpisodeProviderPlugin(packageRoot, overridePath);
        if (!loaded) {
            result.issues.push_back({
                .packagePath = packageRoot,
                .error = std::move(loaded.error()),
            });
            continue;
        }
        pluginIds.insert(loaded->manifest.id);
        result.plugins.push_back(std::move(*loaded));
    }
    return result;
}

auto builtinYhdmmmPackageRoot() -> QString {
    return QStringLiteral(
        ":/anime-land/plugins/episode-providers/org.anime-land.yhdmmm");
}

void initializeBuiltinEpisodeProviderResources() {
    initializeEpisodeProviderPluginResources();
}

} // namespace anime_land::episode_provider_js
