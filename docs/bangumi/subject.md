# Bangumi 条目详情与本地目录同步

## 1. 端点与用途

真实条目详情使用公开 `GET /v0/subjects/{subject_id}`。正整数 `subject_id` 直接进入路径；
请求没有 body。端点支持可选 Bearer Token：存在活动会话时可携带 Token，以便访问账号
可见内容；可选 Token 返回 401 时 Client 自动重试一次匿名请求，登录不是查看公开条目的
前置条件。

该端点只负责取得远端完整元数据。QML 不直接消费响应；响应必须先转换为 CatalogStore 的
`SubjectSnapshot(metadataLevel=Details)`，详情页面随后重新读取本地数据库。

## 2. DTO 与校验

官方 `GET /v0/subjects/{id}` 与 `POST /v0/search/subjects` 的单个结果都使用 `Subject`
Schema，因此协议层复用 `BangumiSearchSubject` 字段投影，并以
`BangumiSubjectDetails` 命名详情端点的语义别名。当前消费：

- `id`、`type`、`name`、`name_cn`、`summary`、`date`；
- 五种封面 URL；
- `eps`、`total_episodes`；
- `rating`、`meta_tags` 和带计数的 `tags`。

解析后检查 ID 为正、类型数值为正、至少一个标题非空、集数/册数/评分非负、评分为有限
数、日期符合 ISO `YYYY-MM-DD`，并检查标签名和计数。响应 ID 还必须与请求 ID 一致。
`infobox` 和评分直方图目前不进入 v0.1 Catalog 投影，解析器忽略未知或未映射字段。
当前 Catalog Schema 只持久化带计数的 `tags`；`rating` 与 `meta_tags` 虽经过协议解析，
尚未落入本地详情字段，页面不会用 fixture 分数补位。

## 3. 点击详情的落库流程

```text
每日放送/搜索 DTO 中的 Bangumi ID
    → SubjectDetailsViewModel::openBangumiSubject()
    → LocalMediaImportService::ensureBangumiSubject()
    → GET /v0/subjects/{id}
    → GET /v0/episodes 全部分页
    → CatalogStore Subject Details 快照 + Episode 快照
    → 重新按本地 SubjectId 读取数据库详情
    → 合并 LibraryStore EpisodeMediaLink/SourceItem
    → QML DTO
```

缓存、连载更新与失败语义：

- 已有 `Details` 且 `metadata_refreshed_at` 距当前不超过 6 小时时，直接使用
  数据库，不重复请求；过期时重新抓取完整 Subject 与全部 Episode；
- Episode 按 `(provider_key, external_id)` upsert。连载初期尚未公布的章节标题在页面
  显示为“标题待公布”；后续刷新会更新原来的本地 `EpisodeId`，已有媒体关联
  保持不变，不创建重复章节；
- 只有 `Summary` 快照时尝试升级为 `Details`；远端失败则保留并使用已有本地摘要，保证
  已关联条目离线可打开；
- 完全没有本地条目时，远端失败会显示错误和“重试读取”；
- 所有章节分页成功后才开始写数据库，避免把半页远端结果持久化；
- 写入顺序是 Subject Summary 占位、Episode 批量写入、最后把 Subject 升级为 Details；
  因此章节持久化失败不会留下虚假的“详情已完整”标记；
- Subject upsert 和 Episode 批量 upsert 各自在 CatalogStore 事务中提交；当前不会删除
  本次响应中缺少的旧章节。

详情页面的身份仍是本地 `SubjectId`/`EpisodeId`。Bangumi ID 只用于外部身份解析，远端
DTO、真实媒体路径和 provider descriptor 都不进入 QML。

## 4. 测试与核对

- `BangumiSubjectDetails.*`：请求、官方响应形状、字段校验和可选 Token 匿名回退；
- `RemoteSubjectCatalog.FetchesCompleteSubjectAndEpisodesOnceThenLoadsFromDatabase`：首次抓取
  并落库、`Details` 完整度、章节写入和新鲜期内二次打开不重复请求；
- `RemoteSubjectCatalog.RefreshesStaleDetailsAndUpdatesFutureEpisodeTitle`：过期详情会刷新，
  同一外部 Episode ID 保留本地 ID 并补全新公布标题；
- `RemoteSubjectCatalog.KeepsExistingSummaryAvailableWhenRefreshFails`：远端刷新失败时仍返回
  已有 Summary，且不会继续请求章节；
- `SubjectDetailsViewModel.*`：远端同步错误、数据库 DTO 映射、待公布标题和章节播放。

2026-07-26 还使用公开条目 `400602` 核对了真实响应：ID/类型/日期、标签数组、章节总数和
图片对象均符合上述投影。

## 5. 外部依据

- [Bangumi API：GET /v0/subjects/{subject_id}](https://bangumi.github.io/api/#/Subject/getSubjectById)
- [Bangumi 官方 v0 OpenAPI](https://github.com/bangumi/api/blob/master/open-api/v0.yaml)

外部协议核对日期：2026-07-26。
