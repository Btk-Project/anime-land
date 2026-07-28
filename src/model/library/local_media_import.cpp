#include "pch.hpp"

#include "model/library/local_media_import.hpp"

#include "common/log.hpp"
#include "model/bangumi/bangumi.hpp"
#include "model/persistence/catalog_store.hpp"
#include "model/persistence/library_store.hpp"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <chrono>
#include <system_error>
#include <utility>

namespace anime_land {
namespace {

constexpr auto kLocalFileProviderKey = "local-file";
constexpr auto kSubjectDetailsFreshness = std::chrono::hours(6);

auto stablePathKey(QString path) -> QString {
    path = QDir::cleanPath(QDir::fromNativeSeparators(path));
#if defined(Q_OS_WIN)
    path = path.toCaseFolded();
#endif
    return path;
}

auto encodeDescriptor(QStringView key, const QString &value) -> QByteArray {
    return QJsonDocument(QJsonObject {{key.toString(), value}})
        .toJson(QJsonDocument::Compact);
}

auto persistenceFailure(const std::error_code &error) -> LibraryError {
    return libraryError(
        LibraryErrorCode::PersistenceFailure,
        QStringLiteral("无法写入本地媒体库：%1")
            .arg(QString::fromStdString(error.message())));
}

auto catalogFailure(const std::error_code &error) -> LibraryError {
    return libraryError(
        LibraryErrorCode::PersistenceFailure,
        QStringLiteral("无法更新本地条目目录：%1")
            .arg(QString::fromStdString(error.message())));
}

auto remoteFailure(const BangumiError &error) -> LibraryError {
    return libraryError(LibraryErrorCode::RemoteLookupFailure, error.message);
}

auto preferredTitle(QString titleCn, QString title) -> QString {
    return titleCn.trimmed().isEmpty() ? std::move(title)
                                       : std::move(titleCn);
}

auto optionalDate(const std::optional<QString> &value)
    -> std::optional<QDate> {
    if (!value || value->isEmpty()) {
        return std::nullopt;
    }
    const auto date = QDate::fromString(*value, Qt::ISODate);
    return date.isValid() ? std::optional<QDate> {date} : std::nullopt;
}

auto optionalDate(const QString &value) -> std::optional<QDate> {
    return optionalDate(std::optional<QString> {value});
}

auto subjectDetailsAreFresh(const persistence::SubjectDetails &subject,
                            const QDateTime &now) -> bool {
    if (subject.summary.metadataLevel
            != persistence::SubjectMetadataLevel::Details
        || !subject.metadataRefreshedAt
        || !subject.metadataRefreshedAt->isValid()) {
        return false;
    }
    const qint64 ageSeconds = subject.metadataRefreshedAt->secsTo(now);
    const auto freshnessSeconds =
        std::chrono::duration_cast<std::chrono::seconds>(
            kSubjectDetailsFreshness)
            .count();
    return ageSeconds < freshnessSeconds;
}

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
            return QStringLiteral("其他");
    }
}

auto displayEpisodeNumber(const BangumiEpisode &episode) -> QString {
    const double number = episode.episodeNumber.value_or(episode.sort);
    return QStringLiteral("%1%2")
        .arg(episodeTypePrefix(episode.type),
             QString::number(number, 'g', 8));
}

auto subjectSnapshot(
    const BangumiSubjectDetails &subject,
    persistence::SubjectMetadataLevel metadataLevel,
    const QDateTime &fetchedAt) -> persistence::SubjectSnapshot {
    QString cover = subject.images.large;
    if (cover.isEmpty()) {
        cover = subject.images.common;
    }
    if (cover.isEmpty()) {
        cover = subject.images.medium;
    }
    std::optional<QUrl> coverUrl;
    const QUrl parsedCover(cover);
    if (!cover.isEmpty() && parsedCover.isValid()) {
        coverUrl = parsedCover;
    }

    std::vector<persistence::SubjectTagSnapshot> tags;
    tags.reserve(subject.tags.size());
    for (const auto &tag : subject.tags) {
        tags.push_back({.name = tag.name,
                        .weight = static_cast<double>(tag.count)});
    }

    return {
        .origin = {.providerKey = QStringLiteral("bangumi"),
                   .externalId = QString::number(subject.id)},
        .metadataLevel = metadataLevel,
        .subjectType = static_cast<int>(subject.type),
        .title = subject.name.trimmed().isEmpty() ? subject.nameCn
                                                  : subject.name,
        .titleCn = subject.nameCn.isEmpty()
                       ? std::nullopt
                       : std::optional<QString> {subject.nameCn},
        .summary = subject.summary.isEmpty()
                       ? std::nullopt
                       : std::optional<QString> {subject.summary},
        .airDate = optionalDate(subject.date),
        .coverUrl = std::move(coverUrl),
        .aliases = std::nullopt,
        .tags = std::move(tags),
        .fetchedAt = fetchedAt,
        .remoteUpdatedAt = std::nullopt,
    };
}

auto fetchAllEpisodes(LocalMediaImportService::EpisodeLookup &lookup,
                      std::int64_t subjectId)
    -> ilias::Task<LibraryResult<std::vector<BangumiEpisode>>> {
    if (!lookup || subjectId <= 0) {
        co_return ilias::Err(libraryError(
            LibraryErrorCode::RemoteLookupFailure,
            QStringLiteral("Bangumi 章节读取服务未配置")));
    }

    std::vector<BangumiEpisode> episodes;
    int offset = 0;
    int total = 0;
    do {
        auto response = co_await lookup(subjectId, 200, offset);
        if (!response) {
            co_return ilias::Err(remoteFailure(response.error()));
        }
        total = response->value.total;
        auto &page = response->value.data;
        offset += static_cast<int>(page.size());
        episodes.insert(episodes.end(),
                        std::make_move_iterator(page.begin()),
                        std::make_move_iterator(page.end()));
        if (page.empty()) {
            break;
        }
    } while (offset < total);
    co_return episodes;
}

auto episodeSnapshots(const std::vector<BangumiEpisode> &episodes,
                      const QDateTime &fetchedAt, int baseOffset = 0)
    -> std::vector<persistence::EpisodeSnapshot> {
    std::vector<persistence::EpisodeSnapshot> snapshots;
    snapshots.reserve(episodes.size());
    for (std::size_t index = 0; index < episodes.size(); ++index) {
        const auto &episode = episodes[index];
        std::optional<std::chrono::milliseconds> duration;
        if (episode.durationSeconds && *episode.durationSeconds > 0) {
            duration =
                std::chrono::milliseconds {*episode.durationSeconds * 1000};
        }
        snapshots.push_back({
            .origin = {.providerKey = QStringLiteral("bangumi"),
                       .externalId = QString::number(episode.id)},
            .sortOrder = baseOffset + static_cast<int>(index),
            .episodeType = episode.type,
            .episodeNumber = episode.episodeNumber
                                 ? episode.episodeNumber
                                 : std::optional<double> {episode.sort},
            .title = episode.name.isEmpty()
                         ? std::nullopt
                         : std::optional<QString> {episode.name},
            .titleCn = episode.nameCn.isEmpty()
                           ? std::nullopt
                           : std::optional<QString> {episode.nameCn},
            .summary = episode.summary.isEmpty()
                           ? std::nullopt
                           : std::optional<QString> {episode.summary},
            .airDate = optionalDate(episode.airDate),
            .duration = duration,
            .fetchedAt = fetchedAt,
            .remoteUpdatedAt = std::nullopt,
        });
    }
    return snapshots;
}

} // namespace

auto resolveLocalMediaUrl(const MediaEntry &entry) -> LibraryResult<QUrl> {
    if (entry.resource.providerKey != QString::fromLatin1(kLocalFileProviderKey)
        || entry.resource.descriptorVersion != 1) {
        return ilias::Err(libraryError(
            LibraryErrorCode::InvalidMediaDescriptor,
            QStringLiteral("该媒体来源暂不支持本地播放")));
    }

    QJsonParseError resourceError;
    const auto resourceDocument =
        QJsonDocument::fromJson(entry.resource.descriptor, &resourceError);
    QJsonParseError itemError;
    const auto itemDocument =
        QJsonDocument::fromJson(entry.item.descriptor, &itemError);
    if (resourceError.error != QJsonParseError::NoError
        || itemError.error != QJsonParseError::NoError
        || !resourceDocument.isObject() || !itemDocument.isObject()) {
        return ilias::Err(libraryError(
            LibraryErrorCode::InvalidMediaDescriptor,
            QStringLiteral("本地媒体描述符损坏")));
    }

    const QString directory = resourceDocument.object()
                                  .value(QStringLiteral("directory"))
                                  .toString();
    const QString relativePath = itemDocument.object()
                                     .value(QStringLiteral("relativePath"))
                                     .toString();
    if (directory.isEmpty() || relativePath.isEmpty()
        || QFileInfo(relativePath).isAbsolute()) {
        return ilias::Err(libraryError(
            LibraryErrorCode::InvalidMediaDescriptor,
            QStringLiteral("本地媒体路径描述符无效")));
    }

    QString canonicalDirectory = QFileInfo(directory).canonicalFilePath();
    const QString candidate =
        QFileInfo(QDir(directory).filePath(relativePath)).canonicalFilePath();
    if (canonicalDirectory.isEmpty() || candidate.isEmpty()) {
        return ilias::Err(libraryError(
            LibraryErrorCode::MediaFileNotFound,
            QStringLiteral("本地媒体文件或所属目录不存在")));
    }

    canonicalDirectory =
        QDir::cleanPath(QDir::fromNativeSeparators(canonicalDirectory));
    QString canonicalCandidate =
        QDir::cleanPath(QDir::fromNativeSeparators(candidate));
    QString directoryPrefix = canonicalDirectory;
    if (!directoryPrefix.endsWith(QChar('/'))) {
        directoryPrefix += QChar('/');
    }
#if defined(Q_OS_WIN)
    directoryPrefix = directoryPrefix.toCaseFolded();
    canonicalCandidate = canonicalCandidate.toCaseFolded();
#endif
    if (!canonicalCandidate.startsWith(directoryPrefix)) {
        return ilias::Err(libraryError(
            LibraryErrorCode::InvalidMediaDescriptor,
            QStringLiteral("本地媒体路径越出了已导入目录")));
    }

    const QFileInfo file(candidate);
    if (!file.isFile() || !file.isReadable()) {
        return ilias::Err(libraryError(
            LibraryErrorCode::MediaFileUnreadable,
            QStringLiteral("本地媒体文件不可读取：%1")
                .arg(QDir::toNativeSeparators(candidate))));
    }
    return QUrl::fromLocalFile(candidate);
}

LocalMediaImportService::LocalMediaImportService(
    persistence::LibraryStore &store, persistence::CatalogStore &catalog,
    BangumiModule &bangumi, MediaLauncher launcher)
    : LocalMediaImportService(
          store, catalog,
          [&bangumi](BangumiSubjectSearchQuery query) {
              return bangumi.searchSubjects(std::move(query));
          },
          [&bangumi](std::int64_t subjectId) {
              return bangumi.getSubject(subjectId);
          },
          [&bangumi](std::int64_t subjectId, int limit, int offset) {
              return bangumi.getEpisodes(subjectId, limit, offset);
          },
          std::move(launcher)) {}

LocalMediaImportService::LocalMediaImportService(
    persistence::LibraryStore &store, persistence::CatalogStore &catalog,
    SubjectLookup subjects, EpisodeLookup episodes, MediaLauncher launcher)
    : LocalMediaImportService(store, catalog, std::move(subjects), {},
                              std::move(episodes), std::move(launcher)) {}

LocalMediaImportService::LocalMediaImportService(
    persistence::LibraryStore &store, persistence::CatalogStore &catalog,
    SubjectLookup subjects, SubjectDetailsLookup subjectDetails,
    EpisodeLookup episodes, MediaLauncher launcher)
    : mStore(store), mCatalog(&catalog),
      mSubjectLookup(std::move(subjects)),
      mSubjectDetailsLookup(std::move(subjectDetails)),
      mEpisodeLookup(std::move(episodes)), mLauncher(std::move(launcher)) {
    if (!mLauncher) {
        mLauncher = [](const QUrl &url) {
            return QDesktopServices::openUrl(url);
        };
    }
}

auto LocalMediaImportService::importFiles(QList<QUrl> files)
    -> ilias::Task<LibraryResult<LocalMediaImportResult>> {
    if (files.isEmpty()) {
        co_return ilias::Err(libraryError(
            LibraryErrorCode::EmptyImport,
            QStringLiteral("请至少选择一个本地媒体文件")));
    }

    const QDateTime observedAt = QDateTime::currentDateTimeUtc();
    std::vector<MediaDiscovery> discoveries;
    QHash<QString, qsizetype> resourceIndices;
    QSet<QString> selectedItems;
    std::size_t duplicates = 0;

    for (const auto &url : files) {
        if (!url.isValid() || !url.isLocalFile()) {
            co_return ilias::Err(libraryError(
                LibraryErrorCode::InvalidMediaUrl,
                QStringLiteral("媒体导入只接受本地文件 URL")));
        }

        const QFileInfo requested(url.toLocalFile());
        if (!requested.exists() || !requested.isFile()) {
            co_return ilias::Err(libraryError(
                LibraryErrorCode::MediaFileNotFound,
                QStringLiteral("找不到媒体文件：%1")
                    .arg(QDir::toNativeSeparators(requested.filePath()))));
        }
        if (!requested.isReadable()) {
            co_return ilias::Err(libraryError(
                LibraryErrorCode::MediaFileUnreadable,
                QStringLiteral("无法读取媒体文件：%1")
                    .arg(QDir::toNativeSeparators(requested.filePath()))));
        }

        const QString canonicalFile = requested.canonicalFilePath();
        if (canonicalFile.isEmpty()) {
            co_return ilias::Err(libraryError(
                LibraryErrorCode::MediaFileNotFound,
                QStringLiteral("无法解析媒体文件路径：%1")
                    .arg(QDir::toNativeSeparators(requested.filePath()))));
        }
        QFileInfo canonicalInfo(canonicalFile);
        QString canonicalDirectory = canonicalInfo.dir().canonicalPath();
        if (canonicalDirectory.isEmpty()) {
            canonicalDirectory = canonicalInfo.absolutePath();
        }

        const QString resourceKey = stablePathKey(canonicalDirectory);
        const QString relativePath =
            QDir(canonicalDirectory).relativeFilePath(canonicalFile);
        const QString itemKey = stablePathKey(relativePath);
        const QString itemIdentity =
            resourceKey + QChar(0x1f) + itemKey;
        if (selectedItems.contains(itemIdentity)) {
            ++duplicates;
            continue;
        }
        selectedItems.insert(itemIdentity);

        qsizetype resourceIndex = resourceIndices.value(resourceKey, -1);
        if (resourceIndex < 0) {
            QString displayName = QFileInfo(canonicalDirectory).fileName();
            if (displayName.isEmpty()) {
                displayName = QDir::toNativeSeparators(canonicalDirectory);
            }
            resourceIndex = static_cast<qsizetype>(discoveries.size());
            resourceIndices.insert(resourceKey, resourceIndex);
            discoveries.push_back({
                .resource =
                    {
                        .providerKey = QString::fromLatin1(
                            kLocalFileProviderKey),
                        .stableKey = resourceKey,
                        .descriptorVersion = 1,
                        .descriptor = encodeDescriptor(
                            u"directory", canonicalDirectory),
                        .displayName = std::move(displayName),
                    },
                .items = {},
                .observedAt = observedAt,
            });
        }

        discoveries[static_cast<std::size_t>(resourceIndex)].items.push_back({
            .stableKey = itemKey,
            .descriptor = encodeDescriptor(u"relativePath", relativePath),
            .displayName = canonicalInfo.fileName(),
            .duration = std::nullopt,
        });
    }

    if (discoveries.empty()) {
        co_return ilias::Err(libraryError(
            LibraryErrorCode::EmptyImport,
            QStringLiteral("没有可导入的本地媒体文件")));
    }

    const std::size_t persistedCount = selectedItems.size();
    auto stored =
        co_await mStore.upsertDiscoveredMedia(std::move(discoveries));
    if (!stored) {
        AL_LOG_ERROR("[library.import] persistence failed files={}",
                     persistedCount);
        co_return ilias::Err(persistenceFailure(stored.error()));
    }

    AL_LOG_INFO(
        "[library.import] completed resources={} files={} duplicates={}",
        stored->size(), persistedCount, duplicates);
    co_return LocalMediaImportResult {
        .resources = std::move(*stored),
        .persistedFileCount = persistedCount,
        .duplicateSelectionCount = duplicates,
    };
}

auto LocalMediaImportService::listMedia()
    -> ilias::Task<LibraryResult<std::vector<MediaEntry>>> {
    auto entries = co_await mStore.listMediaEntries();
    if (!entries) {
        co_return ilias::Err(persistenceFailure(entries.error()));
    }
    co_return std::move(*entries);
}

auto LocalMediaImportService::listLibraryMedia()
    -> ilias::Task<LibraryResult<std::vector<LibraryMediaEntry>>> {
    auto media = co_await mStore.listMediaEntries();
    if (!media) {
        co_return ilias::Err(persistenceFailure(media.error()));
    }

    std::vector<LibraryMediaEntry> entries;
    entries.reserve(media->size());
    for (auto &entry : *media) {
        LibraryMediaEntry value {.media = std::move(entry),
                                 .associations = {}};
        if (mCatalog != nullptr) {
            auto links =
                co_await mStore.listSourceItemMediaLinks(value.media.item.id);
            if (!links) {
                co_return ilias::Err(persistenceFailure(links.error()));
            }
            value.associations.reserve(links->size());
            for (const auto &link : *links) {
                auto episode = co_await mCatalog->getEpisode(link.episodeId);
                if (!episode) {
                    co_return ilias::Err(catalogFailure(episode.error()));
                }
                if (!*episode) {
                    continue;
                }
                auto subject =
                    co_await mCatalog->getSubject((*episode)->subjectId);
                if (!subject) {
                    co_return ilias::Err(catalogFailure(subject.error()));
                }
                if (!*subject) {
                    continue;
                }
                const QString subjectTitle = preferredTitle(
                    (*subject)->summary.titleCn.value_or(QString {}),
                    (*subject)->summary.title);
                QString episodeTitle = preferredTitle(
                    (*episode)->titleCn.value_or(QString {}),
                    (*episode)->title.value_or(QString {}));
                if (episodeTitle.isEmpty()) {
                    episodeTitle = QStringLiteral("标题待公布");
                }
                value.associations.push_back({
                    .episodeId = (*episode)->id,
                    .subjectId = (*episode)->subjectId,
                    .subjectTitle = subjectTitle,
                    .episodeTitle = episodeTitle,
                    .episodeNumber = (*episode)->episodeNumber,
                    .episodeType = (*episode)->episodeType,
                    .sortOrder = (*episode)->sortOrder,
                    .subjectCoverUrl =
                        (*subject)->coverUrl
                            ? (*subject)->coverUrl->toString()
                            : QString {},
                });
            }
        }
        entries.push_back(std::move(value));
    }
    co_return entries;
}

auto LocalMediaImportService::removeMedia(SourceItemId item)
    -> ilias::Task<LibraryResult<void>> {
    if (!isValid(item)) {
        co_return ilias::Err(libraryError(
            LibraryErrorCode::InvalidIdentity,
            QStringLiteral("媒体项 ID 无效")));
    }

    auto removed = co_await mStore.removeSourceItem(item);
    if (!removed) {
        co_return ilias::Err(persistenceFailure(removed.error()));
    }
    if (!*removed) {
        co_return ilias::Err(libraryError(
            LibraryErrorCode::MediaItemNotFound,
            QStringLiteral("该媒体文件已不在本地媒体库中")));
    }

    AL_LOG_INFO("[library.remove] completed source_item_id={}",
                item.value);
    co_return LibraryResult<void> {};
}

auto LocalMediaImportService::searchAssociationSubjects(QString query)
    -> ilias::Task<LibraryResult<std::vector<BangumiSearchSubject>>> {
    query = query.trimmed();
    if (!mSubjectLookup || query.isEmpty()) {
        co_return ilias::Err(libraryError(
            LibraryErrorCode::RemoteLookupFailure,
            query.isEmpty() ? QStringLiteral("请输入要搜索的动画名称")
                            : QStringLiteral("Bangumi 搜索服务未配置")));
    }

    BangumiSubjectSearchQuery request {
        .keyword = std::move(query),
        .sort = BangumiSubjectSearchSort::Match,
        .filter = {.types = {BangumiSubjectType::Anime}},
        .limit = 20,
        .offset = 0,
    };
    auto result = co_await mSubjectLookup(std::move(request));
    if (!result) {
        co_return ilias::Err(remoteFailure(result.error()));
    }
    co_return std::move(result->value.data);
}

auto LocalMediaImportService::loadAssociationEpisodes(
    const BangumiSearchSubject &subject, int limit, int offset)
    -> ilias::Task<LibraryResult<AssociationEpisodePage>> {
    if (mCatalog == nullptr || !mEpisodeLookup || subject.id <= 0
        || limit < 1 || limit > 200 || offset < 0) {
        co_return ilias::Err(libraryError(
            LibraryErrorCode::RemoteLookupFailure,
            QStringLiteral("章节关联服务未配置或分页参数无效")));
    }

    const QDateTime fetchedAt = QDateTime::currentDateTimeUtc();
    auto localSubject = co_await mCatalog->upsertSubjectSnapshot(
        subjectSnapshot(subject,
                        persistence::SubjectMetadataLevel::Summary,
                        fetchedAt));
    if (!localSubject) {
        co_return ilias::Err(catalogFailure(localSubject.error()));
    }

    auto remotePage = co_await mEpisodeLookup(subject.id, limit, offset);
    if (!remotePage) {
        co_return ilias::Err(remoteFailure(remotePage.error()));
    }
    auto &page = remotePage->value;
    auto localEpisodes = co_await mCatalog->upsertEpisodeSnapshots(
        *localSubject,
        episodeSnapshots(page.data, fetchedAt, page.offset));
    if (!localEpisodes) {
        co_return ilias::Err(catalogFailure(localEpisodes.error()));
    }

    std::vector<AssociationEpisodeOption> options;
    options.reserve(localEpisodes->size());
    for (std::size_t index = 0; index < localEpisodes->size(); ++index) {
        const auto &episode = page.data.at(index);
        QString title = preferredTitle(episode.nameCn, episode.name);
        if (title.isEmpty()) {
            title = QStringLiteral("标题待公布");
        }
        options.push_back({
            .id = localEpisodes->at(index),
            .title = std::move(title),
            .displayNumber = displayEpisodeNumber(episode),
            .episodeNumber = episode.episodeNumber
                                 ? episode.episodeNumber
                                 : std::optional<double> {episode.sort},
            .episodeType = episode.type,
        });
    }
    co_return AssociationEpisodePage {
        .episodes = std::move(options),
        .total = page.total,
        .limit = page.limit,
        .offset = page.offset,
    };
}

auto LocalMediaImportService::linkMedia(SourceItemId item, EpisodeId episode)
    -> ilias::Task<LibraryResult<void>> {
    if (!isValid(item) || !isValid(episode)) {
        co_return ilias::Err(libraryError(
            LibraryErrorCode::InvalidIdentity,
            QStringLiteral("媒体或章节 ID 无效")));
    }
    auto linked = co_await mStore.upsertEpisodeMediaLink({
        .episodeId = episode,
        .sourceItemId = item,
        .kind = MediaLinkKind::Manual,
        .updatedAt = QDateTime::currentDateTimeUtc(),
    });
    if (!linked) {
        co_return ilias::Err(persistenceFailure(linked.error()));
    }
    co_return LibraryResult<void> {};
}

auto LocalMediaImportService::unlinkMedia(SourceItemId item,
                                          EpisodeId episode)
    -> ilias::Task<LibraryResult<void>> {
    if (!isValid(item) || !isValid(episode)) {
        co_return ilias::Err(libraryError(
            LibraryErrorCode::InvalidIdentity,
            QStringLiteral("媒体或章节 ID 无效")));
    }
    auto removed = co_await mStore.removeEpisodeMediaLink(episode, item);
    if (!removed) {
        co_return ilias::Err(persistenceFailure(removed.error()));
    }
    if (!*removed) {
        co_return ilias::Err(libraryError(
            LibraryErrorCode::MediaItemNotFound,
            QStringLiteral("该媒体关联已经不存在")));
    }
    co_return LibraryResult<void> {};
}

auto LocalMediaImportService::playMedia(SourceItemId item)
    -> ilias::Task<LibraryResult<void>> {
    if (!isValid(item)) {
        co_return ilias::Err(libraryError(
            LibraryErrorCode::InvalidIdentity,
            QStringLiteral("媒体项 ID 无效")));
    }
    auto entries = co_await mStore.listMediaEntries();
    if (!entries) {
        co_return ilias::Err(persistenceFailure(entries.error()));
    }
    const auto found = std::ranges::find_if(
        *entries, [item](const MediaEntry &entry) {
            return entry.item.id == item;
        });
    if (found == entries->end()) {
        co_return ilias::Err(libraryError(
            LibraryErrorCode::MediaItemNotFound,
            QStringLiteral("该媒体文件已不在本地媒体库中")));
    }
    auto url = resolveLocalMediaUrl(*found);
    if (!url) {
        co_return ilias::Err(std::move(url.error()));
    }
    if (!mLauncher || !mLauncher(*url)) {
        co_return ilias::Err(libraryError(
            LibraryErrorCode::MediaLaunchFailed,
            QStringLiteral("系统没有接受该视频的播放请求")));
    }
    AL_LOG_INFO("[library.playback] external player launched "
                "source_item_id={}",
                item.value);
    co_return LibraryResult<void> {};
}

auto LocalMediaImportService::findBangumiSubject(
    std::int64_t bangumiSubjectId)
    -> ilias::Task<LibraryResult<std::optional<SubjectId>>> {
    if (mCatalog == nullptr || bangumiSubjectId <= 0) {
        co_return ilias::Err(libraryError(
            LibraryErrorCode::InvalidIdentity,
            QStringLiteral("Bangumi 条目 ID 无效或目录服务未配置")));
    }
    auto found = co_await mCatalog->findSubjectByExternalRef({
        .providerKey = QStringLiteral("bangumi"),
        .externalId = QString::number(bangumiSubjectId),
    });
    if (!found) {
        co_return ilias::Err(catalogFailure(found.error()));
    }
    co_return *found;
}

auto LocalMediaImportService::ensureBangumiSubject(
    std::int64_t bangumiSubjectId)
    -> ilias::Task<LibraryResult<SubjectId>> {
    if (mCatalog == nullptr || bangumiSubjectId <= 0) {
        co_return ilias::Err(libraryError(
            LibraryErrorCode::InvalidIdentity,
            QStringLiteral("Bangumi 条目 ID 无效或目录服务未配置")));
    }

    auto found = co_await findBangumiSubject(bangumiSubjectId);
    if (!found) {
        co_return ilias::Err(std::move(found.error()));
    }
    std::optional<SubjectId> localSubject = *found;
    const QDateTime checkedAt = QDateTime::currentDateTimeUtc();
    if (localSubject) {
        auto stored = co_await mCatalog->getSubject(*localSubject);
        if (!stored) {
            co_return ilias::Err(catalogFailure(stored.error()));
        }
        if (*stored && subjectDetailsAreFresh(**stored, checkedAt)) {
            co_return *localSubject;
        }
    }

    if (!mSubjectDetailsLookup || !mEpisodeLookup) {
        if (localSubject) {
            AL_LOG_WARN(
                "[library.catalog] remote refresh unavailable; using local "
                "summary subject_id={}",
                localSubject->value);
            co_return *localSubject;
        }
        co_return ilias::Err(libraryError(
            LibraryErrorCode::RemoteLookupFailure,
            QStringLiteral("Bangumi 条目详情服务未配置")));
    }

    auto remoteSubject = co_await mSubjectDetailsLookup(bangumiSubjectId);
    if (!remoteSubject) {
        if (localSubject) {
            AL_LOG_WARN(
                "[library.catalog] subject refresh failed code={}; using "
                "local summary subject_id={}",
                bangumiErrorCodeName(remoteSubject.error().code),
                localSubject->value);
            co_return *localSubject;
        }
        co_return ilias::Err(remoteFailure(remoteSubject.error()));
    }
    if (remoteSubject->value.id != bangumiSubjectId) {
        co_return ilias::Err(libraryError(
            LibraryErrorCode::RemoteLookupFailure,
            QStringLiteral("Bangumi 条目详情 ID 与请求不一致")));
    }

    auto remoteEpisodes =
        co_await fetchAllEpisodes(mEpisodeLookup, bangumiSubjectId);
    if (!remoteEpisodes) {
        if (localSubject) {
            AL_LOG_WARN(
                "[library.catalog] episode refresh failed; using local "
                "summary subject_id={}",
                localSubject->value);
            co_return *localSubject;
        }
        co_return ilias::Err(std::move(remoteEpisodes.error()));
    }

    const QDateTime fetchedAt = QDateTime::currentDateTimeUtc();
    auto persistedSubject = co_await mCatalog->upsertSubjectSnapshot(
        subjectSnapshot(remoteSubject->value,
                        persistence::SubjectMetadataLevel::Summary,
                        fetchedAt));
    if (!persistedSubject) {
        co_return ilias::Err(catalogFailure(persistedSubject.error()));
    }
    auto persistedEpisodes = co_await mCatalog->upsertEpisodeSnapshots(
        *persistedSubject, episodeSnapshots(*remoteEpisodes, fetchedAt));
    if (!persistedEpisodes) {
        co_return ilias::Err(catalogFailure(persistedEpisodes.error()));
    }
    auto completedSubject = co_await mCatalog->upsertSubjectSnapshot(
        subjectSnapshot(remoteSubject->value,
                        persistence::SubjectMetadataLevel::Details,
                        fetchedAt));
    if (!completedSubject) {
        co_return ilias::Err(catalogFailure(completedSubject.error()));
    }

    AL_LOG_INFO(
        "[library.catalog] subject details persisted subject_id={} "
        "episode_count={}",
        completedSubject->value, persistedEpisodes->size());
    co_return *completedSubject;
}

auto LocalMediaImportService::getSubjectLibraryDetails(SubjectId subject,
                                                       int limit,
                                                       int offset,
                                                       bool descending)
    -> ilias::Task<LibraryResult<std::optional<SubjectLibraryDetails>>> {
    if (mCatalog == nullptr || !isValid(subject) || limit <= 0 || limit > 50
        || offset < 0) {
        co_return ilias::Err(libraryError(
            LibraryErrorCode::InvalidIdentity,
            QStringLiteral("本地条目 ID 或章节分页参数无效")));
    }
    auto storedSubject = co_await mCatalog->getSubject(subject);
    if (!storedSubject) {
        co_return ilias::Err(catalogFailure(storedSubject.error()));
    }
    if (!*storedSubject) {
        co_return std::optional<SubjectLibraryDetails> {};
    }
    auto episodes = co_await mCatalog->listEpisodesPage(
        subject, limit, offset, descending);
    if (!episodes) {
        co_return ilias::Err(catalogFailure(episodes.error()));
    }
    auto mediaEntries = co_await mStore.listMediaEntries();
    if (!mediaEntries) {
        co_return ilias::Err(persistenceFailure(mediaEntries.error()));
    }

    SubjectLibraryDetails details {
        .subject = std::move(**storedSubject),
        .episodes = {},
        .totalEpisodeCount = episodes->total,
        .offset = episodes->offset,
    };
    details.episodes.reserve(episodes->items.size());
    for (auto &episode : episodes->items) {
        EpisodeLibraryEntry entry {.episode = std::move(episode), .media = {}};
        auto links =
            co_await mStore.listEpisodeMediaLinks(entry.episode.id);
        if (!links) {
            co_return ilias::Err(persistenceFailure(links.error()));
        }
        entry.media.reserve(links->size());
        for (const auto &link : *links) {
            const auto found = std::ranges::find_if(
                *mediaEntries, [&link](const MediaEntry &media) {
                    return media.item.id == link.sourceItemId;
                });
            if (found != mediaEntries->end()) {
                entry.media.push_back(*found);
            }
        }
        details.episodes.push_back(std::move(entry));
    }
    co_return std::optional<SubjectLibraryDetails> {std::move(details)};
}

auto LocalMediaImportService::playEpisode(EpisodeId episode)
    -> ilias::Task<LibraryResult<void>> {
    if (!isValid(episode)) {
        co_return ilias::Err(libraryError(
            LibraryErrorCode::InvalidIdentity,
            QStringLiteral("章节 ID 无效")));
    }
    auto links = co_await mStore.listEpisodeMediaLinks(episode);
    if (!links) {
        co_return ilias::Err(persistenceFailure(links.error()));
    }
    if (links->empty()) {
        co_return ilias::Err(libraryError(
            LibraryErrorCode::MediaItemNotFound,
            QStringLiteral("该章节还没有关联本地媒体")));
    }
    co_return co_await playMedia(links->front().sourceItemId);
}

} // namespace anime_land
