#pragma once

#include <QNetworkAccessManager>

class QIODevice;
class QNetworkReply;
class QNetworkRequest;

namespace anime_land::qml {

/** Network manager used by Qt Quick image loading. */
class ImageNetworkAccessManager final : public QNetworkAccessManager {
public:
    explicit ImageNetworkAccessManager(QObject *parent = nullptr);

protected:
    auto createRequest(Operation operation, const QNetworkRequest &request,
                       QIODevice *outgoingData) -> QNetworkReply * override;
};

} // namespace anime_land::qml
