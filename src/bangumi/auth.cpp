#include "bangumi/auth.hpp"
#include "bangumi/http_request.hpp"
#include "bangumi/protocol.hpp"
#include "common/log.hpp"

#include <QDateTime>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QTcpSocket>

#include <ilias_qt/network.hpp>
#include <ilias_qt/object.hpp>

#include <sodium.h>

#include <array>
#include <chrono>
#include <tuple>
#include <utility>

namespace anime_land {
namespace {

using namespace std::chrono_literals;

constexpr qsizetype kMaximumCallbackRequestSize = 8 * 1024;
constexpr qsizetype kMaximumTokenResponseSize = 64 * 1024;
constexpr qsizetype kMaximumAuthorizationPageSize = 512 * 1024;
constexpr auto kNetworkTimeout = 30s;

auto isLoopbackHost(const QString &host, QHostAddress &address) -> bool {
    if (host.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0) {
        address = QHostAddress::LocalHost;
        return true;
    }
    if (!address.setAddress(host)) {
        return false;
    }
    return address.isLoopback();
}

auto constantTimeEqual(const QByteArray &left, const QByteArray &right) -> bool {
    return left.size() == right.size() && sodium_memcmp(left.constData(), right.constData(), static_cast<std::size_t>(left.size())) == 0;
}

auto makeState() -> QString {
    std::array<quint32, 8> random {};
    QRandomGenerator::system()->fillRange(random.data(), random.size());
    const QByteArray bytes(reinterpret_cast<const char *>(random.data()), static_cast<qsizetype>(sizeof(random)));
    return QString::fromLatin1(bytes.toHex());
}

auto endpointUrl(const QUrl &base, QStringView path) -> BangumiResult<QUrl> {
    if (!base.isValid() || base.scheme() != QStringLiteral("https") ||
        base.host().isEmpty()) {
        auto message = QStringLiteral("无效的 Bangumi HTTPS Base URL：%1").arg(base.toString());
        return ilias::Err(bangumiError(BangumiErrorCode::InvalidConfiguration, message));
    }
    return base.resolved(QUrl(path.toString()));
}

auto networkError(QNetworkReply &reply, QStringView operation) -> BangumiError {
    if (reply.error() == QNetworkReply::OperationCanceledError) {
        return bangumiError(BangumiErrorCode::Cancelled, QStringLiteral("%1已取消").arg(operation));
    }
    return bangumiError(BangumiErrorCode::NetworkError, QStringLiteral("%1失败：%2").arg(operation, reply.errorString()));
}

auto applicationConfigurationMessage(const BangumiSettings &settings, QStringView reason) -> QString {
    return QStringLiteral("%1。请在 %2 确认应用仍存在、App ID/App Secret 正确，"
                          "且回调地址严格等于 %3")
        .arg(reason, settings.oauth_application_page.toString(), settings.redirect_uri.toString());
}

/**
 * @brief 本地验证 OAuth 相关配置
 *
 * @param settings
 * @return BangumiResult<void>
 */
auto locallyValidateOAuthApplication(const BangumiSettings &settings) -> BangumiResult<void> {
    const QString &clientId = settings.client_id;
    const QString clientSecret = QString::fromStdString(settings.client_secret);
    const auto containsUnsafeWhitespace = [](QStringView value) {
        for (const QChar character : value) {
            if (character.isSpace() || character.category() == QChar::Other_Control) {
                return true;
            }
        }
        return false;
    };
    if (clientId.isEmpty() || clientSecret.isEmpty()) {
        auto message = applicationConfigurationMessage(settings, QStringLiteral("Bangumi App ID/App Secret 尚未配置"));
        return ilias::Err(bangumiError(BangumiErrorCode::InvalidConfiguration, message));
    }
    if (clientId.size() > 512 || clientSecret.size() > 4096 ||
        containsUnsafeWhitespace(clientId) ||
        containsUnsafeWhitespace(clientSecret)) {
        auto message = applicationConfigurationMessage(settings, QStringLiteral("Bangumi App ID/App Secret 含空白、控制字符或长度异常"));
        return ilias::Err(bangumiError(BangumiErrorCode::InvalidConfiguration, message));
    }
    const QUrl &applicationPage = settings.oauth_application_page;
    if (!applicationPage.isValid() ||
        applicationPage.scheme() != QStringLiteral("https") ||
        applicationPage.host().isEmpty()) {
        return ilias::Err(bangumiError(BangumiErrorCode::InvalidConfiguration, QStringLiteral("oauth_application_page 必须是有效的 HTTPS URL")));
    }
    return {};
}

/**
 * @brief 检查Token获取错误
 *
 * @param data
 * @param status
 * @param settings
 * @return std::optional<BangumiError>
 */
auto tokenExchangeConfigurationError(const QByteArray &data, int status, const BangumiSettings &settings) -> std::optional<BangumiError> {
    bangumi_protocol::ApiErrorResponse response;
    QString errorName;
    if (!bangumi_protocol::decode(data, response) && response.error) {
        errorName = response.error->toLower();
    }
    if (status == 400 || status == 401 ||
        errorName == QStringLiteral("invalid_client") ||
        errorName == QStringLiteral("invalid_grant") ||
        data.contains("app_nonexistence")) {
        auto reason = QStringLiteral("Bangumi 拒绝了 OAuth Token 交换（HTTP %1）").arg(status);
        return bangumiError(BangumiErrorCode::InvalidConfiguration, applicationConfigurationMessage(settings, reason));
    }
    return std::nullopt;
}

struct LoginGuard {
    BangumiAuth &auth;
    ~LoginGuard() { auth.cancelLogin(); }
};

} // namespace

namespace detail {

/**
 * @brief 构建 Bangumi 授权 URL
 *
 * @param settings
 * @param state
 * @return BangumiResult<QUrl>
 */
auto buildBangumiAuthorizationUrl(const BangumiSettings &settings, const QString &state) -> BangumiResult<QUrl> {
    auto application = locallyValidateOAuthApplication(settings);
    if (!application) {
        return ilias::Err(std::move(application.error()));
    }
    if (state.isEmpty()) {
        return ilias::Err(bangumiError(BangumiErrorCode::InvalidConfiguration, QStringLiteral("OAuth state 不能为空")));
    }

    auto endpoint = endpointUrl(settings.oauth_base, u"/oauth/authorize");
    if (!endpoint) {
        return ilias::Err(std::move(endpoint.error()));
    }

    const QUrl &redirect = settings.redirect_uri;
    QHostAddress callbackAddress;
    if (!redirect.isValid() || redirect.scheme() != QStringLiteral("http") ||
        redirect.port() <= 0 || redirect.path().isEmpty() ||
        !isLoopbackHost(redirect.host(), callbackAddress)) {
        return ilias::Err(bangumiError(BangumiErrorCode::InvalidConfiguration, QStringLiteral("redirect_uri 必须是带固定端口和路径的 HTTP 回环地址")));
    }

    QUrl url = *endpoint;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("client_id"), settings.client_id);
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("redirect_uri"), redirect.toString());
    query.addQueryItem(QStringLiteral("state"), state);
    url.setQuery(query);
    return url;
}

auto inspectBangumiAuthorizationPage(const QByteArray &response, const BangumiSettings &settings) -> BangumiResult<void> {
    const QByteArray lower = response.toLower();
    if (lower.contains("app_nonexistence") ||
        response.contains(QByteArrayLiteral("应用不存在"))) {
        auto message = applicationConfigurationMessage(settings, QStringLiteral("Bangumi 返回 app_nonexistence，App ID 不存在或应用已删除"));
        return ilias::Err(bangumiError(BangumiErrorCode::InvalidConfiguration, message));
    }
    return {};
}

auto parseBangumiCallbackRequest(const QByteArray &request, const QString &expectedPath,
                                 const QString &expectedState) -> BangumiResult<QString> {
    if (request.size() > kMaximumCallbackRequestSize) {
        return ilias::Err(bangumiError(BangumiErrorCode::InvalidCallback, QStringLiteral("OAuth 回调请求过大")));
    }

    const qsizetype lineEnd = request.indexOf("\r\n");
    if (lineEnd <= 0) {
        return ilias::Err(bangumiError(BangumiErrorCode::InvalidCallback, QStringLiteral("OAuth 回调请求行不完整")));
    }
    const auto parts = request.first(lineEnd).split(' ');
    if (parts.size() != 3 || parts[0] != "GET" ||
        !parts[2].startsWith("HTTP/1.")) {
        return ilias::Err(bangumiError(BangumiErrorCode::InvalidCallback, QStringLiteral("OAuth 回调只接受 HTTP GET")));
    }

    const QUrl target = QUrl::fromEncoded(parts[1], QUrl::StrictMode);
    if (!target.isValid() || target.path() != expectedPath) {
        return ilias::Err(bangumiError(BangumiErrorCode::InvalidCallback, QStringLiteral("OAuth 回调路径不匹配")));
    }

    const QUrlQuery query(target);
    const QString returnedState = query.queryItemValue(QStringLiteral("state"));
    if (!constantTimeEqual(returnedState.toUtf8(), expectedState.toUtf8())) {
        // State is the CSRF boundary for the loopback callback and must be checked
        // before the authorization code is consumed.
        return ilias::Err(bangumiError(BangumiErrorCode::InvalidCallback, QStringLiteral("OAuth state 校验失败")));
    }

    if (query.hasQueryItem(QStringLiteral("error"))) {
        QString description = query.queryItemValue(QStringLiteral("error_description"));
        if (description.isEmpty()) {
            description = query.queryItemValue(QStringLiteral("error"));
        }
        auto message = QStringLiteral("Bangumi 授权未完成：%1").arg(description);
        return ilias::Err(bangumiError(BangumiErrorCode::AuthorizationDenied, message));
    }

    const QString code = query.queryItemValue(QStringLiteral("code"));
    if (code.isEmpty()) {
        return ilias::Err(bangumiError(BangumiErrorCode::InvalidCallback, QStringLiteral("OAuth 回调缺少 code")));
    }
    return code;
}

} // namespace detail

BangumiAuth::BangumiAuth(QNetworkAccessManager &network, BangumiSettings settings, QObject *parent)
    : QObject(parent), mNetwork(network), mSettings(std::move(settings)),
      mCallbackServer(this) {}

void BangumiAuth::setOAuthApplication(const BangumiOAuthApplication &application) {
    mSettings.client_id = application.clientId;
    mSettings.client_secret = application.clientSecret;
}

auto BangumiAuth::login(std::chrono::nanoseconds callback_timeout)
    -> ilias::Task<BangumiResult<BangumiToken>> {
    const auto timeoutSeconds = std::chrono::duration_cast<std::chrono::seconds>(callback_timeout).count();
    AL_LOG_DEBUG("[bangumi.oauth] authorization flow requested timeout_seconds={}", timeoutSeconds);
    if (mActive) {
        AL_LOG_WARN("[bangumi.oauth] authorization flow already active");
        co_return ilias::Err(bangumiError(BangumiErrorCode::InvalidState, QStringLiteral("Bangumi 登录已在进行中")));
    }
    mActive = true;
    LoginGuard guard {*this};

    mExpectedState = makeState();
    auto authorizationUrl = detail::buildBangumiAuthorizationUrl(mSettings, mExpectedState);
    if (!authorizationUrl) {
        AL_LOG_ERROR("[bangumi.oauth] local configuration validation failed code={}", bangumiErrorCodeName(authorizationUrl.error().code));
        co_return ilias::Err(std::move(authorizationUrl.error()));
    }

    emit phaseChanged(BangumiAuthPhase::CheckingConfiguration);

    const QUrl &redirect = mSettings.redirect_uri;
    QHostAddress listenAddress;
    if (!isLoopbackHost(redirect.host(), listenAddress)) {
        AL_LOG_ERROR("[bangumi.oauth] callback host is not loopback");
        co_return ilias::Err(bangumiError(BangumiErrorCode::InvalidConfiguration, QStringLiteral("redirect_uri 不是回环地址")));
    }
    mExpectedCallbackPath = redirect.path();
    if (!mCallbackServer.listen(listenAddress, static_cast<quint16>(redirect.port()))) {
        AL_LOG_ERROR("[bangumi.oauth] callback listener failed port={} socket_error={}",
                     redirect.port(), static_cast<int>(mCallbackServer.serverError()));
        auto message = QStringLiteral("无法监听 OAuth 回调 %1：%2").arg(redirect.authority(), mCallbackServer.errorString());
        co_return ilias::Err(bangumiError(BangumiErrorCode::CallbackListenFailed, message));
    }
    AL_LOG_INFO("[bangumi.oauth] callback listener ready port={}", redirect.port());

    emit phaseChanged(BangumiAuthPhase::OpeningBrowser);
    if (!QDesktopServices::openUrl(*authorizationUrl)) {
        AL_LOG_ERROR("[bangumi.oauth] failed to open authorization page");
        co_return ilias::Err(bangumiError(BangumiErrorCode::BrowserOpenFailed, QStringLiteral("无法打开系统默认浏览器")));
    }
    AL_LOG_INFO("[bangumi.oauth] authorization page opened");

    emit phaseChanged(BangumiAuthPhase::WaitingForCallback);
    AL_LOG_INFO("[bangumi.oauth] waiting for browser callback");
    auto [callback, cancelled, timeout] = co_await ilias::whenAny(
        waitForCallback(), ilias_qt::QSignal(this, &BangumiAuth::cancelRequested),
        ilias::sleep(callback_timeout));

    if (cancelled.has_value()) {
        AL_LOG_INFO("[bangumi.oauth] authorization cancelled");
        co_return ilias::Err(bangumiError(BangumiErrorCode::Cancelled, QStringLiteral("Bangumi 登录已取消")));
    }
    if (timeout.has_value()) {
        AL_LOG_WARN("[bangumi.oauth] browser callback timed out");
        co_return ilias::Err(bangumiError(BangumiErrorCode::CallbackTimeout, QStringLiteral("等待浏览器回调超时")));
    }
    if (!callback.has_value()) {
        AL_LOG_WARN("[bangumi.oauth] callback wait stopped without a result");
        co_return ilias::Err(bangumiError(BangumiErrorCode::Cancelled, QStringLiteral("浏览器回调等待已停止")));
    }
    auto callbackResult = std::move(*callback);
    if (!callbackResult) {
        AL_LOG_WARN("[bangumi.oauth] callback rejected code={}", bangumiErrorCodeName(callbackResult.error().code));
        co_return ilias::Err(std::move(callbackResult.error()));
    }

    mCallbackServer.close();
    AL_LOG_INFO("[bangumi.oauth] callback accepted");
    emit phaseChanged(BangumiAuthPhase::ExchangingToken);
    co_return co_await exchangeCode(*callbackResult, mExpectedState);
}

void BangumiAuth::cancelLogin() {
    if (!mActive) {
        return;
    }
    AL_LOG_INFO("[bangumi.oauth] cancelling active authorization flow");
    // Waking both the signal wait and an active QNetworkReply gives cancellation
    // a deterministic path regardless of the current OAuth phase.
    emit cancelRequested();
    mCallbackServer.close();
    if (mActiveReply) {
        mActiveReply->abort();
    }
    mExpectedState.clear();
    mExpectedCallbackPath.clear();
    mActive = false;
}

auto BangumiAuth::waitForCallback() -> ilias::Task<BangumiResult<QString>> {
    if (!mCallbackServer.hasPendingConnections()) {
        const auto signal = co_await ilias_qt::QSignal(&mCallbackServer, &QTcpServer::newConnection);
        if (!signal) {
            co_return ilias::Err(bangumiError(BangumiErrorCode::Cancelled, QStringLiteral("OAuth 回调服务器已销毁")));
        }
    }

    QTcpSocket *socket = mCallbackServer.nextPendingConnection();
    if (socket == nullptr) {
        AL_LOG_WARN("[bangumi.oauth] callback connection unavailable");
        co_return ilias::Err(bangumiError(BangumiErrorCode::InvalidCallback, QStringLiteral("OAuth 回调连接不可用")));
    }
    struct SocketGuard {
        QTcpSocket *socket;
        ~SocketGuard() { socket->deleteLater(); }
    } socketGuard {socket};

    auto request = co_await readCallbackRequest(*socket);
    if (!request) {
        AL_LOG_WARN("[bangumi.oauth] callback read failed code={}", bangumiErrorCodeName(request.error().code));
        sendBrowserResponse(*socket, 400, QByteArrayLiteral("Authorization callback rejected."));
        co_return ilias::Err(std::move(request.error()));
    }

    auto code = detail::parseBangumiCallbackRequest(*request, mExpectedCallbackPath, mExpectedState);
    if (!code) {
        AL_LOG_WARN("[bangumi.oauth] callback validation failed code={}", bangumiErrorCodeName(code.error().code));
        sendBrowserResponse(*socket, 400, QByteArrayLiteral("Authorization callback rejected."));
        co_return ilias::Err(std::move(code.error()));
    }

    sendBrowserResponse(*socket, 200, QByteArrayLiteral("Bangumi authorization received. You may close this page."));
    co_return *code;
}

auto BangumiAuth::readCallbackRequest(QTcpSocket &socket) -> ilias::Task<BangumiResult<QByteArray>> {
    QByteArray request;
    while (!request.contains("\r\n\r\n")) {
        request.append(socket.readAll());
        if (request.size() > kMaximumCallbackRequestSize) {
            co_return ilias::Err(bangumiError(BangumiErrorCode::InvalidCallback, QStringLiteral("OAuth 回调请求过大")));
        }
        if (request.contains("\r\n\r\n")) {
            break;
        }

        auto [ready, disconnected] = co_await ilias::whenAny(
            ilias_qt::QSignal(&socket, &QIODevice::readyRead),
            ilias_qt::QSignal(&socket, &QAbstractSocket::disconnected));
        if (disconnected.has_value() && !ready.has_value()) {
            request.append(socket.readAll());
            if (!request.contains("\r\n\r\n")) {
                co_return ilias::Err(bangumiError(BangumiErrorCode::InvalidCallback, QStringLiteral("OAuth 回调连接在请求完成前关闭")));
            }
        }
    }
    co_return request;
}

auto BangumiAuth::exchangeCode(const QString &code, const QString &state) -> ilias::Task<BangumiResult<BangumiToken>> {
    auto endpoint = endpointUrl(mSettings.oauth_base, u"/oauth/access_token");
    if (!endpoint) {
        AL_LOG_ERROR("[bangumi.oauth] token endpoint configuration invalid");
        co_return ilias::Err(std::move(endpoint.error()));
    }

    const BangumiTokenExchangeRequest exchange {
        .clientId = mSettings.client_id,
        .clientSecret = QString::fromStdString(mSettings.client_secret),
        .code = code,
        .redirectUri = mSettings.redirect_uri.toString(),
        .state = state,
    };
    const BangumiHttpRequest requestValue {
        .url = *endpoint,
        .headers = {
            .userAgent = mSettings.user_agent,
            .accept = QByteArrayLiteral("application/json"),
            .bearerToken = std::nullopt,
            .contentType = QByteArrayLiteral("application/x-www-form-urlencoded"),
        },
        .body = exchange.toFormData(),
        .timeout = kNetworkTimeout,
    };
    QNetworkRequest request = requestValue.toQt();

    AL_LOG_INFO("[bangumi.oauth] token exchange started");
    QElapsedTimer timer;
    timer.start();
    QNetworkReply *rawReply = mNetwork.post(request, requestValue.body);
    mActiveReply = rawReply;
    auto reply = co_await rawReply;
    if (!reply) {
        AL_LOG_ERROR("[bangumi.oauth] token request could not be created");
        co_return ilias::Err(bangumiError(BangumiErrorCode::NetworkError, QStringLiteral("无法创建 token 请求")));
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = reply->readAll();
    mActiveReply.clear();
    AL_LOG_INFO("[bangumi.oauth] token exchange response status={} bytes={} elapsed_ms={} network_error={}",
                status, data.size(), timer.elapsed(), static_cast<int>(reply->error()));
    if (data.size() > kMaximumTokenResponseSize) {
        AL_LOG_WARN("[bangumi.oauth] token response exceeded size limit");
        co_return ilias::Err(bangumiError(BangumiErrorCode::InvalidResponse, QStringLiteral("Token 响应过大")));
    }
    if (auto configuration = tokenExchangeConfigurationError(data, status, mSettings)) {
        AL_LOG_WARN("[bangumi.oauth] token exchange rejected application configuration");
        co_return ilias::Err(std::move(*configuration));
    }
    if (reply->error() != QNetworkReply::NoError) {
        AL_LOG_WARN("[bangumi.oauth] token exchange network failure error={}",
                    static_cast<int>(reply->error()));
        co_return ilias::Err(networkError(*reply, u"Token 交换"));
    }
    if (status < 200 || status >= 300) {
        AL_LOG_WARN("[bangumi.oauth] token exchange returned unexpected status={}",
                    status);
        auto message = QStringLiteral("Token 交换返回 HTTP %1").arg(status);
        co_return ilias::Err(bangumiError(BangumiErrorCode::NetworkError, message));
    }

    bangumi_protocol::OAuthTokenResponse response;
    if (auto error = bangumi_protocol::decode(data, response)) {
        // The response contains credentials on success, so never include raw data
        // in diagnostics even when JSON parsing fails.
        AL_LOG_WARN("[bangumi.oauth] token response JSON decoding failed");
        auto message = QStringLiteral("Token 响应不是有效 JSON：%1").arg(*error);
        co_return ilias::Err(bangumiError(BangumiErrorCode::InvalidResponse, message));
    }
    if (response.accessToken.isEmpty() || response.refreshToken.isEmpty() ||
        response.tokenType.isEmpty() || response.userId < 0 ||
        response.expiresIn <= 0) {
        AL_LOG_WARN("[bangumi.oauth] token response validation failed");
        co_return ilias::Err(bangumiError(BangumiErrorCode::InvalidResponse, QStringLiteral("Token 响应缺少必需字段或字段类型不正确")));
    }

    AL_LOG_INFO("[bangumi.oauth] token exchange completed");
    co_return BangumiToken {
        .accessToken = std::move(response.accessToken),
        .refreshToken = std::move(response.refreshToken),
        .tokenType = std::move(response.tokenType),
        .scope = std::move(response.scope),
        .userId = response.userId,
        .expiresAt = QDateTime::currentSecsSinceEpoch() + response.expiresIn,
    };
}

void BangumiAuth::sendBrowserResponse(QTcpSocket &socket, int status, const QByteArray &body) {
    QByteArray reason = QByteArrayLiteral("Bad Request");
    if (status == 200) {
        reason = QByteArrayLiteral("OK");
    }
    QByteArray response = QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(status) + ' ' + reason;
    response += QByteArrayLiteral("\r\nContent-Type: text/plain; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: ");
    response += QByteArray::number(body.size()) + QByteArrayLiteral("\r\n\r\n") + body;
    socket.write(response);
    socket.flush();
    socket.disconnectFromHost();
}

} // namespace anime_land
