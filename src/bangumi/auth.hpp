#pragma once

#include <QNetworkAccessManager>
#include <QPointer>
#include <QTcpServer>
#include <QUrl>

#include <ilias/task.hpp>

#include "bangumi/config.hpp"
#include "common/app_settings.hpp"

#include <chrono>

class QNetworkReply;
class QTcpSocket;

namespace anime_land {

using namespace std::chrono_literals;

enum class BangumiAuthPhase {
    CheckingConfiguration,
    OpeningBrowser,
    WaitingForCallback,
    ExchangingToken,
};

namespace detail {

auto buildBangumiAuthorizationUrl(const BangumiSettings &settings, const QString &state) -> BangumiResult<QUrl>;
auto inspectBangumiAuthorizationPage(const QByteArray &response, const BangumiSettings &settings) -> BangumiResult<void>;
auto parseBangumiCallbackRequest(const QByteArray &request, const QString &expectedPath,
                                 const QString &expectedState) -> BangumiResult<QString>;

} // namespace detail

/**
 * OAuth authorization-code flow implemented with Qt objects awaited by Ilias.
 *
 * Completion is returned only through Task. phaseChanged is progress telemetry
 * for the Presenter and is intentionally not a second completion API.
 */
class BangumiAuth final : public QObject {
    Q_OBJECT

public:
    BangumiAuth(QNetworkAccessManager &network, BangumiSettings settings, QObject *parent = nullptr);

    void setOAuthApplication(const BangumiOAuthApplication &application);
    auto login(std::chrono::nanoseconds callback_timeout = 120s) -> ilias::Task<BangumiResult<BangumiToken>>;
    void cancelLogin();

signals:
    void phaseChanged(anime_land::BangumiAuthPhase phase);

private:
    auto waitForCallback() -> ilias::Task<BangumiResult<QString>>;
    auto readCallbackRequest(QTcpSocket &socket) -> ilias::Task<BangumiResult<QByteArray>>;
    auto exchangeCode(const QString &code, const QString &state) -> ilias::Task<BangumiResult<BangumiToken>>;
    void sendBrowserResponse(QTcpSocket &socket, int status, const QByteArray &body);

signals:
    void cancelRequested();

private:
    QNetworkAccessManager &mNetwork;
    BangumiSettings mSettings;
    QTcpServer mCallbackServer;
    QPointer<QNetworkReply> mActiveReply;
    QString mExpectedState;
    QString mExpectedCallbackPath;
    bool mActive = false;
};

} // namespace anime_land

Q_DECLARE_METATYPE(anime_land::BangumiAuthPhase)
