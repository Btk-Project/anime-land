#pragma once

namespace anime_land {

class ProcessGuard final {
public:
    ProcessGuard();
    ~ProcessGuard();

    ProcessGuard(const ProcessGuard &) = delete;
    auto operator=(const ProcessGuard &) -> ProcessGuard & = delete;
};

} // namespace anime_land
