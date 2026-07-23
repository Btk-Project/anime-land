#pragma once

#include <QStringView>

#include <ilias/task.hpp>

#include "bangumi/bangumi.hpp"

namespace anime_land {

/** View contract shared by the first CLI and future Qt views. */
class BangumiView {
public:
  virtual ~BangumiView() = default;

  virtual void showState(BangumiLoginState state) = 0;
  virtual void showUser(const BangumiUser &user) = 0;
  virtual void
  showCollections(const BangumiUserCollectionsResponse &response) = 0;
  virtual void
  showSearchResults(const BangumiSubjectSearchResponse &response) = 0;
  virtual void showError(const BangumiError &error) = 0;
  virtual void showMessage(QStringView message) = 0;

  /**
   * Request a required value through the active frontend.
   *
   * The CLI reads stdin asynchronously; a future Qt View can implement this
   * contract with a dialog without moving input policy into the Model.
   */
  virtual auto
  requestOAuthApplication(const BangumiOAuthApplicationGuide &guide)
      -> ilias::Task<BangumiResult<BangumiOAuthApplication>> = 0;
};

} // namespace anime_land
