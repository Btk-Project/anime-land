#pragma once

#include <QString>

#include <ilias/result.hpp>

#include <string_view>

namespace anime_land {

enum class LibraryErrorCode {
    InvalidIdentity,
    InvalidProviderKey,
    InvalidStableKey,
    InvalidDescriptorVersion,
    InvalidDuration,
    InvalidPosition,
    InvalidTimestamp,
};

struct LibraryError {
    LibraryErrorCode code = LibraryErrorCode::InvalidIdentity;
    QString message;
};

template <typename T>
using LibraryResult = ilias::Result<T, LibraryError>;

auto libraryError(LibraryErrorCode code, QString message) -> LibraryError;
auto libraryErrorCodeName(LibraryErrorCode code) -> std::string_view;

} // namespace anime_land
