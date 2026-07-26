# Library 领域设计

## 1. 职责

Library 是本地条目目录与播放系统之间的领域层。它回答三个问题：

1. 应用当前知道哪些媒体资源和可播放项；
2. 某个本地章节可以使用哪些可播放项；
3. 某个章节上次播放到哪里、是否已经完成。

Library 不解析 Bangumi DTO，不扫描文件，不创建 nekoav Pipeline，也不向 UI 暴露
provider 私有 descriptor。扫描器、网络源适配器和播放器分别消费或产生 Library 的领域对象。

## 2. 稳定身份

| 类型 | 含义 | 生命周期所有者 |
| --- | --- | --- |
| `SubjectId` | 本地标准化条目 | CatalogStore |
| `EpisodeId` | 本地标准化章节 | CatalogStore |
| `MediaResourceId` | provider 发现的一棵资源根 | Library Store |
| `SourceItemId` | 资源根下的一个可播放项 | Library Store |

这些 ID 都是强类型正整数，不能互换。`SubjectId` 和 `EpisodeId` 的定义位于
`model/library/identity.hpp`；Persistence 只是复用它们，不再拥有另一套同名身份。

provider 使用 `(providerKey, stableKey)` 幂等更新 `MediaResource`。一个资源内部，
`SourceItem::stableKey` 必须唯一。它们是导入键，不是跨层公开的对象 ID；文件移动、
provider 升级和 descriptor 迁移不得让 UI 或播放用例依赖这些字符串。

## 3. 领域对象

```text
MediaResource 1 ────── N SourceItem
                              │
                              N
                              │ EpisodeMediaLink
                              N
                              │
                           Episode 1 ────── 1 PlaybackProgress
```

### MediaResource

资源根保存 provider 身份、显示名以及不透明 descriptor。`descriptorVersion` 描述该
provider 的 descriptor 格式版本，同时约束资源和其子项的 descriptor。Library 核心只
保存字节，不解释本地路径、远端 URL 或 provider 的内部字段。

descriptor 可以保存重新打开资源所需的非敏感配置，但不得保存访问令牌、Cookie、
短期签名 URL 等凭据。凭据只能通过专门的凭据提供者按需取得。

### SourceItem

可播放项必须属于一个资源根。它保存资源内稳定键、显示名、可选时长和 provider 私有
descriptor。真正打开媒体时，播放用例以 `SourceItemId` 请求对应 provider 解析
descriptor，而不是直接接收路径或 URL。

### EpisodeMediaLink

章节与可播放项是多对多关系：同一章节可以有多个画质或版本，同一可播放项也可以覆盖
多个章节。`MediaLinkKind` 记录关系来源：

- `Manual`：用户明确关联；
- `Filename`：文件名规则匹配；
- `Sequence`：按资源顺序与章节顺序匹配。

Store 以 `(EpisodeId, SourceItemId)` 保证关系唯一。自动匹配不得覆盖或降级手动关系。

### PlaybackProgress

进度以 `EpisodeId` 唯一保存，并可记录上次使用的 `SourceItemId`。`position` 和
`duration` 均不得为负数；`duration` 未知或为零时 `playbackFraction()` 返回空值，
否则返回限制在 `[0, 1]` 的比例。

`completed` 是播放策略产生的业务结论，不能仅凭 `position / duration` 在领域结构中
隐式修改。EOS、完成阈值和用户手动标记完成都由后续播放用例统一处理。

## 4. 校验与错误

`validate()` 在对象进入 Store 或跨用例传递前检查：

- 所有本地 ID 为正数；
- provider key 和 stable key 去除空白后非空；
- descriptor 版本为正数；
- 位置和时长非负；
- 关系与进度具有有效更新时间。

错误使用 `LibraryErrorCode` 分类，日志和遥测使用稳定的英文 code name，界面文案由
Presentation 映射，不能依赖领域错误中的中文诊断字符串。

## 5. 后续持久化契约

当前实现只落地稳定身份、领域对象、校验和纯计算，不提前虚构数据库关系。Library Store
实现前须冻结下列事务语义：

1. 一个资源及本次扫描得到的可播放项在同一事务内 upsert；
2. 扫描结果缺少旧项时先标记失效，不在一次不完整扫描中立即删除；
3. 章节关联写入与播放进度写入在统一数据库执行域串行化；
4. 删除资源时保留 Subject、Episode 和 PlaybackProgress，清除其 SourceItem 与章节
   关联，并将受影响进度的 `lastSourceItemId` 置空；
5. 查询播放候选项时手动关系优先，其余关系按明确且稳定的排序返回。

后续 Store 至少需要资源 upsert、扫描提交、章节关联增删、章节候选查询、进度保存、
最近播放查询六类接口。关系、索引和 migration 版本应在实现这些接口时一并评审。

## 6. 当前实现

- `library.hpp`：Library 稳定领域接口的统一入口；
- `identity.hpp`：四种强类型本地 ID；
- `media.hpp/.cpp`：媒体资源、可播放项、章节关联和校验；
- `progress.hpp/.cpp`：播放进度、校验和进度比例；
- `test_library_foundation.cpp`：身份、错误和领域不变量测试。

尚未实现媒体扫描 provider、Library Store、文件名匹配、最近播放查询和 PlaybackSession
集成。
