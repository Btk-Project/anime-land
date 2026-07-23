#include "bangumi/client.hpp"
#include "bangumi/http_request.hpp"
#include "bangumi/protocol.hpp"
#include "common/log.hpp"

#include <QElapsedTimer>
#include <QNetworkReply>
#include <QUrl>

#include <ilias_qt/network.hpp>

#include <chrono>
#include <utility>

namespace anime_land {
namespace {

using namespace std::chrono_literals;
constexpr auto kNetworkTimeout = 30s;
constexpr qsizetype kMaximumUserResponseSize = 256 * 1024;
constexpr qsizetype kMaximumCollectionsResponseSize = 4 * 1024 * 1024;

auto responseMentionsMissingPermission(const QByteArray &data) -> bool {
    bangumi_protocol::ApiErrorResponse response;
    if (bangumi_protocol::decode(data, response)) {
        return false;
    }
    const QString text = (response.title.value_or(QString {}) + QLatin1Char(' ') +
                          response.description.value_or(QString {}))
                             .toLower();
    return text.contains(QStringLiteral("permission")) ||
           text.contains(QStringLiteral("scope")) ||
           text.contains(QStringLiteral("forbidden")) ||
           text.contains(QStringLiteral("权限"));
}

} // namespace

BangumiClient::BangumiClient(QNetworkAccessManager &network,
                             BangumiSettings settings, QObject *parent)
    : QObject(parent), mNetwork(network), mSettings(std::move(settings)) {}

auto BangumiClient::getCurrentUser(const BangumiToken &token)
    -> ilias::Task<BangumiResult<BangumiUser>> {
    const QUrl &base = mSettings.bangumi_api;
    if (!base.isValid() || base.scheme() != QStringLiteral("https") ||
        base.host().isEmpty()) {
        AL_LOG_ERROR("[bangumi.http] invalid API base for route=/v0/me");
        co_return ilias::Err(
            bangumiError(BangumiErrorCode::InvalidConfiguration,
                         QStringLiteral("bangumi_api 必须是有效的 HTTPS URL")));
    }
    if (token.accessToken.isEmpty()) {
        AL_LOG_WARN("[bangumi.http] request rejected: empty access token "
                    "route=/v0/me");
        co_return ilias::Err(bangumiError(BangumiErrorCode::NotLoggedIn,
                                          QStringLiteral("Access Token 为空")));
    }

    const BangumiHttpRequest requestValue {
        .url = base.resolved(QUrl(QStringLiteral("/v0/me"))),
        .headers = {.userAgent = mSettings.user_agent,
                    .accept = QByteArrayLiteral("application/json"),
                    .bearerToken = token.accessToken,
                    .contentType = std::nullopt},
        .body = {},
        .timeout = kNetworkTimeout,
    };
    QNetworkRequest request = requestValue.toQt();

    AL_LOG_INFO("[bangumi.http] request started method=GET route=/v0/me");
    QElapsedTimer timer;
    timer.start();
    QNetworkReply *rawReply = mNetwork.get(request);
    mActiveReply = rawReply;
    auto reply = co_await rawReply;
    if (!reply) {
        mActiveReply.clear();
        AL_LOG_ERROR("[bangumi.http] request creation failed route=/v0/me");
        co_return ilias::Err(bangumiError(BangumiErrorCode::NetworkError,
                                          QStringLiteral("无法创建 /v0/me 请求")));
    }

    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    AL_LOG_INFO("[bangumi.http] response received route=/v0/me status={} "
                "elapsed_ms={} network_error={}",
                status, timer.elapsed(), static_cast<int>(reply->error()));
    if (status == 401 || status == 403) {
        mActiveReply.clear();
        AL_LOG_WARN("[bangumi.http] authorization rejected route=/v0/me status={}",
                    status);
        co_return ilias::Err(
            bangumiError(BangumiErrorCode::Unauthorized,
                         QStringLiteral("Bangumi 凭据已失效或没有访问权限")));
    }
    if (reply->error() == QNetworkReply::OperationCanceledError) {
        mActiveReply.clear();
        AL_LOG_INFO("[bangumi.http] request cancelled route=/v0/me");
        co_return ilias::Err(bangumiError(BangumiErrorCode::Cancelled,
                                          QStringLiteral("/v0/me 请求已取消")));
    }
    if (reply->error() != QNetworkReply::NoError) {
        const QString error = reply->errorString();
        mActiveReply.clear();
        AL_LOG_WARN("[bangumi.http] network failure route=/v0/me error={}",
                    static_cast<int>(reply->error()));
        co_return ilias::Err(
            bangumiError(BangumiErrorCode::NetworkError,
                         QStringLiteral("/v0/me 请求失败：%1").arg(error)));
    }
    if (status < 200 || status >= 300) {
        mActiveReply.clear();
        AL_LOG_WARN("[bangumi.http] unexpected response route=/v0/me status={}",
                    status);
        co_return ilias::Err(
            bangumiError(BangumiErrorCode::NetworkError,
                         QStringLiteral("/v0/me 返回 HTTP %1").arg(status)));
    }

    const QByteArray data = reply->readAll();
    mActiveReply.clear();
    if (data.size() > kMaximumUserResponseSize) {
        AL_LOG_WARN("[bangumi.http] response exceeded size limit route=/v0/me "
                    "bytes={}",
                    data.size());
        co_return ilias::Err(bangumiError(BangumiErrorCode::InvalidResponse,
                                          QStringLiteral("/v0/me 响应过大")));
    }

    BangumiUser user;
    if (auto error = bangumi_protocol::decode(data, user)) {
        AL_LOG_WARN("[bangumi.http] response decode failed route=/v0/me");
        co_return ilias::Err(bangumiError(
            BangumiErrorCode::InvalidResponse,
            QStringLiteral("/v0/me 响应不是有效 JSON：%1").arg(*error)));
    }
    if (user.id <= 0 || user.username.isEmpty()) {
        AL_LOG_WARN("[bangumi.http] response validation failed route=/v0/me");
        co_return ilias::Err(
            bangumiError(BangumiErrorCode::InvalidResponse,
                         QStringLiteral("/v0/me 缺少必需字段或字段类型不正确")));
    }
    AL_LOG_INFO("[bangumi.http] request completed route=/v0/me bytes={}",
                data.size());
    co_return user;
}

auto BangumiClient::getUserCollections(const BangumiToken &token,
                                       QStringView username,
                                       const BangumiCollectionQuery &query,
                                       const BangumiFeatureDeclaration &feature)
    -> ilias::Task<BangumiResult<BangumiUserCollectionsResponse>> {
    if (token.accessToken.isEmpty()) {
        AL_LOG_WARN("[bangumi.http] request rejected: empty access token "
                    "route=/v0/users/{{user}}/collections");
        co_return ilias::Err(bangumiError(BangumiErrorCode::NotLoggedIn,
                                          QStringLiteral("Access Token 为空")));
    }
    auto url = detail::buildBangumiUserCollectionsUrl(mSettings, username, query);
    if (!url) {
        AL_LOG_ERROR("[bangumi.http] request validation failed "
                     "route=/v0/users/{{user}}/collections code={}",
                     bangumiErrorCodeName(url.error().code));
        co_return ilias::Err(std::move(url.error()));
    }

    const BangumiHttpRequest requestValue {
        .url = *url,
        .headers = {.userAgent = mSettings.user_agent,
                    .accept = QByteArrayLiteral("application/json"),
                    .bearerToken = token.accessToken,
                    .contentType = std::nullopt},
        .body = {},
        .timeout = kNetworkTimeout,
    };
    QNetworkRequest request = requestValue.toQt();

    AL_LOG_INFO("[bangumi.http] request started method=GET "
                "route=/v0/users/{{user}}/collections limit={} offset={}",
                query.limit, query.offset);
    QElapsedTimer timer;
    timer.start();
    QNetworkReply *rawReply = mNetwork.get(request);
    mActiveReply = rawReply;
    auto reply = co_await rawReply;
    if (!reply) {
        mActiveReply.clear();
        AL_LOG_ERROR("[bangumi.http] request creation failed "
                     "route=/v0/users/{{user}}/collections");
        co_return ilias::Err(bangumiError(BangumiErrorCode::NetworkError,
                                          QStringLiteral("无法创建用户收藏请求")));
    }

    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = reply->readAll();
    mActiveReply.clear();
    AL_LOG_INFO("[bangumi.http] response received "
                "route=/v0/users/{{user}}/collections status={} bytes={} "
                "elapsed_ms={} network_error={}",
                status, data.size(), timer.elapsed(),
                static_cast<int>(reply->error()));
    if (data.size() > kMaximumCollectionsResponseSize) {
        AL_LOG_WARN("[bangumi.http] response exceeded size limit "
                    "route=/v0/users/{{user}}/collections bytes={}",
                    data.size());
        co_return ilias::Err(bangumiError(BangumiErrorCode::InvalidResponse,
                                          QStringLiteral("用户收藏响应过大")));
    }
    if (status == 403 ||
        (status == 401 && responseMentionsMissingPermission(data))) {
        AL_LOG_WARN("[bangumi.http] capability missing feature={} status={}",
                    feature.id, status);
        co_return ilias::Err(missingBangumiCapabilityError(
            BangumiCapability::CollectionRead, feature, mSettings));
    }
    if (status == 401) {
        AL_LOG_WARN("[bangumi.http] authorization rejected "
                    "route=/v0/users/{{user}}/collections");
        co_return ilias::Err(
            bangumiError(BangumiErrorCode::Unauthorized,
                         QStringLiteral("Bangumi 凭据已失效，请重新登录")));
    }
    if (reply->error() == QNetworkReply::OperationCanceledError) {
        AL_LOG_INFO("[bangumi.http] request cancelled "
                    "route=/v0/users/{{user}}/collections");
        co_return ilias::Err(bangumiError(BangumiErrorCode::Cancelled,
                                          QStringLiteral("用户收藏请求已取消")));
    }
    if (reply->error() != QNetworkReply::NoError) {
        AL_LOG_WARN("[bangumi.http] network failure "
                    "route=/v0/users/{{user}}/collections error={}",
                    static_cast<int>(reply->error()));
        co_return ilias::Err(bangumiError(
            BangumiErrorCode::NetworkError,
            QStringLiteral("用户收藏请求失败：%1").arg(reply->errorString())));
    }
    if (status < 200 || status >= 300) {
        AL_LOG_WARN("[bangumi.http] unexpected response "
                    "route=/v0/users/{{user}}/collections status={}",
                    status);
        co_return ilias::Err(
            bangumiError(BangumiErrorCode::NetworkError,
                         QStringLiteral("用户收藏请求返回 HTTP %1").arg(status)));
    }
    auto parsed = detail::parseBangumiUserCollectionsResponse(data);
    if (!parsed) {
        AL_LOG_WARN("[bangumi.http] response validation failed "
                    "route=/v0/users/{{user}}/collections");
        co_return ilias::Err(std::move(parsed.error()));
    }
    AL_LOG_INFO("[bangumi.http] request completed "
                "route=/v0/users/{{user}}/collections returned={} total={}",
                parsed->data.size(), parsed->total);
    co_return BangumiUserCollectionsResponse {
        .value = std::move(parsed).value(),
#if defined(QT_DEBUG)
        .rawBody = data,
#else
        .rawBody = {},
#endif
    };
}

void BangumiClient::cancel() {
    if (mActiveReply) {
        AL_LOG_INFO("[bangumi.http] cancelling active request");
        mActiveReply->abort();
    }
}

} // namespace anime_land
