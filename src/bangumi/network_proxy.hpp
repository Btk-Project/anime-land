#pragma once

#include <QNetworkAccessManager>

#include "bangumi/config.hpp"
#include "common/app_settings.hpp"

namespace anime_land::detail {

/**
 * @brief Apply the optional custom proxy to Bangumi's shared network manager.
 *
 * @return true when a custom proxy was applied; false when proxy_url is empty
 * and the manager's existing/default policy was preserved.
 * @post On invalid configuration, network remains unchanged.
 */
auto configureBangumiNetworkProxy(QNetworkAccessManager &network, const BangumiSettings &settings) -> BangumiResult<bool>;

} // namespace anime_land::detail
