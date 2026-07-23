#include "persistence/database_schema.hpp"

#include <ilias/sql_orm/dialect.hpp>

#include <utility>

namespace anime_land::persistence::schema {
namespace {

/**
 * @brief 生成一张关系表及其索引，并按执行顺序追加到 Migration。
 *
 * SchemaGenerator 返回的附属语句必须紧跟建表语句，才能保证后续外键依赖表在
 * 完整的基础结构上创建。
 */
template <typename BackendTag, typename Record>
auto appendTable(std::vector<std::string> &statements,
                 std::string_view tableName,
                 bool ifNotExists = false) -> ilias::IoResult<void> {
    using Generator = ilias::sql::detail::SchemaGenerator<BackendTag>;
    ILIAS_TRY(auto generated,
              Generator::template generateTableSchema<Record>(
                  tableName, ifNotExists));
    auto complete = generated.completeStatements();
    statements.insert(statements.end(),
                      std::make_move_iterator(complete.begin()),
                      std::make_move_iterator(complete.end()));
    return {};
}

/// 使用指定后端方言单独生成 schema_migrations 建表语句。
template <typename BackendTag>
auto migrationTableStatementFor(bool ifNotExists)
    -> ilias::IoResult<std::string> {
    using Generator = ilias::sql::detail::SchemaGenerator<BackendTag>;
    ILIAS_TRY(auto generated,
              Generator::template generateTableSchema<SchemaMigrationRecord>(
                  table::schemaMigrations, ifNotExists));
    return std::move(generated.createTableSql);
}

/// 按外键依赖顺序生成 v1 目录关系表和索引。
template <typename BackendTag>
auto catalogMigrationV1StatementsFor()
    -> ilias::IoResult<std::vector<std::string>> {
    std::vector<std::string> statements;
    statements.reserve(13);

    const auto append = [&statements]<typename Record>(
                            std::string_view tableName)
        -> ilias::IoResult<void> {
        return appendTable<BackendTag, Record>(statements, tableName);
    };

    // 被引用的主表必须早于关联表创建；顺序也是 Migration 的执行契约。
    ILIAS_TRYV(
        append.template operator()<SubjectRecord>(table::subjects));
    ILIAS_TRYV(append.template operator()<SubjectExternalRefRecord>(
        table::subjectExternalRefs));
    ILIAS_TRYV(
        append.template operator()<TagRecord>(table::tags));
    ILIAS_TRYV(append.template operator()<SubjectTagRecord>(
        table::subjectTags));
    ILIAS_TRYV(
        append.template operator()<EpisodeRecord>(table::episodes));
    ILIAS_TRYV(append.template operator()<EpisodeExternalRefRecord>(
        table::episodeExternalRefs));
    return statements;
}

} // namespace

auto migrationTableStatement(std::string_view backend, bool ifNotExists)
    -> ilias::IoResult<std::string> {
    if (backend == "sqlite") {
        return migrationTableStatementFor<ilias::sql::SqliteTag>(ifNotExists);
    }
    if (backend == "mysql" || backend == "mariadb") {
        return migrationTableStatementFor<ilias::sql::MysqlTag>(ifNotExists);
    }
    if (backend == "postgres" || backend == "postgresql" || backend == "pg") {
        return migrationTableStatementFor<ilias::sql::PostgresTag>(
            ifNotExists);
    }
    return ilias::Err(ilias::sql::SqlError::Code::DialectNotSupported);
}

auto catalogMigrationV1Statements(std::string_view backend)
    -> ilias::IoResult<std::vector<std::string>> {
    ilias::IoResult<std::vector<std::string>> generated =
        ilias::Err(ilias::sql::SqlError::Code::DialectNotSupported);
    if (backend == "sqlite") {
        generated =
            catalogMigrationV1StatementsFor<ilias::sql::SqliteTag>();
    }
    else if (backend == "mysql" || backend == "mariadb") {
        generated =
            catalogMigrationV1StatementsFor<ilias::sql::MysqlTag>();
    }
    else if (backend == "postgres" || backend == "postgresql" ||
             backend == "pg") {
        generated =
            catalogMigrationV1StatementsFor<ilias::sql::PostgresTag>();
    }
    if (!generated) {
        return ilias::Err(generated.error());
    }

    if (backend == "sqlite") {
        // FTS5 是显式的 SQLite 扩展，不属于关系 ORM Schema；其他后端由 Store
        // 提供自己的搜索实现。
        generated->emplace_back(
            "CREATE VIRTUAL TABLE " + std::string(table::subjectFts) +
            " USING fts5("
            "subject_id UNINDEXED, title, title_cn, aliases, summary)");
    }
    return generated;
}

} // namespace anime_land::persistence::schema
