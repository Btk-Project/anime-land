#pragma once

#include <ilias/task.hpp>

#include "presentation/bangumi/bangumi_presenter.hpp"
#include "view/cli/bangumi_cli_options.hpp"

namespace anime_land::cli {

auto runBangumiCliCommand(BangumiPresenter &presenter, BangumiView &view,
                          const Command &command) -> ilias::Task<int>;

} // namespace anime_land::cli
