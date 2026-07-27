#pragma once

#include "model/bangumi/bangumi.hpp"

#include <QObject>
#include <QString>
#include <QVariantList>

#include <ilias/task.hpp>
#include <ilias/task/scope.hpp>

#include <cstdint>
#include <functional>

namespace anime_land {

/** QML-facing public search, account session and collection browser state. */
class BangumiBrowserViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList searchResults READ searchResults
                   NOTIFY searchChanged)
    Q_PROPERTY(int searchTotal READ searchTotal NOTIFY searchChanged)
    Q_PROPERTY(bool searchLoading READ searchLoading NOTIFY stateChanged)
    Q_PROPERTY(bool hasMoreSearch READ hasMoreSearch NOTIFY searchChanged)
    Q_PROPERTY(QString searchError READ searchError NOTIFY stateChanged)
    Q_PROPERTY(QVariantList collectionResults READ collectionResults
                   NOTIFY collectionsChanged)
    Q_PROPERTY(int collectionTotal READ collectionTotal
                   NOTIFY collectionsChanged)
    Q_PROPERTY(bool collectionsLoading READ collectionsLoading
                   NOTIFY stateChanged)
    Q_PROPERTY(bool hasMoreCollections READ hasMoreCollections
                   NOTIFY collectionsChanged)
    Q_PROPERTY(QString collectionsError READ collectionsError
                   NOTIFY stateChanged)
    Q_PROPERTY(bool accountBusy READ accountBusy NOTIFY stateChanged)
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY accountChanged)
    Q_PROPERTY(QString accountName READ accountName NOTIFY accountChanged)
    Q_PROPERTY(QString accountStatus READ accountStatus
                   NOTIFY accountChanged)

public:
    using SearchLoader = std::function<ilias::Task<
        BangumiResult<BangumiSubjectSearchResponse>>(
        BangumiSubjectSearchQuery)>;
    using CollectionLoader = std::function<ilias::Task<
        BangumiResult<BangumiUserCollectionsResponse>>(
        BangumiCollectionQuery)>;
    using SessionLoader =
        std::function<ilias::Task<BangumiResult<BangumiUser>>() >;
    using SessionLogout =
        std::function<ilias::Task<BangumiResult<void>>() >;
    using Canceller = std::function<void()>;

    explicit BangumiBrowserViewModel(BangumiModule &module,
                                     QObject *parent = nullptr);
    BangumiBrowserViewModel(SearchLoader searchLoader,
                            CollectionLoader collectionLoader = {},
                            QObject *parent = nullptr);
    ~BangumiBrowserViewModel() override;

    auto searchResults() const -> QVariantList { return mSearchResults; }
    auto searchTotal() const noexcept -> int { return mSearchTotal; }
    auto searchLoading() const noexcept -> bool { return mSearchLoading; }
    auto hasMoreSearch() const noexcept -> bool {
        return mSearchResults.size() < mSearchTotal;
    }
    auto searchError() const -> QString { return mSearchError; }
    auto collectionResults() const -> QVariantList {
        return mCollectionResults;
    }
    auto collectionTotal() const noexcept -> int { return mCollectionTotal; }
    auto collectionsLoading() const noexcept -> bool {
        return mCollectionsLoading;
    }
    auto hasMoreCollections() const noexcept -> bool {
        return mCollectionResults.size() < mCollectionTotal;
    }
    auto collectionsError() const -> QString { return mCollectionsError; }
    auto accountBusy() const noexcept -> bool { return mAccountBusy; }
    auto loggedIn() const noexcept -> bool { return mLoggedIn; }
    auto accountName() const -> QString { return mAccountName; }
    auto accountStatus() const -> QString { return mAccountStatus; }

    Q_INVOKABLE void search(const QString &query);
    Q_INVOKABLE void loadMoreSearch();
    Q_INVOKABLE void clearSearch();
    Q_INVOKABLE void refreshCollections();
    Q_INVOKABLE void loadMoreCollections();
    Q_INVOKABLE void restoreSession();
    Q_INVOKABLE void login();
    Q_INVOKABLE void logout();

signals:
    void searchChanged();
    void collectionsChanged();
    void accountChanged();
    void stateChanged();

private:
    enum class SessionAction { Restore, Login, Logout };

    BangumiBrowserViewModel(SearchLoader searchLoader,
                            CollectionLoader collectionLoader,
                            SessionLoader restoreSession,
                            SessionLoader loginSession,
                            SessionLogout logoutSession,
                            Canceller cancelPendingOperations,
                            QObject *parent);
    auto loadSearch(int offset, bool append, std::uint64_t generation)
        -> ilias::Task<void>;
    auto loadCollections(int offset, bool append,
                         std::uint64_t generation) -> ilias::Task<void>;
    auto runSession(SessionAction action, std::uint64_t generation)
        -> ilias::Task<void>;
    auto busy() const noexcept -> bool;
    void beginSession(SessionAction action);

    SearchLoader mSearchLoader;
    CollectionLoader mCollectionLoader;
    SessionLoader mRestoreSession;
    SessionLoader mLoginSession;
    SessionLogout mLogoutSession;
    Canceller mCancelPendingOperations;
    ilias::TaskScope mTasks;
    QVariantList mSearchResults;
    QVariantList mCollectionResults;
    QString mSearchQuery;
    QString mSearchError;
    QString mCollectionsError;
    QString mAccountName;
    QString mAccountStatus = QStringLiteral("未登录");
    std::uint64_t mGeneration = 0;
    int mSearchTotal = 0;
    int mCollectionTotal = 0;
    bool mSearchLoading = false;
    bool mCollectionsLoading = false;
    bool mAccountBusy = false;
    bool mLoggedIn = false;
    bool mDestroying = false;
};

} // namespace anime_land
