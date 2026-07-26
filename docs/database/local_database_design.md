# 本地目录数据库设计

本文描述当前代码已经实现的持久化边界，不把未来设想写成现有约束。

## 1. 职责边界

`LocalDatabase` 只负责把应用配置翻译成 ilias-sql 的连接参数并打开驱动：

- `sqlite` 与 `sqlcipher` 使用 SQLite 驱动；
- SQLCipher 密钥通过 `ConnectOptions.extra["key"]` 交给驱动；
- `mysql` 与 `mariadb` 使用 MySQL 驱动；
- 不支持的类型返回 `DialectNotSupported`。

`LocalDatabase` 不创建业务表，不运行迁移，不查询数据库系统目录，也不执行
`PRAGMA`。后端默认行为保持不变；确需改变时使用驱动提供的显式 option。
journal/WAL 等部署策略不由应用偷偷修改。

anime-land 的关系模型依赖外键执行，因此 SQLite/SQLCipher 连接分支明确传入
`ConnectOptions.extra["EnableFKey"] = "true"`。这是应用在连接边界声明的策略，
不是 ilias-sql 的隐藏默认；如需改变，修改该显式 option。

`CatalogStore` 负责自己使用的六个关系和长期 `Form`：

- `subjects`
- `subject_external_refs`
- `tags`
- `subject_tags`
- `episodes`
- `episode_external_refs`

`LibraryStore` 负责媒体资源与章节关联使用的三个关系和长期 `Form`：

- `media_resources`
- `source_items`
- `episode_media_links`

数据库中的其他表、视图、索引和调用方数据不属于这两个 Store。

图形入口只在 SQLite/SQLCipher 使用相对 `database_path` 时把它解析到 Qt 的
`AppLocalDataLocation`，并在打开连接前创建父目录；配置中的绝对路径和 MySQL 连接参数
保持调用方含义。fixture 模式完全绕过配置与数据库。
当前 Windows 开发机的默认相对配置实际解析为
`C:\Users\HP\AppData\Local\Btk-Project\anime-land\anime_land.db`。2026-07-27 已核对该
文件头为 `SQLite format 3`，当前库是可用普通 SQLite 工具打开的文件，不是加密
SQLCipher 文件。应关闭应用或复制一份后再用外部工具长时间查看，避免干扰运行中写入。

## 2. Form 生命周期

每个 Store 的 `open()` 根据连接方言选择 `BackendTag`，然后直接在长生命周期
`SqlDatabase` 上为自己拥有的关系调用
`Form<Record, BackendTag>::create_if_not_exists()`。每次调用返回的 `Form` 被移入对应
Store 的 `State`，在 Store 生命周期内复用。

流程只有一条：

```text
SqlDatabase
  -> create_if_not_exists() 返回该 Store 的 Forms
  -> Store::State 持有 Forms<BackendTag>
  -> 业务方法复用
```

没有第二次 `attach()`，也没有 `select().limit(0)` 探测。创建 Form 是异步操作，
C++ 构造器不能 `co_await`，因此异步部分必须在各 Store 的 `open()` 完成；
`State` 构造器只接收已经创建完成的 Form。

事务开始后，业务方法使用长期 Form 的表名把同一记录类型 `bind()` 到
`SqlTransaction`。`bind()` 只建立事务视图，不重复访问 Schema。

## 3. 兼容边界

ilias-sql 的 compatible attach 只检查 ORM 能否完成当前读写：

- 每个映射列必须存在；
- 每个映射列的数据库类型必须与记录字段兼容。

以下差异不能成为拒绝数据库的理由：

- 数据库中存在额外表或视图；
- 目标表存在额外列；
- 数据库的主键、唯一约束、NOT NULL、默认值、CHECK 或索引与 C++ 描述不同；
- 同一数据库被其他程序复用并包含自己的对象。

默认 attach 不知道调用方未来会写入哪些值，因此不能把不同的 CHECK 或业务值域
直接推断成固定冲突。将来若增加严格模式，必须先定义能够静态证明、并会影响所有
相关读写的冲突类别；在此之前不预设没有行为依据的严格度级别。

显式调用 `Form::attach()` 时，缺少映射列或列类型不兼容会返回 Schema 错误。
`CatalogStore::open()` 使用的是 `create_if_not_exists()`，不会为了启动再追加
attach；既有表的实际不兼容由第一次相关读写返回数据库错误。

当前没有跨版本数据转换，所以没有 `schema_migrations`、版本号检查或启动迁移。
将来出现真实的不兼容转换时，应单独设计可恢复迁移，而不是先建立一套空版本
仪式。

## 4. 关系模型

### subjects

统一条目主记录。`id` 是本地主键；`subject_type` 和 `metadata_level` 按数值保存，
不使用白名单 CHECK 限制远端未来可能增加的值。

详情刷新使用 `metadata_level` 和 `metadata_refreshed_at`。摘要更新通过
`COALESCE` 保留未提供的详情字段，通过 `GREATEST` 防止完整度降级。
每日放送点击取得官方完整 Subject 后写入 `Details`；后续详情打开在
`metadata_refreshed_at` 的 6 小时新鲜期内直接复用本地快照，过期后重新获取 Subject 与
全部 Episode。关联搜索结果只写 `Summary`，当详情同步失败时仍可作为离线回退。

### subject_external_refs

保存 `(provider_key, external_id) -> subject_id`。这两个外部身份字段组成创建
Schema 时使用的复合主键，也是条目 upsert 的冲突目标。

### tags 与 subject_tags

`tags.normalized_name` 是应用创建 Schema 时使用的唯一冲突键。规范化采用
Unicode NFKC、trim 和 case-fold。

`subject_tags` 按 `(subject_id, tag_id, provider_key)` 保存来源关系。某个来源
返回空标签集合时，只清除该来源关系，不影响其他来源。

### episodes 与 episode_external_refs

章节按本地 `subject_id` 归属条目。`episode_external_refs` 使用
`(provider_key, external_id)` 作为章节 upsert 的外部身份。

枚举、排序、时长等数值按来源原样保存；数据库描述不设置业务范围 CHECK。

### media_resources 与 source_items

`media_resources` 以 `(provider_key, stable_key)` 作为幂等身份，保存 descriptor 版本、
显示名和创建/更新/最后发现时间。`source_items` 通过 `resource_id` 外键归属资源，以
`(resource_id, stable_key)` 唯一，保存显示名、可选时长、可用状态与观察时间；资源删除
时子项级联删除。

领域 descriptor 是不透明字节。当前关系的 `descriptor` 物理列使用 Base64 文本保存，
只为规避当前 ilias-sql SQLite BLOB 结果转换限制；编码细节封装在 `LibraryStore`，不是
Schema 外调用方的协议。

### episode_media_links

章节与可播放项是多对多关系，`episode_media_links` 以
`(episode_id, source_item_id)` 唯一。两个字段分别外键引用 CatalogStore 的 `episodes`
和 LibraryStore 的 `source_items`，父记录删除时关系级联清理。`kind` 保存显式稳定的
`MediaLinkKind` 数值，`updated_at` 保存关系最后更新时间。

关联 upsert 在事务内先读取旧关系：`Manual` 可以覆盖自动匹配，已存在的 `Manual` 不会
被 `Filename` 或 `Sequence` 覆盖或降级。Store 提供按章节、按媒体项双向列举和显式解除。
由于链接表引用 `episodes`，composition root 必须按 `CatalogStore` → `LibraryStore`
顺序打开；这不是由 LibraryStore 偷偷创建 Catalog 关系。

## 5. 写入与读取

条目 upsert 在一个事务中完成：

1. 按外部身份查找本地条目；
2. 必要时创建主记录；
3. 更新标准字段；
4. upsert 外部身份；
5. 来源明确提供标签时替换该来源的标签关系；
6. 提交事务。

只有没有 `provider_key` 或 `external_id` 时拒绝写入，因为该操作无法建立稳定
upsert 身份。未知枚举、负数或服务端新增数值不由本层猜测为非法。

章节批量 upsert 同样在一个事务中完成。父条目不存在时返回 not-found；每条章节
缺少外部身份时返回 invalid-argument。其他值交给数据库类型和具体业务消费者。

查询只通过 ORM 表达式实现：

- 文本搜索在标题、中文标题、别名 JSON 和简介上使用 `contains`；
- 标签使用规范化名称精确匹配；
- 排序和分页在完整记录集合上稳定完成；
- SQLite 与 MySQL 共享同一业务流程，不包含 FTS、系统表 SQL 或 PRAGMA 分支。

媒体发现写入先校验完整批次，再在单事务内 upsert 所有资源与子项。用户显式选择文件
不代表完整目录快照，因此未出现在批次中的旧子项保持原状态。列表查询按资源显示名、
子项显示名和本地 ID 稳定排序。

媒体项移除同样在单事务内执行：先按本地 `SourceItemId` 定位子项并删除；如果所属资源
已没有其他子项，再删除空的 `media_resources` 记录。该操作只维护数据库关系，不解析
descriptor，也不删除磁盘文件。不存在的子项返回 false，由 Library 用例映射为稳定的
not-found 业务错误。

## 6. 协议数据容忍

Bangumi JSON 的结构和字段类型由 serializer 在解析边界检查。numeric enum parser
保留未知整数，响应中的计数、评分和未来新增枚举不因本地白名单而使整页失败。

请求侧仍检查当前操作必须满足的限制，例如 HTTPS base URL、分页范围、用户名和
请求体大小。这些条件不满足时请求本身无法正确发出，与响应值白名单不是一回事。

## 7. 必须覆盖的测试

- 空数据库按 Catalog → Library 顺序打开两个 Store 后创建九个应用关系；
- 重复打开 Store 幂等；
- 数据库包含额外表仍可打开，额外数据保持可用；
- 应用表包含额外列或额外物理约束仍可打开；
- ilias-sql 的显式 attach 覆盖缺少映射列和类型不兼容；
- SQLite/SQLCipher 通过显式 `EnableFKey=true` option 启用外键，不修改驱动默认值；
- 复合外部身份支持 upsert 和唯一冲突；
- SQLCipher 正确密钥可用，错误密钥在实际访问数据库时失败；
- 未知 numeric enum 能解析、保存并再次编码；
- 条目、标签和章节事务在任一步失败时整体回滚。
- 媒体资源/子项重复 upsert 保持本地 ID，descriptor 与可选时长可往返；
- 显式导入按父目录归组、同次选择去重，非法批次不产生部分写入。
- 单个子项移除保留仍有其他子项的资源，最后一个子项移除后清理空资源，且不删除原文件。
- 章节关联双向查询、手动关系优先级、显式解除和父记录删除级联均保持一致。
