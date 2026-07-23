#pragma once

#include <QNetworkAccessManager>
#include <QObject>

#include <ilias/task.hpp>

#include "bangumi/auth.hpp"
#include "bangumi/client.hpp"
#include "bangumi/config.hpp"
#include "common/app_settings.hpp"

#include <memory>
#include <optional>

namespace anime_land {

enum class BangumiLoginState {
  LoggedOut,
  RestoringSession,
  CheckingConfiguration,
  OpeningBrowser,
  WaitingForCallback,
  ExchangingToken,
  VerifyingUser,
  LoggedIn,
};

auto bangumiLoginStateName(BangumiLoginState state) -> std::string_view;

/**
 * Model facade for the Bangumi login lifecycle.
 *
 * The object and all Qt collaborators stay on their creation thread. Operation
 * completion is returned by Task; loginStateChanged only reports progress.
 */
class BangumiModule final : public QObject {
  Q_OBJECT

public:
  BangumiModule(BangumiSettings settings,
                std::unique_ptr<TokenStore> tokenStore,
                BangumiModuleOptions options = {}, QObject *parent = nullptr);
  ~BangumiModule() override;

  auto restoreSession() -> ilias::Task<BangumiResult<BangumiUser>>;
  auto login() -> ilias::Task<BangumiResult<BangumiUser>>;
  auto logout() -> ilias::Task<BangumiResult<void>>;
  auto searchSubjects(BangumiSubjectSearchQuery query)
      -> ilias::Task<BangumiResult<BangumiSubjectSearchResponse>>;
  auto getCurrentUserCollections(BangumiCollectionQuery query = {})
      -> ilias::Task<BangumiResult<BangumiUserCollectionsResponse>>;

  auto hasOAuthApplication() const noexcept -> bool;
  auto oauthApplicationGuide() const -> BangumiOAuthApplicationGuide;
  auto enabledFeatures() const noexcept
      -> const std::vector<BangumiFeatureDeclaration> & {
    return mOptions.features;
  }
  void setOAuthApplication(const BangumiOAuthApplication &application);
  auto loginState() const noexcept -> BangumiLoginState { return mState; }
  auto currentUser() const -> std::optional<BangumiUser> { return mUser; }

signals:
  void loginStateChanged(anime_land::BangumiLoginState state);

private:
  void setState(BangumiLoginState state);
  void onAuthPhaseChanged(BangumiAuthPhase phase);
  auto verifyAndCommit(BangumiToken token)
      -> ilias::Task<BangumiResult<BangumiUser>>;
  auto isLogout() const noexcept -> bool;

  BangumiSettings mSettings;
  BangumiModuleOptions mOptions;
  std::unique_ptr<TokenStore> mTokenStore;
  QNetworkAccessManager mNetwork;
  std::optional<BangumiError> mNetworkConfigurationError;
  BangumiAuth mAuth;
  BangumiClient mClient;
  BangumiLoginState mState = BangumiLoginState::LoggedOut;
  std::optional<BangumiToken> mToken;
  std::optional<BangumiUser> mUser;
};

} // namespace anime_land

Q_DECLARE_METATYPE(anime_land::BangumiLoginState)
