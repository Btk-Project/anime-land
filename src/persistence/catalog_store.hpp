#pragma once

#include "persistence/database.hpp"

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QUrl>

#include <chrono>
#include <compare>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace anime_land::persistence {

/// 应用内部稳定的条目主键，不与任何远端服务 ID 混用。
struct SubjectId {
    std::int64_t value = 0;
    auto operator<=>(const SubjectId &) const = default;
};

/// 应用内部稳定的章节主键。
struct EpisodeId {
    std::int64_t value = 0;
    auto operator<=>(const EpisodeId &) const = default;
};

/// 快照包含的元数据完整度；高完整度数据不会被摘要快照降级。
enum class SubjectMetadataLevel : std::int64_t {
    Summary = 0,
    Details = 1,
};

/// 一个外部提供者对本地条目的稳定身份映射。
struct ExternalRef {
    QString providerKey;
    QString externalId;
};

/// 已持久化的外部身份映射及其远端时间信息。
struct StoredExternalRef {
    ExternalRef ref;
    QDateTime fetchedAt;
    std::optional<QDateTime> remoteUpdatedAt;
};

/// 远端快照中的单个标签及其可选权重。
struct SubjectTagSnapshot {
    QString name;
    std::optional<double> weight;
};

/**
 * @brief 写入本地目录的条目元数据快照。
 *
 * @details aliases 和 tags 使用双层可空语义：nullopt 表示本次响应没有提供该
 * 字段，保留数据库旧值；有值但容器为空表示远端明确返回空集合，需要清空旧值。
 */
struct SubjectSnapshot {
    ExternalRef origin;
    SubjectMetadataLevel metadataLevel = SubjectMetadataLevel::Summary;
    int subjectType = 2;
    QString title;
    std::optional<QString> titleCn;
    std::optional<QString> summary;
    std::optional<QDate> airDate;
    std::optional<QUrl> coverUrl;
    std::optional<std::vector<QString>> aliases;
    std::optional<std::vector<SubjectTagSnapshot>> tags;
    QDateTime fetchedAt;
    std::optional<QDateTime> remoteUpdatedAt;
};

/// 写入本地目录的章节元数据快照。
struct EpisodeSnapshot {
    ExternalRef origin;
    int sortOrder = 0;
    int episodeType = 0;
    std::optional<double> episodeNumber;
    std::optional<QString> title;
    std::optional<QString> titleCn;
    std::optional<QString> summary;
    std::optional<QDate> airDate;
    std::optional<std::chrono::milliseconds> duration;
    QDateTime fetchedAt;
    std::optional<QDateTime> remoteUpdatedAt;
};

/// 从数据库读取的标签关系，保留标签来源以支持多提供者共存。
struct StoredSubjectTag {
    QString name;
    QString providerKey;
    std::optional<double> weight;
};

/// 列表和搜索场景使用的轻量条目投影。
struct SubjectSummary {
    SubjectId id;
    int subjectType = 0;
    QString title;
    std::optional<QString> titleCn;
    std::optional<QString> summary;
    SubjectMetadataLevel metadataLevel = SubjectMetadataLevel::Summary;
    QDateTime updatedAt;
};

/// 条目详情投影，包含别名、标签和详情元数据刷新时间。
struct SubjectDetails {
    SubjectSummary summary;
    std::optional<QDate> airDate;
    std::optional<QUrl> coverUrl;
    std::vector<QString> aliases;
    std::vector<StoredSubjectTag> tags;
    std::vector<StoredExternalRef> externalRefs;
    std::optional<QDateTime> metadataRefreshedAt;
};

/// 章节详情投影及其全部外部身份映射。
struct EpisodeDetails {
    EpisodeId id;
    SubjectId subjectId;
    int sortOrder = 0;
    int episodeType = 0;
    std::optional<double> episodeNumber;
    std::optional<QString> title;
    std::optional<QString> titleCn;
    std::optional<QString> summary;
    std::optional<QDate> airDate;
    std::optional<std::chrono::milliseconds> duration;
    std::vector<StoredExternalRef> externalRefs;
    QDateTime updatedAt;
};

/// 标签列表项以及当前关联的不同条目数。
struct TagFacet {
    QString name;
    std::int64_t subjectCount = 0;
};

/// 本地条目搜索条件；limit 有效范围为 1 到 200，offset 必须非负。
struct LocalSubjectQuery {
    QString text;
    std::optional<QString> tag;
    int limit = 50;
    int offset = 0;
};

/// 标签枚举条件；prefix 会按与写入相同的 Unicode 规则规范化。
struct LocalTagQuery {
    QString prefix;
    int limit = 50;
    int offset = 0;
};

/**
 * @brief v0.1 目录表的事务化关系数据库 Store。
 *
 * @details Store 不访问网络。关系型 CRUD 通过 ilias-sql ORM 按当前后端
 * 方言生成；全文搜索由数据库后端适配。调用方必须通过统一数据库执行上下文
 * 串行调用写方法。
 */
class CatalogStore {
public:
    /**
     * @brief 审查目录表并绑定整个 Store 生命周期内复用的 Form。
     *
     * 表不存在或结构与 Record 不一致时直接返回错误；Store 不负责修复 Schema。
     */
    static auto open(LocalDatabase &database) -> ilias::IoTask<CatalogStore>;

    ~CatalogStore();

    CatalogStore(const CatalogStore &) = delete;
    auto operator=(const CatalogStore &) -> CatalogStore & = delete;
    CatalogStore(CatalogStore &&) noexcept;
    auto operator=(CatalogStore &&) noexcept -> CatalogStore & = delete;

    /**
     * @brief 按外部身份插入或更新条目快照。
     *
     * @return 已存在或新创建的本地 SubjectId。
     *
     * 条目、外部映射和标签在同一事务中提交。
     */
    auto upsertSubjectSnapshot(SubjectSnapshot snapshot) -> ilias::IoTask<SubjectId>;

    /**
     * @brief 按外部章节身份批量插入或更新章节快照。
     *
     * 所有快照与外部映射在同一事务中提交，返回 ID 顺序与输入一致。空输入是合法
     * 的无操作；该方法不会删除本次响应中缺失的旧章节。
     */
    auto upsertEpisodeSnapshots(SubjectId subject,
                                std::vector<EpisodeSnapshot> snapshots)
        -> ilias::IoTask<std::vector<EpisodeId>>;

    /// 原子替换指定来源的条目标签；空集合表示清空该来源标签。
    auto replaceSubjectTags(SubjectId subject, QString providerKey,
                            std::vector<SubjectTagSnapshot> tags)
        -> ilias::IoTask<void>;

    /// 按 provider_key 与 external_id 查找本地条目 ID。
    auto findSubjectByExternalRef(const ExternalRef &ref) -> ilias::IoTask<std::optional<SubjectId>>;

    /// 按 provider_key 与 external_id 查找本地章节 ID。
    auto findEpisodeByExternalRef(const ExternalRef &ref) -> ilias::IoTask<std::optional<EpisodeId>>;

    /// 按本地主键读取条目详情；不存在时返回空 optional。
    auto getSubject(SubjectId subject) -> ilias::IoTask<std::optional<SubjectDetails>>;

    /// 按本地主键读取章节详情；不存在时返回空 optional。
    auto getEpisode(EpisodeId episode) -> ilias::IoTask<std::optional<EpisodeDetails>>;

    /// 按稳定顺序列出条目的全部章节。
    auto listEpisodes(SubjectId subject) -> ilias::IoTask<std::vector<EpisodeDetails>>;

    /**
     * @brief 搜索已经持久化的条目。
     *
     * 文本字段使用 ORM 的 contains 表达式；标签按规范化名称精确匹配。空文本
     * 和空标签表示按更新时间列出条目。
     */
    auto searchSubjects(const LocalSubjectQuery &query) -> ilias::IoTask<std::vector<SubjectSummary>>;

    /// 按关联条目数和规范化名称稳定列出当前使用中的标签。
    auto listTags(const LocalTagQuery &query) -> ilias::IoTask<std::vector<TagFacet>>;

private:
    struct State;
    CatalogStore(LocalDatabase &database, std::unique_ptr<State> state);

    /// 非拥有引用；调用方必须保证 LocalDatabase 比 Store 存活更久。
    LocalDatabase &mDatabase;
    std::unique_ptr<State> mState;
};

} // namespace anime_land::persistence
