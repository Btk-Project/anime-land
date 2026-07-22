#pragma once

#include <QMetaObject>

#include <ilias/task.hpp>

#include "bangumi/bangumi.hpp"
#include "presentation/bangumi_view.hpp"
#include "presentation/cli/bangumi_cli_options.hpp"

#include <functional>
#include <optional>

namespace anime_land {

/**
 * Presenter for Bangumi account actions.
 *
 * It is the only object allowed to translate Model results into View output and
 * process exit codes. It never parses OAuth payloads or accesses raw tokens.
 */
class BangumiPresenter final {
public:
  using OAuthApplicationSaver =
      std::function<ilias::Task<BangumiResult<void>>(BangumiOAuthApplication)>;

  BangumiPresenter(BangumiModule &module, BangumiView &view,
                   OAuthApplicationSaver applicationSaver);
  ~BangumiPresenter();

  auto run(const cli::LoginCommand &) -> ilias::Task<int>;
  auto run(const cli::StatusCommand &) -> ilias::Task<int>;
  auto run(const cli::LogoutCommand &) -> ilias::Task<int>;
  auto run(const cli::CollectionsCommand &command) -> ilias::Task<int>;

private:
  auto present(BangumiResult<BangumiUser> result) -> int;
  auto present(BangumiResult<void> result, QStringView successMessage) -> int;
  auto present(BangumiResult<BangumiUserCollectionsResponse> result) -> int;
  static auto exitCode(const BangumiError &error) -> int;
  auto ensureOAuthApplication()
      -> ilias::Task<BangumiResult<std::optional<BangumiOAuthApplication>>>;

  BangumiModule &mModule;
  BangumiView &mView;
  OAuthApplicationSaver mApplicationSaver;
  QMetaObject::Connection mStateConnection;
};

} // namespace anime_land
