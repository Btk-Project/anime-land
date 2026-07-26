#include "presentation/bangumi/bangumi_presenter.hpp"

#include <QObject>

#include <utility>

namespace anime_land {
namespace {

auto presentError(BangumiView &view, BangumiError error)
    -> BangumiResult<void> {
    view.showError(error);
    return ilias::Err(std::move(error));
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

auto BangumiPresenter::login() -> ilias::Task<BangumiResult<void>> {
    auto pendingApplication = co_await ensureOAuthApplication();
    if (!pendingApplication) {
        co_return presentError(mView, std::move(pendingApplication.error()));
    }

    auto loginResult = co_await mModule.login();
    if (!loginResult) {
        if (*pendingApplication) {
            clearBangumiOAuthApplication(**pendingApplication);
        }
        co_return present(std::move(loginResult));
    }

    if (*pendingApplication) {
        if (!mApplicationSaver) {
            clearBangumiOAuthApplication(**pendingApplication);
            co_return presentError(
                mView, bangumiError(BangumiErrorCode::InvalidConfiguration,
                                    QStringLiteral("未配置 OAuth 应用参数保存器")));
        }
        auto saved = co_await mApplicationSaver(**pendingApplication);
        clearBangumiOAuthApplication(**pendingApplication);
        if (!saved) {
            co_return presentError(mView, std::move(saved.error()));
        }
    }
    co_return present(std::move(loginResult));
}

auto BangumiPresenter::restoreSession() -> ilias::Task<BangumiResult<void>> {
    co_return present(co_await mModule.restoreSession());
}

auto BangumiPresenter::logout() -> ilias::Task<BangumiResult<void>> {
    co_return present(co_await mModule.logout(), u"Bangumi credentials cleared");
}

auto BangumiPresenter::getCollections(BangumiCollectionQuery query)
    -> ilias::Task<BangumiResult<void>> {
    auto restored = co_await mModule.restoreSession();
    if (!restored) {
        co_return presentError(mView, std::move(restored.error()));
    }
    co_return present(
        co_await mModule.getCurrentUserCollections(std::move(query)));
}

auto BangumiPresenter::searchSubjects(BangumiSubjectSearchQuery query)
    -> ilias::Task<BangumiResult<void>> {
    // Reuse a valid saved account when one is available. Failure to restore is
    // deliberately non-fatal because subject search is a public endpoint.
    static_cast<void>(co_await mModule.restoreSession());
    co_return present(co_await mModule.searchSubjects(std::move(query)));
}

auto BangumiPresenter::ensureOAuthApplication()
    -> ilias::Task<BangumiResult<std::optional<BangumiOAuthApplication>>> {
    if (mModule.hasOAuthApplication()) {
        co_return std::optional<BangumiOAuthApplication> {};
    }

    mView.showMessage(u"首次登录需要你自己的 Bangumi OAuth 应用参数；验证成功后 "
                      u"client_secret 会加密写入配置。无需退出或重新启动。");
    auto application =
        co_await mView.requestOAuthApplication(mModule.oauthApplicationGuide());
    if (!application) {
        co_return ilias::Err(std::move(application.error()));
    }
    mModule.setOAuthApplication(*application);
    co_return std::optional<BangumiOAuthApplication> {std::move(*application)};
}

auto BangumiPresenter::present(BangumiResult<BangumiUser> result)
    -> BangumiResult<void> {
    if (!result) {
        return presentError(mView, std::move(result.error()));
    }
    mView.showUser(*result);
    return {};
}

auto BangumiPresenter::present(BangumiResult<void> result,
                               QStringView successMessage)
    -> BangumiResult<void> {
    if (!result) {
        return presentError(mView, std::move(result.error()));
    }
    mView.showMessage(successMessage);
    return {};
}

auto BangumiPresenter::present(
    BangumiResult<BangumiUserCollectionsResponse> result)
    -> BangumiResult<void> {
    if (!result) {
        return presentError(mView, std::move(result.error()));
    }
    mView.showCollections(*result);
    return {};
}

auto BangumiPresenter::present(BangumiResult<BangumiSubjectSearchResponse> result)
    -> BangumiResult<void> {
    if (!result) {
        return presentError(mView, std::move(result.error()));
    }
    mView.showSearchResults(*result);
    return {};
}

} // namespace anime_land
