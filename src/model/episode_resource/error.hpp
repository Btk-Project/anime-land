#pragma once

#include <QString>

#include <ilias/result.hpp>

#include <string_view>

namespace anime_land {

enum class EpisodeProviderErrorCode {
    InvalidQuery,
    InvalidProvider,
    DuplicateProvider,
    ProviderNotFound,
    InvalidManifest,
    InvalidConfiguration,
    InvalidScript,
    InvalidScriptResult,
    PermissionDenied,
    Busy,
    Cancelled,
    NetworkError,
    HttpError,
    ResponseTooLarge,
    RequestLimitExceeded,
    ResultExpired,
};

struct EpisodeProviderError {
    EpisodeProviderErrorCode code = EpisodeProviderErrorCode::InvalidProvider;
    QString message;
    bool retryable = false;
};

template <typename T>
using EpisodeProviderResult = ilias::Result<T, EpisodeProviderError>;

auto episodeProviderError(EpisodeProviderErrorCode code, QString message,
                          bool retryable = false) -> EpisodeProviderError;
auto episodeProviderErrorCodeName(EpisodeProviderErrorCode code)
    -> std::string_view;

} // namespace anime_land
