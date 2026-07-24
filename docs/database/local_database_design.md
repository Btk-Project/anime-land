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

数据库中的其他表、视图、索引和调用方数据不属于 `CatalogStore`。

## 2. Form 生命周期

`CatalogStore::open()` 根据连接方言选择 `BackendTag`，然后直接在长生命周期
`SqlDatabase` 上依次调用六次 `Form<Record, BackendTag>::create_if_not_exists()`。
每次调用返回的 `Form` 被移入 `CatalogStore::State`，在 Store 生命周期内复用。

流程只有一条：

```text
SqlDatabase
  -> create_if_not_exists() 返回六个 Form
  -> State 持有 Forms<BackendTag>
  -> 业务方法复用
```

没有第二次 `attach()`，也没有 `select().limit(0)` 探测。创建 Form 是异步操作，
C++ 构造器不能 `co_await`，因此异步部分必须在 `CatalogStore::open()` 完成；
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

## 6. 协议数据容忍

Bangumi JSON 的结构和字段类型由 serializer 在解析边界检查。numeric enum parser
保留未知整数，响应中的计数、评分和未来新增枚举不因本地白名单而使整页失败。

请求侧仍检查当前操作必须满足的限制，例如 HTTPS base URL、分页范围、用户名和
请求体大小。这些条件不满足时请求本身无法正确发出，与响应值白名单不是一回事。

## 7. 必须覆盖的测试

- 空数据库打开 Store 后创建六个应用关系；
- 重复打开 Store 幂等；
- 数据库包含额外表仍可打开，额外数据保持可用；
- 应用表包含额外列或额外物理约束仍可打开；
- ilias-sql 的显式 attach 覆盖缺少映射列和类型不兼容；
- SQLite/SQLCipher 通过显式 `EnableFKey=true` option 启用外键，不修改驱动默认值；
- 复合外部身份支持 upsert 和唯一冲突；
- SQLCipher 正确密钥可用，错误密钥在实际访问数据库时失败；
- 未知 numeric enum 能解析、保存并再次编码；
- 条目、标签和章节事务在任一步失败时整体回滚。
