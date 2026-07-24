#pragma once

#include "common/app_settings.hpp"

#include <ilias/sql/sqldatabase.hpp>

#include <string>
#include <string_view>
#include <utility>

namespace anime_land::persistence {

/// LocalDatabase 支持的关系数据库方言。
enum class DatabaseBackend {
    Sqlite,
    Mysql,
};

/**
 * @brief 由应用配置创建的单连接数据库。
 *
 * @details open() 只从 SqlSettings 选择并打开 SQLite/SQLCipher 或 MySQL
 * 驱动。应用表及其长期 Form 由对应 Store 创建和持有。
 */
class LocalDatabase {
public:
    /**
     * @brief 按应用配置打开数据库并完成连接初始化。
     *
     * @param settings 数据库类型和连接参数；密码只传递给驱动，不写入日志。
     * @return 已打开的数据库连接。
     */
    static auto open(const SqlSettings &settings) -> ilias::IoTask<LocalDatabase>;

    LocalDatabase(const LocalDatabase &) = delete;
    auto operator=(const LocalDatabase &) -> LocalDatabase & = delete;
    LocalDatabase(LocalDatabase &&) noexcept = default;
    auto operator=(LocalDatabase &&) noexcept -> LocalDatabase & = default;
    ~LocalDatabase() = default;

    /// 显式关闭底层数据库连接。
    auto close() -> ilias::IoTask<void>;

    /// 返回当前连接使用的后端枚举。
    auto backend() const noexcept -> DatabaseBackend { return mBackend; }

    /// 返回用于选择 Schema 方言的稳定后端名称。
    auto backendName() const noexcept -> std::string_view;

    /**
     * @brief 返回用于特殊场景的原生 SQL 连接逃生口。
     *
     * @warning 常规业务代码应优先使用 CatalogStore 等类型化 API。该接口只用于
     * 诊断工具、一次性维护和底层测试；调用方自行负责 SQL 方言、事务一致性和
     * 参数绑定。返回引用不得超过 LocalDatabase 生命周期。
     */
    [[nodiscard]] auto advancedConnection() noexcept -> ilias::sql::SqlDatabase & {
        return mDatabase;
    }

private:
    explicit LocalDatabase(ilias::sql::SqlDatabase database,
                           DatabaseBackend backend)
        : mDatabase(std::move(database)), mBackend(backend) {}

    static auto open(std::string driver,
                     ilias::sql::ConnectOptions options,
                     DatabaseBackend type)
        -> ilias::IoTask<LocalDatabase>;

    ilias::sql::SqlDatabase mDatabase;
    DatabaseBackend mBackend;
};

} // namespace anime_land::persistence
