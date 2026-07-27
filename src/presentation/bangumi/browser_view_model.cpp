#include "presentation/bangumi/browser_view_model.hpp"

#include "common/log.hpp"

#include <QVariantMap>

#include <algorithm>
#include <array>
#include <utility>

namespace anime_land {
namespace {

constexpr int kPageSize = 24;
constexpr std::array<const char *, 8> kSubjectColors = {
    "#59636b", "#5f6859", "#685d58", "#6b5c65",
    "#5c626d", "#545d63", "#655b60", "#58656a",
};

auto subjectColor(std::int64_t id) -> QString {
    const auto index = static_cast<std::size_t>(
        id % static_cast<std::int64_t>(kSubjectColors.size()));
    return QString::fromLatin1(kSubjectColors[index]);
}

auto coverUrl(const BangumiSubjectImages &images) -> QString {
    return !images.medium.isEmpty() ? images.medium : images.common;
}

auto episodeMeta(int episodeCount) -> QString {
    return episodeCount > 0 ? QStringLiteral("%1 话").arg(episodeCount)
                            : QStringLiteral("话数待定");
}

auto searchSubjectMap(const BangumiSearchSubject &subject) -> QVariantMap {
    const QString title = subject.nameCn.isEmpty() ? subject.name
                                                    : subject.nameCn;
    const QString subtitle = subject.nameCn.isEmpty() ? QString {}
                                                       : subject.name;
    QStringList meta;
    if (subject.date && !subject.date->isEmpty()) {
        meta.push_back(*subject.date);
    }
    meta.push_back(episodeMeta(std::max(subject.episodes,
                                       subject.totalEpisodes)));
    return {
        {QStringLiteral("id"), QVariant::fromValue(subject.id)},
        {QStringLiteral("bangumiId"), QVariant::fromValue(subject.id)},
        {QStringLiteral("title"), title},
        {QStringLiteral("subtitle"), subtitle},
        {QStringLiteral("summary"), subject.summary},
        {QStringLiteral("meta"), meta.join(QStringLiteral(" · "))},
        {QStringLiteral("episode"), episodeMeta(subject.totalEpisodes)},
        {QStringLiteral("progress"), 0.0},
        {QStringLiteral("score"),
         subject.rating.score > 0
             ? QString::number(subject.rating.score, 'f', 1)
             : QStringLiteral("—")},
        {QStringLiteral("status"), QStringLiteral("Bangumi")},
        {QStringLiteral("color"), subjectColor(subject.id)},
        {QStringLiteral("coverUrl"), coverUrl(subject.images)},
    };
}

auto collectionTypeLabel(BangumiCollectionType type) -> QString {
    switch (type) {
        case BangumiCollectionType::Wish:
            return QStringLiteral("想看");
        case BangumiCollectionType::Done:
            return QStringLiteral("看过");
        case BangumiCollectionType::Doing:
            return QStringLiteral("在看");
        case BangumiCollectionType::OnHold:
            return QStringLiteral("搁置");
        case BangumiCollectionType::Dropped:
            return QStringLiteral("抛弃");
    }
    return QStringLiteral("收藏");
}

auto collectionMap(const BangumiUserCollection &collection) -> QVariantMap {
    const auto &subject = collection.subject;
    const QString title = subject
        ? (subject->nameCn.isEmpty() ? subject->name : subject->nameCn)
        : QStringLiteral("Bangumi #%1").arg(collection.subjectId);
    const QString subtitle = subject && !subject->nameCn.isEmpty()
                                 ? subject->name
                                 : QString {};
    const int episodeCount = subject ? subject->episodes : 0;
    const double progress = episodeCount > 0
        ? std::clamp(static_cast<double>(collection.episodeStatus)
                         / static_cast<double>(episodeCount),
                     0.0, 1.0)
        : 0.0;
    QStringList meta;
    if (subject && subject->date && !subject->date->isEmpty()) {
        meta.push_back(*subject->date);
    }
    meta.push_back(episodeMeta(episodeCount));
    return {
        {QStringLiteral("id"),
         QVariant::fromValue(collection.subjectId)},
        {QStringLiteral("bangumiId"),
         QVariant::fromValue(collection.subjectId)},
        {QStringLiteral("title"), title},
        {QStringLiteral("subtitle"), subtitle},
        {QStringLiteral("summary"),
         subject ? subject->shortSummary : QString {}},
        {QStringLiteral("meta"), meta.join(QStringLiteral(" · "))},
        {QStringLiteral("episode"),
         QStringLiteral("%1 / %2").arg(collection.episodeStatus)
             .arg(episodeCount)},
        {QStringLiteral("progress"), progress},
        {QStringLiteral("score"),
         subject && subject->score > 0
             ? QString::number(subject->score, 'f', 1)
             : QStringLiteral("—")},
        {QStringLiteral("status"),
         collectionTypeLabel(collection.collectionType)},
        {QStringLiteral("color"), subjectColor(collection.subjectId)},
        {QStringLiteral("coverUrl"),
         subject ? coverUrl(subject->images) : QString {}},
    };
}

} // namespace

BangumiBrowserViewModel::BangumiBrowserViewModel(BangumiModule &module,
                                                 QObject *parent)
    : BangumiBrowserViewModel(
          [&module](BangumiSubjectSearchQuery query) {
              return module.searchSubjects(std::move(query));
          },
          [&module](BangumiCollectionQuery query) {
              return module.getCurrentUserCollections(std::move(query));
          },
          [&module]() { return module.restoreSession(); },
          [&module]() { return module.login(); },
          [&module]() { return module.logout(); },
          [&module]() { module.cancelPendingOperations(); }, parent) {}

BangumiBrowserViewModel::BangumiBrowserViewModel(
    SearchLoader searchLoader, CollectionLoader collectionLoader,
    QObject *parent)
    : BangumiBrowserViewModel(std::move(searchLoader),
                              std::move(collectionLoader), {}, {}, {}, {},
                              parent) {}

BangumiBrowserViewModel::BangumiBrowserViewModel(
    SearchLoader searchLoader, CollectionLoader collectionLoader,
    SessionLoader restoreSession, SessionLoader loginSession,
    SessionLogout logoutSession, Canceller cancelPendingOperations,
    QObject *parent)
    : QObject(parent), mSearchLoader(std::move(searchLoader)),
      mCollectionLoader(std::move(collectionLoader)),
      mRestoreSession(std::move(restoreSession)),
      mLoginSession(std::move(loginSession)),
      mLogoutSession(std::move(logoutSession)),
      mCancelPendingOperations(std::move(cancelPendingOperations)) {}

BangumiBrowserViewModel::~BangumiBrowserViewModel() {
    mDestroying = true;
    ++mGeneration;
    if (mCancelPendingOperations) {
        mCancelPendingOperations();
    }
    mTasks.shutdown().wait();
}

auto BangumiBrowserViewModel::busy() const noexcept -> bool {
    return mSearchLoading || mCollectionsLoading || mAccountBusy
           || mDestroying;
}

void BangumiBrowserViewModel::search(const QString &query) {
    const QString normalized = query.trimmed();
    if (busy()) {
        return;
    }
    if (normalized.isEmpty()) {
        clearSearch();
        mSearchError = QStringLiteral("请输入要搜索的动画名称");
        emit stateChanged();
        return;
    }
    mSearchQuery = normalized;
    mSearchResults.clear();
    mSearchTotal = 0;
    mSearchError.clear();
    mSearchLoading = true;
    const auto generation = ++mGeneration;
    emit searchChanged();
    emit stateChanged();
    mTasks.spawn(loadSearch(0, false, generation));
}

void BangumiBrowserViewModel::loadMoreSearch() {
    if (busy() || !hasMoreSearch() || mSearchQuery.isEmpty()) {
        return;
    }
    mSearchLoading = true;
    mSearchError.clear();
    const auto generation = ++mGeneration;
    emit stateChanged();
    mTasks.spawn(loadSearch(static_cast<int>(mSearchResults.size()), true,
                            generation));
}

void BangumiBrowserViewModel::clearSearch() {
    if (mSearchLoading) {
        return;
    }
    mSearchQuery.clear();
    mSearchResults.clear();
    mSearchTotal = 0;
    mSearchError.clear();
    emit searchChanged();
    emit stateChanged();
}

auto BangumiBrowserViewModel::loadSearch(
    int offset, bool append, std::uint64_t generation) -> ilias::Task<void> {
    if (!mSearchLoader) {
        if (!mDestroying && generation == mGeneration) {
            mSearchLoading = false;
            mSearchError = QStringLiteral("Bangumi 搜索服务未配置");
            emit stateChanged();
        }
        co_return;
    }
    auto result = co_await mSearchLoader({
        .keyword = mSearchQuery,
        .sort = BangumiSubjectSearchSort::Match,
        .filter = {.types = {BangumiSubjectType::Anime}},
        .limit = kPageSize,
        .offset = offset,
    });
    if (mDestroying || generation != mGeneration) {
        co_return;
    }
    mSearchLoading = false;
    if (!result) {
        mSearchError = result.error().message;
        AL_LOG_WARN("[presentation.browser] search failed code={}",
                    bangumiErrorCodeName(result.error().code));
        emit stateChanged();
        co_return;
    }
    QVariantList mapped;
    mapped.reserve(static_cast<qsizetype>(result->value.data.size()));
    for (const auto &subject : result->value.data) {
        mapped.push_back(searchSubjectMap(subject));
    }
    if (append) {
        mSearchResults.append(mapped);
    }
    else {
        mSearchResults = std::move(mapped);
    }
    mSearchTotal = result->value.total;
    mSearchError.clear();
    emit searchChanged();
    emit stateChanged();
}

void BangumiBrowserViewModel::refreshCollections() {
    if (busy()) {
        return;
    }
    if (!mLoggedIn) {
        mCollectionsError = QStringLiteral("请先登录 Bangumi 再读取收藏");
        emit stateChanged();
        return;
    }
    mCollectionResults.clear();
    mCollectionTotal = 0;
    mCollectionsError.clear();
    mCollectionsLoading = true;
    const auto generation = ++mGeneration;
    emit collectionsChanged();
    emit stateChanged();
    mTasks.spawn(loadCollections(0, false, generation));
}

void BangumiBrowserViewModel::loadMoreCollections() {
    if (busy() || !mLoggedIn || !hasMoreCollections()) {
        return;
    }
    mCollectionsError.clear();
    mCollectionsLoading = true;
    const auto generation = ++mGeneration;
    emit stateChanged();
    mTasks.spawn(loadCollections(
        static_cast<int>(mCollectionResults.size()), true, generation));
}

auto BangumiBrowserViewModel::loadCollections(
    int offset, bool append, std::uint64_t generation) -> ilias::Task<void> {
    if (!mCollectionLoader) {
        if (!mDestroying && generation == mGeneration) {
            mCollectionsLoading = false;
            mCollectionsError = QStringLiteral("Bangumi 收藏服务未配置");
            emit stateChanged();
        }
        co_return;
    }
    auto result = co_await mCollectionLoader({
        .subjectType = BangumiSubjectType::Anime,
        .collectionType = std::nullopt,
        .limit = kPageSize,
        .offset = offset,
    });
    if (mDestroying || generation != mGeneration) {
        co_return;
    }
    mCollectionsLoading = false;
    if (!result) {
        mCollectionsError = result.error().message;
        emit stateChanged();
        co_return;
    }
    QVariantList mapped;
    mapped.reserve(static_cast<qsizetype>(result->value.data.size()));
    for (const auto &collection : result->value.data) {
        mapped.push_back(collectionMap(collection));
    }
    if (append) {
        mCollectionResults.append(mapped);
    }
    else {
        mCollectionResults = std::move(mapped);
    }
    mCollectionTotal = result->value.total;
    mCollectionsError.clear();
    emit collectionsChanged();
    emit stateChanged();
}

void BangumiBrowserViewModel::beginSession(SessionAction action) {
    if (busy()) {
        return;
    }
    mAccountBusy = true;
    mAccountStatus = action == SessionAction::Restore
        ? QStringLiteral("正在恢复登录…")
        : action == SessionAction::Login
              ? QStringLiteral("正在登录，请在浏览器中继续…")
              : QStringLiteral("正在退出…");
    const auto generation = ++mGeneration;
    emit accountChanged();
    emit stateChanged();
    mTasks.spawn(runSession(action, generation));
}

void BangumiBrowserViewModel::restoreSession() {
    if (!mRestoreSession || mLoggedIn) {
        return;
    }
    beginSession(SessionAction::Restore);
}

void BangumiBrowserViewModel::login() {
    if (!mLoginSession || mLoggedIn) {
        return;
    }
    beginSession(SessionAction::Login);
}

void BangumiBrowserViewModel::logout() {
    if (!mLogoutSession || !mLoggedIn) {
        return;
    }
    beginSession(SessionAction::Logout);
}

auto BangumiBrowserViewModel::runSession(
    SessionAction action, std::uint64_t generation) -> ilias::Task<void> {
    if (action == SessionAction::Logout) {
        auto result = co_await mLogoutSession();
        if (mDestroying || generation != mGeneration) {
            co_return;
        }
        mAccountBusy = false;
        if (!result) {
            mAccountStatus = result.error().message;
        }
        else {
            mLoggedIn = false;
            mAccountName.clear();
            mAccountStatus = QStringLiteral("未登录");
            mCollectionResults.clear();
            mCollectionTotal = 0;
            mCollectionsError.clear();
            emit collectionsChanged();
        }
        emit accountChanged();
        emit stateChanged();
        co_return;
    }

    auto &loader = action == SessionAction::Restore ? mRestoreSession
                                                    : mLoginSession;
    auto result = co_await loader();
    if (mDestroying || generation != mGeneration) {
        co_return;
    }
    mAccountBusy = false;
    if (!result) {
        mLoggedIn = false;
        mAccountName.clear();
        mAccountStatus =
            action == SessionAction::Restore
                && result.error().code == BangumiErrorCode::NotLoggedIn
            ? QStringLiteral("未登录")
            : result.error().message;
    }
    else {
        mLoggedIn = true;
        mAccountName = result->nickname.isEmpty() ? result->username
                                                  : result->nickname;
        mAccountStatus = QStringLiteral("已登录：%1").arg(mAccountName);
    }
    emit accountChanged();
    emit stateChanged();
}

} // namespace anime_land
