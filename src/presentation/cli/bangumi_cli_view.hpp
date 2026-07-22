#pragma once

#include "presentation/bangumi_view.hpp"

namespace anime_land::cli {

/** Text-only MVP View. Sensitive input is accepted but never printed. */
class BangumiCliView final : public BangumiView {
public:
  void showState(BangumiLoginState state) override;
  void showUser(const BangumiUser &user) override;
  void showCollections(const BangumiUserCollectionsResponse &response) override;
  void showError(const BangumiError &error) override;
  void showMessage(QStringView message) override;
  auto requestOAuthApplication(const BangumiOAuthApplicationGuide &guide)
      -> ilias::Task<BangumiResult<BangumiOAuthApplication>> override;
};

} // namespace anime_land::cli
