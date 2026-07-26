#pragma once

#include <QMetaObject>

#include <ilias/task.hpp>

#include "model/bangumi/bangumi.hpp"
#include "presentation/bangumi/bangumi_view.hpp"

#include <functional>
#include <optional>

namespace anime_land {

/**
 * Presenter for Bangumi account actions.
 *
 * It translates Model results into View output without depending on a concrete
 * CLI or QML frontend. It never parses OAuth payloads or accesses raw tokens.
 */
class BangumiPresenter final {
public:
    using OAuthApplicationSaver =
        std::function<ilias::Task<BangumiResult<void>>(BangumiOAuthApplication)>;

    BangumiPresenter(BangumiModule &module, BangumiView &view,
                     OAuthApplicationSaver applicationSaver);
    ~BangumiPresenter();

    auto login() -> ilias::Task<BangumiResult<void>>;
    auto restoreSession() -> ilias::Task<BangumiResult<void>>;
    auto logout() -> ilias::Task<BangumiResult<void>>;
    auto getCollections(BangumiCollectionQuery query)
        -> ilias::Task<BangumiResult<void>>;
    auto searchSubjects(BangumiSubjectSearchQuery query)
        -> ilias::Task<BangumiResult<void>>;

private:
    auto present(BangumiResult<BangumiUser> result) -> BangumiResult<void>;
    auto present(BangumiResult<void> result, QStringView successMessage)
        -> BangumiResult<void>;
    auto present(BangumiResult<BangumiUserCollectionsResponse> result)
        -> BangumiResult<void>;
    auto present(BangumiResult<BangumiSubjectSearchResponse> result)
        -> BangumiResult<void>;
    auto ensureOAuthApplication()
        -> ilias::Task<BangumiResult<std::optional<BangumiOAuthApplication>>>;

    BangumiModule &mModule;
    BangumiView &mView;
    OAuthApplicationSaver mApplicationSaver;
    QMetaObject::Connection mStateConnection;
};

} // namespace anime_land
