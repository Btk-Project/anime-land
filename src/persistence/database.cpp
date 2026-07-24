#include "persistence/database.hpp"

#include "common/log.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

namespace anime_land::persistence {
namespace {

using ilias::Err;
using ilias::sql::SqlDatabase;

auto lowercase(std::string value) -> std::string {
    std::ranges::transform(value, value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

} // namespace

auto LocalDatabase::open(const SqlSettings &settings)
    -> ilias::IoTask<LocalDatabase> {
    const std::string type = lowercase(settings.database_type);
    ilias::sql::ConnectOptions options;

    if (type == "sqlite" || type == "sqlcipher") {
        options.filename = settings.database_path;
        options.extra.emplace("EnableFKey", "true");
        if (type == "sqlcipher") {
            options.extra.emplace("key", settings.database_password);
        }
        co_return co_await open("sqlite", std::move(options), DatabaseBackend::Sqlite);
    }
    if (type == "mysql" || type == "mariadb") {
        options.host = settings.database_host;
        options.port = settings.database_port;
        options.user = settings.database_user;
        options.password = settings.database_password;
        options.database = settings.database_name;
        co_return co_await open("mysql", std::move(options), DatabaseBackend::Mysql);
    }
    co_return Err(ilias::sql::SqlError::Code::DialectNotSupported);
}

auto LocalDatabase::open(std::string driver,
                         ilias::sql::ConnectOptions options,
                         DatabaseBackend type)
    -> ilias::IoTask<LocalDatabase> {
    AL_LOG_INFO("[database.connection] opening driver={}", driver);
    ILIAS_CO_TRY(
        auto connection,
        co_await SqlDatabase::open(driver, std::move(options)));
    co_return LocalDatabase(std::move(connection), type);
}

auto LocalDatabase::backendName() const noexcept -> std::string_view {
    switch (mBackend) {
        case DatabaseBackend::Sqlite:
            return "sqlite";
        case DatabaseBackend::Mysql:
            return "mysql";
    }
    std::unreachable();
}

auto LocalDatabase::close() -> ilias::IoTask<void> {
    co_return co_await mDatabase.close();
}

} // namespace anime_land::persistence
