#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

namespace anime_land::episode_provider_js {

class HtmlBridge final : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    Q_INVOKABLE QVariantList queryAll(const QString &source,
                                      const QString &xpath,
                                      const QVariantMap &fields) const;
};

} // namespace anime_land::episode_provider_js
