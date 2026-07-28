#include "presentation/library/subject_details_view_model.hpp"

#include "common/log.hpp"

#include <QStringList>

#include <algorithm>
#include <array>
#include <chrono>
#include <utility>

namespace anime_land {
namespace {

constexpr std::array<const char *, 8> kSubjectColors = {
    "#59636b", "#5f6859", "#685d58", "#6b5c65",
    "#5c626d", "#545d63", "#655b60", "#58656a",
};

auto preferredTitle(const std::optional<QString> &titleCn,
                    const QString &title) -> QString {
    return titleCn && !titleCn->trimmed().isEmpty() ? *titleCn : title;
}

auto episodeTitle(const persistence::EpisodeDetails &episode) -> QString {
    if (episode.titleCn && !episode.titleCn->trimmed().isEmpty()) {
        return *episode.titleCn;
    }
    if (episode.title && !episode.title->trimmed().isEmpty()) {
        return *episode.title;
    }
    return QStringLiteral("标题待公布");
}

auto episodePrefix(int type) -> QString {
    switch (type) {
        case 0:
            return QStringLiteral("EP");
        case 1:
            return QStringLiteral("SP");
        case 2:
            return QStringLiteral("OP");
        case 3:
            return QStringLiteral("ED");
        case 4:
            return QStringLiteral("PV");
        case 5:
            return QStringLiteral("MAD");
        default:
            return QStringLiteral("其他");
    }
}

auto episodeNumber(const persistence::EpisodeDetails &episode) -> QString {
    const double number = episode.episodeNumber.value_or(
        static_cast<double>(episode.sortOrder + 1));
    return QStringLiteral("%1%2")
        .arg(episodePrefix(episode.episodeType),
             QString::number(number, 'g', 8));
}

auto durationText(
    const std::optional<std::chrono::milliseconds> &duration) -> QString {
    if (!duration || duration->count() <= 0) {
        return QStringLiteral("—");
    }
    const auto totalSeconds =
        std::chrono::duration_cast<std::chrono::seconds>(*duration).count();
    const auto hours = totalSeconds / 3600;
    const auto minutes = (totalSeconds % 3600) / 60;
    const auto seconds = totalSeconds % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
}

auto bangumiId(const persistence::SubjectDetails &subject) -> qlonglong {
    for (const auto &reference : subject.externalRefs) {
        if (reference.ref.providerKey == QStringLiteral("bangumi")) {
            bool valid = false;
            const auto value = reference.ref.externalId.toLongLong(&valid);
            if (valid && value > 0) {
                return value;
            }
        }
    }
    return 0;
}

} // namespace

SubjectDetailsViewModel::SubjectDetailsViewModel(
    LocalMediaImportService &service, QObject *parent)
    : SubjectDetailsViewModel(
          [&service](SubjectId subject, int limit, int offset,
                     bool descending) {
              return service.getSubjectLibraryDetails(
                  subject, limit, offset, descending);
          },
          [&service](std::int64_t bangumiSubjectId) {
              return service.ensureBangumiSubject(bangumiSubjectId);
          },
          [&service](EpisodeId episode) {
              return service.playEpisode(episode);
          },
          parent,
          [&service](std::int64_t bangumiSubjectId) {
              return service.findBangumiSubject(bangumiSubjectId);
          }) {}

SubjectDetailsViewModel::SubjectDetailsViewModel(
    DetailsLoader loader, BangumiResolver resolver, EpisodePlayer player,
    QObject *parent, BangumiFinder finder)
    : SubjectDetailsViewModel(
          [loader = std::move(loader)](SubjectId subject, int, int, bool)
              -> ilias::Task<LibraryResult<
                  std::optional<SubjectLibraryDetails>>> {
              if (!loader) {
                  co_return ilias::Err(libraryError(
                      LibraryErrorCode::PersistenceFailure,
                      QStringLiteral("条目详情加载器未配置")));
              }
              auto result = co_await loader(subject);
              if (result && *result) {
                  auto &details = **result;
                  details.totalEpisodeCount =
                      static_cast<int>(details.episodes.size());
                  details.offset = 0;
              }
              co_return result;
          },
          std::move(resolver), std::move(player), parent,
          std::move(finder)) {}

SubjectDetailsViewModel::SubjectDetailsViewModel(
    PagedDetailsLoader loader, BangumiResolver resolver,
    EpisodePlayer player, QObject *parent, BangumiFinder finder)
    : QObject(parent), mLoader(std::move(loader)),
      mResolver(std::move(resolver)),
      mFinder(std::move(finder)), mPlayer(std::move(player)) {}

SubjectDetailsViewModel::~SubjectDetailsViewModel() {
    mDestroying = true;
    ++mGeneration;
    mTasks.shutdown().wait();
}

void SubjectDetailsViewModel::reportInvalid(QString message) {
    mErrorMessage = std::move(message);
    mNoticeMessage.clear();
    emit stateChanged();
}

void SubjectDetailsViewModel::openSubject(qlonglong subjectId) {
    if (mPlaying || mDestroying) {
        return;
    }
    const SubjectId subject {subjectId};
    if (!isValid(subject)) {
        reportInvalid(QStringLiteral("本地条目 ID 无效"));
        return;
    }
    mLoading = true;
    mRefreshing = false;
    mLoadingMore = false;
    mSubject.clear();
    mEpisodes.clear();
    mFirstPlayableEpisode.reset();
    mCurrentSubject = subject;
    mPlayableEpisodeCount = 0;
    mTotalEpisodeCount = 0;
    mCurrentEpisodePage = 1;
    mEpisodeSortDescending = false;
    mErrorMessage.clear();
    mNoticeMessage.clear();
    const auto generation = ++mGeneration;
    emit detailsChanged();
    emit stateChanged();
    mTasks.spawn(loadPage(subject, 1, false, generation));
}

void SubjectDetailsViewModel::openBangumiSubject(
    qlonglong bangumiSubjectId) {
    if (mPlaying || mDestroying) {
        return;
    }
    if (bangumiSubjectId <= 0) {
        reportInvalid(QStringLiteral("Bangumi 条目 ID 无效"));
        return;
    }
    mLoading = true;
    mRefreshing = false;
    mLoadingMore = false;
    mSubject.clear();
    mEpisodes.clear();
    mFirstPlayableEpisode.reset();
    mCurrentSubject.reset();
    mPlayableEpisodeCount = 0;
    mTotalEpisodeCount = 0;
    mCurrentEpisodePage = 1;
    mEpisodeSortDescending = false;
    mErrorMessage.clear();
    mNoticeMessage.clear();
    const auto generation = ++mGeneration;
    emit detailsChanged();
    emit stateChanged();
    mTasks.spawn(resolveAndLoad(bangumiSubjectId, generation));
}

void SubjectDetailsViewModel::playEpisode(qlonglong episodeId) {
    if (mLoading || mLoadingMore || mPlaying || mDestroying) {
        return;
    }
    const EpisodeId episode {episodeId};
    if (!isValid(episode)) {
        reportInvalid(QStringLiteral("章节 ID 无效"));
        return;
    }
    mPlaying = true;
    mErrorMessage.clear();
    mNoticeMessage.clear();
    const auto generation = mGeneration;
    emit stateChanged();
    mTasks.spawn(play(episode, generation));
}

void SubjectDetailsViewModel::playFirstAvailable() {
    if (!mFirstPlayableEpisode) {
        reportInvalid(QStringLiteral("这个条目还没有关联可播放媒体"));
        return;
    }
    playEpisode(mFirstPlayableEpisode->value);
}

void SubjectDetailsViewModel::loadMoreEpisodes() {
    nextEpisodePage();
}

void SubjectDetailsViewModel::goToEpisodePage(int page) {
    if (mLoading || mLoadingMore || mPlaying || mDestroying
        || !mCurrentSubject || page < 1 || page > episodePageCount()
        || page == mCurrentEpisodePage) {
        return;
    }
    // Page navigation takes priority over an optional stale-while-revalidate
    // task. The network synchronization may still finish persisting in the
    // background, but its now-stale presentation result is ignored.
    mRefreshing = false;
    mLoadingMore = true;
    mErrorMessage.clear();
    const auto generation = ++mGeneration;
    emit stateChanged();
    mTasks.spawn(loadPage(*mCurrentSubject, page,
                          mEpisodeSortDescending, generation));
}

void SubjectDetailsViewModel::previousEpisodePage() {
    goToEpisodePage(mCurrentEpisodePage - 1);
}

void SubjectDetailsViewModel::nextEpisodePage() {
    goToEpisodePage(mCurrentEpisodePage + 1);
}

void SubjectDetailsViewModel::setEpisodeSortDescending(bool descending) {
    if (mLoading || mLoadingMore || mPlaying || mDestroying
        || !mCurrentSubject || descending == mEpisodeSortDescending) {
        return;
    }
    mRefreshing = false;
    mEpisodeSortDescending = descending;
    mLoadingMore = true;
    mErrorMessage.clear();
    const auto generation = ++mGeneration;
    emit detailsChanged();
    emit stateChanged();
    mTasks.spawn(loadPage(*mCurrentSubject, 1, descending, generation));
}

void SubjectDetailsViewModel::clear() {
    if (mLoading || mLoadingMore || mPlaying) {
        return;
    }
    ++mGeneration;
    mRefreshing = false;
    mSubject.clear();
    mEpisodes.clear();
    mFirstPlayableEpisode.reset();
    mCurrentSubject.reset();
    mPlayableEpisodeCount = 0;
    mTotalEpisodeCount = 0;
    mCurrentEpisodePage = 1;
    mEpisodeSortDescending = false;
    mErrorMessage.clear();
    mNoticeMessage.clear();
    emit detailsChanged();
    emit stateChanged();
}

auto SubjectDetailsViewModel::loadPage(SubjectId subject, int page,
                                       bool descending,
                                       std::uint64_t generation)
    -> ilias::Task<void> {
    if (!mLoader) {
        if (!mDestroying && generation == mGeneration) {
            mLoading = false;
            mLoadingMore = false;
            reportInvalid(QStringLiteral("条目详情加载器未配置"));
        }
        co_return;
    }
    const int offset = (page - 1) * kEpisodePageSize;
    auto result = co_await mLoader(subject, kEpisodePageSize, offset,
                                   descending);
    if (mDestroying || generation != mGeneration) {
        co_return;
    }
    mLoading = false;
    mLoadingMore = false;
    mRefreshing = false;
    if (!result) {
        mErrorMessage = result.error().message;
        AL_LOG_WARN("[presentation.subject] load failed code={}",
                    libraryErrorCodeName(result.error().code));
        emit stateChanged();
        co_return;
    }
    if (!*result) {
        mErrorMessage = QStringLiteral("本地数据库中没有这个条目");
        emit stateChanged();
        co_return;
    }
    mCurrentSubject = subject;
    mCurrentEpisodePage = page;
    mEpisodeSortDescending = descending;
    applyDetails(**result, false);
    emit detailsChanged();
    emit stateChanged();
}

auto SubjectDetailsViewModel::resolveAndLoad(
    std::int64_t bangumiSubjectId, std::uint64_t generation)
    -> ilias::Task<void> {
    if (!mResolver) {
        if (!mDestroying && generation == mGeneration) {
            mLoading = false;
            reportInvalid(QStringLiteral("Bangumi 条目目录同步未配置"));
        }
        co_return;
    }
    std::optional<SubjectId> localSubject;
    if (mFinder) {
        auto found = co_await mFinder(bangumiSubjectId);
        if (mDestroying || generation != mGeneration) {
            co_return;
        }
        if (!found) {
            mLoading = false;
            mErrorMessage = found.error().message;
            emit stateChanged();
            co_return;
        }
        localSubject = *found;
    }

    if (localSubject) {
        if (!mLoader) {
            mLoading = false;
            reportInvalid(QStringLiteral("条目详情加载服务未配置"));
            co_return;
        }
        auto local = co_await mLoader(*localSubject, kEpisodePageSize, 0,
                                      false);
        if (mDestroying || generation != mGeneration) {
            co_return;
        }
        if (local && *local) {
            mCurrentSubject = *localSubject;
            mCurrentEpisodePage = 1;
            mEpisodeSortDescending = false;
            applyDetails(**local, false);
            mLoading = false;
            mRefreshing = true;
            emit detailsChanged();
            emit stateChanged();
        }
        else if (!local) {
            AL_LOG_WARN("[presentation.subject] local-first load failed code={}",
                        libraryErrorCodeName(local.error().code));
        }
    }

    auto resolved = co_await mResolver(bangumiSubjectId);
    if (mDestroying || generation != mGeneration) {
        co_return;
    }
    if (!resolved) {
        mLoading = false;
        mRefreshing = false;
        if (hasSubject()) {
            mErrorMessage.clear();
            mNoticeMessage = QStringLiteral(
                "已显示本地数据；Bangumi 后台刷新失败：%1")
                                 .arg(resolved.error().message);
        }
        else {
            mErrorMessage = resolved.error().message;
        }
        emit stateChanged();
        co_return;
    }
    if (!mLoader) {
        mLoading = false;
        reportInvalid(QStringLiteral("条目详情加载服务未配置"));
        co_return;
    }
    auto result = co_await mLoader(*resolved, kEpisodePageSize, 0, false);
    if (mDestroying || generation != mGeneration) {
        co_return;
    }
    mLoading = false;
    mRefreshing = false;
    if (!result) {
        if (hasSubject()) {
            mErrorMessage.clear();
            mNoticeMessage = QStringLiteral(
                "已显示本地数据；刷新后的数据库读取失败：%1")
                                 .arg(result.error().message);
        }
        else {
            mErrorMessage = result.error().message;
        }
        emit stateChanged();
        co_return;
    }
    if (!*result) {
        if (hasSubject()) {
            mNoticeMessage = QStringLiteral("已显示本地数据；后台刷新未返回条目");
        }
        else {
            mErrorMessage = QStringLiteral("本地数据库中没有这个条目");
        }
        emit stateChanged();
        co_return;
    }
    mCurrentSubject = *resolved;
    mCurrentEpisodePage = 1;
    mEpisodeSortDescending = false;
    applyDetails(**result, false);
    emit detailsChanged();
    emit stateChanged();
}

auto SubjectDetailsViewModel::play(EpisodeId episode,
                                   std::uint64_t generation)
    -> ilias::Task<void> {
    if (!mPlayer) {
        if (!mDestroying && generation == mGeneration) {
            mPlaying = false;
            reportInvalid(QStringLiteral("章节播放服务未配置"));
        }
        co_return;
    }
    auto result = co_await mPlayer(episode);
    if (mDestroying || generation != mGeneration) {
        co_return;
    }
    mPlaying = false;
    if (!result) {
        mErrorMessage = result.error().message;
        emit stateChanged();
        co_return;
    }
    mErrorMessage.clear();
    mNoticeMessage = QStringLiteral("已在内置播放器中打开");
    emit stateChanged();
}

void SubjectDetailsViewModel::applyDetails(
    const SubjectLibraryDetails &details, bool append) {
    const QString title = preferredTitle(details.subject.summary.titleCn,
                                         details.subject.summary.title);
    const QString subtitle =
        details.subject.summary.titleCn
            && *details.subject.summary.titleCn
                   != details.subject.summary.title
        ? details.subject.summary.title
        : QString {};
    QStringList tags;
    for (const auto &tag : details.subject.tags) {
        tags.push_back(tag.name);
    }
    QStringList metadata;
    if (details.subject.airDate) {
        metadata.push_back(details.subject.airDate->toString(Qt::ISODate));
    }
    if (!tags.isEmpty()) {
        metadata.push_back(tags.mid(0, 4).join(QStringLiteral(" / ")));
    }
    const auto colorIndex = static_cast<std::size_t>(
        details.subject.summary.id.value
        % static_cast<std::int64_t>(kSubjectColors.size()));
    if (!append) {
        mSubject = {
        {QStringLiteral("subjectId"), QVariant::fromValue(details.subject.summary.id.value)},
        {QStringLiteral("bangumiId"), bangumiId(details.subject)},
        {QStringLiteral("title"), title},
        {QStringLiteral("subtitle"), subtitle},
        {QStringLiteral("summary"),
         details.subject.summary.summary.value_or(QString {})},
        {QStringLiteral("meta"), metadata.join(QStringLiteral(" · "))},
        {QStringLiteral("coverUrl"),
         details.subject.coverUrl
             ? details.subject.coverUrl->toString()
             : QString {}},
        {QStringLiteral("color"),
         QString::fromLatin1(kSubjectColors[colorIndex])},
        };
    }

    if (!append) {
        mEpisodes.clear();
        mFirstPlayableEpisode.reset();
        mPlayableEpisodeCount = 0;
    }
    mEpisodes.reserve(mEpisodes.size()
                      + static_cast<qsizetype>(details.episodes.size()));
    mTotalEpisodeCount = details.totalEpisodeCount > 0
        ? details.totalEpisodeCount
        : static_cast<int>(details.episodes.size());
    for (const auto &entry : details.episodes) {
        const bool linked = !entry.media.empty();
        if (linked) {
            ++mPlayableEpisodeCount;
            if (!mFirstPlayableEpisode) {
                mFirstPlayableEpisode = entry.episode.id;
            }
        }
        QString source;
        if (entry.media.empty()) {
            source = QStringLiteral("未关联媒体");
        }
        else if (entry.media.size() == 1) {
            source = entry.media.front().item.displayName;
        }
        else {
            source = QStringLiteral("%1 个关联媒体")
                         .arg(entry.media.size());
        }
        mEpisodes.push_back(QVariantMap {
            {QStringLiteral("id"), QVariant::fromValue(entry.episode.id.value)},
            {QStringLiteral("number"), episodeNumber(entry.episode)},
            {QStringLiteral("title"), episodeTitle(entry.episode)},
            {QStringLiteral("source"), source},
            {QStringLiteral("duration"),
             durationText(entry.episode.duration)},
            {QStringLiteral("linked"), linked},
            {QStringLiteral("mediaCount"),
             static_cast<int>(entry.media.size())},
            {QStringLiteral("progress"), 0.0},
        });
    }
    mErrorMessage.clear();
    mNoticeMessage.clear();
}

} // namespace anime_land
