#include "view/qml/image_network_access_manager.hpp"

#include <QNetworkReply>
#include <QNetworkRequest>

namespace anime_land::qml {

ImageNetworkAccessManager::ImageNetworkAccessManager(QObject *parent)
    : QNetworkAccessManager(parent) {}

auto ImageNetworkAccessManager::createRequest(
    Operation operation, const QNetworkRequest &request,
    QIODevice *outgoingData) -> QNetworkReply * {
    auto compatibleRequest = request;

    // A burst of Bangumi cover requests can otherwise share one HTTP/2
    // connection. Qt treats a late DATA frame for a closed stream as a
    // connection error, which then fails the remaining covers on that
    // connection. This manager is dedicated to QML image traffic, so keep the
    // API/OAuth managers on their default protocol while using HTTP/1.1 here.
    compatibleRequest.setAttribute(QNetworkRequest::Http2DirectAttribute,
                                   false);
    compatibleRequest.setAttribute(QNetworkRequest::Http2AllowedAttribute,
                                   false);
    return QNetworkAccessManager::createRequest(operation, compatibleRequest,
                                                outgoingData);
}

} // namespace anime_land::qml
