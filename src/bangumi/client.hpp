#pragma once

#include <QNetworkAccessManager>
#include <QPointer>

#include <ilias/task.hpp>

#include "bangumi/capability.hpp"
#include "bangumi/collection.hpp"
#include "bangumi/config.hpp"
#include "common/app_settings.hpp"

class QNetworkReply;

namespace anime_land {

/** Minimal authenticated Bangumi v0 API client used by the login module. */
class BangumiClient final : public QObject {
public:
  BangumiClient(QNetworkAccessManager &network, BangumiSettings settings,
                QObject *parent = nullptr);

  auto getCurrentUser(const BangumiToken &token)
      -> ilias::Task<BangumiResult<BangumiUser>>;
  auto getUserCollections(const BangumiToken &token, QStringView username,
                          const BangumiCollectionQuery &query,
                          const BangumiFeatureDeclaration &feature)
      -> ilias::Task<BangumiResult<BangumiUserCollectionsResponse>>;
  void cancel();

private:
  QNetworkAccessManager &mNetwork;
  BangumiSettings mSettings;
  QPointer<QNetworkReply> mActiveReply;
};

} // namespace anime_land
