#include "pch.hpp"

#include "model/episode_resource/error.hpp"

#include <utility>

namespace anime_land {

auto episodeProviderError(EpisodeProviderErrorCode code, QString message,
                          bool retryable) -> EpisodeProviderError {
    return {
        .code = code,
        .message = std::move(message),
        .retryable = retryable,
    };
}

auto episodeProviderErrorCodeName(EpisodeProviderErrorCode code)
    -> std::string_view {
    switch (code) {
        case EpisodeProviderErrorCode::InvalidQuery:
            return "invalid-query";
        case EpisodeProviderErrorCode::InvalidProvider:
            return "invalid-provider";
        case EpisodeProviderErrorCode::DuplicateProvider:
            return "duplicate-provider";
        case EpisodeProviderErrorCode::ProviderNotFound:
            return "provider-not-found";
        case EpisodeProviderErrorCode::InvalidManifest:
            return "invalid-manifest";
        case EpisodeProviderErrorCode::InvalidConfiguration:
            return "invalid-configuration";
        case EpisodeProviderErrorCode::InvalidScript:
            return "invalid-script";
        case EpisodeProviderErrorCode::InvalidScriptResult:
            return "invalid-script-result";
        case EpisodeProviderErrorCode::PermissionDenied:
            return "permission-denied";
        case EpisodeProviderErrorCode::Busy:
            return "busy";
        case EpisodeProviderErrorCode::Cancelled:
            return "cancelled";
        case EpisodeProviderErrorCode::NetworkError:
            return "network-error";
        case EpisodeProviderErrorCode::HttpError:
            return "http-error";
        case EpisodeProviderErrorCode::ResponseTooLarge:
            return "response-too-large";
        case EpisodeProviderErrorCode::RequestLimitExceeded:
            return "request-limit-exceeded";
        case EpisodeProviderErrorCode::ResultExpired:
            return "result-expired";
    }
    return "unknown";
}

} // namespace anime_land
