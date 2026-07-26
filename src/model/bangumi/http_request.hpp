/**
 * @file http_request.hpp
 * @brief Bangumi HTTP 请求、请求头和表单 body 的强类型边界。
 *
 * @details
 * 业务/认证代码先构造普通 C++ 值，再在调用 QNetworkAccessManager 前一次性
 * 转换成 QNetworkRequest。转换完成后不会再散落地增删 header。
 */
#pragma once

#include "common/qt_version.hpp"

#include <QByteArray>
#include <QNetworkRequest>
#include <QString>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <chrono>
#include <limits>
#include <optional>

namespace anime_land {

/**
 * @brief 所有 Bangumi HTTP 请求共享的 header 协议对象。
 *
 * @par 前置条件
 * userAgent 应为非空且可安全写入 HTTP header；bearerToken 只保存 token
 * 本体，不得包含 "Bearer " 前缀或 CR/LF；contentType 若存在必须为合法 MIME。
 *
 * @par 后果
 * toQt() 总是设置 Accept 和 User-Agent，只在 optional 有值时设置
 * Authorization/Content-Type。对象本身不执行网络操作。
 */
struct BangumiRequestHeaders {
    QString userAgent;
    QByteArray accept = QByteArrayLiteral("application/json");
    std::optional<QString> bearerToken;
    std::optional<QByteArray> contentType;

    /**
     * @brief 把 header 物化到 Qt 请求。
     * @pre 成员满足本类说明中的 header 合法性条件。
     * @post 不修改本对象；覆盖 request 中的同名 header，bearer token 被精确
     * 添加一次 "Bearer " 前缀。
     */
    void applyTo(QNetworkRequest &request) const {
        request.setRawHeader(QByteArrayLiteral("Accept"), accept);
        request.setRawHeader(QByteArrayLiteral("User-Agent"), userAgent.toUtf8());
        if (bearerToken) {
            request.setRawHeader(QByteArrayLiteral("Authorization"), QStringLiteral("Bearer %1").arg(*bearerToken).toUtf8());
        }
        if (contentType) {
            request.setRawHeader(QByteArrayLiteral("Content-Type"), *contentType);
        }
    }
};

/**
 * @brief 进入 QNetworkAccessManager 前的完整请求值。
 *
 * @par 前置条件
 * url 必须由对应 endpoint 的业务校验通过；timeout 必须为正。body 是否为空
 * 由具体 HTTP method/endpoint 决定。
 *
 * @par 后果
 * toQt() 只生成 QNetworkRequest；不会发送请求，也不会复制/消费 body。
 */
struct BangumiHttpRequest {
    QUrl url;
    BangumiRequestHeaders headers;
    QByteArray body;
    std::chrono::milliseconds timeout {30'000};

    /**
     * @brief 生成带完整 headers 和 transfer timeout 的 QNetworkRequest。
     * @pre url、headers、timeout 满足本协议对象的前置条件。
     * @return 可传给 QNetworkAccessManager 的独立请求对象。
     * @post 本对象及 body 保持不变；调用方仍明确选择 GET/POST 等 method。
     */
    auto toQt() const -> QNetworkRequest {
        QNetworkRequest request(url);
        headers.applyTo(request);
        using TimeoutRep = std::chrono::milliseconds::rep;
        const TimeoutRep milliseconds = std::clamp(timeout.count(), TimeoutRep {1}, static_cast<TimeoutRep>(std::numeric_limits<int>::max()));
        request.setTransferTimeout(static_cast<int>(milliseconds));
        return request;
    }
};

/**
 * @brief OAuth authorization-code token 交换请求的协议对象。
 *
 * @par 前置条件
 * 五个字段必须经过 OAuth 配置/回调验证，且 clientSecret 不得被记录到日志。
 *
 * @par 后果
 * toFormData() 生成 application/x-www-form-urlencoded body；该类型不负责设置
 * Content-Type，也不发送网络请求。
 */
struct BangumiTokenExchangeRequest {
    QString clientId;
    QString clientSecret;
    QString code;
    QString redirectUri;
    QString state;

    /**
     * @brief 按 Bangumi OAuth endpoint 的字段名编码表单。
     * @pre 所有字段均为经过验证的 QString；调用者负责限制其最大长度。
     * @return 完整的 percent-encoded UTF-8 表单 body。
     * @post 不修改本对象；返回值拥有数据，且不得写入诊断日志。
     */
    auto toFormData() const -> QByteArray {
        QUrlQuery form;
        form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
        form.addQueryItem(QStringLiteral("client_id"), clientId);
        form.addQueryItem(QStringLiteral("client_secret"), clientSecret);
        form.addQueryItem(QStringLiteral("code"), code);
        form.addQueryItem(QStringLiteral("redirect_uri"), redirectUri);
        form.addQueryItem(QStringLiteral("state"), state);
        return form.query(QUrl::FullyEncoded).toUtf8();
    }
};

} // namespace anime_land
