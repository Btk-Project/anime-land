# v0.1 本地数据库设计

> 文档状态：Draft / v0.1 实施契约
> 适用范围：Bangumi 目录、本地收藏与星标、本地媒体关联、播放进度和最近播放
> 技术栈：C++23 / Qt 6 / Ilias / IliasMySql / SQLite / Bangumi API
> 产品范围以 [`docs/plan.md`](../plan.md) 的 v0.1 MVP 为准

## 1. 文档目的

本文档只约束 v0.1 必须实现的数据库能力、应用层 API 和持久化语义。

v0.1 必须形成以下数据闭环：

```text
搜索或浏览 Bangumi 条目
    ↓
保存统一条目、标签和章节
    ↓
把本地视频文件关联到章节
    ↓
打开统一媒体源并播放
    ↓
保存播放进度
    ↓
从最近播放继续观看
```

本设计保留 `provider_key + versioned descriptor` 扩展边界，但 v0.1 只实现内置
`local.filesystem` 媒体提供者。Bilibili、BT、HTTP 网络源和第三方源插件不属于
v0.1。

---

## 2. v0.1 范围

### 2.1 必须实现

| 能力 | v0.1 行为 |
|---|---|
| Schema Migration | 创建、升级和验证 SQLite Schema |
| Bangumi 目录 | 搜索条目、读取详情与章节，并写入本地统一模型 |
| 本地查询 | 按标题、别名和简介搜索已经保存的条目 |
| 标签 | 保存条目内容标签，按标签列出条目 |
| 外部身份映射 | 保存本地条目/章节与 Bangumi ID 的映射 |
| 本地用户状态 | 保存收藏状态、星标、评分和备注 |
| 本地媒体资源 | 导入本地文件，关联到本地章节 |
| 媒体源分发 | 根据 `provider_key` 把持久化描述交给正确提供者 |
| 播放进度 | 保存断点、完成状态和最后播放时间 |
| 最近播放 | 按最后播放时间查询并恢复播放 |

### 2.2 明确不属于 v0.1

以下能力不得成为 v0.1 数据库实现的前置条件：

```text
Bangumi 账号表和收藏双向同步
远程收藏冲突与 remote shadow
Bangumi 章节观看状态回写
search_queries / search_results 精确网络查询缓存
网络搜索空结果缓存
在线数据源插件发现
source_candidates 临时发现索引缓存
Bilibili、HTTP、BT 和 qBittorrent 媒体提供者
下载任务
完整播放会话流水
多用户 Profile
多设备同步
图片二进制缓存
插件设置和插件市场
```

已经存在的 Bangumi 登录或收藏读取代码可以继续保留，但不要求在 v0.1 本地数据库
中建立账号、同步队列或冲突状态。

### 2.3 v0.1 的缓存取舍

Bangumi 搜索或详情返回后，v0.1 会持久化标准化的 `subjects`、`tags` 和
`episodes`。因此离线时仍能搜索已经见过的条目。

v0.1 不持久化一次远程搜索的完整请求、结果顺序或空结果。当前网络响应的排序只在
该次调用结果中有效。精确查询缓存以后再通过可清除的 `search_queries` 和
`search_results` 实现，不属于核心业务数据。

---

## 3. 分层职责

```text
Presentation
    ↓
Application Services
    ├─ SubjectCatalogService
    ├─ LocalLibraryService
    ├─ MediaLibraryService
    └─ PlaybackProgressService
    ↓
Infrastructure
    ├─ Relational Database Stores
    ├─ Bangumi Catalog Client
    ├─ MediaSourceProviderRegistry
    └─ local.filesystem Provider
```

### 3.1 Presentation

Presentation 只负责：

- 显示条目、标签、收藏状态、章节、媒体源和播放进度；
- 接收搜索、收藏、关联文件、播放和继续观看等用户意图；
- 显示本地结果、网络刷新、文件缺失和无法恢复等状态。

Presentation 不负责：

- 执行 SQL；
- 解析 Bangumi JSON；
- 解析 `descriptor_json`；
- 根据文件路径直接创建播放器 Pipeline；
- 决定写入播放进度的频率；
- 合并本地 ID 与 Bangumi ID。

### 3.2 Application Services

应用层负责：

- 调用 Bangumi Client，并把响应转换为本地统一快照；
- 以事务写入条目、标签、外部映射、章节和 FTS；
- 管理本地收藏与星标；
- 导入本地文件并关联章节；
- 根据 `provider_key` 分发持久化媒体描述；
- 把 `PlaybackSession` 的标准进度写入数据库；
- 定义播放完成判定与最近播放顺序。

### 3.3 Infrastructure

Infrastructure 负责：

- SQLite Migration、约束、事务和查询；
- Bangumi HTTP 请求和 DTO 解析；
- 媒体提供者注册与分发；
- 校验并打开本地文件；
- 系统凭据存储和图片文件缓存。

数据库不会主动请求 Bangumi、扫描文件或打开媒体源。

---

## 4. 核心数据原则

### 4.1 本地 ID 是唯一内部身份

Bangumi ID、插件 ID、URL 和文件路径都不能作为核心表主键。

```text
本地 subject_id = 1001
Bangumi subject_id = 400602

本地 episode_id = 5001
Bangumi episode_id = 1234567
```

收藏、章节、媒体映射和播放进度统一引用本地 ID。

### 4.2 描述元数据、外部身份和媒体源必须分离

```text
subjects / episodes
→ 应用理解并展示的标准化描述

subject_external_refs / episode_external_refs
→ 本地对象与外部对象的身份映射

source_resources / source_items
→ 由媒体提供者重新打开资源所需的数据
```

原设计中的 `subject_origins` 容易被理解为“媒体源”或“元数据扩展”。v0.1 将其
改名为 `subject_external_refs`，只承担外部身份映射。

### 4.3 持久化的不是 C++ 闭包，而是闭包捕获数据

真正的 C++ 闭包、打开的文件句柄或播放器对象不能持久化。数据库保存：

```text
provider_key
+ descriptor_version
+ descriptor_json
= 重新构造媒体源所需的持久化描述
```

宿主只根据 `provider_key` 分发描述。描述的字段语义、兼容和迁移由对应媒体提供者
负责。

v0.1 示例：

```json
{
  "path": "/videos/Frieren/01.mkv",
  "size": 1542312345,
  "modified_at": 1710000000,
  "fingerprint": "..."
}
```

未来可以使用同一边界保存：

```text
bilibili
→ season/episode/page 的稳定标识

qbittorrent
→ torrent info hash、文件索引和非敏感定位信息
```

短期签名 URL、Cookie、Token、代理密码等敏感或易过期数据不得写入
`descriptor_json`。它们应在打开资源时重新获取，凭据应存入系统凭据存储。

### 4.4 标签是可查询关系，不是 JSON 数组

“治愈”“百合”等内容标签需要支持精确查询和索引，因此使用 `tags` 与
`subject_tags`。标签不放入 `subjects.tags_json`。

标签来源通过 `subject_tags.provider_key` 区分。v0.1 至少支持：

```text
bangumi
manual
```

不同来源给同一条目添加相同标签时可以同时保留。查询条目时使用 `DISTINCT` 合并。

### 4.5 收藏、星标和有效源不是条目元数据

- 是否收藏由 `subject_user_state.collection_status IS NOT NULL` 推导；
- 是否星标由 `subject_user_state.is_favorite` 表达；
- 是否存在有效媒体源由媒体资源、资源项和提供者状态联合查询；
- 不在 `subjects` 中保存 `is_collected` 或 `has_valid_source`。

这些派生布尔值如果以后需要加速，只能作为可重建缓存，不能成为唯一事实来源。

### 4.6 播放进度由宿主统一定义

媒体提供者只负责把自己的描述解析为标准媒体输入，不负责定义或直接写入播放记录。

```text
Provider 私有描述
    ↓
标准 SourceItem / AsyncMediaSource
    ↓
PlaybackSession
    ↓
标准 PlaybackProgress
    ↓
PlaybackProgressService
    ↓
SQLite
```

同一章节切换不同媒体源时，进度仍以本地 `episode_id` 为逻辑身份。

---

## 5. v0.1 数据关系

```text
subjects
├─ subject_external_refs
├─ subject_tags ── tags
├─ subject_user_state
└─ episodes
   ├─ episode_external_refs
   ├─ episode_source_links
   │  └─ source_items
   │     └─ source_resources
   │        └─ source_providers
   └─ playback_progress
      └─ last_source_item_id ── source_items
```

`source_resources` 表示资源根，`source_items` 表示其中的具体可播放单元：

| 提供者 | `source_resources` | `source_items` |
|---|---|---|
| `local.filesystem` | 一个本地文件资源 | 该文件的可播放项 |
| 未来 Bilibili | 一个番剧/视频资源 | 分集或 page |
| 未来 BT | 一个 torrent | torrent 中的视频文件 |

v0.1 本地文件可以一项对应一个资源。保留两层是为了避免以后把多文件 torrent 或
多分集网络资源重新塞回单层表。

---

# 6. v0.1 表结构

## 6.1 Migration 记录

```sql
CREATE TABLE schema_migrations (
    version INTEGER NOT NULL PRIMARY KEY,
    applied_at INTEGER NOT NULL
);
```

规则：

- 每次 Schema 变化必须新增 Migration；
- Migration 必须在同一事务中完成；
- 应用启动先运行 Migration，再构造 Store；
- 测试必须能从空数据库迁移到当前版本；
- 不允许依赖开发机已有表状态。

## 6.2 统一条目

### `subjects`

```sql
CREATE TABLE subjects (
    id INTEGER PRIMARY KEY AUTOINCREMENT,

    subject_type INTEGER NOT NULL DEFAULT 2,

    title TEXT NOT NULL,
    title_cn TEXT,
    summary TEXT,

    air_date TEXT,
    cover_url TEXT,
    aliases_json TEXT NOT NULL DEFAULT '[]',

    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    metadata_level INTEGER NOT NULL DEFAULT 0,
    metadata_refreshed_at INTEGER,

    CHECK(subject_type >= 0),
    CHECK(metadata_level BETWEEN 0 AND 1)
);
```

`aliases_json` 在 v0.1 只用于整体读取和写入。别名搜索由写入
`subject_fts.aliases` 的扁平文本完成。

```cpp
enum class SubjectMetadataLevel {
    Summary = 0,
    Details = 1
};
```

Bangumi 搜索摘要只能补充已有字段，不能把已经保存的详情降级为空值。
`metadata_refreshed_at` 只表示详情级数据的最近刷新时间；写入搜索摘要时不得更新它。

### `subject_external_refs`

```sql
CREATE TABLE subject_external_refs (
    subject_id INTEGER NOT NULL,

    provider_key TEXT NOT NULL,
    external_id TEXT NOT NULL,

    fetched_at INTEGER NOT NULL,
    remote_updated_at INTEGER,

    PRIMARY KEY(provider_key, external_id),

    FOREIGN KEY(subject_id)
        REFERENCES subjects(id)
        ON DELETE CASCADE
);

CREATE INDEX idx_subject_external_refs_subject
ON subject_external_refs(subject_id);
```

示例：

```text
subject_id | provider_key | external_id
-----------+--------------+------------
1001       | bangumi      | 400602
```

v0.1 不保存完整 `raw_metadata_json`。Bangumi DTO 经校验后写入标准字段。以后如需保留
原始响应，应作为可清除的 provider snapshot cache 单独设计，不能混入媒体源描述。

## 6.3 内容标签

### `tags`

```sql
CREATE TABLE tags (
    id INTEGER PRIMARY KEY AUTOINCREMENT,

    normalized_name TEXT NOT NULL UNIQUE,
    display_name TEXT NOT NULL,

    CHECK(normalized_name <> ''),
    CHECK(display_name <> '')
);
```

### `subject_tags`

```sql
CREATE TABLE subject_tags (
    subject_id INTEGER NOT NULL,
    tag_id INTEGER NOT NULL,

    provider_key TEXT NOT NULL,
    weight REAL,
    updated_at INTEGER NOT NULL,

    PRIMARY KEY(subject_id, tag_id, provider_key),

    FOREIGN KEY(subject_id)
        REFERENCES subjects(id)
        ON DELETE CASCADE,

    FOREIGN KEY(tag_id)
        REFERENCES tags(id)
        ON DELETE CASCADE
);

CREATE INDEX idx_subject_tags_tag_subject
ON subject_tags(tag_id, subject_id);

CREATE INDEX idx_subject_tags_subject
ON subject_tags(subject_id);
```

`normalized_name` 由应用统一执行 Unicode 规范化、去除首尾空白和大小写折叠。v0.1
不自动合并“百合”“GL”等语义近义词。

`weight` 可以保存 Bangumi 标签计数或未来提供者的置信度。用户按标签筛选时不依赖
FTS 分词。

## 6.4 章节

### `episodes`

```sql
CREATE TABLE episodes (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    subject_id INTEGER NOT NULL,

    sort_order INTEGER NOT NULL,
    episode_type INTEGER NOT NULL DEFAULT 0,
    episode_number REAL,

    title TEXT,
    title_cn TEXT,
    summary TEXT,
    air_date TEXT,
    duration_ms INTEGER,

    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,

    FOREIGN KEY(subject_id)
        REFERENCES subjects(id)
        ON DELETE CASCADE,

    CHECK(sort_order >= 0),
    CHECK(episode_type >= 0),
    CHECK(duration_ms IS NULL OR duration_ms >= 0)
);

CREATE INDEX idx_episodes_subject_order
ON episodes(subject_id, sort_order, id);
```

`sort_order` 是同一条目内稳定的 UI 顺序；`episode_number` 用于显示 Bangumi 的话数。
它们不能代替外部章节 ID。

### `episode_external_refs`

```sql
CREATE TABLE episode_external_refs (
    episode_id INTEGER NOT NULL,

    provider_key TEXT NOT NULL,
    external_id TEXT NOT NULL,

    fetched_at INTEGER NOT NULL,
    remote_updated_at INTEGER,

    PRIMARY KEY(provider_key, external_id),

    FOREIGN KEY(episode_id)
        REFERENCES episodes(id)
        ON DELETE CASCADE
);

CREATE INDEX idx_episode_external_refs_episode
ON episode_external_refs(episode_id);
```

从 Bangumi 刷新章节时，先通过 `(provider_key, external_id)` 查找本地章节，再更新标准
字段。标题和排序相似不能作为自动合并的唯一依据。

## 6.5 本地用户状态

### `subject_user_state`

```sql
CREATE TABLE subject_user_state (
    subject_id INTEGER PRIMARY KEY,

    collection_status INTEGER,
    is_favorite INTEGER NOT NULL DEFAULT 0,

    rating INTEGER,
    comment TEXT,

    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,

    FOREIGN KEY(subject_id)
        REFERENCES subjects(id)
        ON DELETE CASCADE,

    CHECK(collection_status IS NULL
          OR collection_status BETWEEN 1 AND 5),
    CHECK(is_favorite IN (0, 1)),
    CHECK(rating IS NULL OR rating BETWEEN 1 AND 10)
);

CREATE INDEX idx_subject_user_state_collection
ON subject_user_state(collection_status);

CREATE INDEX idx_subject_user_state_favorite
ON subject_user_state(is_favorite);
```

v0.1 收藏状态：

```cpp
enum class LocalCollectionStatus {
    WantToWatch = 1,
    Watching = 2,
    Watched = 3,
    OnHold = 4,
    Dropped = 5
};
```

语义：

- `collection_status IS NULL`：没有加入本地收藏；
- `is_favorite = 1`：用户独立设置了星标；
- 一条记录可以只有星标而没有收藏状态；
- 所有字段恢复默认值后，Store 可以删除空状态行；
- v0.1 不把这些字段同步到 Bangumi。

Bangumi 收藏标签、远程隐私状态、同步状态和 `remote_shadow` 留到后续账号同步设计。

## 6.6 媒体提供者

### `source_providers`

```sql
CREATE TABLE source_providers (
    provider_key TEXT PRIMARY KEY,

    display_name TEXT NOT NULL,
    provider_version TEXT NOT NULL,
    enabled INTEGER NOT NULL DEFAULT 1,

    installed_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,

    CHECK(enabled IN (0, 1))
);
```

v0.1 Migration 或启动初始化必须注册：

```text
provider_key = local.filesystem
```

数据库中的 provider 记录用于展示历史资源和判断提供者是否可用；真正的打开行为由
运行时 `MediaSourceProviderRegistry` 提供。

## 6.7 资源根和可播放项

### `source_resources`

```sql
CREATE TABLE source_resources (
    id INTEGER PRIMARY KEY AUTOINCREMENT,

    provider_key TEXT NOT NULL,
    stable_key TEXT NOT NULL,

    display_title TEXT NOT NULL,

    descriptor_version INTEGER NOT NULL,
    descriptor_json TEXT NOT NULL,

    state INTEGER NOT NULL DEFAULT 0,
    expires_at INTEGER,
    last_verified_at INTEGER,

    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,

    UNIQUE(provider_key, stable_key),

    FOREIGN KEY(provider_key)
        REFERENCES source_providers(provider_key)
        ON DELETE RESTRICT,

    CHECK(descriptor_version >= 1),
    CHECK(state BETWEEN 0 AND 4)
);

CREATE INDEX idx_source_resources_state
ON source_resources(state);
```

### `source_items`

```sql
CREATE TABLE source_items (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    resource_id INTEGER NOT NULL,

    stable_key TEXT NOT NULL,
    display_title TEXT NOT NULL,
    item_index INTEGER,

    descriptor_json TEXT NOT NULL DEFAULT '{}',
    duration_ms INTEGER,
    state INTEGER NOT NULL DEFAULT 0,

    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,

    UNIQUE(resource_id, stable_key),

    FOREIGN KEY(resource_id)
        REFERENCES source_resources(id)
        ON DELETE CASCADE,

    CHECK(item_index IS NULL OR item_index >= 0),
    CHECK(state BETWEEN 0 AND 4),
    CHECK(duration_ms IS NULL OR duration_ms >= 0)
);

CREATE INDEX idx_source_items_resource
ON source_items(resource_id);
```

状态定义：

```cpp
enum class SourceState {
    Available = 0,
    Missing = 1,
    ProviderUnavailable = 2,
    InvalidDescriptor = 3,
    Stale = 4
};
```

`source_resources.descriptor_json` 是提供者拥有的根描述，
`source_items.descriptor_json` 是某个具体可播放项的补充描述。宿主只校验：

- JSON 大小与语法；
- `provider_key` 已注册；
- `descriptor_version` 为正数；
- 状态值与时间范围合法。

宿主不得依赖 provider 私有字段进行业务查询。

v0.1 `local.filesystem` 提供者需要进一步校验规范路径、文件类型、文件大小、修改时间
和可选指纹。路径不存在时更新为 `Missing`，不删除资源及历史播放进度。

## 6.8 章节与资源项关联

### `episode_source_links`

```sql
CREATE TABLE episode_source_links (
    episode_id INTEGER NOT NULL,
    source_item_id INTEGER NOT NULL,

    match_kind INTEGER NOT NULL,
    confidence REAL,
    priority INTEGER NOT NULL DEFAULT 0,
    created_at INTEGER NOT NULL,

    PRIMARY KEY(episode_id, source_item_id),

    FOREIGN KEY(episode_id)
        REFERENCES episodes(id)
        ON DELETE CASCADE,

    FOREIGN KEY(source_item_id)
        REFERENCES source_items(id)
        ON DELETE CASCADE,

    CHECK(match_kind BETWEEN 0 AND 2),
    CHECK(confidence IS NULL
          OR (confidence >= 0.0 AND confidence <= 1.0))
);

CREATE INDEX idx_episode_source_links_item
ON episode_source_links(source_item_id);
```

```cpp
enum class MediaMatchKind {
    UserConfirmed = 0,
    FilenameParsed = 1,
    OrderInferred = 2
};
```

v0.1 必须支持 `UserConfirmed`。文件名解析可以产生候选，但低置信度结果在用户确认前
不得永久关联。

### 有效媒体源查询

“章节是否有有效源”由以下条件推导：

```text
episode_source_links 存在
AND source_items.state = Available
AND source_resources.state = Available
AND source_providers.enabled = 1
AND source_resources 未过期
```

概念查询：

```sql
SELECT EXISTS (
    SELECT 1
    FROM episode_source_links l
    JOIN source_items i
      ON i.id = l.source_item_id
    JOIN source_resources r
      ON r.id = i.resource_id
    JOIN source_providers p
      ON p.provider_key = r.provider_key
    WHERE l.episode_id = :episode_id
      AND i.state = 0
      AND r.state = 0
      AND p.enabled = 1
      AND (r.expires_at IS NULL OR r.expires_at > :now)
);
```

不得在 `subjects` 或 `episodes` 中重复保存 `has_valid_source`。

## 6.9 播放进度与最近播放

### `playback_progress`

```sql
CREATE TABLE playback_progress (
    episode_id INTEGER PRIMARY KEY,
    last_source_item_id INTEGER,

    position_ms INTEGER NOT NULL,
    duration_ms INTEGER,
    completed INTEGER NOT NULL DEFAULT 0,

    last_played_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,

    FOREIGN KEY(episode_id)
        REFERENCES episodes(id)
        ON DELETE CASCADE,

    FOREIGN KEY(last_source_item_id)
        REFERENCES source_items(id)
        ON DELETE SET NULL,

    CHECK(position_ms >= 0),
    CHECK(duration_ms IS NULL OR duration_ms >= 0),
    CHECK(completed IN (0, 1))
);

CREATE INDEX idx_playback_progress_recent
ON playback_progress(last_played_at DESC, episode_id DESC);

CREATE INDEX idx_playback_progress_source
ON playback_progress(last_source_item_id);
```

v0.1 语义：

- 每个本地章节只有一份当前恢复进度；
- `last_source_item_id` 记录上次实际播放的资源项；
- 切换同一章节的媒体源不会创建另一份逻辑进度；
- `completed` 由应用层完成判定产生；
- 文件缺失时保留进度，UI 可以提示重新关联；
- 最近播放按 `last_played_at DESC, episode_id DESC` 查询，确保相同时间戳下分页
  顺序稳定。

播放位置不得按每个 ClockUpdate 写入：

- 正常播放每 10～15 秒保存一次；
- Pause、Stop、切集和正常退出时立即保存；
- Seek 后等待播放状态稳定再保存；
- 数据库只执行传入的标准进度，不自行推断播放状态。

v0.1 不保存每次 Play/Pause/Seek 的完整事件流水。以后若需要观看统计，再增加
`playback_sessions`，不能从当前进度表反推完整历史。

## 6.10 本地全文搜索

### `subject_fts`

```sql
CREATE VIRTUAL TABLE subject_fts USING fts5(
    subject_id UNINDEXED,
    title,
    title_cn,
    aliases,
    summary
);
```

FTS5 是 v0.1 的构建时必需能力，而不是运行时可选优化。xmake 使用的 SQLite
amalgamation 必须带 `SQLITE_ENABLE_FTS5` 编译；应用启动时创建 `subject_fts`
失败应使 Migration 整体回滚并报告不兼容，不能留下缺少本地搜索能力的半成品
Schema。

条目创建、更新和合并时必须在同一事务中同步 FTS。标签筛选使用
`subject_tags`，不把标签拼接进 FTS 作为唯一查询方式。

---

# 7. v0.1 完整 Migration

以下 SQL 是 v0.1 的目标 Schema。实际实现可以拆为多个按版本编号的 Migration。

```sql
PRAGMA foreign_keys = ON;

CREATE TABLE schema_migrations (
    version INTEGER NOT NULL PRIMARY KEY,
    applied_at INTEGER NOT NULL
);

CREATE TABLE subjects (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    subject_type INTEGER NOT NULL DEFAULT 2,
    title TEXT NOT NULL,
    title_cn TEXT,
    summary TEXT,
    air_date TEXT,
    cover_url TEXT,
    aliases_json TEXT NOT NULL DEFAULT '[]',
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    metadata_level INTEGER NOT NULL DEFAULT 0,
    metadata_refreshed_at INTEGER,
    CHECK(subject_type >= 0),
    CHECK(metadata_level BETWEEN 0 AND 1)
);

CREATE TABLE subject_external_refs (
    subject_id INTEGER NOT NULL,
    provider_key TEXT NOT NULL,
    external_id TEXT NOT NULL,
    fetched_at INTEGER NOT NULL,
    remote_updated_at INTEGER,
    PRIMARY KEY(provider_key, external_id),
    FOREIGN KEY(subject_id)
        REFERENCES subjects(id)
        ON DELETE CASCADE
);

CREATE INDEX idx_subject_external_refs_subject
ON subject_external_refs(subject_id);

CREATE TABLE tags (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    normalized_name TEXT NOT NULL UNIQUE,
    display_name TEXT NOT NULL,
    CHECK(normalized_name <> ''),
    CHECK(display_name <> '')
);

CREATE TABLE subject_tags (
    subject_id INTEGER NOT NULL,
    tag_id INTEGER NOT NULL,
    provider_key TEXT NOT NULL,
    weight REAL,
    updated_at INTEGER NOT NULL,
    PRIMARY KEY(subject_id, tag_id, provider_key),
    FOREIGN KEY(subject_id)
        REFERENCES subjects(id)
        ON DELETE CASCADE,
    FOREIGN KEY(tag_id)
        REFERENCES tags(id)
        ON DELETE CASCADE
);

CREATE INDEX idx_subject_tags_tag_subject
ON subject_tags(tag_id, subject_id);

CREATE INDEX idx_subject_tags_subject
ON subject_tags(subject_id);

CREATE TABLE episodes (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    subject_id INTEGER NOT NULL,
    sort_order INTEGER NOT NULL,
    episode_type INTEGER NOT NULL DEFAULT 0,
    episode_number REAL,
    title TEXT,
    title_cn TEXT,
    summary TEXT,
    air_date TEXT,
    duration_ms INTEGER,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    FOREIGN KEY(subject_id)
        REFERENCES subjects(id)
        ON DELETE CASCADE,
    CHECK(sort_order >= 0),
    CHECK(episode_type >= 0),
    CHECK(duration_ms IS NULL OR duration_ms >= 0)
);

CREATE INDEX idx_episodes_subject_order
ON episodes(subject_id, sort_order, id);

CREATE TABLE episode_external_refs (
    episode_id INTEGER NOT NULL,
    provider_key TEXT NOT NULL,
    external_id TEXT NOT NULL,
    fetched_at INTEGER NOT NULL,
    remote_updated_at INTEGER,
    PRIMARY KEY(provider_key, external_id),
    FOREIGN KEY(episode_id)
        REFERENCES episodes(id)
        ON DELETE CASCADE
);

CREATE INDEX idx_episode_external_refs_episode
ON episode_external_refs(episode_id);

CREATE TABLE subject_user_state (
    subject_id INTEGER PRIMARY KEY,
    collection_status INTEGER,
    is_favorite INTEGER NOT NULL DEFAULT 0,
    rating INTEGER,
    comment TEXT,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    FOREIGN KEY(subject_id)
        REFERENCES subjects(id)
        ON DELETE CASCADE,
    CHECK(collection_status IS NULL
          OR collection_status BETWEEN 1 AND 5),
    CHECK(is_favorite IN (0, 1)),
    CHECK(rating IS NULL OR rating BETWEEN 1 AND 10)
);

CREATE INDEX idx_subject_user_state_collection
ON subject_user_state(collection_status);

CREATE INDEX idx_subject_user_state_favorite
ON subject_user_state(is_favorite);

CREATE TABLE source_providers (
    provider_key TEXT PRIMARY KEY,
    display_name TEXT NOT NULL,
    provider_version TEXT NOT NULL,
    enabled INTEGER NOT NULL DEFAULT 1,
    installed_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    CHECK(enabled IN (0, 1))
);

CREATE TABLE source_resources (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    provider_key TEXT NOT NULL,
    stable_key TEXT NOT NULL,
    display_title TEXT NOT NULL,
    descriptor_version INTEGER NOT NULL,
    descriptor_json TEXT NOT NULL,
    state INTEGER NOT NULL DEFAULT 0,
    expires_at INTEGER,
    last_verified_at INTEGER,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    UNIQUE(provider_key, stable_key),
    FOREIGN KEY(provider_key)
        REFERENCES source_providers(provider_key)
        ON DELETE RESTRICT,
    CHECK(descriptor_version >= 1),
    CHECK(state BETWEEN 0 AND 4)
);

CREATE INDEX idx_source_resources_state
ON source_resources(state);

CREATE TABLE source_items (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    resource_id INTEGER NOT NULL,
    stable_key TEXT NOT NULL,
    display_title TEXT NOT NULL,
    item_index INTEGER,
    descriptor_json TEXT NOT NULL DEFAULT '{}',
    duration_ms INTEGER,
    state INTEGER NOT NULL DEFAULT 0,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    UNIQUE(resource_id, stable_key),
    FOREIGN KEY(resource_id)
        REFERENCES source_resources(id)
        ON DELETE CASCADE,
    CHECK(item_index IS NULL OR item_index >= 0),
    CHECK(state BETWEEN 0 AND 4),
    CHECK(duration_ms IS NULL OR duration_ms >= 0)
);

CREATE INDEX idx_source_items_resource
ON source_items(resource_id);

CREATE TABLE episode_source_links (
    episode_id INTEGER NOT NULL,
    source_item_id INTEGER NOT NULL,
    match_kind INTEGER NOT NULL,
    confidence REAL,
    priority INTEGER NOT NULL DEFAULT 0,
    created_at INTEGER NOT NULL,
    PRIMARY KEY(episode_id, source_item_id),
    FOREIGN KEY(episode_id)
        REFERENCES episodes(id)
        ON DELETE CASCADE,
    FOREIGN KEY(source_item_id)
        REFERENCES source_items(id)
        ON DELETE CASCADE,
    CHECK(match_kind BETWEEN 0 AND 2),
    CHECK(confidence IS NULL
          OR (confidence >= 0.0 AND confidence <= 1.0))
);

CREATE INDEX idx_episode_source_links_item
ON episode_source_links(source_item_id);

CREATE TABLE playback_progress (
    episode_id INTEGER PRIMARY KEY,
    last_source_item_id INTEGER,
    position_ms INTEGER NOT NULL,
    duration_ms INTEGER,
    completed INTEGER NOT NULL DEFAULT 0,
    last_played_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    FOREIGN KEY(episode_id)
        REFERENCES episodes(id)
        ON DELETE CASCADE,
    FOREIGN KEY(last_source_item_id)
        REFERENCES source_items(id)
        ON DELETE SET NULL,
    CHECK(position_ms >= 0),
    CHECK(duration_ms IS NULL OR duration_ms >= 0),
    CHECK(completed IN (0, 1))
);

CREATE INDEX idx_playback_progress_recent
ON playback_progress(last_played_at DESC, episode_id DESC);

CREATE INDEX idx_playback_progress_source
ON playback_progress(last_source_item_id);

CREATE VIRTUAL TABLE subject_fts USING fts5(
    subject_id UNINDEXED,
    title,
    title_cn,
    aliases,
    summary
);
```

---

# 8. v0.1 领域值对象

数据库整数 ID 不直接散落在 Presentation 中，至少定义：

```cpp
struct SubjectId {
    std::int64_t value;
};

struct EpisodeId {
    std::int64_t value;
};

struct SourceResourceId {
    std::int64_t value;
};

struct SourceItemId {
    std::int64_t value;
};
```

核心输入对象：

```cpp
struct ExternalRef {
    QString providerKey;
    QString externalId;
};

struct SubjectTagSnapshot {
    QString name;
    std::optional<double> weight;
};

struct SubjectSnapshot {
    ExternalRef origin;
    SubjectMetadataLevel metadataLevel;
    int subjectType;
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

struct EpisodeSnapshot {
    ExternalRef origin;
    int sortOrder;
    int episodeType;
    std::optional<double> episodeNumber;
    QString title;
    QString titleCn;
    QString summary;
    std::optional<QDate> airDate;
    std::optional<std::chrono::milliseconds> duration;
    QDateTime fetchedAt;
    std::optional<QDateTime> remoteUpdatedAt;
};
```

媒体描述：

```cpp
struct PersistedSourceDescriptor {
    QString providerKey;
    int descriptorVersion;
    QByteArray resourceJson;
    QByteArray itemJson;
};

struct PlaybackTarget {
    SubjectId subjectId;
    EpisodeId episodeId;
    SourceItemId sourceItemId;
    PersistedSourceDescriptor descriptor;
    std::chrono::milliseconds resumePosition;
};
```

标准播放进度：

```cpp
struct PlaybackProgressUpdate {
    EpisodeId episodeId;
    SourceItemId sourceItemId;
    std::chrono::milliseconds position;
    std::optional<std::chrono::milliseconds> duration;
    bool completed;
    QDateTime playedAt;
};
```

provider 私有 JSON 不进入 `PlaybackProgressUpdate`。

---

# 9. v0.1 Application API

这些接口供 Presentation 或上层用例调用。具体信号、Model 和分页 DTO 可以按 Qt UI
需要扩充，但不得让 UI 直接依赖 Store 或 SQL。

## 9.1 条目目录

```cpp
struct LocalSubjectQuery {
    QString text;
    std::optional<QString> tag;
    int limit = 50;
    int offset = 0;
};

class SubjectCatalogService {
public:
    auto searchLocal(LocalSubjectQuery query)
        -> ilias::Task<Result<std::vector<SubjectSummary>>>;

    auto searchBangumiAndStore(
        QString query,
        int limit,
        int offset
    ) -> ilias::Task<Result<std::vector<SubjectSummary>>>;

    auto getLocalSubject(SubjectId subject)
        -> ilias::Task<Result<std::optional<SubjectDetails>>>;

    auto refreshSubjectFromBangumi(SubjectId subject)
        -> ilias::Task<Result<SubjectDetails>>;

    auto refreshEpisodesFromBangumi(SubjectId subject)
        -> ilias::Task<Result<std::vector<EpisodeDetails>>>;

    auto listSubjectsByTag(QString tag, int limit, int offset)
        -> ilias::Task<Result<std::vector<SubjectSummary>>>;

    auto listTags(QString prefix, int limit)
        -> ilias::Task<Result<std::vector<TagFacet>>>;
};
```

v0.1 使用明确的两阶段搜索：

```text
searchLocal()
→ 立即显示已经持久化的条目

searchBangumiAndStore()
→ 请求网络
→ 原子写入条目、外部映射、标签和 FTS
→ 返回该次 Bangumi 排序结果
```

因为 v0.1 不保存 `search_queries/search_results`，Presentation 不能假设重启后仍能恢复
上次网络结果的精确顺序。

## 9.2 本地收藏与星标

```cpp
class LocalLibraryService {
public:
    auto getUserState(SubjectId subject)
        -> ilias::Task<Result<SubjectUserState>>;

    auto setCollectionStatus(
        SubjectId subject,
        std::optional<LocalCollectionStatus> status
    ) -> ilias::Task<Result<void>>;

    auto setFavorite(SubjectId subject, bool favorite)
        -> ilias::Task<Result<void>>;

    auto setRating(SubjectId subject, std::optional<int> rating)
        -> ilias::Task<Result<void>>;

    auto setComment(SubjectId subject, QString comment)
        -> ilias::Task<Result<void>>;

    auto listCollection(
        std::optional<LocalCollectionStatus> status,
        int limit,
        int offset
    ) -> ilias::Task<Result<std::vector<LibrarySubject>>>;

    auto listFavorites(int limit, int offset)
        -> ilias::Task<Result<std::vector<LibrarySubject>>>;
};
```

所有方法只修改本地状态，不调用 Bangumi。

## 9.3 本地媒体库

```cpp
struct LocalFileImportRequest {
    EpisodeId episodeId;
    QString absolutePath;
    MediaMatchKind matchKind = MediaMatchKind::UserConfirmed;
    std::optional<double> confidence;
};

class MediaLibraryService {
public:
    auto importLocalFileAndLink(LocalFileImportRequest request)
        -> ilias::Task<Result<SourceItemId>>;

    auto linkSourceItem(
        EpisodeId episode,
        SourceItemId item,
        MediaMatchKind matchKind,
        std::optional<double> confidence
    ) -> ilias::Task<Result<void>>;

    auto unlinkSourceItem(EpisodeId episode, SourceItemId item)
        -> ilias::Task<Result<void>>;

    auto listSources(EpisodeId episode)
        -> ilias::Task<Result<std::vector<EpisodeSource>>>;

    auto verifySource(SourceItemId item)
        -> ilias::Task<Result<SourceState>>;

    auto makePlaybackTarget(
        EpisodeId episode,
        std::optional<SourceItemId> preferred
    ) -> ilias::Task<Result<PlaybackTarget>>;
};
```

`importLocalFileAndLink()` 必须在单一事务中：

1. 规范化并验证绝对路径；
2. 计算稳定键和可选文件指纹；
3. upsert `source_resources`；
4. upsert `source_items`；
5. upsert `episode_source_links`；
6. 返回本地 `SourceItemId`。

`makePlaybackTarget()` 只组装目标并读取恢复进度，不直接创建或启动
`PlaybackSession`。

## 9.4 播放进度和最近播放

```cpp
class PlaybackProgressService {
public:
    auto saveProgress(PlaybackProgressUpdate update)
        -> ilias::Task<Result<void>>;

    auto getProgress(EpisodeId episode)
        -> ilias::Task<Result<std::optional<PlaybackProgress>>>;

    auto listRecent(int limit, int offset)
        -> ilias::Task<Result<std::vector<RecentPlayback>>>;

    auto clearProgress(EpisodeId episode)
        -> ilias::Task<Result<void>>;
};
```

`saveProgress()` 必须校验：

- `position >= 0`；
- `duration` 非负；
- 已知时 `position` 不得无界超过 `duration`；
- `sourceItemId` 确实关联到该章节；
- 旧时钟或旧播放 generation 的更新不得覆盖新会话状态。

播放保存节流属于 Application Service，不属于 Store。

---

# 10. v0.1 Infrastructure API

## 10.1 Bangumi 目录客户端

数据库设计只要求以下只读目录能力：

```cpp
class BangumiCatalogClient {
public:
    auto searchSubjects(QString query, int limit, int offset)
        -> ilias::Task<Result<std::vector<BangumiSubjectDto>>>;

    auto getSubject(std::int64_t bangumiSubjectId)
        -> ilias::Task<Result<BangumiSubjectDetailsDto>>;

    auto getEpisodes(std::int64_t bangumiSubjectId)
        -> ilias::Task<Result<std::vector<BangumiEpisodeDto>>>;
};
```

Bangumi DTO 必须先经过协议和业务约束校验，再转换为 `SubjectSnapshot` 与
`EpisodeSnapshot`。Client 不直接执行 SQL。

`searchSubjects()` 是公开目录 API：未登录时匿名请求，当前会话已有 Token 时可以作为
可选 Authorization Header 使用。搜索不触发登录恢复，也不检查
`BangumiCapability`；可选 Token 失效不能把公开搜索变成必须重新登录的功能。

以下接口不属于本数据库 v0.1 契约：

```text
getCollections()
updateCollection()
updateEpisodeProgress()
pullCollections()
pushPendingChanges()
resolveConflict()
```

## 10.2 关系数据库 Store

`LocalDatabase` 必须从 `AppSettings::sql_settings` 创建连接，由
`database_type` 选择 SQLite、SQLCipher 或 MySQL，不在 Store 中写死驱动。
表结构、约束和通用 CRUD 由 ilias-sql 的后端描述与 ORM 生成；SQLite 的
PRAGMA、SQLCipher 密钥校验和 FTS5 同步属于显式后端扩展，其他后端提供对应的
搜索实现。

`Form` 是关系表实例，而不是一次性查询构造器。`CatalogStore` 首次使用时统一
attach/审查所需 Form，并在整个 Store 生命周期内复用；查询需要别名时从已有 Form
调用 `as()` 派生。事务开始后使用已有 Form 的 `getTableName()` 建立事务视图，
不得再次散落表名字符串。Record struct 只描述字段参数包，不承载表名或 alias。

Store API 只表达持久化操作：

```cpp
class CatalogStore {
public:
    auto upsertSubjectSnapshot(SubjectSnapshot snapshot)
        -> ilias::Task<Result<SubjectId>>;

    auto upsertEpisodeSnapshots(
        SubjectId subject,
        std::vector<EpisodeSnapshot> snapshots
    ) -> ilias::Task<Result<std::vector<EpisodeId>>>;

    auto replaceSubjectTags(
        SubjectId subject,
        QString providerKey,
        std::vector<SubjectTagSnapshot> tags
    ) -> ilias::Task<Result<void>>;

    auto getSubject(SubjectId subject)
        -> ilias::Task<Result<std::optional<SubjectDetails>>>;

    auto getEpisode(EpisodeId episode)
        -> ilias::Task<Result<std::optional<EpisodeDetails>>>;

    auto findSubjectByExternalRef(ExternalRef ref)
        -> ilias::Task<Result<std::optional<SubjectId>>>;

    auto findEpisodeByExternalRef(ExternalRef ref)
        -> ilias::Task<Result<std::optional<EpisodeId>>>;

    auto searchSubjects(LocalSubjectQuery query)
        -> ilias::Task<Result<std::vector<SubjectSummary>>>;

    auto listEpisodes(SubjectId subject)
        -> ilias::Task<Result<std::vector<EpisodeDetails>>>;

    auto listTags(LocalTagQuery query)
        -> ilias::Task<Result<std::vector<TagFacet>>>;
};
```

正常业务代码不得通过 `LocalDatabase` 拼接 SQL。关系表的完整读写必须以反射
`struct` 为字段集合的唯一事实来源；只有明确的局部更新/投影才枚举成员指针，不得
手写完整列清单。`advancedConnection()` 只作为明确的高级逃生口保留，适用于 ORM
无法表达的后端扩展（如 FTS5、PRAGMA 和系统目录探测）、诊断工具、一次性维护和
Schema 约束测试。使用该接口的调用方自行承担 SQL 方言、事务一致性和参数绑定责任；
新增业务能力时应优先扩展接受/返回领域 `struct` 的 Store API。

`upsertSubjectSnapshot()` 必须原子完成：

```text
查找或创建本地 subject
→ 更新 subject_external_refs
→ 更新标准元数据
→ 替换该 provider 的 subject_tags
→ 更新 subject_fts
```

快照合并必须遵守：

- `metadataLevel` 只能升级，不能从 `Details` 降回 `Summary`；
- 摘要快照中未提供的可选字段不得清空已有详情；
- `aliases` 或 `tags` 为 `nullopt` 表示“不更新”；
- `aliases` 或 `tags` 为存在但空的数组，才表示该 provider 明确返回空集合；
- 只有详情级刷新更新 `metadata_refreshed_at`。

其他 Store：

```cpp
class UserStateStore {
public:
    auto get(SubjectId subject)
        -> ilias::Task<Result<SubjectUserState>>;

    auto upsert(SubjectId subject, SubjectUserStatePatch patch)
        -> ilias::Task<Result<void>>;

    auto list(UserStateQuery query)
        -> ilias::Task<Result<std::vector<LibrarySubject>>>;
};

class MediaResourceStore {
public:
    auto importLocalFileAndLink(LocalFileImportRecord record)
        -> ilias::Task<Result<SourceItemId>>;

    auto listForEpisode(EpisodeId episode)
        -> ilias::Task<Result<std::vector<EpisodeSource>>>;

    auto getDescriptor(SourceItemId item)
        -> ilias::Task<Result<std::optional<PersistedSourceDescriptor>>>;

    auto updateState(SourceResourceId resource, SourceState state)
        -> ilias::Task<Result<void>>;
};

class PlaybackProgressStore {
public:
    auto upsert(PlaybackProgressUpdate update)
        -> ilias::Task<Result<void>>;

    auto get(EpisodeId episode)
        -> ilias::Task<Result<std::optional<PlaybackProgress>>>;

    auto listRecent(int limit, int offset)
        -> ilias::Task<Result<std::vector<RecentPlayback>>>;

    auto remove(EpisodeId episode)
        -> ilias::Task<Result<void>>;
};
```

Store 不调用 Bangumi Client、不打开文件、不构建播放器。

## 10.3 媒体提供者

```cpp
class MediaSourceProvider {
public:
    virtual ~MediaSourceProvider() = default;

    virtual auto providerKey() const -> QString = 0;

    virtual auto supportsDescriptorVersion(int version) const
        -> bool = 0;

    virtual auto verify(PersistedSourceDescriptor descriptor)
        -> ilias::Task<Result<SourceState>> = 0;

    virtual auto open(PersistedSourceDescriptor descriptor)
        -> ilias::Task<Result<std::unique_ptr<AsyncMediaSource>>> = 0;
};

class MediaSourceProviderRegistry {
public:
    auto registerProvider(std::unique_ptr<MediaSourceProvider> provider)
        -> Result<void>;

    auto find(QString providerKey)
        -> MediaSourceProvider*;
};
```

v0.1 只要求：

```text
LocalFilesystemMediaSourceProvider
providerKey() == "local.filesystem"
```

打开流程：

```text
PlaybackTarget
    ↓
registry.find(descriptor.providerKey)
    ↓
provider.verify(descriptor)
    ↓
provider.open(descriptor)
    ↓
AsyncMediaSource
    ↓
PlaybackSession
```

未来插件可以实现同一接口，但插件发现、安装、能力协商和候选索引均不属于 v0.1。

---

# 11. v0.1 关键流程

## 11.1 搜索 Bangumi 并保存条目

```text
Presentation.search(query)
    ↓
SubjectCatalogService.searchLocal(query)
    ↓
立即显示已知条目
    ↓
SubjectCatalogService.searchBangumiAndStore(query)
    ↓
BangumiCatalogClient.searchSubjects()
    ↓
DTO → SubjectSnapshot
    ↓
CatalogStore.upsertSubjectSnapshot()
    ├─ subjects
    ├─ subject_external_refs
    ├─ tags / subject_tags
    └─ subject_fts
    ↓
返回本次 Bangumi 排序结果
```

网络失败时保留本地结果，不回滚之前已经存在的条目。

## 11.2 获取详情和章节

```text
打开条目详情
    ↓
先读取本地 SubjectDetails
    ↓
按需 refreshSubjectFromBangumi()
    ↓
按需 refreshEpisodesFromBangumi()
    ↓
原子 upsert 条目和章节快照
    ↓
刷新 UI
```

远程刷新失败不删除旧元数据和已有媒体关联。

## 11.3 收藏和星标

```text
用户修改收藏状态或星标
    ↓
LocalLibraryService
    ↓
upsert subject_user_state
    ↓
UI 读取本地结果
```

v0.1 不等待也不调用 Bangumi 收藏接口。

## 11.4 关联本地文件

```text
用户选择章节和本地文件
    ↓
MediaLibraryService.importLocalFileAndLink()
    ↓
local.filesystem 校验并生成描述
    ↓
事务写入 resource + item + episode link
    ↓
章节显示“存在可用本地源”
```

文件名自动匹配只能产生候选；用户确认后才写入永久关联。

## 11.5 播放和恢复

```text
用户播放章节
    ↓
MediaLibraryService.makePlaybackTarget()
    ├─ 选择可用 source item
    └─ 读取 playback_progress
    ↓
ProviderRegistry 打开 AsyncMediaSource
    ↓
PlaybackSession 播放并 Seek 到恢复点
    ↓
PlaybackProgressService 定期保存
```

如果 `last_source_item_id` 已缺失，可以选择同章节其他有效源；逻辑恢复进度不变。

## 11.6 最近播放

```text
PlaybackProgressService.listRecent()
    ↓
playback_progress.last_played_at DESC
    ↓
JOIN episodes / subjects / last source
    ↓
显示条目、章节、进度和源状态
```

完成项目是否继续出现在最近播放由 Presentation 查询参数或产品策略决定，不改变
进度表的事实记录。

---

# 12. 一致性与事务规则

以下操作必须原子执行：

1. `SubjectSnapshot` 写入条目、外部映射、标签和 FTS；
2. 章节快照写入章节和外部映射；
3. 导入本地文件并建立章节关联；
4. 合并重复条目时移动章节、用户状态、媒体映射和进度；
5. 删除条目时依赖外键级联清理业务数据。

数据库连接必须启用：

```sql
PRAGMA foreign_keys = ON;
```

`PRAGMA foreign_keys = ON` 必须在开始 Migration 事务前执行并查询确认；SQLite 在事务
中切换该设置不会对当前事务生效。

ilias-sql 的 SQLite 驱动通过单个 prepared statement 执行 `execute()`。Migration
实现必须把下方完整 Schema 拆成逐条 DDL 调用，并在同一个
`SqlTransaction` 中依次执行，不能把整段多语句 SQL 一次传给 `execute()`。

建议启用 WAL，但它属于数据库运行配置，不写入业务 Schema：

```sql
PRAGMA journal_mode = WAL;
```

所有写操作通过统一数据库执行上下文串行化。Repository/Store 不向 UI 暴露事务对象。

### 12.1 条目合并

标题相似只能提示可能重复，不能直接自动合并。

`mergeSubjects(sourceId, targetId)` 后续实现时，事务至少需要：

1. 合并 `subject_external_refs`；
2. 合并 `subject_tags`；
3. 合并 `subject_user_state`；
4. 移动 `episodes` 及其外部映射；
5. 保留章节的资源关联和播放进度；
6. 更新 FTS；
7. 删除源条目。

v0.1 可以只提供手工确认后的合并，不要求自动消歧。

### 12.2 时间和单位

- 数据库时间统一保存 Unix epoch 毫秒；
- 媒体位置和时长统一保存毫秒；
- 日期字段使用 `YYYY-MM-DD`；
- 所有输入在 Application 层转换后再交给 Store；
- 不把 Qt 本地时区格式字符串直接写入时间戳字段。

---

# 13. v0.1 测试和验收

## 13.1 Migration

- 空数据库可以一次迁移到当前版本；
- 重复启动不会重复应用 Migration；
- 外键约束确实启用；
- 失败 Migration 整体回滚；
- 测试使用临时数据库。

## 13.2 条目和标签

- 同一 `bangumi + external_id` 重复写入不会创建重复条目；
- 同名但不同外部 ID 的条目不会自动合并；
- 搜索摘要不会清空或降级已经保存的详情；
- Bangumi 标签能按 provider 替换；
- 可以准确查询“治愈”“百合”等标签对应的全部本地条目；
- 更新条目后 FTS 与标准表一致。

## 13.3 本地用户状态

- 未收藏条目可以单独星标；
- 清除收藏状态不会误删仍存在的星标或备注；
- 收藏列表和星标列表可分页；
- 所有状态均为本地操作，不依赖登录和网络。

## 13.4 媒体资源

- 本地文件重复导入能通过稳定键复用资源；
- 一个章节可以关联多个资源项；
- 一个资源项可以手工关联到章节；
- 文件删除后资源标记 `Missing`，数据库记录和播放进度仍保留；
- provider 不存在或禁用时返回结构化错误；
- 非法或不支持版本的 descriptor 不会传给播放器。

## 13.5 播放进度

- 播放位置可以持久化并在重启后恢复；
- Pause、Stop、切集和退出会立即保存；
- 同一章节切换媒体源仍读取同一逻辑进度；
- 旧播放 generation 的延迟更新不会覆盖新进度；
- 最近播放按 `last_played_at DESC, episode_id DESC` 稳定排序；
- 删除最后使用的资源后仍保留章节进度；
- 删除章节后进度按外键级联删除。

## 13.6 v0.1 端到端验收

必须通过：

```text
搜索 Bangumi 条目
→ 保存条目和标签
→ 获取并保存章节
→ 设置本地收藏或星标
→ 选择本地 MP4/MKV
→ 手工关联到章节
→ 使用 local.filesystem Provider 播放
→ 保存进度
→ 重启应用
→ 从最近播放继续
```

断网不得影响已经关联的本地文件播放。

---

# 14. 推荐实现顺序

## 阶段 1：数据库基础和目录

实现：

```text
schema_migrations
subjects
subject_external_refs
tags
subject_tags
episodes
episode_external_refs
subject_fts
CatalogStore
SubjectCatalogService
```

验收：Bangumi 搜索、详情和章节可写入并从本地读取，标签可查询。

## 阶段 2：本地用户状态

实现：

```text
subject_user_state
UserStateStore
LocalLibraryService
```

验收：未登录时可以收藏、取消收藏、星标和查询列表。

## 阶段 3：本地媒体资源

实现：

```text
source_providers
source_resources
source_items
episode_source_links
MediaResourceStore
MediaLibraryService
LocalFilesystemMediaSourceProvider
```

验收：选择本地文件、关联章节、重启后重新打开。

## 阶段 4：播放进度与最近播放

实现：

```text
playback_progress
PlaybackProgressStore
PlaybackProgressService
最近播放查询
PlaybackSession 保存节流
```

验收：位置可持久化、恢复并显示最近播放。

---

# 15. 后续版本演进

v0.1 完成后再按实际需求增加：

| 能力 | 建议数据结构 |
|---|---|
| 精确网络搜索缓存 | `search_queries`、`search_results` |
| Bangumi 账号 | `external_accounts` |
| 远程收藏状态 | `collection_remote_states`，按账号关联 |
| 同步队列 | `sync_operations` |
| 数据源插件发现 | `source_candidates` |
| 完整观看统计 | `playback_sessions` |
| 下载 | `downloads`、`download_items` |
| 图片缓存 | 独立可清理缓存索引 |

Bangumi 远程收藏同步不得把 `remote_shadow` 重新塞进
`subject_user_state`。它必须关联具体外部账号，避免退出账号 A 后登录账号 B 时复用错误
同步状态。

在线源插件可以复用 `source_resources/source_items`。发现阶段的临时
`opaque_index` 与已解析、可重新打开的持久化 descriptor 必须继续分离。

---

# 16. 结论

v0.1 数据库是本地播放闭环的统一事实来源：

```text
标准化描述
├─ subjects
├─ tags
└─ episodes

外部身份
├─ subject_external_refs
└─ episode_external_refs

本地用户状态
└─ subject_user_state

媒体源
├─ source_providers
├─ source_resources
├─ source_items
└─ episode_source_links

播放状态
└─ playback_progress
```

Bangumi 在 v0.1 是可选的只读目录提供者；`local.filesystem` 是唯一必须实现的媒体
提供者；播放进度由宿主统一记录。数据库不保存真正闭包，只保存可以由 provider
重新构造媒体源的版本化描述。
