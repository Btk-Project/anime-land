#include "model/library/local_metadata.hpp"

#include "common/log.hpp"
#include "model/library/media.hpp"
#include "model/persistence/catalog_store.hpp"
#include "model/persistence/library_store.hpp"

#include <QDateTime>
#include <QUuid>

#include <cmath>
#include <system_error>
#include <utility>
#include <vector>

namespace anime_land {
namespace {

constexpr auto kLocalMetadataProvider = "local-manual";

auto persistenceFailure(const std::error_code &error) -> LibraryError {
    return libraryError(
        LibraryErrorCode::PersistenceFailure,
        QStringLiteral("无法写入本地元数据：%1")
            .arg(QString::fromStdString(error.message())));
}

auto newExternalId() -> QString {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

auto normalizeMetadata(LocalSubjectMetadata &metadata)
    -> std::optional<LibraryError> {
    metadata.displayTitle = metadata.displayTitle.trimmed();
    metadata.originalTitle = metadata.originalTitle.trimmed();
    metadata.summary = metadata.summary.trimmed();
    metadata.episodeTitle = metadata.episodeTitle.trimmed();
    if (metadata.displayTitle.isEmpty()) {
        return libraryError(LibraryErrorCode::InvalidStableKey,
                            QStringLiteral("自定义条目标题不能为空"));
    }
    if (metadata.episodeNumber
        && (!std::isfinite(*metadata.episodeNumber)
            || *metadata.episodeNumber < 0.0)) {
        return libraryError(LibraryErrorCode::InvalidPosition,
                            QStringLiteral("章节序号必须是非负数字"));
    }
    return std::nullopt;
}

} // namespace

auto LocalMetadataService::createAndLink(
    SourceItemId item, LocalSubjectMetadata metadata)
    -> ilias::Task<LibraryResult<SubjectId>> {
    if (!isValid(item)) {
        co_return ilias::Err(libraryError(
            LibraryErrorCode::InvalidIdentity,
            QStringLiteral("媒体项 ID 无效")));
    }
    if (auto error = normalizeMetadata(metadata)) {
        co_return ilias::Err(std::move(*error));
    }

    const QString subjectExternalId = newExternalId();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    std::optional<QString> titleCn;
    QString storedTitle = metadata.displayTitle;
    if (!metadata.originalTitle.isEmpty()
        && metadata.originalTitle != metadata.displayTitle) {
        storedTitle = metadata.originalTitle;
        titleCn = metadata.displayTitle;
    }

    std::optional<std::vector<QString>> aliases = std::vector<QString> {};
    auto subject = co_await mCatalog.upsertSubjectSnapshot({
        .origin = {
            .providerKey = QString::fromLatin1(kLocalMetadataProvider),
            .externalId = subjectExternalId,
        },
        .metadataLevel = persistence::SubjectMetadataLevel::Details,
        .subjectType = 2,
        .title = std::move(storedTitle),
        .titleCn = std::move(titleCn),
        .summary = metadata.summary.isEmpty()
                       ? std::optional<QString> {}
                       : std::optional<QString> {metadata.summary},
        .airDate = std::nullopt,
        .coverUrl = std::move(metadata.coverUrl),
        .aliases = std::move(aliases),
        .tags = std::vector<persistence::SubjectTagSnapshot> {},
        .fetchedAt = now,
        .remoteUpdatedAt = std::nullopt,
    });
    if (!subject) {
        co_return ilias::Err(persistenceFailure(subject.error()));
    }

    auto episodes = co_await mCatalog.upsertEpisodeSnapshots(
        *subject,
        {{
            .origin = {
                .providerKey = QString::fromLatin1(kLocalMetadataProvider),
                .externalId = subjectExternalId
                              + QStringLiteral(":episode:1"),
            },
            .sortOrder = 0,
            .episodeType = 0,
            .episodeNumber = metadata.episodeNumber,
            .title = metadata.episodeTitle.isEmpty()
                         ? std::optional<QString> {}
                         : std::optional<QString> {
                               std::move(metadata.episodeTitle)},
            .titleCn = std::nullopt,
            .summary = std::nullopt,
            .airDate = std::nullopt,
            .duration = std::nullopt,
            .fetchedAt = now,
            .remoteUpdatedAt = std::nullopt,
        }});
    if (!episodes) {
        co_return ilias::Err(persistenceFailure(episodes.error()));
    }
    if (episodes->empty()) {
        co_return ilias::Err(libraryError(
            LibraryErrorCode::PersistenceFailure,
            QStringLiteral("自定义章节未能写入本地数据库")));
    }

    auto linked = co_await mLibrary.upsertEpisodeMediaLink({
        .episodeId = episodes->front(),
        .sourceItemId = item,
        .kind = MediaLinkKind::Manual,
        .updatedAt = now,
    });
    if (!linked) {
        co_return ilias::Err(persistenceFailure(linked.error()));
    }
    AL_LOG_INFO("[library.metadata] local subject created subject_id={} "
                "source_item_id={}",
                subject->value, item.value);
    co_return *subject;
}

auto LocalMetadataService::load(SubjectId subject)
    -> ilias::Task<
        LibraryResult<std::optional<StoredLocalSubjectMetadata>>> {
    if (!isValid(subject)) {
        co_return ilias::Err(libraryError(
            LibraryErrorCode::InvalidIdentity,
            QStringLiteral("本地条目 ID 无效")));
    }
    auto storedSubject = co_await mCatalog.getSubject(subject);
    if (!storedSubject) {
        co_return ilias::Err(persistenceFailure(storedSubject.error()));
    }
    if (!*storedSubject) {
        co_return std::optional<StoredLocalSubjectMetadata> {};
    }

    const auto &details = **storedSubject;
    LocalSubjectMetadata metadata;
    if (details.summary.titleCn
        && !details.summary.titleCn->trimmed().isEmpty()) {
        metadata.displayTitle = *details.summary.titleCn;
        metadata.originalTitle = details.summary.title;
    }
    else {
        metadata.displayTitle = details.summary.title;
    }
    metadata.summary = details.summary.summary.value_or(QString {});
    metadata.coverUrl = details.coverUrl;
    co_return std::optional<StoredLocalSubjectMetadata> {{
        .subjectId = subject,
        .subjectType = details.summary.subjectType,
        .metadata = std::move(metadata),
    }};
}

auto LocalMetadataService::update(SubjectId subject,
                                  LocalSubjectMetadata metadata)
    -> ilias::Task<LibraryResult<void>> {
    if (auto error = normalizeMetadata(metadata)) {
        co_return ilias::Err(std::move(*error));
    }
    auto stored = co_await load(subject);
    if (!stored) {
        co_return ilias::Err(std::move(stored.error()));
    }
    if (!*stored) {
        co_return ilias::Err(libraryError(
            LibraryErrorCode::InvalidIdentity,
            QStringLiteral("本地数据库中没有这个元数据条目")));
    }

    QString title = metadata.displayTitle;
    std::optional<QString> titleCn;
    if (!metadata.originalTitle.isEmpty()
        && metadata.originalTitle != metadata.displayTitle) {
        title = metadata.originalTitle;
        titleCn = metadata.displayTitle;
    }
    auto replaced = co_await mCatalog.replaceSubjectMetadata(
        subject,
        {
            .subjectType = (**stored).subjectType,
            .title = std::move(title),
            .titleCn = std::move(titleCn),
            .summary = metadata.summary.isEmpty()
                           ? std::optional<QString> {}
                           : std::optional<QString> {metadata.summary},
            .coverUrl = std::move(metadata.coverUrl),
        });
    if (!replaced) {
        co_return ilias::Err(persistenceFailure(replaced.error()));
    }
    AL_LOG_INFO("[library.metadata] local subject updated subject_id={}",
                subject.value);
    co_return LibraryResult<void> {};
}

auto LocalMetadataService::remove(SubjectId subject)
    -> ilias::Task<LibraryResult<void>> {
    auto stored = co_await load(subject);
    if (!stored) {
        co_return ilias::Err(std::move(stored.error()));
    }
    if (!*stored) {
        co_return ilias::Err(libraryError(
            LibraryErrorCode::InvalidIdentity,
            QStringLiteral("本地数据库中没有这个元数据条目")));
    }
    auto removed = co_await mCatalog.removeSubject(subject);
    if (!removed) {
        co_return ilias::Err(persistenceFailure(removed.error()));
    }
    if (!*removed) {
        co_return ilias::Err(libraryError(
            LibraryErrorCode::MediaItemNotFound,
            QStringLiteral("本地元数据条目已经不存在")));
    }
    AL_LOG_INFO("[library.metadata] local subject removed subject_id={}",
                subject.value);
    co_return LibraryResult<void> {};
}

} // namespace anime_land
