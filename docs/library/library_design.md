# Library 领域设计

## 1. 职责

Library 是本地条目目录与播放系统之间的领域层。它回答三个问题：

1. 应用当前知道哪些媒体资源和可播放项；
2. 某个本地章节可以使用哪些可播放项；
3. 某个章节上次播放到哪里、是否已经完成。

Library 领域对象不包含或持久化 Bangumi DTO，不自行递归扫描目录，不创建 nekoav
Pipeline，也不向 UI 暴露 provider 私有 descriptor。当前 Library 应用服务除处理文件
选择器交来的文件外，还在明确的集成边界把已验证 Bangumi 搜索/章节 DTO 映射成 Catalog
快照；DTO 本身不进入 Store 或 QML。后续扫描器、网络源适配器和播放器分别消费或产生
Library 的领域对象。

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
物理关系 `episode_media_links` 已落地：`episode_id` 与 `source_item_id` 分别外键引用
Catalog 和 Library 拥有的主记录，任一父记录删除时关系级联清理。`kind` 使用显式稳定
数值保存；Store 的 upsert 会让手动关系覆盖自动关系，并拒绝后续自动结果降级手动关系。

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

## 5. 当前导入、关联、播放与持久化契约

`LocalMediaImportService` 实现用户明确选择文件的第一条用例链：

1. 只接受有效、存在、可读的本地普通文件；整批先校验，任一项非法时不写数据库；
2. 使用 canonical path 规范化文件身份，Windows 上对稳定键 case-fold；
3. 按 canonical 父目录归组为 `MediaResource`，目录内相对路径作为 `SourceItem` 稳定键；
4. 同一次选择中的重复文件去重并向 Presentation 返回重复数；
5. 一个导入批次在单事务中 upsert，重复导入复用原有本地 ID；
6. 显式导入不是完整目录快照，因此不会把本次未选择的旧文件标记失效或删除。

用户移除操作以 `SourceItemId` 为边界，只改变媒体库成员关系，不删除 provider 指向的
磁盘文件或远端对象。`LibraryStore::removeSourceItem()` 在单事务中删除子项；如果它是
所属 `MediaResource` 的最后一个子项，同时清理空资源。重复移除返回 not-found，之后
再次显式导入同一文件会按正常发现流程重新创建库记录。

`LibraryStore` 当前拥有 `media_resources`、`source_items` 和 `episode_media_links`。资源以
`(provider_key, stable_key)` 唯一，子项以 `(resource_id, stable_key)` 唯一，删除资源时
由外键级联删除子项。descriptor 在领域层始终是不透明 `QByteArray`；由于当前
ilias-sql SQLite BLOB 读回转换存在缺口，落盘列暂用 Base64 文本封装，Store 边界负责
无损编解码，调用方不可依赖该物理编码。

章节关联以 `(episode_id, source_item_id)` 唯一。Store 已提供关联 upsert、按章节/媒体项
双向查询和显式解除；删除章节或媒体项时由外键级联清理关系。因为关联表跨越 Catalog
与 Library 所有权，composition root 和测试装配必须先打开 `CatalogStore`，再打开
`LibraryStore`。

手动关联用例已接入真实 Presentation/QML：

1. 以动画名称调用 Bangumi 公开搜索；
2. 选中远端条目后分页读取章节，并映射为 Catalog 的 Subject/Episode 快照；
3. 用户选中章节后，使用本地 `EpisodeId` 与 `SourceItemId` 写入 `Manual` 关系；
4. 媒体列表按关系反查 Catalog 标题，只向 QML 暴露本地 ID 与显示 DTO；
5. 已有关联可以从卡片菜单或关联管理窗口显式解除。

当前本地播放是内置 PlaybackSession 前的安全过渡入口。QML 只提交 `SourceItemId`；
`LocalMediaImportService` 读取资源/子项 descriptor，要求 provider 为 `local-file`、版本为
1，并验证 JSON 字段、相对路径、canonical 路径仍位于导入目录内、文件存在且可读。验证
完成后才将本地 URL 交给系统默认播放器。此入口不暴露路径给 QML，也不提供暂停、Seek、
内置渲染或进度保存；这些能力仍由后续 PlaybackSession/nekoav 负责。

Store 还提供资源批量 upsert、资源查询、资源/子项列表、媒体扁平列表和单个子项移除。
递归扫描仍须单独定义“完整快照”语义；文件名自动匹配、播放进度、最近播放、内置播放
和显式整资源删除尚未落地。

真实 QML 将扁平媒体投影为两条互斥分支：有关联的 `SourceItem` 按
`SubjectId → EpisodeId → SourceItemId` 显示，无关联文件才按父目录进入“待整理”区。
同一文件可因多对多关系出现在多个章节下，但条目和全局媒体数均按唯一文件计算。
章节下的卡片带有 `contextAssociation`，所以多重关联也能精确解除当前章节；播放、追加关联、
条目详情和安全移除入口仍保留在单文件卡片。

条目详情读取同样由 `LocalMediaImportService` 编排，而不是让 Presentation 直接访问两个
Store：`getSubjectLibraryDetails(SubjectId)` 读取 Catalog Subject/Episode，再按 Episode
查询 Library 关系和媒体项，返回 `SubjectLibraryDetails` 只读快照；
`ensureBangumiSubject()` 先按 Bangumi 外部身份查找本地 `SubjectId`，必要时获取完整
Subject 与全部 Episode 并写入 Catalog；`playEpisode()` 按本地 `EpisodeId` 选择首个关联
媒体并复用上述安全外部播放入口。已有 Details 快照在 6 小时内不重复请求，过期后更新
Subject/Episode；Episode 按外部 ID upsert，因此连载中后续公布的标题会补全原记录而不影响已有
媒体关联。Summary 升级远端失败时仍使用本地摘要。真实详情因此始终读取数据库，
而不会把远端 DTO 临时伪装成页面数据。

## 6. 当前实现

- `library.hpp`：Library 稳定领域接口的统一入口；
- `identity.hpp`：四种强类型本地 ID；
- `media.hpp/.cpp`：媒体资源、可播放项、章节关联和校验；
- `progress.hpp/.cpp`：播放进度、校验和进度比例；
- `local_media_import.hpp/.cpp`：显式导入/移除、Bangumi 章节快照映射、手动关联、跨 Store
  条目详情快照、远端 Subject/Episode 目录同步和安全外部播放用例；
- `persistence/library_schema.hpp`：三张 Library 关系及跨 Store 外键；
- `persistence/library_store.*`：媒体资源、可播放项、章节关联和空资源清理的事务持久化；
- `presentation/library/library_view_model.*`：QML 唯一媒体、Subject/Episode 层级、未关联目录/
  关联 DTO，以及导入、关联、解除、播放、移除状态和错误映射；
- `presentation/library/subject_details_view_model.*`：数据库 Subject/Episode/关联媒体 DTO、
  外部身份反查和章节播放；
- `test_library_foundation.cpp`、`test_library_store.cpp` 与
  `test_library_view_model.cpp`、`test_subject_details_view_model.cpp`：领域、事务、去重、
  完整手动关联/播放编排和 Presentation 测试。

尚未实现递归媒体扫描 provider、文件名匹配、最近播放查询、进度保存和内置
PlaybackSession/nekoav 集成。
