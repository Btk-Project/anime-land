#include "pch.hpp"

#include "model/library/media.hpp"

#include <utility>

namespace anime_land {

auto libraryError(LibraryErrorCode code, QString message) -> LibraryError {
    return {.code = code, .message = std::move(message)};
}

auto libraryErrorCodeName(LibraryErrorCode code) -> std::string_view {
    switch (code) {
        case LibraryErrorCode::InvalidIdentity:
            return "invalid-identity";
        case LibraryErrorCode::InvalidProviderKey:
            return "invalid-provider-key";
        case LibraryErrorCode::InvalidStableKey:
            return "invalid-stable-key";
        case LibraryErrorCode::InvalidDescriptorVersion:
            return "invalid-descriptor-version";
        case LibraryErrorCode::InvalidDuration:
            return "invalid-duration";
        case LibraryErrorCode::InvalidPosition:
            return "invalid-position";
        case LibraryErrorCode::InvalidTimestamp:
            return "invalid-timestamp";
    }
    return "unknown";
}

auto mediaLinkKindName(MediaLinkKind kind) -> std::string_view {
    switch (kind) {
        case MediaLinkKind::Manual:
            return "manual";
        case MediaLinkKind::Filename:
            return "filename";
        case MediaLinkKind::Sequence:
            return "sequence";
    }
    return "unknown";
}

auto validate(const MediaResource &resource) -> LibraryResult<void> {
    if (!isValid(resource.id)) {
        return ilias::Err(libraryError(LibraryErrorCode::InvalidIdentity, QStringLiteral("媒体资源 ID 无效")));
    }
    if (resource.providerKey.trimmed().isEmpty()) {
        return ilias::Err(libraryError(LibraryErrorCode::InvalidProviderKey, QStringLiteral("媒体资源 providerKey 不能为空")));
    }
    if (resource.stableKey.trimmed().isEmpty()) {
        return ilias::Err(libraryError(LibraryErrorCode::InvalidStableKey, QStringLiteral("媒体资源 stableKey 不能为空")));
    }
    if (resource.descriptorVersion <= 0) {
        return ilias::Err(libraryError(LibraryErrorCode::InvalidDescriptorVersion, QStringLiteral("媒体资源 descriptorVersion 必须为正数")));
    }
    return {};
}

auto validate(const SourceItem &item) -> LibraryResult<void> {
    if (!isValid(item.id) || !isValid(item.resourceId)) {
        return ilias::Err(libraryError(LibraryErrorCode::InvalidIdentity, QStringLiteral("媒体项 ID 或资源 ID 无效")));
    }
    if (item.stableKey.trimmed().isEmpty()) {
        return ilias::Err(libraryError(LibraryErrorCode::InvalidStableKey, QStringLiteral("媒体项 stableKey 不能为空")));
    }
    if (item.duration && *item.duration < std::chrono::milliseconds::zero()) {
        return ilias::Err(libraryError(LibraryErrorCode::InvalidDuration, QStringLiteral("媒体项时长不能为负数")));
    }
    return {};
}

auto validate(const EpisodeMediaLink &link) -> LibraryResult<void> {
    if (!isValid(link.episodeId) || !isValid(link.sourceItemId)) {
        return ilias::Err(libraryError(LibraryErrorCode::InvalidIdentity, QStringLiteral("章节关联包含无效 ID")));
    }
    if (!link.updatedAt.isValid()) {
        return ilias::Err(libraryError(LibraryErrorCode::InvalidTimestamp, QStringLiteral("章节关联更新时间无效")));
    }
    return {};
}

} // namespace anime_land
