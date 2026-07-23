#include "persistence/database.hpp"

#include "common/log.hpp"
#include "persistence/database_schema.hpp"

#include <ilias/sql_orm/orm_form.hpp>

#include <QDateTime>

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace anime_land::persistence {
namespace {

using ilias::Err;

/// 执行只关心首行首列的标量查询，并区分“无行”和查询失败。
template <typename SqlApi, typename Value>
auto queryOne(SqlApi &database, std::string_view sql, Value &value)
    -> ilias::IoTask<bool> {
    ILIAS_CO_TRY(auto result, co_await database.query(sql));
    ilias_for_await(auto rowResult, result.range(value)) {
        ILIAS_CO_TRYV(rowResult);
        co_return true;
    }
    co_return false;
}

/// 返回写入 Migration 记录所用的 UTC Unix 毫秒时间戳。
auto appliedAtNow() -> std::int64_t {
    return static_cast<std::int64_t>(
        QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());
}

/// 统一用户配置中的后端大小写，避免方言选择受输入格式影响。
auto normalizedBackend(std::string backend) -> std::string {
    std::ranges::transform(backend, backend.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return backend;
}

/// 把受信任的 Schema 表名组成系统目录查询使用的 SQL 字符串字面量列表。
auto tableNameList(std::initializer_list<std::string_view> tableNames)
    -> std::string {
    std::string result;
    for (const auto tableName : tableNames) {
        if (!result.empty()) {
            result += ", ";
        }
        result += '\'';
        result += tableName;
        result += '\'';
    }
    return result;
}

/// 使用对应后端的 ORM 方言写入当前 Migration 版本。
template <typename BackendTag>
auto insertMigration(ilias::sql::SqlTransaction &transaction)
    -> ilias::IoTask<std::size_t> {
    using Migration = schema::SchemaMigrationRecord;
    ILIAS_CO_TRY(auto migrations,
                 ilias::sql::Form<Migration, BackendTag>::bind(
                     transaction,
                     std::string(schema::table::schemaMigrations)));
    co_return co_await migrations.insert()
        .set(Migration {
            .version = kCurrentSchemaVersion,
            .appliedAt = appliedAtNow(),
        })
        .execute();
}

/// 通过完整反射记录读取最高 Migration 版本；空表即版本 0。
template <typename BackendTag, typename SqlApi>
auto readSchemaVersion(SqlApi &database) -> ilias::IoTask<std::int64_t> {
    using Migration = schema::SchemaMigrationRecord;
    ILIAS_CO_TRY(auto migrations,
                 ilias::sql::Form<Migration, BackendTag>::bind(
                     database,
                     std::string(schema::table::schemaMigrations)));
    ILIAS_CO_TRY(
        auto result,
        co_await migrations.select()
            .orderBy(migrations.sql(&Migration::version), true)
            .limit(1)
            .query());
    ilias_for_await(auto rowResult, result.rangeResult()) {
        ILIAS_CO_TRY(auto row, rowResult);
        co_return row.version;
    }
    co_return 0;
}

} // namespace

auto LocalDatabase::open(const SqlSettings &settings)
    -> ilias::IoTask<LocalDatabase> {
    const std::string backend = normalizedBackend(settings.database_type);
    AL_LOG_INFO("[database.connection] open started backend={}", backend);

    ilias::sql::ConnectOptions options;
    if (backend == "sqlite" || backend == "sqlcipher") {
        if (settings.database_path.empty() ||
            (backend == "sqlcipher" && settings.database_password.empty())) {
            AL_LOG_WARN(
                "[database.connection] invalid SQLite configuration "
                "backend={} path_configured={} cipher_key_configured={}",
                backend, !settings.database_path.empty(),
                !settings.database_password.empty());
            co_return Err(std::make_error_code(std::errc::invalid_argument));
        }
        options.filename = settings.database_path;
        if (backend == "sqlcipher") {
            // 密钥只进入驱动选项；任何连接日志都不得输出 options 内容。
            options.extra.emplace("key", settings.database_password);
        }
        auto opened = co_await openConfigured(
            "sqlite", std::move(options), DatabaseBackend::Sqlite,
            settings.database_path != ":memory:", backend == "sqlcipher");
        if (!opened) {
            AL_LOG_ERROR("[database.connection] open failed backend={} error={}",
                         backend, opened.error().message());
            co_return Err(opened.error());
        }
        AL_LOG_INFO(
            "[database.connection] open completed backend={} schema_version={}",
            backend, kCurrentSchemaVersion);
        co_return std::move(*opened);
    }
    if (backend == "mysql" || backend == "mariadb") {
        if (settings.database_host.empty() || settings.database_port == 0 ||
            settings.database_user.empty() || settings.database_name.empty()) {
            AL_LOG_WARN(
                "[database.connection] invalid MySQL configuration "
                "host_configured={} port_configured={} user_configured={} "
                "database_configured={}",
                !settings.database_host.empty(), settings.database_port != 0,
                !settings.database_user.empty(), !settings.database_name.empty());
            co_return Err(std::make_error_code(std::errc::invalid_argument));
        }
        options.host = settings.database_host;
        options.port = settings.database_port;
        options.user = settings.database_user;
        options.password = settings.database_password;
        options.database = settings.database_name;
        auto opened =
            co_await openConfigured("mysql", std::move(options),
                                    DatabaseBackend::Mysql, false, false);
        if (!opened) {
            AL_LOG_ERROR("[database.connection] open failed backend={} error={}",
                         backend, opened.error().message());
            co_return Err(opened.error());
        }
        AL_LOG_INFO(
            "[database.connection] open completed backend={} schema_version={}",
            backend, kCurrentSchemaVersion);
        co_return std::move(*opened);
    }
    AL_LOG_WARN("[database.connection] unsupported backend requested");
    co_return Err(ilias::sql::SqlError::Code::DialectNotSupported);
}

auto LocalDatabase::openConfigured(std::string driver,
                                   ilias::sql::ConnectOptions options,
                                   DatabaseBackend backend,
                                   bool enableWal,
                                   bool requireCipher)
    -> ilias::IoTask<LocalDatabase> {
    const std::string_view backendLabel =
        backend == DatabaseBackend::Sqlite ? "sqlite" : "mysql";
    AL_LOG_DEBUG(
        "[database.connection] driver open backend={} wal_requested={} "
        "cipher_required={}",
        backendLabel, enableWal, requireCipher);

    auto opened = co_await ilias::sql::SqlDatabase::open(driver,
                                                         std::move(options));
    if (!opened) {
        AL_LOG_ERROR(
            "[database.connection] driver open failed backend={} error={}",
            backendLabel, opened.error().message());
        co_return Err(opened.error());
    }

    LocalDatabase database(std::move(*opened), backend);

    if (backend == DatabaseBackend::Sqlite) {
        if (requireCipher) {
            // 成功打开并不代表链接的 SQLite 实现支持 SQLCipher，必须主动探测。
            std::string cipherVersion;
            auto cipherAvailable = co_await queryOne(
                database.mDatabase, "PRAGMA cipher_version", cipherVersion);
            if (!cipherAvailable) {
                AL_LOG_ERROR(
                    "[database.connection] cipher capability query failed "
                    "error={}",
                    cipherAvailable.error().message());
                co_return Err(cipherAvailable.error());
            }
            if (!*cipherAvailable || cipherVersion.empty()) {
                AL_LOG_ERROR(
                    "[database.connection] SQLCipher capability unavailable");
                co_return Err(
                    std::make_error_code(std::errc::operation_not_supported));
            }
            AL_LOG_DEBUG(
                "[database.connection] SQLCipher capability verified");
        }

        // SQLite 外键开关按连接生效，因此必须在任何 Migration 或 Store 查询前设置。
        auto foreignKeys =
            co_await database.mDatabase.execute("PRAGMA foreign_keys = ON");
        if (!foreignKeys) {
            AL_LOG_ERROR(
                "[database.connection] enabling foreign keys failed error={}",
                foreignKeys.error().message());
            co_return Err(foreignKeys.error());
        }

        auto foreignKeysEnabled = co_await database.foreignKeysEnabled();
        if (!foreignKeysEnabled) {
            AL_LOG_ERROR(
                "[database.connection] foreign key verification failed error={}",
                foreignKeysEnabled.error().message());
            co_return Err(foreignKeysEnabled.error());
        }
        if (!*foreignKeysEnabled) {
            AL_LOG_ERROR(
                "[database.connection] foreign keys remained disabled");
            co_return Err(
                std::make_error_code(std::errc::operation_not_supported));
        }
        AL_LOG_DEBUG("[database.connection] foreign keys enabled");

        if (enableWal) {
            auto wal = co_await database.mDatabase.execute(
                "PRAGMA journal_mode = WAL");
            if (!wal) {
                AL_LOG_ERROR(
                    "[database.connection] enabling WAL failed error={}",
                    wal.error().message());
                co_return Err(wal.error());
            }
            AL_LOG_DEBUG("[database.connection] WAL enabled");
        }
    }

    auto migrated = co_await database.migrate();
    if (!migrated) {
        AL_LOG_ERROR("[database.connection] migration failed backend={} error={}",
                     backendLabel, migrated.error().message());
        co_return Err(migrated.error());
    }
    auto validated = co_await database.validateSchema();
    if (!validated) {
        AL_LOG_ERROR(
            "[database.connection] schema validation failed backend={} error={}",
            backendLabel, validated.error().message());
        co_return Err(validated.error());
    }
    co_return database;
}

auto LocalDatabase::migrate() -> ilias::IoTask<void> {
    AL_LOG_INFO("[database.migration] started backend={} target_version={}",
                backendName(), kCurrentSchemaVersion);

    auto transactionResult = co_await mDatabase.transaction();
    if (!transactionResult) {
        AL_LOG_ERROR("[database.migration] transaction start failed error={}",
                     transactionResult.error().message());
        co_return Err(transactionResult.error());
    }
    auto transaction = std::move(*transactionResult);

    // Migration 记录表是版本判断的根，必须先以幂等方式创建。
    auto migrationTableSql = schema::migrationTableStatement(backendName());
    if (!migrationTableSql) {
        AL_LOG_ERROR(
            "[database.migration] migration table generation failed error={}",
            migrationTableSql.error().message());
        co_return Err(migrationTableSql.error());
    }
    auto created = co_await transaction.execute(*migrationTableSql);
    if (!created) {
        AL_LOG_ERROR(
            "[database.migration] migration table creation failed error={}",
            created.error().message());
        co_return Err(created.error());
    }

    ilias::IoResult<std::int64_t> versionResult =
        Err(ilias::sql::SqlError::Code::DialectNotSupported);
    if (mBackend == DatabaseBackend::Sqlite) {
        versionResult =
            co_await readSchemaVersion<ilias::sql::SqliteTag>(transaction);
    }
    else if (mBackend == DatabaseBackend::Mysql) {
        versionResult =
            co_await readSchemaVersion<ilias::sql::MysqlTag>(transaction);
    }
    if (!versionResult) {
        AL_LOG_ERROR("[database.migration] version query failed error={}",
                     versionResult.error().message());
        co_return Err(versionResult.error());
    }
    const std::int64_t version = *versionResult;
    AL_LOG_DEBUG("[database.migration] current_version={} target_version={}",
                 version, kCurrentSchemaVersion);

    // 不支持自动降级，避免旧程序把较新的数据库结构改坏。
    if (version > kCurrentSchemaVersion) {
        AL_LOG_ERROR(
            "[database.migration] newer schema rejected current_version={} "
            "supported_version={}",
            version, kCurrentSchemaVersion);
        co_return Err(std::make_error_code(std::errc::not_supported));
    }
    if (version == kCurrentSchemaVersion) {
        auto committed = co_await transaction.commit();
        if (!committed) {
            AL_LOG_ERROR(
                "[database.migration] no-op transaction commit failed error={}",
                committed.error().message());
            co_return Err(committed.error());
        }
        AL_LOG_INFO(
            "[database.migration] completed version={} schema_changed=false",
            version);
        co_return {};
    }

    auto migrationStatements =
        schema::catalogMigrationV1Statements(backendName());
    if (!migrationStatements) {
        AL_LOG_ERROR("[database.migration] DDL generation failed error={}",
                     migrationStatements.error().message());
        co_return Err(migrationStatements.error());
    }
    AL_LOG_INFO(
        "[database.migration] applying version={} statements={}",
        kCurrentSchemaVersion, migrationStatements->size());

    std::size_t statementIndex = 0;
    for (const auto &sql : *migrationStatements) {
        ++statementIndex;
        auto executed = co_await transaction.execute(sql);
        if (!executed) {
            const auto native = transaction.lastNativeError();
            AL_LOG_ERROR(
                "[database.migration] version={} statement={} native_code={} "
                "native_message={}",
                kCurrentSchemaVersion, statementIndex,
                native ? native->code : 0,
                native ? native->message : executed.error().message());
            co_return Err(executed.error());
        }
    }

    ilias::IoResult<std::size_t> inserted =
        Err(ilias::sql::SqlError::Code::DialectNotSupported);
    if (mBackend == DatabaseBackend::Sqlite) {
        inserted = co_await insertMigration<ilias::sql::SqliteTag>(transaction);
    }
    else if (mBackend == DatabaseBackend::Mysql) {
        inserted = co_await insertMigration<ilias::sql::MysqlTag>(transaction);
    }
    if (!inserted) {
        AL_LOG_ERROR(
            "[database.migration] version record insert failed version={} "
            "error={}",
            kCurrentSchemaVersion, inserted.error().message());
        co_return Err(inserted.error());
    }

    // DDL 与版本记录共同提交，保证失败时数据库不会处于“半迁移”状态。
    auto committed = co_await transaction.commit();
    if (!committed) {
        AL_LOG_ERROR("[database.migration] commit failed version={} error={}",
                     kCurrentSchemaVersion, committed.error().message());
        co_return Err(committed.error());
    }
    AL_LOG_INFO(
        "[database.migration] completed version={} schema_changed=true",
        kCurrentSchemaVersion);
    co_return {};
}

auto LocalDatabase::validateSchema() -> ilias::IoTask<void> {
    AL_LOG_DEBUG("[database.schema] validation started backend={}",
                 backendName());

    if (mBackend == DatabaseBackend::Sqlite) {
        auto foreignKeys = co_await foreignKeysEnabled();
        if (!foreignKeys) {
            AL_LOG_ERROR(
                "[database.schema] foreign key check failed error={}",
                foreignKeys.error().message());
            co_return Err(foreignKeys.error());
        }
        if (!*foreignKeys) {
            AL_LOG_ERROR("[database.schema] foreign keys are disabled");
            co_return Err(
                std::make_error_code(std::errc::operation_not_supported));
        }
    }

    auto version = co_await schemaVersion();
    if (!version) {
        AL_LOG_ERROR("[database.schema] version check failed error={}",
                     version.error().message());
        co_return Err(version.error());
    }
    if (*version != kCurrentSchemaVersion) {
        AL_LOG_ERROR(
            "[database.schema] version mismatch actual={} expected={}", *version,
            kCurrentSchemaVersion);
        co_return Err(std::make_error_code(std::errc::protocol_error));
    }

    // 这里只检查应用启动所需的核心对象，额外的用户索引或后续对象不会导致误报。
    std::int64_t objectCount = 0;
    ilias::IoResult<bool> counted;
    std::int64_t expectedObjectCount = 0;
    if (mBackend == DatabaseBackend::Sqlite) {
        expectedObjectCount = 8;
        const std::string requiredObjects = tableNameList(
            {schema::table::schemaMigrations, schema::table::subjects,
             schema::table::subjectExternalRefs, schema::table::tags,
             schema::table::subjectTags, schema::table::episodes,
             schema::table::episodeExternalRefs,
             schema::table::subjectFts});
        const std::string objectQuery =
            "SELECT COUNT(*) FROM sqlite_master "
            "WHERE type IN ('table', 'view') AND name IN (" +
            requiredObjects + ")";
        counted = co_await queryOne(
            mDatabase, objectQuery, objectCount);
    }
    else {
        expectedObjectCount = 7;
        const std::string requiredObjects = tableNameList(
            {schema::table::schemaMigrations, schema::table::subjects,
             schema::table::subjectExternalRefs, schema::table::tags,
             schema::table::subjectTags, schema::table::episodes,
             schema::table::episodeExternalRefs});
        const std::string objectQuery =
            "SELECT COUNT(*) FROM information_schema.tables "
            "WHERE table_schema = DATABASE() AND table_name IN (" +
            requiredObjects + ")";
        counted = co_await queryOne(
            mDatabase, objectQuery, objectCount);
    }
    if (!counted) {
        AL_LOG_ERROR("[database.schema] object query failed error={}",
                     counted.error().message());
        co_return Err(counted.error());
    }
    if (!*counted || objectCount != expectedObjectCount) {
        AL_LOG_ERROR(
            "[database.schema] required object mismatch actual={} expected={}",
            objectCount, expectedObjectCount);
        co_return Err(std::make_error_code(std::errc::protocol_error));
    }

    if (mBackend == DatabaseBackend::Sqlite) {
        // quick_check 比完整 integrity_check 更适合启动路径，同时仍能发现页损坏。
        std::string quickCheck;
        auto checked =
            co_await queryOne(mDatabase, "PRAGMA quick_check", quickCheck);
        if (!checked) {
            AL_LOG_ERROR("[database.schema] quick_check failed error={}",
                         checked.error().message());
            co_return Err(checked.error());
        }
        if (!*checked || quickCheck != "ok") {
            AL_LOG_ERROR(
                "[database.schema] quick_check reported database corruption");
            co_return Err(std::make_error_code(std::errc::io_error));
        }
    }
    AL_LOG_INFO(
        "[database.schema] validation completed backend={} version={} "
        "objects={}",
        backendName(), *version, objectCount);
    co_return {};
}

auto LocalDatabase::schemaVersion() -> ilias::IoTask<std::int64_t> {
    ilias::IoResult<std::int64_t> version =
        Err(ilias::sql::SqlError::Code::DialectNotSupported);
    if (mBackend == DatabaseBackend::Sqlite) {
        version =
            co_await readSchemaVersion<ilias::sql::SqliteTag>(mDatabase);
    }
    else if (mBackend == DatabaseBackend::Mysql) {
        version =
            co_await readSchemaVersion<ilias::sql::MysqlTag>(mDatabase);
    }
    ILIAS_CO_TRY(auto value, std::move(version));
    AL_LOG_TRACE("[database.schema] version read value={}", value);
    co_return value;
}

auto LocalDatabase::foreignKeysEnabled() -> ilias::IoTask<bool> {
    if (mBackend != DatabaseBackend::Sqlite) {
        co_return true;
    }
    std::int64_t enabled = 0;
    ILIAS_CO_TRY(auto queried,
                 co_await queryOne(mDatabase, "PRAGMA foreign_keys", enabled));
    if (!queried) {
        co_return Err(std::make_error_code(std::errc::protocol_error));
    }
    AL_LOG_TRACE("[database.connection] foreign key state enabled={}",
                 enabled == 1);
    co_return enabled == 1;
}

auto LocalDatabase::backendName() const noexcept -> std::string_view {
    switch (mBackend) {
        case DatabaseBackend::Sqlite:
            return "sqlite";
        case DatabaseBackend::Mysql:
            return "mysql";
    }
    return {};
}

auto LocalDatabase::close() -> ilias::IoTask<void> {
    AL_LOG_DEBUG("[database.connection] close started backend={}",
                 backendName());
    auto closed = co_await mDatabase.close();
    if (!closed) {
        AL_LOG_ERROR("[database.connection] close failed backend={} error={}",
                     backendName(), closed.error().message());
        co_return Err(closed.error());
    }
    AL_LOG_INFO("[database.connection] close completed backend={}",
                backendName());
    co_return {};
}

} // namespace anime_land::persistence
