#include "presentation/library/library_view_model.hpp"

#include "common/log.hpp"

#include <QHash>
#include <QSet>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <utility>

namespace anime_land {
namespace {

constexpr std::array<const char *, 8> kMediaColors = {
    "#59636b", "#5f6859", "#685d58", "#6b5c65",
    "#5c626d", "#545d63", "#655b60", "#58656a",
};

struct EpisodeMediaGroup {
    qlonglong episodeId = 0;
    QString title;
    QString number;
    std::optional<double> episodeNumber;
    int sortOrder = 0;
    QVariantList items;
};

struct SubjectMediaGroup {
    qlonglong subjectId = 0;
    QString title;
    QHash<qlonglong, qsizetype> episodeIndices;
    std::vector<EpisodeMediaGroup> episodes;
    QSet<qlonglong> mediaIds;
};

auto episodeTypePrefix(int type) -> QString {
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
            return QStringLiteral("章节");
    }
}

auto episodeNumberLabel(const MediaAssociationSummary &association)
    -> QString {
    if (!association.episodeNumber) {
        return episodeTypePrefix(association.episodeType);
    }
    return QStringLiteral("%1%2")
        .arg(episodeTypePrefix(association.episodeType))
        .arg(*association.episodeNumber, 0, 'g', 8);
}

auto associationLabel(const MediaAssociationSummary &association) -> QString {
    const QString episode = QStringLiteral("%1 · %2")
                                .arg(episodeNumberLabel(association),
                                     association.episodeTitle);
    return QStringLiteral("%1 · %2")
        .arg(association.subjectTitle, episode);
}

auto associationMap(const MediaAssociationSummary &association)
    -> QVariantMap {
    return {
        {QStringLiteral("episodeId"), QVariant::fromValue(association.episodeId.value)},
        {QStringLiteral("subjectId"), QVariant::fromValue(association.subjectId.value)},
        {QStringLiteral("subjectTitle"), association.subjectTitle},
        {QStringLiteral("episodeTitle"), association.episodeTitle},
        {QStringLiteral("episodeNumber"),
         association.episodeNumber
             ? QVariant {*association.episodeNumber}
             : QVariant {}},
        {QStringLiteral("episodeType"), association.episodeType},
        {QStringLiteral("number"), episodeNumberLabel(association)},
        {QStringLiteral("label"), associationLabel(association)},
    };
}

auto mediaEntryMap(const LibraryMediaEntry &entry) -> QVariantMap {
    const auto colorIndex = static_cast<std::size_t>(
        entry.media.item.id.value
        % static_cast<std::int64_t>(kMediaColors.size()));

    QVariantList associations;
    associations.reserve(static_cast<qsizetype>(entry.associations.size()));
    QStringList searchTerms {entry.media.item.displayName,
                             entry.media.resource.displayName};
    for (const auto &association : entry.associations) {
        associations.push_back(associationMap(association));
        searchTerms.push_back(association.subjectTitle);
        searchTerms.push_back(association.episodeTitle);
        searchTerms.push_back(episodeNumberLabel(association));
    }

    QVariantMap value;
    value.insert(QStringLiteral("id"), QVariant::fromValue(entry.media.item.id.value));
    value.insert(QStringLiteral("resourceId"),
                 QVariant::fromValue(entry.media.resource.id.value));
    value.insert(QStringLiteral("title"), entry.media.item.displayName);
    value.insert(QStringLiteral("resourceTitle"),
                 entry.media.resource.displayName);
    value.insert(QStringLiteral("subtitle"),
                 entry.associations.empty()
                     ? entry.media.resource.displayName
                     : associationLabel(entry.associations.front()));
    value.insert(QStringLiteral("meta"), QStringLiteral("本地文件"));
    value.insert(QStringLiteral("status"),
                 entry.associations.empty()
                     ? QStringLiteral("未关联")
                     : entry.associations.size() == 1
                           ? QStringLiteral("已关联")
                           : QStringLiteral("已关联 %1 个章节")
                                 .arg(entry.associations.size()));
    value.insert(QStringLiteral("associationCount"),
                 static_cast<int>(entry.associations.size()));
    value.insert(QStringLiteral("associations"), associations);
    value.insert(QStringLiteral("searchText"),
                 searchTerms.join(QLatin1Char('\n')).toCaseFolded());
    value.insert(QStringLiteral("color"),
                 QString::fromLatin1(kMediaColors[colorIndex]));
    return value;
}

void appendDirectoryItem(QVariantList &groups,
                         QHash<qlonglong, qsizetype> &groupIndices,
                         const LibraryMediaEntry &entry,
                         const QVariantMap &media) {
    const qlonglong resourceId = entry.media.resource.id.value;
    qsizetype groupIndex = groupIndices.value(resourceId, -1);
    if (groupIndex < 0) {
        groupIndex = groups.size();
        groupIndices.insert(resourceId, groupIndex);
        groups.push_back(QVariantMap {
            {QStringLiteral("resourceId"), resourceId},
            {QStringLiteral("title"), entry.media.resource.displayName},
            {QStringLiteral("itemCount"), 0},
            {QStringLiteral("items"), QVariantList {}},
        });
    }
    QVariantMap group = groups[groupIndex].toMap();
    QVariantList groupItems = group.value(QStringLiteral("items")).toList();
    groupItems.push_back(media);
    group.insert(QStringLiteral("itemCount"), groupItems.size());
    group.insert(QStringLiteral("items"), groupItems);
    groups[groupIndex] = std::move(group);
}

auto subjectMap(const BangumiSearchSubject &subject) -> QVariantMap {
    const QString title = subject.nameCn.isEmpty() ? subject.name
                                                    : subject.nameCn;
    QVariantMap value;
    value.insert(QStringLiteral("bangumiId"), QVariant::fromValue(subject.id));
    value.insert(QStringLiteral("title"), title);
    value.insert(QStringLiteral("subtitle"),
                 subject.nameCn.isEmpty() ? QString {} : subject.name);
    value.insert(QStringLiteral("meta"),
                 QStringLiteral("Bangumi %1 · %2 话")
                     .arg(subject.id)
                     .arg(std::max(subject.episodes,
                                   subject.totalEpisodes)));
    return value;
}

auto episodeMap(const AssociationEpisodeOption &episode) -> QVariantMap {
    return QVariantMap {
        {QStringLiteral("id"), QVariant::fromValue(episode.id.value)},
        {QStringLiteral("title"), episode.title},
        {QStringLiteral("number"), episode.displayNumber},
        {QStringLiteral("type"), episode.episodeType},
    };
}

} // namespace

LibraryViewModel::LibraryViewModel(LocalMediaImportService &service,
                                   QObject *parent)
    : QObject(parent),
      mLibraryLoader([&service]() { return service.listLibraryMedia(); }),
      mImporter([&service](QList<QUrl> files) {
          return service.importFiles(std::move(files));
      }),
      mRemover([&service](SourceItemId item) {
          return service.removeMedia(item);
      }),
      mSubjectSearcher([&service](QString query) {
          return service.searchAssociationSubjects(std::move(query));
      }),
      mEpisodeLoader([&service](const BangumiSearchSubject &subject) {
          return service.loadAssociationEpisodes(subject);
      }),
      mLinker([&service](SourceItemId item, EpisodeId episode) {
          return service.linkMedia(item, episode);
      }),
      mUnlinker([&service](SourceItemId item, EpisodeId episode) {
          return service.unlinkMedia(item, episode);
      }),
      mPlayer([&service](SourceItemId item) {
          return service.playMedia(item);
      }) {}

LibraryViewModel::LibraryViewModel(LibraryLoader loader, QObject *parent)
    : QObject(parent), mLibraryLoader(std::move(loader)) {}

LibraryViewModel::LibraryViewModel(MediaLoader loader,
                                   MediaImporter importer,
                                   MediaRemover remover, QObject *parent)
    : QObject(parent), mLegacyLoader(std::move(loader)),
      mImporter(std::move(importer)), mRemover(std::move(remover)) {}

LibraryViewModel::~LibraryViewModel() {
    mDestroying = true;
    ++mGeneration;
    mTasks.shutdown().wait();
}

auto LibraryViewModel::isBusy() const noexcept -> bool {
    return mLoading || mImporting || mRemoving || mAssociating || mPlaying
           || mDestroying;
}

auto LibraryViewModel::reloadMedia()
    -> ilias::Task<LibraryResult<std::vector<LibraryMediaEntry>>> {
    if (mLibraryLoader) {
        co_return co_await mLibraryLoader();
    }
    if (!mLegacyLoader) {
        co_return ilias::Err(libraryError(
            LibraryErrorCode::PersistenceFailure,
            QStringLiteral("本地媒体库加载器未配置")));
    }
    auto legacy = co_await mLegacyLoader();
    if (!legacy) {
        co_return ilias::Err(std::move(legacy.error()));
    }
    std::vector<LibraryMediaEntry> entries;
    entries.reserve(legacy->size());
    for (auto &entry : *legacy) {
        entries.push_back({.media = std::move(entry), .associations = {}});
    }
    co_return entries;
}

void LibraryViewModel::refresh() {
    if (isBusy()) {
        return;
    }
    mLoading = true;
    mErrorMessage.clear();
    const auto generation = ++mGeneration;
    emit stateChanged();
    mTasks.spawn(load(generation));
}

void LibraryViewModel::importFiles(const QList<QUrl> &files) {
    if (isBusy()) {
        return;
    }
    mImporting = true;
    mErrorMessage.clear();
    mNoticeMessage.clear();
    const auto generation = ++mGeneration;
    emit stateChanged();
    mTasks.spawn(import(files, generation));
}

void LibraryViewModel::removeMedia(qlonglong sourceItemId) {
    if (isBusy()) {
        return;
    }
    const SourceItemId item {sourceItemId};
    if (!isValid(item)) {
        mErrorMessage = QStringLiteral("媒体项 ID 无效");
        emit stateChanged();
        return;
    }
    mRemoving = true;
    mErrorMessage.clear();
    mNoticeMessage.clear();
    const auto generation = ++mGeneration;
    emit stateChanged();
    mTasks.spawn(remove(item, generation));
}

void LibraryViewModel::searchAssociationSubjects(const QString &query) {
    if (isBusy()) {
        return;
    }
    mAssociating = true;
    mErrorMessage.clear();
    mNoticeMessage.clear();
    mAssociationEpisodes.clear();
    const auto generation = ++mGeneration;
    emit associationChanged();
    emit stateChanged();
    mTasks.spawn(search(query, generation));
}

void LibraryViewModel::selectAssociationSubject(
    qlonglong bangumiSubjectId) {
    if (isBusy() || bangumiSubjectId <= 0) {
        return;
    }
    mAssociating = true;
    mErrorMessage.clear();
    mAssociationEpisodes.clear();
    const auto generation = ++mGeneration;
    emit associationChanged();
    emit stateChanged();
    mTasks.spawn(loadEpisodes(bangumiSubjectId, generation));
}

void LibraryViewModel::linkMedia(qlonglong sourceItemId,
                                 qlonglong episodeId) {
    if (isBusy()) {
        return;
    }
    const SourceItemId item {sourceItemId};
    const EpisodeId episode {episodeId};
    if (!isValid(item) || !isValid(episode)) {
        mErrorMessage = QStringLiteral("媒体或章节 ID 无效");
        emit stateChanged();
        return;
    }
    mAssociating = true;
    mErrorMessage.clear();
    const auto generation = ++mGeneration;
    emit stateChanged();
    mTasks.spawn(link(item, episode, false, generation));
}

void LibraryViewModel::unlinkMedia(qlonglong sourceItemId,
                                   qlonglong episodeId) {
    if (isBusy()) {
        return;
    }
    const SourceItemId item {sourceItemId};
    const EpisodeId episode {episodeId};
    if (!isValid(item) || !isValid(episode)) {
        mErrorMessage = QStringLiteral("媒体或章节 ID 无效");
        emit stateChanged();
        return;
    }
    mAssociating = true;
    mErrorMessage.clear();
    const auto generation = ++mGeneration;
    emit stateChanged();
    mTasks.spawn(link(item, episode, true, generation));
}

void LibraryViewModel::playMedia(qlonglong sourceItemId) {
    if (isBusy()) {
        return;
    }
    const SourceItemId item {sourceItemId};
    if (!isValid(item)) {
        mErrorMessage = QStringLiteral("媒体项 ID 无效");
        emit stateChanged();
        return;
    }
    mPlaying = true;
    mErrorMessage.clear();
    mNoticeMessage.clear();
    const auto generation = ++mGeneration;
    emit stateChanged();
    mTasks.spawn(play(item, generation));
}

void LibraryViewModel::clearAssociationPicker() {
    if (mAssociating) {
        return;
    }
    mSubjectResults.clear();
    mAssociationSubjects.clear();
    mAssociationEpisodes.clear();
    emit associationChanged();
}

auto LibraryViewModel::load(std::uint64_t generation) -> ilias::Task<void> {
    auto result = co_await reloadMedia();
    if (mDestroying || generation != mGeneration) {
        co_return;
    }
    mLoading = false;
    if (!result) {
        mErrorMessage = result.error().message;
        emit stateChanged();
        co_return;
    }
    applyMedia(*result);
    mErrorMessage.clear();
    emit mediaChanged();
    emit stateChanged();
}

auto LibraryViewModel::import(QList<QUrl> files,
                              std::uint64_t generation)
    -> ilias::Task<void> {
    if (!mImporter) {
        if (!mDestroying && generation == mGeneration) {
            mImporting = false;
            mErrorMessage = QStringLiteral("本地媒体导入器未配置");
            emit stateChanged();
        }
        co_return;
    }
    auto imported = co_await mImporter(std::move(files));
    if (mDestroying || generation != mGeneration) {
        co_return;
    }
    if (!imported) {
        mImporting = false;
        mErrorMessage = imported.error().message;
        emit stateChanged();
        co_return;
    }
    auto loaded = co_await reloadMedia();
    if (mDestroying || generation != mGeneration) {
        co_return;
    }
    mImporting = false;
    if (!loaded) {
        mErrorMessage = loaded.error().message;
        emit stateChanged();
        co_return;
    }
    applyMedia(*loaded);
    mErrorMessage.clear();
    mNoticeMessage = imported->duplicateSelectionCount == 0
                         ? QStringLiteral("已导入 %1 个媒体文件")
                               .arg(imported->persistedFileCount)
                         : QStringLiteral(
                               "已导入 %1 个媒体文件，忽略 %2 个重复选择")
                               .arg(imported->persistedFileCount)
                               .arg(imported->duplicateSelectionCount);
    emit mediaChanged();
    emit stateChanged();
}

auto LibraryViewModel::remove(SourceItemId item,
                              std::uint64_t generation)
    -> ilias::Task<void> {
    if (!mRemover) {
        if (!mDestroying && generation == mGeneration) {
            mRemoving = false;
            mErrorMessage = QStringLiteral("本地媒体移除器未配置");
            emit stateChanged();
        }
        co_return;
    }
    auto removed = co_await mRemover(item);
    if (mDestroying || generation != mGeneration) {
        co_return;
    }
    if (!removed) {
        mRemoving = false;
        mErrorMessage = removed.error().message;
        emit stateChanged();
        co_return;
    }
    auto loaded = co_await reloadMedia();
    if (mDestroying || generation != mGeneration) {
        co_return;
    }
    mRemoving = false;
    if (!loaded) {
        mErrorMessage = loaded.error().message;
        emit stateChanged();
        co_return;
    }
    applyMedia(*loaded);
    mErrorMessage.clear();
    mNoticeMessage =
        QStringLiteral("已从媒体库移除 1 个媒体文件，磁盘文件未删除");
    emit mediaChanged();
    emit stateChanged();
}

auto LibraryViewModel::search(QString query, std::uint64_t generation)
    -> ilias::Task<void> {
    if (!mSubjectSearcher) {
        if (!mDestroying && generation == mGeneration) {
            mAssociating = false;
            mErrorMessage = QStringLiteral("Bangumi 关联搜索未配置");
            emit stateChanged();
        }
        co_return;
    }
    auto result = co_await mSubjectSearcher(std::move(query));
    if (mDestroying || generation != mGeneration) {
        co_return;
    }
    mAssociating = false;
    if (!result) {
        mErrorMessage = result.error().message;
        emit stateChanged();
        co_return;
    }
    mSubjectResults = std::move(*result);
    mAssociationSubjects.clear();
    mAssociationSubjects.reserve(
        static_cast<qsizetype>(mSubjectResults.size()));
    for (const auto &subject : mSubjectResults) {
        mAssociationSubjects.push_back(subjectMap(subject));
    }
    if (mSubjectResults.empty()) {
        mNoticeMessage = QStringLiteral("没有找到匹配的 Bangumi 动画条目");
    }
    emit associationChanged();
    emit stateChanged();
}

auto LibraryViewModel::loadEpisodes(std::int64_t bangumiSubjectId,
                                    std::uint64_t generation)
    -> ilias::Task<void> {
    const auto found = std::ranges::find_if(
        mSubjectResults, [bangumiSubjectId](const BangumiSearchSubject &value) {
            return value.id == bangumiSubjectId;
        });
    if (!mEpisodeLoader || found == mSubjectResults.end()) {
        if (!mDestroying && generation == mGeneration) {
            mAssociating = false;
            mErrorMessage = QStringLiteral("找不到所选 Bangumi 条目");
            emit stateChanged();
        }
        co_return;
    }
    const BangumiSearchSubject selected = *found;
    auto result = co_await mEpisodeLoader(selected);
    if (mDestroying || generation != mGeneration) {
        co_return;
    }
    mAssociating = false;
    if (!result) {
        mErrorMessage = result.error().message;
        emit stateChanged();
        co_return;
    }
    mAssociationEpisodes.clear();
    mAssociationEpisodes.reserve(static_cast<qsizetype>(result->size()));
    for (const auto &episode : *result) {
        mAssociationEpisodes.push_back(episodeMap(episode));
    }
    if (result->empty()) {
        mNoticeMessage = QStringLiteral("该条目没有可关联章节");
    }
    emit associationChanged();
    emit stateChanged();
}

auto LibraryViewModel::link(SourceItemId item, EpisodeId episode,
                            bool removeLink, std::uint64_t generation)
    -> ilias::Task<void> {
    auto &operation = removeLink ? mUnlinker : mLinker;
    if (!operation) {
        if (!mDestroying && generation == mGeneration) {
            mAssociating = false;
            mErrorMessage = QStringLiteral("媒体关联操作未配置");
            emit stateChanged();
        }
        co_return;
    }
    auto result = co_await operation(item, episode);
    if (mDestroying || generation != mGeneration) {
        co_return;
    }
    if (!result) {
        mAssociating = false;
        mErrorMessage = result.error().message;
        emit stateChanged();
        co_return;
    }
    auto loaded = co_await reloadMedia();
    if (mDestroying || generation != mGeneration) {
        co_return;
    }
    mAssociating = false;
    if (!loaded) {
        mErrorMessage = loaded.error().message;
        emit stateChanged();
        co_return;
    }
    applyMedia(*loaded);
    mErrorMessage.clear();
    mNoticeMessage = removeLink ? QStringLiteral("已解除章节关联")
                                : QStringLiteral("已关联到所选章节");
    emit mediaChanged();
    emit stateChanged();
}

auto LibraryViewModel::play(SourceItemId item, std::uint64_t generation)
    -> ilias::Task<void> {
    if (!mPlayer) {
        if (!mDestroying && generation == mGeneration) {
            mPlaying = false;
            mErrorMessage = QStringLiteral("本地播放服务未配置");
            emit stateChanged();
        }
        co_return;
    }
    auto result = co_await mPlayer(item);
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
    mNoticeMessage = QStringLiteral("已交给系统默认播放器打开");
    emit stateChanged();
}

void LibraryViewModel::applyMedia(
    const std::vector<LibraryMediaEntry> &entries) {
    QVariantList items;
    QVariantList unassociatedGroups;
    QHash<qlonglong, qsizetype> unassociatedGroupIndices;
    std::vector<SubjectMediaGroup> subjects;
    QHash<qlonglong, qsizetype> subjectIndices;
    items.reserve(static_cast<qsizetype>(entries.size()));

    for (const auto &entry : entries) {
        const QVariantMap media = mediaEntryMap(entry);
        items.push_back(media);

        if (entry.associations.empty()) {
            appendDirectoryItem(unassociatedGroups,
                                unassociatedGroupIndices, entry, media);
            continue;
        }

        for (const auto &association : entry.associations) {
            const qlonglong subjectId = association.subjectId.value;
            qsizetype subjectIndex = subjectIndices.value(subjectId, -1);
            if (subjectIndex < 0) {
                subjectIndex = static_cast<qsizetype>(subjects.size());
                subjectIndices.insert(subjectId, subjectIndex);
                subjects.push_back({
                    .subjectId = subjectId,
                    .title = association.subjectTitle,
                    .episodeIndices = {},
                    .episodes = {},
                    .mediaIds = {},
                });
            }

            auto &subject = subjects[static_cast<std::size_t>(subjectIndex)];
            subject.mediaIds.insert(entry.media.item.id.value);
            const qlonglong episodeId = association.episodeId.value;
            qsizetype episodeIndex =
                subject.episodeIndices.value(episodeId, -1);
            if (episodeIndex < 0) {
                episodeIndex =
                    static_cast<qsizetype>(subject.episodes.size());
                subject.episodeIndices.insert(episodeId, episodeIndex);
                subject.episodes.push_back({
                    .episodeId = episodeId,
                    .title = association.episodeTitle,
                    .number = episodeNumberLabel(association),
                    .episodeNumber = association.episodeNumber,
                    .sortOrder = association.sortOrder,
                });
            }

            QVariantMap groupedMedia = media;
            groupedMedia.insert(QStringLiteral("subtitle"),
                                entry.media.resource.displayName);
            groupedMedia.insert(QStringLiteral("contextAssociation"),
                                associationMap(association));
            subject.episodes[static_cast<std::size_t>(episodeIndex)]
                .items.push_back(std::move(groupedMedia));
        }
    }

    std::sort(subjects.begin(), subjects.end(),
              [](const SubjectMediaGroup &left,
                 const SubjectMediaGroup &right) {
                  const int titleOrder = QString::localeAwareCompare(
                      left.title, right.title);
                  return titleOrder == 0
                             ? left.subjectId < right.subjectId
                             : titleOrder < 0;
              });

    QVariantList subjectGroups;
    subjectGroups.reserve(static_cast<qsizetype>(subjects.size()));
    for (auto &subject : subjects) {
        std::sort(subject.episodes.begin(), subject.episodes.end(),
                  [](const EpisodeMediaGroup &left,
                     const EpisodeMediaGroup &right) {
                      if (left.episodeNumber && right.episodeNumber
                          && *left.episodeNumber != *right.episodeNumber) {
                          return *left.episodeNumber < *right.episodeNumber;
                      }
                      if (left.episodeNumber.has_value()
                          != right.episodeNumber.has_value()) {
                          return left.episodeNumber.has_value();
                      }
                      if (left.sortOrder != right.sortOrder) {
                          return left.sortOrder < right.sortOrder;
                      }
                      return left.episodeId < right.episodeId;
                  });

        QVariantList episodeGroups;
        episodeGroups.reserve(
            static_cast<qsizetype>(subject.episodes.size()));
        for (auto &episode : subject.episodes) {
            episodeGroups.push_back(QVariantMap {
                {QStringLiteral("episodeId"), episode.episodeId},
                {QStringLiteral("title"), episode.title},
                {QStringLiteral("number"), episode.number},
                {QStringLiteral("label"),
                 QStringLiteral("%1 · %2")
                     .arg(episode.number, episode.title)},
                {QStringLiteral("itemCount"), episode.items.size()},
                {QStringLiteral("items"), std::move(episode.items)},
            });
        }
        subjectGroups.push_back(QVariantMap {
            {QStringLiteral("subjectId"), subject.subjectId},
            {QStringLiteral("title"), subject.title},
            {QStringLiteral("episodeCount"), episodeGroups.size()},
            {QStringLiteral("mediaCount"), subject.mediaIds.size()},
            {QStringLiteral("episodes"), std::move(episodeGroups)},
        });
    }

    mMediaItems = std::move(items);
    mSubjectGroups = std::move(subjectGroups);
    mUnassociatedGroups = std::move(unassociatedGroups);
}

} // namespace anime_land
