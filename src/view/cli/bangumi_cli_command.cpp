#include "view/cli/bangumi_cli_command.hpp"

#include <utility>

namespace anime_land::cli {
namespace {

auto collectionQuery(const CollectionsCommand &command)
    -> BangumiResult<BangumiCollectionQuery> {
    BangumiCollectionQuery query {
        .subjectType = std::nullopt,
        .collectionType = std::nullopt,
        .limit = command.limit,
        .offset = command.offset,
    };

    if (command.subjectType == "book") {
        query.subjectType = BangumiSubjectType::Book;
    }
    else if (command.subjectType == "anime") {
        query.subjectType = BangumiSubjectType::Anime;
    }
    else if (command.subjectType == "music") {
        query.subjectType = BangumiSubjectType::Music;
    }
    else if (command.subjectType == "game") {
        query.subjectType = BangumiSubjectType::Game;
    }
    else if (command.subjectType == "real") {
        query.subjectType = BangumiSubjectType::Real;
    }
    else if (command.subjectType != "all") {
        return ilias::Err(
            bangumiError(BangumiErrorCode::InvalidConfiguration,
                         QStringLiteral("未知的条目类型：%1")
                             .arg(QString::fromStdString(command.subjectType))));
    }

    if (command.collectionType == "wish") {
        query.collectionType = BangumiCollectionType::Wish;
    }
    else if (command.collectionType == "done") {
        query.collectionType = BangumiCollectionType::Done;
    }
    else if (command.collectionType == "doing") {
        query.collectionType = BangumiCollectionType::Doing;
    }
    else if (command.collectionType == "on-hold") {
        query.collectionType = BangumiCollectionType::OnHold;
    }
    else if (command.collectionType == "dropped") {
        query.collectionType = BangumiCollectionType::Dropped;
    }
    else if (command.collectionType != "all") {
        return ilias::Err(
            bangumiError(BangumiErrorCode::InvalidConfiguration,
                         QStringLiteral("未知的收藏状态：%1")
                             .arg(QString::fromStdString(command.collectionType))));
    }
    if (query.limit < 1 || query.limit > 50 || query.offset < 0) {
        return ilias::Err(bangumiError(
            BangumiErrorCode::InvalidConfiguration,
            QStringLiteral("--limit 必须为 1..50，--offset 不能为负")));
    }
    return query;
}

auto searchQuery(const SearchCommand &command)
    -> BangumiResult<BangumiSubjectSearchQuery> {
    BangumiSubjectSearchQuery query;
    query.keyword = QString::fromUtf8(
        command.keyword.data(), static_cast<qsizetype>(command.keyword.size()));
    query.limit = command.limit;
    query.offset = command.offset;

    if (command.subjectType == "book") {
        query.filter.types.push_back(BangumiSubjectType::Book);
    }
    else if (command.subjectType == "anime") {
        query.filter.types.push_back(BangumiSubjectType::Anime);
    }
    else if (command.subjectType == "music") {
        query.filter.types.push_back(BangumiSubjectType::Music);
    }
    else if (command.subjectType == "game") {
        query.filter.types.push_back(BangumiSubjectType::Game);
    }
    else if (command.subjectType == "real") {
        query.filter.types.push_back(BangumiSubjectType::Real);
    }
    else if (command.subjectType != "all") {
        return ilias::Err(
            bangumiError(BangumiErrorCode::InvalidConfiguration,
                         QStringLiteral("未知的条目类型：%1")
                             .arg(QString::fromStdString(command.subjectType))));
    }

    if (command.sort == "match") {
        query.sort = BangumiSubjectSearchSort::Match;
    }
    else if (command.sort == "heat") {
        query.sort = BangumiSubjectSearchSort::Heat;
    }
    else if (command.sort == "rank") {
        query.sort = BangumiSubjectSearchSort::Rank;
    }
    else if (command.sort == "score") {
        query.sort = BangumiSubjectSearchSort::Score;
    }
    else {
        return ilias::Err(
            bangumiError(BangumiErrorCode::InvalidConfiguration,
                         QStringLiteral("未知的搜索排序：%1")
                             .arg(QString::fromStdString(command.sort))));
    }

    const auto appendTags = [](const std::vector<std::string> &source,
                               std::vector<QString> &destination) {
        destination.reserve(source.size());
        for (const auto &value : source) {
            destination.push_back(QString::fromUtf8(
                value.data(), static_cast<qsizetype>(value.size())));
        }
    };
    appendTags(command.metaTags, query.filter.metaTags);
    appendTags(command.tags, query.filter.tags);

    if (query.limit < 1 || query.limit > 50 || query.offset < 0) {
        return ilias::Err(bangumiError(
            BangumiErrorCode::InvalidConfiguration,
            QStringLiteral("--limit 必须为 1..50，--offset 不能为负")));
    }
    return query;
}

auto exitCode(const BangumiError &error) -> int {
    switch (error.code) {
        case BangumiErrorCode::InvalidConfiguration:
        case BangumiErrorCode::InvalidState:
            return 2;
        case BangumiErrorCode::CredentialStoreError:
        case BangumiErrorCode::UnsupportedCredentialStore:
            return 4;
        default:
            return 3;
    }
}

auto finish(BangumiResult<void> result) -> int {
    return result ? 0 : exitCode(result.error());
}

auto run(BangumiPresenter &presenter, BangumiView &, const LoginCommand &)
    -> ilias::Task<int> {
    co_return finish(co_await presenter.login());
}

auto run(BangumiPresenter &presenter, BangumiView &, const StatusCommand &)
    -> ilias::Task<int> {
    co_return finish(co_await presenter.restoreSession());
}

auto run(BangumiPresenter &presenter, BangumiView &, const LogoutCommand &)
    -> ilias::Task<int> {
    co_return finish(co_await presenter.logout());
}

auto run(BangumiPresenter &presenter, BangumiView &view,
         const CollectionsCommand &command) -> ilias::Task<int> {
    auto query = collectionQuery(command);
    if (!query) {
        view.showError(query.error());
        co_return exitCode(query.error());
    }
    co_return finish(co_await presenter.getCollections(std::move(*query)));
}

auto run(BangumiPresenter &presenter, BangumiView &view,
         const SearchCommand &command) -> ilias::Task<int> {
    auto query = searchQuery(command);
    if (!query) {
        view.showError(query.error());
        co_return exitCode(query.error());
    }
    co_return finish(co_await presenter.searchSubjects(std::move(*query)));
}

} // namespace

auto runBangumiCliCommand(BangumiPresenter &presenter, BangumiView &view,
                          const Command &command) -> ilias::Task<int> {
    auto task = std::visit(
        [&presenter, &view](const auto &value) {
            return run(presenter, view, value);
        },
        command);
    co_return co_await std::move(task);
}

} // namespace anime_land::cli
