#include "bangumi/network_proxy.hpp"

#include <QByteArrayView>
#include <QNetworkProxy>
#include <QStringDecoder>

#include <cstdint>
#include <utility>

namespace anime_land::detail {
namespace {

auto invalidProxy(QString message) -> BangumiResult<bool> {
    return ilias::Err(
        bangumiError(BangumiErrorCode::InvalidConfiguration, std::move(message)));
}

} // namespace

auto configureBangumiNetworkProxy(QNetworkAccessManager &network,
                                  const BangumiSettings &settings)
    -> BangumiResult<bool> {
    const QUrl &url = settings.proxy_url;
    if (url.isEmpty()) {
        if (!settings.proxy_username.isEmpty() || !settings.proxy_password.empty()) {
            return invalidProxy(
                QStringLiteral("已配置 Bangumi 代理凭据，但 proxy_url 为空"));
        }
        return false;
    }

    if (!url.isValid() || url.host().isEmpty()) {
        return invalidProxy(QStringLiteral("Bangumi proxy_url 必须包含有效主机名"));
    }
    if (!url.userName().isEmpty() || !url.password().isEmpty()) {
        return invalidProxy(QStringLiteral(
            "请使用 proxy_username 和 proxy_password 配置 Bangumi 代理凭据"));
    }
    if ((!url.path().isEmpty() && url.path() != QStringLiteral("/")) ||
        url.hasQuery() || url.hasFragment()) {
        return invalidProxy(
            QStringLiteral("Bangumi proxy_url 不能包含路径、查询参数或片段"));
    }
    if (settings.proxy_username.isEmpty() && !settings.proxy_password.empty()) {
        return invalidProxy(
            QStringLiteral("Bangumi proxy_password 需要同时配置 proxy_username"));
    }

    const QString scheme = url.scheme().toLower();
    QNetworkProxy::ProxyType type;
    int defaultPort = 0;
    if (scheme == QStringLiteral("http")) {
        type = QNetworkProxy::HttpProxy;
        defaultPort = 80;
    }
    else if (scheme == QStringLiteral("socks5")) {
        type = QNetworkProxy::Socks5Proxy;
        defaultPort = 1080;
    }
    else {
        return invalidProxy(
            QStringLiteral("Bangumi proxy_url 仅支持 http:// 或 socks5://"));
    }

    const int port = url.port(defaultPort);
    if (port <= 0 || port > 65'535) {
        return invalidProxy(QStringLiteral("Bangumi proxy_url 端口无效"));
    }

    const QByteArrayView encodedPassword(settings.proxy_password.data(),
                                         static_cast<qsizetype>(
                                             settings.proxy_password.size()));
    QStringDecoder passwordDecoder(QStringDecoder::Utf8);
    const QString password = passwordDecoder(encodedPassword);
    if (passwordDecoder.hasError()) {
        return invalidProxy(
            QStringLiteral("Bangumi proxy_password 必须是有效 UTF-8"));
    }

    network.setProxy(QNetworkProxy(type, url.host(),
                                   static_cast<std::uint16_t>(port),
                                   settings.proxy_username, password));
    return true;
}

} // namespace anime_land::detail
