#pragma once

#include "common/app_settings.hpp"

#include <ilias/sql/sqldatabase.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace anime_land::persistence {

/// 当前应用能够创建并验证的数据库 Schema 版本。
inline constexpr std::int64_t kCurrentSchemaVersion = 1;

/// LocalDatabase 支持的关系数据库方言。
enum class DatabaseBackend {
    Sqlite,
    Mysql,
};

/**
 * @brief 由应用配置创建的单连接数据库及其 Migration 生命周期。
 *
 * @details open() 从 SqlSettings 选择 SQLite/SQLCipher 或 MySQL
 * 驱动，并在返回前完成该后端的连接配置、Migration 和 Schema 验证。
 * 当前阶段由上层数据库执行上下文保证串行访问。
 */
class LocalDatabase {
public:
    /**
     * @brief 按应用配置打开数据库并完成连接初始化。
     *
     * @param settings 数据库类型和连接参数；密码只传递给驱动，不写入日志。
     * @return 已完成 Migration 和 Schema 验证的数据库连接。
     */
    static auto open(const SqlSettings &settings) -> ilias::IoTask<LocalDatabase>;

    LocalDatabase(const LocalDatabase &) = delete;
    auto operator=(const LocalDatabase &) -> LocalDatabase & = delete;
    LocalDatabase(LocalDatabase &&) noexcept = default;
    auto operator=(LocalDatabase &&) noexcept -> LocalDatabase & = default;
    ~LocalDatabase() = default;

    /**
     * @brief 在单个事务中把数据库升级到 kCurrentSchemaVersion。
     *
     * 已处于当前版本时只提交空 Migration 事务；高于当前版本的数据库会被拒绝，
     * 防止旧应用误写新 Schema。
     */
    auto migrate() -> ilias::IoTask<void>;

    /**
     * @brief 验证版本、核心表和后端完整性约束。
     *
     * SQLite 还会验证外键开关并执行 quick_check。
     */
    auto validateSchema() -> ilias::IoTask<void>;

    /// 读取 schema_migrations 中已经应用的最高版本。
    auto schemaVersion() -> ilias::IoTask<std::int64_t>;

    /// 检查当前连接是否启用了外键约束；非 SQLite 后端恒为 true。
    auto foreignKeysEnabled() -> ilias::IoTask<bool>;

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
     * ORM 无法表达的后端扩展（如 FTS/PRAGMA）、诊断工具、一次性维护和底层
     * Schema 约束测试；调用方自行负责 SQL 方言、事务一致性和参数绑定。返回引用
     * 不得超过 LocalDatabase 生命周期。
     */
    [[nodiscard]] auto advancedConnection() noexcept
        -> ilias::sql::SqlDatabase & {
        return mDatabase;
    }

private:
    explicit LocalDatabase(ilias::sql::SqlDatabase database,
                           DatabaseBackend backend)
        : mDatabase(std::move(database)), mBackend(backend) {}

    /**
     * @brief 打开已经归一化的驱动配置并执行后端专属初始化。
     *
     * SQLite 的 SQLCipher 检查、外键开关和 WAL 设置必须在 Migration 前完成，
     * 因为这些 PRAGMA 作用于当前连接。
     */
    static auto openConfigured(std::string driver,
                               ilias::sql::ConnectOptions options,
                               DatabaseBackend backend,
                               bool enableWal,
                               bool requireCipher)
        -> ilias::IoTask<LocalDatabase>;

    ilias::sql::SqlDatabase mDatabase;
    DatabaseBackend mBackend;
};

} // namespace anime_land::persistence
