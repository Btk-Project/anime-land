#pragma once

#include <compare>
#include <cstdint>

namespace anime_land {

struct SubjectId {
    std::int64_t value = 0;
    auto operator<=>(const SubjectId &) const = default;
};

struct EpisodeId {
    std::int64_t value = 0;
    auto operator<=>(const EpisodeId &) const = default;
};

struct MediaResourceId {
    std::int64_t value = 0;
    auto operator<=>(const MediaResourceId &) const = default;
};

struct SourceItemId {
    std::int64_t value = 0;
    auto operator<=>(const SourceItemId &) const = default;
};

constexpr auto isValid(SubjectId id) noexcept -> bool { return id.value > 0; }
constexpr auto isValid(EpisodeId id) noexcept -> bool { return id.value > 0; }
constexpr auto isValid(MediaResourceId id) noexcept -> bool { return id.value > 0; }
constexpr auto isValid(SourceItemId id) noexcept -> bool { return id.value > 0; }

} // namespace anime_land
