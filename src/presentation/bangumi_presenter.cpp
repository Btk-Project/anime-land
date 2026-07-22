#include "presentation/bangumi_presenter.hpp"

#include <QObject>

#include <utility>

namespace anime_land {
namespace {

auto collectionQuery(const cli::CollectionsCommand &command)
    -> BangumiResult<BangumiCollectionQuery> {
  BangumiCollectionQuery query{
      .subjectType = std::nullopt,
      .collectionType = std::nullopt,
      .limit = command.limit,
      .offset = command.offset,
  };

  if (command.subjectType == "book") {
    query.subjectType = BangumiSubjectType::Book;
  } else if (command.subjectType == "anime") {
    query.subjectType = BangumiSubjectType::Anime;
  } else if (command.subjectType == "music") {
    query.subjectType = BangumiSubjectType::Music;
  } else if (command.subjectType == "game") {
    query.subjectType = BangumiSubjectType::Game;
  } else if (command.subjectType == "real") {
    query.subjectType = BangumiSubjectType::Real;
  } else if (command.subjectType != "all") {
    return ilias::Err(
        bangumiError(BangumiErrorCode::InvalidConfiguration,
                     QStringLiteral("未知的条目类型：%1")
                         .arg(QString::fromStdString(command.subjectType))));
  }

  if (command.collectionType == "wish") {
    query.collectionType = BangumiCollectionType::Wish;
  } else if (command.collectionType == "done") {
    query.collectionType = BangumiCollectionType::Done;
  } else if (command.collectionType == "doing") {
    query.collectionType = BangumiCollectionType::Doing;
  } else if (command.collectionType == "on-hold") {
    query.collectionType = BangumiCollectionType::OnHold;
  } else if (command.collectionType == "dropped") {
    query.collectionType = BangumiCollectionType::Dropped;
  } else if (command.collectionType != "all") {
    return ilias::Err(
        bangumiError(BangumiErrorCode::InvalidConfiguration,
                     QStringLiteral("未知的收藏状态：%1")
                         .arg(QString::fromStdString(command.collectionType))));
  }
  if (query.limit < 1 || query.limit > 50 || query.offset < 0) {
    return ilias::Err(bangumiError(
        BangumiErrorCode::InvalidConfiguration,
        QStringLiteral("--limit 必须为 1..50，--offset 不能为负")));
  }
  return query;
}

} // namespace

BangumiPresenter::BangumiPresenter(BangumiModule &module, BangumiView &view,
                                   OAuthApplicationSaver applicationSaver)
    : mModule(module), mView(view),
      mApplicationSaver(std::move(applicationSaver)) {
  mStateConnection = QObject::connect(
      &mModule, &BangumiModule::loginStateChanged,
      [this](BangumiLoginState state) { mView.showState(state); });
}

BangumiPresenter::~BangumiPresenter() { QObject::disconnect(mStateConnection); }

auto BangumiPresenter::run(const cli::LoginCommand &) -> ilias::Task<int> {
  auto pendingApplication = co_await ensureOAuthApplication();
  if (!pendingApplication) {
    mView.showError(pendingApplication.error());
    co_return exitCode(pendingApplication.error());
  }

  auto login = co_await mModule.login();
  if (!login) {
    if (*pendingApplication) {
      clearBangumiOAuthApplication(**pendingApplication);
    }
    co_return present(std::move(login));
  }

  if (*pendingApplication) {
    if (!mApplicationSaver) {
      clearBangumiOAuthApplication(**pendingApplication);
      const auto error =
          bangumiError(BangumiErrorCode::InvalidConfiguration,
                       QStringLiteral("未配置 OAuth 应用参数保存器"));
      mView.showError(error);
      co_return exitCode(error);
    }
    auto saved = co_await mApplicationSaver(**pendingApplication);
    clearBangumiOAuthApplication(**pendingApplication);
    if (!saved) {
      mView.showError(saved.error());
      co_return exitCode(saved.error());
    }
  }
  co_return present(std::move(login));
}

auto BangumiPresenter::run(const cli::StatusCommand &) -> ilias::Task<int> {
  co_return present(co_await mModule.restoreSession());
}

auto BangumiPresenter::run(const cli::LogoutCommand &) -> ilias::Task<int> {
  co_return present(co_await mModule.logout(), u"Bangumi credentials cleared");
}

auto BangumiPresenter::run(const cli::CollectionsCommand &command)
    -> ilias::Task<int> {
  auto query = collectionQuery(command);
  if (!query) {
    mView.showError(query.error());
    co_return exitCode(query.error());
  }
  auto restored = co_await mModule.restoreSession();
  if (!restored) {
    mView.showError(restored.error());
    co_return exitCode(restored.error());
  }
  co_return present(co_await mModule.getCurrentUserCollections(*query));
}

auto BangumiPresenter::ensureOAuthApplication()
    -> ilias::Task<BangumiResult<std::optional<BangumiOAuthApplication>>> {
  if (mModule.hasOAuthApplication()) {
    co_return std::optional<BangumiOAuthApplication>{};
  }

  mView.showMessage(u"首次登录需要你自己的 Bangumi OAuth 应用参数；验证成功后 "
                    u"client_secret 会加密写入配置。无需退出或重新启动。");
  auto application =
      co_await mView.requestOAuthApplication(mModule.oauthApplicationGuide());
  if (!application) {
    co_return ilias::Err(std::move(application.error()));
  }
  mModule.setOAuthApplication(*application);
  co_return std::optional<BangumiOAuthApplication>{std::move(*application)};
}

auto BangumiPresenter::present(BangumiResult<BangumiUser> result) -> int {
  if (!result) {
    mView.showError(result.error());
    return exitCode(result.error());
  }
  mView.showUser(*result);
  return 0;
}

auto BangumiPresenter::present(BangumiResult<void> result,
                               QStringView successMessage) -> int {
  if (!result) {
    mView.showError(result.error());
    return exitCode(result.error());
  }
  mView.showMessage(successMessage);
  return 0;
}

auto BangumiPresenter::present(
    BangumiResult<BangumiUserCollectionsResponse> result) -> int {
  if (!result) {
    mView.showError(result.error());
    return exitCode(result.error());
  }
  mView.showCollections(*result);
  return 0;
}

auto BangumiPresenter::exitCode(const BangumiError &error) -> int {
  switch (error.code) {
  case BangumiErrorCode::InvalidConfiguration:
  case BangumiErrorCode::InvalidState:
    return 2;
  case BangumiErrorCode::CredentialStoreError:
  case BangumiErrorCode::UnsupportedCredentialStore:
    return 4;
  default:
    return 3;
  }
}

} // namespace anime_land
