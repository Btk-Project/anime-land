# Bangumi 章节读取

## 1. 用途与端点

本地媒体手动关联和远端条目详情落库使用公开 `GET /v0/episodes` 读取指定 Bangumi 条目的
章节。请求参数：

- `subject_id`：必需的正整数条目 ID；
- `limit`：`1..200`，当前每页请求 200；
- `offset`：非负分页偏移。

该端点允许匿名请求。Library 关联流程只把已验证的远端 DTO 转换为
`SubjectSnapshot`/`EpisodeSnapshot`，再由 `CatalogStore` 分配本地
`SubjectId`/`EpisodeId`；`episode_media_links` 不保存 Bangumi ID。

## 2. DTO 与校验

`BangumiEpisodePage` 保存 `total`、`limit`、`offset` 和章节数组。单章节当前消费：

- `id`、`type`、`name`、`name_cn`；
- `sort` 与可选 `ep`；
- `airdate`、`duration`、`desc`；
- 可选 `duration_seconds`。

解析后检查分页非负、返回项数不超过本页 limit、章节 ID 为正、章节类型位于官方
`0..6` 范围、集数/排序/时长有限且非负。关联流程持续分页到 `offset >= total`，空页会
提前终止，避免异常服务端响应造成死循环。

## 3. 关联边界

选择搜索结果后，应用先保存 Bangumi Subject 摘要，再保存其 Episode 快照；用户最终
点击某一章节时才写入 `EpisodeMediaLink(kind=Manual)`。搜索和章节响应都不直接进入
QML，Presentation 只暴露条目标题、本地 EpisodeId、章节号和章节标题。

## 4. 测试

`test_bangumi_foundation` 覆盖：

- 匿名 GET URL 与分页参数；
- 官方分页章节形状解析；
- Client 的匿名请求方法和响应映射；
- 非法 subject ID 与分页在发起网络请求前拒绝。

## 5. 外部依据

- [Bangumi API：GET /v0/episodes](https://bangumi.github.io/api/)
- [Bangumi OpenAPI JSON](https://bangumi.github.io/api/dist.json)

外部协议核对日期：2026-07-26。
