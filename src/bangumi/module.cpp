#include "bangumi/bangumi.hpp"
#include "common/log.hpp"

#include <QDateTime>

#include <utility>

namespace anime_land {

auto bangumiLoginStateName(BangumiLoginState state) -> std::string_view {
  switch (state) {
  case BangumiLoginState::LoggedOut:
    return "logged_out";
  case BangumiLoginState::RestoringSession:
    return "restoring_session";
  case BangumiLoginState::CheckingConfiguration:
    return "checking_configuration";
  case BangumiLoginState::OpeningBrowser:
    return "opening_browser";
  case BangumiLoginState::WaitingForCallback:
    return "waiting_for_callback";
  case BangumiLoginState::ExchangingToken:
    return "exchanging_token";
  case BangumiLoginState::VerifyingUser:
    return "verifying_user";
  case BangumiLoginState::LoggedIn:
    return "logged_in";
  }
  return "unknown";
}

BangumiModule::BangumiModule(BangumiSettings settings,
                             std::unique_ptr<TokenStore> tokenStore,
                             BangumiModuleOptions options, QObject *parent)
    : QObject(parent), mSettings(std::move(settings)),
      mOptions(std::move(options)), mTokenStore(std::move(tokenStore)),
      mNetwork(this), mAuth(mNetwork, mSettings, this),
      mClient(mNetwork, mSettings, this) {
  Q_ASSERT(mTokenStore != nullptr);
  QObject::connect(&mAuth, &BangumiAuth::phaseChanged, this,
                   &BangumiModule::onAuthPhaseChanged);
  AL_LOG_INFO("[bangumi.module] initialized features={} oauth_configured={}",
              mOptions.features.size(), hasOAuthApplication());
}

BangumiModule::~BangumiModule() {
  AL_LOG_DEBUG("[bangumi.module] shutting down");
  mAuth.cancelLogin();
  mClient.cancel();
  if (mToken) {
    clearBangumiToken(*mToken);
  }
}

auto BangumiModule::hasOAuthApplication() const noexcept -> bool {
  return !mSettings.client_id.isEmpty() && !mSettings.client_secret.empty();
}

auto BangumiModule::oauthApplicationGuide() const
    -> BangumiOAuthApplicationGuide {
  return buildBangumiOAuthApplicationGuide(mSettings, mOptions);
}

void BangumiModule::setOAuthApplication(
    const BangumiOAuthApplication &application) {
  mSettings.client_id = application.clientId;
  mSettings.client_secret = application.clientSecret;
  mAuth.setOAuthApplication(application);
}

auto BangumiModule::restoreSession()
    -> ilias::Task<BangumiResult<BangumiUser>> {
  AL_LOG_INFO("[bangumi.session] restore started");
  if (mState == BangumiLoginState::LoggedIn && mUser) {
    AL_LOG_DEBUG("[bangumi.session] restore reused active session");
    co_return *mUser;
  }
  if (!isLogout()) {
    AL_LOG_WARN("[bangumi.session] restore rejected state={}",
                bangumiLoginStateName(mState));
    co_return ilias::Err(
        bangumiError(BangumiErrorCode::InvalidState,
                     QStringLiteral("当前 Bangumi 状态不允许恢复会话")));
  }

  setState(BangumiLoginState::RestoringSession);
  auto stored = co_await mTokenStore->load();
  if (!stored) {
    AL_LOG_ERROR("[bangumi.session] credential load failed code={}",
                 bangumiErrorCodeName(stored.error().code));
    setState(BangumiLoginState::LoggedOut);
    co_return ilias::Err(std::move(stored.error()));
  }
  if (!*stored) {
    AL_LOG_INFO("[bangumi.session] no stored credentials");
    setState(BangumiLoginState::LoggedOut);
    co_return ilias::Err(
        bangumiError(BangumiErrorCode::NotLoggedIn,
                     QStringLiteral("没有可恢复的 Bangumi 凭据")));
  }

  BangumiToken token = std::move(**stored);
  if (token.expiresAt <= QDateTime::currentSecsSinceEpoch()) {
    AL_LOG_WARN("[bangumi.session] stored access token expired");
    clearBangumiToken(token);
    setState(BangumiLoginState::LoggedOut);
    co_return ilias::Err(bangumiError(
        BangumiErrorCode::TokenExpired,
        QStringLiteral("Bangumi Access Token 已过期，请重新登录")));
  }

  setState(BangumiLoginState::VerifyingUser);
  auto user = co_await mClient.getCurrentUser(token);
  if (!user) {
    const BangumiError error = std::move(user.error());
    AL_LOG_WARN("[bangumi.session] credential verification failed code={}",
                bangumiErrorCodeName(error.code));
    if (error.code == BangumiErrorCode::Unauthorized) {
      // An explicit 401/403 proves the persisted credential is unusable. A
      // transient network error must not delete a recoverable refresh token.
      static_cast<void>(co_await mTokenStore->clear());
    }
    clearBangumiToken(token);
    setState(BangumiLoginState::LoggedOut);
    co_return ilias::Err(error);
  }

  if (token.userId != 0 && token.userId != user->id) {
    AL_LOG_ERROR("[bangumi.session] persisted token user mismatch");
    clearBangumiToken(token);
    setState(BangumiLoginState::LoggedOut);
    co_return ilias::Err(
        bangumiError(BangumiErrorCode::InvalidResponse,
                     QStringLiteral("本地 token 用户与 /v0/me 用户不一致")));
  }

  mToken = std::move(token);
  mUser = *user;
  setState(BangumiLoginState::LoggedIn);
  AL_LOG_INFO("[bangumi.session] restore completed");
  co_return *mUser;
}

auto BangumiModule::login() -> ilias::Task<BangumiResult<BangumiUser>> {
  AL_LOG_INFO("[bangumi.auth] login started");
  if (!isLogout()) {
    AL_LOG_WARN("[bangumi.auth] login rejected state={}",
                bangumiLoginStateName(mState));
    co_return ilias::Err(bangumiError(
        BangumiErrorCode::InvalidState,
        QStringLiteral("请先结束当前操作或退出已有 Bangumi 账号")));
  }

  setState(BangumiLoginState::CheckingConfiguration);
  auto token = co_await mAuth.login();
  if (!token) {
    AL_LOG_WARN("[bangumi.auth] login failed code={}",
                bangumiErrorCodeName(token.error().code));
    setState(BangumiLoginState::LoggedOut);
    co_return ilias::Err(std::move(token.error()));
  }
  co_return co_await verifyAndCommit(std::move(*token));
}

auto BangumiModule::logout() -> ilias::Task<BangumiResult<void>> {
  AL_LOG_INFO("[bangumi.session] logout started");
  mAuth.cancelLogin();
  mClient.cancel();

  auto cleared = co_await mTokenStore->clear();
  if (mToken) {
    clearBangumiToken(*mToken);
    mToken.reset();
  }
  mUser.reset();
  setState(BangumiLoginState::LoggedOut);

  if (!cleared) {
    AL_LOG_ERROR("[bangumi.session] credential clear failed code={}",
                 bangumiErrorCodeName(cleared.error().code));
    co_return ilias::Err(std::move(cleared.error()));
  }
  AL_LOG_INFO("[bangumi.session] logout completed");
  co_return BangumiResult<void>{};
}

auto BangumiModule::getCurrentUserCollections(BangumiCollectionQuery query)
    -> ilias::Task<BangumiResult<BangumiUserCollectionsResponse>> {
  AL_LOG_INFO("[bangumi.collections] fetch started limit={} offset={}",
              query.limit, query.offset);
  if (mState != BangumiLoginState::LoggedIn || !mToken || !mUser) {
    AL_LOG_WARN("[bangumi.collections] fetch rejected: not logged in");
    co_return ilias::Err(
        bangumiError(BangumiErrorCode::NotLoggedIn,
                     QStringLiteral("请先登录 Bangumi 再获取用户收藏")));
  }

  const BangumiFeatureDeclaration *feature = nullptr;
  const auto declaration = bangumiUserCollectionsFeature();
  for (const auto &candidate : mOptions.features) {
    if (candidate.id == declaration.id) {
      feature = &candidate;
      break;
    }
  }
  if (feature == nullptr) {
    AL_LOG_ERROR("[bangumi.collections] feature declaration is not registered");
    co_return ilias::Err(bangumiError(
        BangumiErrorCode::InvalidState,
        QStringLiteral("获取用户收藏功能未在 BangumiModuleOptions 中注册")));
  }

  auto result = co_await mClient.getUserCollections(*mToken, mUser->username,
                                                    query, *feature);
  if (!result) {
    AL_LOG_WARN("[bangumi.collections] fetch failed code={}",
                bangumiErrorCodeName(result.error().code));
    co_return ilias::Err(std::move(result.error()));
  }
  AL_LOG_INFO("[bangumi.collections] fetch completed returned={} total={}",
              result->value.data.size(), result->value.total);
  co_return result;
}

void BangumiModule::setState(BangumiLoginState state) {
  if (mState == state) {
    return;
  }
  const auto previous = mState;
  mState = state;
  AL_LOG_INFO("[bangumi.module] state {} -> {}",
              bangumiLoginStateName(previous), bangumiLoginStateName(state));
  emit loginStateChanged(state);
}

void BangumiModule::onAuthPhaseChanged(BangumiAuthPhase phase) {
  switch (phase) {
  case BangumiAuthPhase::CheckingConfiguration:
    setState(BangumiLoginState::CheckingConfiguration);
    break;
  case BangumiAuthPhase::OpeningBrowser:
    setState(BangumiLoginState::OpeningBrowser);
    break;
  case BangumiAuthPhase::WaitingForCallback:
    setState(BangumiLoginState::WaitingForCallback);
    break;
  case BangumiAuthPhase::ExchangingToken:
    setState(BangumiLoginState::ExchangingToken);
    break;
  }
}

auto BangumiModule::verifyAndCommit(BangumiToken token)
    -> ilias::Task<BangumiResult<BangumiUser>> {
  AL_LOG_DEBUG("[bangumi.auth] verifying authenticated user");
  setState(BangumiLoginState::VerifyingUser);
  auto user = co_await mClient.getCurrentUser(token);
  if (!user) {
    AL_LOG_WARN("[bangumi.auth] user verification failed code={}",
                bangumiErrorCodeName(user.error().code));
    clearBangumiToken(token);
    setState(BangumiLoginState::LoggedOut);
    co_return ilias::Err(std::move(user.error()));
  }
  if (token.userId != 0 && token.userId != user->id) {
    AL_LOG_ERROR("[bangumi.auth] token response user mismatch");
    clearBangumiToken(token);
    setState(BangumiLoginState::LoggedOut);
    co_return ilias::Err(
        bangumiError(BangumiErrorCode::InvalidResponse,
                     QStringLiteral("Token 响应用户与 /v0/me 用户不一致")));
  }

  token.userId = user->id;
  auto saved = co_await mTokenStore->save(token);
  if (!saved) {
    // 保存 token 失败，但是不影响登录成功
    AL_LOG_ERROR("[bangumi.auth] token persistence failed code={}; "
                 "session remains active",
                 bangumiErrorCodeName(saved.error().code));
  }

  mToken = std::move(token);
  mUser = *user;
  setState(BangumiLoginState::LoggedIn);
  AL_LOG_INFO("[bangumi.auth] login verified");
  co_return *mUser;
}

auto BangumiModule::isLogout() const noexcept -> bool {
  return mState == BangumiLoginState::LoggedOut;
}

} // namespace anime_land
