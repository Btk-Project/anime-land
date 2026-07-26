# anime-land 当前工作接力

> 更新时间：2026-07-27（Asia/Shanghai）  
> 用途：新对话开始时先读本文件，再读 `docs/arch.md` 和对应专题文档。  
> 状态：工作区包含尚未提交的连续迁移修改；不要假设所有 dirty files 都属于单一功能。

## 1. 用户明确约束

1. 用户机器内存较小，所有构建和测试最多使用 `-j4`，例如 `xmake -j4`、
   `xmake test -j4`。不要提高并行度。
2. include root 必须是 `$(projectdir)/src`；禁止随目录深度变化的 `./src`、`../../src`
   或 C++ `../` include。
3. 生产 target 已收敛为 `model`、`presentation`、`view`、`main`，不要重新拆出大量
   小 target。
4. PCH 为 `src/pch.hpp`，当前只由 `model` target 通过 `set_pcxxheader()` 构建一次；
   Model 中的 `.cpp` 必须把它作为第一个 include。不要给每个子模块各造一份 PCH。
5. QML fixture 必须长期保留，用于完全隔离地修改 View；不能为了真实接线删除 fixture。
6. 编辑 UTF-8 QML/中文文档使用 `apply_patch`。PowerShell 默认 `Get-Content` 可能显示乱码，
   读取时显式 `-Encoding utf8`，不要用默认 `Set-Content` 重写。

## 2. 当前总体架构

依赖方向已经落实为：

```text
QML / CLI View
      ↓
Presentation（QObject ViewModel / Presenter）
      ↓
Model（Bangumi / Library / Persistence）

main.cpp 是当前 Composition Root；后续才抽到 src/runtime/。
```

真实 QML 当前已有三条主要纵向切片：

- `BangumiCalendarViewModel`：真实 Bangumi 每日放送；
- `LibraryViewModel`：真实本地数据库、文件选择导入、Bangumi 搜索/章节读取、手动关联/
  解除、系统播放器入口，以及已关联媒体的 Subject → Episode → SourceItem
  层级投影；
- `SubjectDetailsViewModel`：从 CatalogStore 读取本地条目/章节，并与 LibraryStore 的
  `EpisodeMediaLink`、`SourceItem` 合并成详情 DTO；支持从章节调用系统播放器。

首页其余数据、Bangumi 主搜索页/收藏和内置播放器仍主要是 fixture。条目详情在真实模式
不再读取 fixture：已落入 CatalogStore 的条目显示数据库元数据、章节及关联媒体；尚未
落库的每日放送条目会先读取 Bangumi 完整 Subject/全部章节并持久化，再从数据库显示。
全局 fixture 开关：

```powershell
$env:ANIME_LAND_UI_FIXTURE='1'; xmake run main
```

`ANIME_LAND_UI_SMOKE_TEST=1` 会自动进入 fixture 并遍历页面。fixture 模式不加载配置、
不访问网络或数据库，也不创建 Bangumi/Library Model。

## 3. 本地媒体导入、分组、关联、播放与移除已经完成的链路

```text
QML FileDialog.selectedFiles（list<url>）
    ↓
LibraryViewModel::importFiles(QList<QUrl>)
    ↓
LocalMediaImportService::importFiles()
    ↓
MediaDiscovery（按 canonical 父目录归组）
    ↓
LibraryStore::upsertDiscoveredMedia()（单事务）
    ↓
media_resources + source_items
    ↓
LibraryViewModel 重新加载并映射 MediaItemCard DTO
```

关联与播放链路为：

```text
MediaItemCard 右键“关联章节”
    → Bangumi 公开条目搜索
    → GET /v0/episodes 分页读取
    → CatalogStore upsert Subject/Episode 快照
    → LibraryStore upsert Manual EpisodeMediaLink

MediaItemCard 双击/右键“播放”
    → LibraryViewModel::playMedia(SourceItemId)
    → Model 解析并安全校验 local-file descriptor
    → 系统默认播放器

MediaItemCard 右键“查看条目详情”
    → SubjectDetailsViewModel::openSubject(SubjectId)
    → CatalogStore Subject/Episode + LibraryStore EpisodeMediaLink/SourceItem
    → 数据库条目详情和章节列表
    → 章节“播放”按 EpisodeId 解析首个关联媒体并交给系统播放器

每日放送卡片点击
    → Details 快照 6 小时内直接读库，过期后请求远端
    → GET /v0/subjects/{id} + GET /v0/episodes 全部分页
    → CatalogStore Subject(Details) + Episode 快照
    → SubjectDetailsViewModel 按本地 SubjectId 重新读取数据库
    → 无关联章节仍显示，已关联章节可播放
```

真实 Library 页面同时保留唯一媒体扁平列表，并产生两个互斥的显示投影：

- 有 `EpisodeMediaLink` 的文件进入 `subjectGroups`，按 `SubjectId`，再按 `EpisodeId`
  展开；
- 无任何关联的文件进入 `unassociatedGroups`，仍按父目录显示为“待整理”。

同一 `SourceItem` 如果确实关联多个章节，会在对应章节下分别出现；章节上下文被带入
卡片菜单，所以可精确解除该章节关联。顶部媒体数仍按唯一 `SourceItem` 计数。
移除链路为：

```text
MediaItemCard 右键“从媒体库移除”
    ↓
LibraryPage 二次确认（明确不删除磁盘文件）
    ↓
LibraryViewModel::removeMedia(SourceItemId)
    ↓
LocalMediaImportService::removeMedia()
    ↓
LibraryStore::removeSourceItem()（单事务）
    ↓
删除 source_item；最后一个子项删除后清理空 media_resource
    ↓
LibraryViewModel 重新加载扁平列表、Subject/Episode 层级与待整理 DTO
```

### 3.1 用户刚报告并已修复的问题

用户选择了带方括号和空格的 Windows MKV 路径，界面提示“媒体导入只接受本地文件 URL”。
根因是 ViewModel 曾把 QML 的 `list<url>` 接成 `QVariantList`，随后逐项 `toUrl()`；该边界
在 Windows 上丢失了本地 URL 类型信息。

当前接口已改为：

```cpp
Q_INVOKABLE void importFiles(const QList<QUrl> &files);
```

`tests/unit/presentation/test_library_view_model.cpp` 中的
`AcceptsQmlListOfLocalUrlsWithoutLosingScheme` 使用真实 `QQmlEngine`，把
`property list<url>` 传给 ViewModel，覆盖了 `file:///D:/Videos/%5BLoliHouse%5D...mkv`
形式。该测试已通过，`xmake -j4 main` 也已通过。

用户随后已成功导入同一季度目录内的 12 集，说明原 Windows MKV URL 已能通过真实入口。
这次复测也暴露出原页面把 12 个文件平铺为 12 张卡片、容易误解为 12 个动画条目的问题；
现已按父目录显示为一个待整理资源组，组内保留 12 个文件 item。

用户在数据库详情页又报告了禁用的“开始播放”显示成黑色，以及连载中章节
显示“未命名章节”。前者是公共 `AppButton` 在 primary + disabled 组合下仍使用
深色 `accentText`，现已改为禁用前景色与普通描边；无已关联媒体时按钮文案改为
“暂无可播放媒体”。空章节标题改为“标题待公布”，明确表达这是远端尚未公布，
不是本地数据丢失。

随后用户要求把媒体库主层级从目录组提升为真正的 Subject → Episode → 媒体文件。
现已完成：实际开发库的 13 个文件运行时显示为 2 个条目、各自的 EP1，以及 1 个
待整理目录中的 11 个未关联文件。

### 3.2 Model 导入语义

- 只接受用户明确选择的本地、存在、普通、可读文件；当前不是递归目录扫描。
- 整批文件先验证，任一项非法则不写入数据库。
- 使用 canonical path；Windows 稳定键会 case-fold，并统一为 `/` 分隔。
- 同一批选择内重复文件去重并返回重复数。
- 文件按 canonical 父目录归成一个 `MediaResource`；目录内相对路径形成 `SourceItem`。
- `MediaResource` 只是 provider 资源根，不等同于一季动画；一个文件仍是一个可播放
  `SourceItem`，当前可通过 `EpisodeMediaLink` 手动关联章节。
- provider key 当前为 `local-file`。
- 重复导入通过复合唯一键 upsert，保留原有本地 ID。
- 显式选择不代表完整目录快照，因此不会把旧文件标记不可用或删除。
- 用户可右键单个 item 从媒体库移除；只删除库记录，不删除磁盘原文件。最后一个子项
  移除时在同一事务中清理空资源，重新导入仍可恢复库记录。
- 日志只记录资源/文件数量，不记录完整媒体路径。

### 3.3 数据库关系

`LibraryStore` 当前拥有：

- `media_resources`：唯一键 `(provider_key, stable_key)`；
- `source_items`：唯一键 `(resource_id, stable_key)`，资源外键级联删除。
- `episode_media_links`：唯一键 `(episode_id, source_item_id)`，章节/媒体项外键均级联删除。

Store 提供资源批量 upsert、单资源查询、资源列表、子项列表、扁平媒体列表和单子项
事务移除/空资源清理；章节关联已提供事务 upsert、按章节/媒体项双向查询和显式解除。
手动关系可以覆盖自动关系，已存在的手动关系不会被 `Filename`/`Sequence` 降级。

手动关联已经接到真实入口：右键媒体项选择“关联章节”，ViewModel 调用公开 Bangumi
搜索；选中动画后分页读取 `/v0/episodes`，把远端 Subject/Episode 映射并 upsert 到
CatalogStore，最终只使用本地 `EpisodeId` 和 `SourceItemId` 写入 `Manual` 关系。卡片会
显示关联条目/章节，可显式解除；QML 不接收 Bangumi DTO。

`LocalMediaImportService::ensureBangumiSubject()` 负责把每日放送的 Bangumi ID 解析/同步为
本地 `SubjectId`：已有且刷新时间不超过 6 小时的 `Details` 直接复用数据库；过期的
`Details` 会重新读取 Subject 和全部 Episode；只有 `Summary` 时尝试升级，远端失败则
使用已有摘要；完全未落库时读取 `/v0/subjects/{id}` 和全部章节，远端读取全部成功后才
开始写 CatalogStore。`getSubjectLibraryDetails()` 再从 CatalogStore 读取 Subject/Episode，
并与 LibraryStore 的关系/媒体项合并成只读快照。Presentation 不直接访问 Store，真实模式
也绝不回退到 fixture 假数据。Episode 按 Bangumi 外部 ID upsert，因此过期刷新会在
原本地 `EpisodeId` 上补全后续公布的标题/时长，不会生成重复章节，也不会丢失已有媒体关联。

本地播放也已形成第一条可用闭环，但当前是系统播放器过渡方案，不是内置 nekoav：双击
卡片或右键“播放”只向 Model 传 `SourceItemId`。Model 校验 local-file descriptor 版本、
JSON、相对路径、canonical 目录边界、文件存在性和可读性后，才调用系统默认播放器。
真实路径不进入 QML；暂停、Seek、内置视频输出和进度仍待 PlaybackSession。

关联表外键引用 CatalogStore 的 `episodes`，因此主程序和测试必须先打开
`CatalogStore`，再打开 `LibraryStore`。`main.cpp` 已按此顺序装配并在数据库关闭前反序
销毁两个 Store。

领域 descriptor 仍是不透明 `QByteArray`。当前 ilias-sql SQLite BLOB 读回存在
`Unsupported convert from SQL type` 缺口，所以物理 `descriptor` 列暂存 Base64 文本，
只在 `LibraryStore` 边界编解码。不要让 UI、Model 调用方或后续 provider 依赖 Base64。

图形启动时，SQLite/SQLCipher 的相对 `database_path` 会解析到 Qt
`AppLocalDataLocation`，不会在仓库当前目录创建数据库。绝对路径和 MySQL 配置保持原意。
当前 Windows 开发机上的实际文件是
`C:\Users\HP\AppData\Local\Btk-Project\anime-land\anime_land.db`；已核对文件头为
`SQLite format 3`，当前是普通 SQLite 文件。

## 4. 关键文件索引

### Model / Persistence

- `src/model/library/error.hpp`：Library 错误码；
- `src/model/library/media.hpp/.cpp`：资源、子项、发现快照和校验；
- `src/model/library/local_media_import.hpp/.cpp`：显式导入/移除、关联编排、列表投影和安全
  外部播放用例；
- `src/model/bangumi/episode.hpp/.cpp`：公开 Bangumi 分页章节请求、DTO 与校验；
- `src/model/bangumi/subject.hpp/.cpp`：公开 Bangumi 条目详情 GET、Subject DTO 语义别名与
  严格响应校验；
- `src/model/persistence/library_schema.hpp`：三张 Library 表与跨 Store 外键的 ORM Schema；
- `src/model/persistence/library_store.hpp/.cpp`：媒体与章节关联的事务 upsert、查询、解除、
  子项移除和空资源清理；
- `src/model/persistence/database.*`：数据库连接，不拥有业务表。

### Presentation / View / Runtime

- `src/presentation/library/library_view_model.hpp/.cpp`：QML 导入、关联、解除、播放、移除、
  加载、错误、Subject/Episode 层级和待整理 DTO；
- `src/presentation/library/subject_details_view_model.hpp/.cpp`：数据库条目/章节/关联媒体
  详情 DTO、Bangumi 外部身份反查和按本地 `EpisodeId` 播放；
- `src/view/qml/LibraryPage.qml`：真实模式文件选择、Subject/Episode/媒体文件层级、
  未关联父目录待整理区、Bangumi 关联 Dialog 与移除确认；
- `src/view/qml/MediaItemCard.qml`：本地媒体卡片、双击播放与右键操作菜单；
- `src/view/qml/SubjectDetailPage.qml`：fixture 模式保留静态详情；真实模式只消费
  `SubjectDetailsViewModel`；
- `src/view/qml/qml_application.*`：注入 Calendar、Library 与 SubjectDetails ViewModel；
- `src/main.cpp`：当前数据库、Store、Service、ViewModel 装配和关闭顺序。

### Tests

- `tests/unit/model/persistence/test_library_store.cpp`：关系、外键、事务、幂等、远端详情
  首次落库/二次本地复用、Base64
  字节往返、文件归组、子项移除、空资源清理、磁盘文件保留、播放路径防穿越和章节关联
  优先级/级联；
- `tests/unit/model/bangumi/test_bangumi_foundation.cpp`：公开条目详情/章节请求、官方响应
  形状、字段校验、可选 Token 回退和 Client GET；
- `tests/unit/model/library/test_library_foundation.cpp`：领域与发现快照校验；
- `tests/unit/presentation/test_library_view_model.cpp`：加载、导入、移除、Subject/Episode 排序与
  待整理分流、关联 DTO、错误和真实 QML URL 边界；
- `tests/unit/presentation/test_subject_details_view_model.cpp`：数据库详情映射、Bangumi
  目录同步、同步错误和按章节播放；
- `tests/xmake.lua`：Presentation 测试目前链接 `QtQml`，供 URL 边界测试使用。

## 5. 最近验证结果

本轮手动关联、数据库条目详情和系统播放器过渡入口后的验证：

```text
xmake run test_library_foundation -> 8/8 passed
xmake run test_library_store      -> 13/13 passed
xmake run test_library_view_model -> 7/7 passed
xmake run test_subject_details_view_model -> 5/5 passed
BangumiEpisodes targeted tests    -> 4/4 passed
BangumiSubjectDetails targeted tests -> 4/4 passed
xmake -j4 main                    -> passed
offscreen fixture QML smoke       -> passed, no QML warnings
```

本轮全套回归：

```text
xmake test -j4 -> 10 组中 8 组通过
```

两个失败仍是迁移前已存在、与本轮远端详情落库无关的问题，单独复跑确认断言未变化：

1. `test_bangumi_foundation`：Windows owner-only 文件权限断言；
2. `test_local_database`：SQLCipher 错误密码测试期望失败，但当前驱动仍能打开。

Library 相关 Store/ViewModel 测试均通过；其中关联集成测试覆盖“Bangumi DTO → Catalog
快照 → Manual Link → 卡片投影 → SourceItemId 播放 → 解除”。QML 离屏启动没有绑定告警。
删除路径测试明确检查原媒体文件仍存在，播放解析测试同时验证合法文件解析和 `../` 目录
穿越拒绝。

用户随后截图报告右键菜单出现白底浅字。原因是 Qt Quick Controls 的独立 `Menu` Popup
没有可靠继承主窗口深色背景；现已为右键菜单及其后续确认 `Dialog` 显式设置 Theme 背景、
描边和文字色，避免平台默认浅色样式泄漏。
详情页禁用 primary 播放按钮的黑字也已在公共 `AppButton` 修复，并用明确的
“暂无可播放媒体”替代不可操作的“开始播放”。
真实库截图验证了 Subject → Episode → 文件层级和待整理区；默认首页在截图后已恢复。

## 6. 已知边界与下一步

当前完成的是“导入、未关联目录整理、手动关联/解除、Subject → Episode → 媒体
层级、数据库条目详情/章节、调用系统播放器和安全移除”。尚未实现：

1. 文件名解析/自动匹配；
2. 递归目录扫描与完整快照的失效语义；
3. 内置 `PlaybackSession`、nekoav 输出、控制和进度保存；
4. 最近播放和继续观看；
5. 把 `main.cpp` 装配抽成 `src/runtime/AppRuntime`。

详情边界需要特别注意：页面始终从 CatalogStore/LibraryStore 读取。每日放送点击只把
Bangumi ID 交给 Model；Model 负责先落库再返回本地 `SubjectId`。`Details` 在 6 小时新鲜期
内不重复请求，过期后刷新；已有本地条目在网络失败时仍可离线打开。播放进度仍未落库，
因此详情章节进度目前固定为零。

建议下一步顺序：

1. 用真实 Bangumi 搜索替换 Bangumi 页搜索 fixture，并复用相同的点击落库链路；
2. 实现文件名 matcher，自动结果必须可确认且不能覆盖手动关联；
3. 建立 PlaybackSession 并用 nekoav 替换系统播放器过渡入口；
4. 增加播放进度和最近播放。

## 7. 操作注意事项

- 只使用 `-j4`。
- 不要删除或覆盖用户已有 dirty changes。
- 用户已批准把 `D:/CodeProject/anime-land` 精确加入当前账户的全局 Git
  `safe.directory`，现在 `git status`/`git diff` 可用；不要改成 `*` 通配，也不要删除或
  覆盖用户已有 dirty changes。
- 不要把 Bangumi DTO、SQL Record、provider descriptor 或真实文件路径暴露成 QML
  核心身份；QML 使用本地强类型 ID 映射后的 UI DTO。
- 不要把 Bangumi 每日放送写入 LibraryStore；它仍是短期远端浏览数据。
- Store 写入保持事务化；递归扫描引入前先冻结“不在快照中的旧项”语义。

## 8. 新对话建议开场

可以直接对新助手说：

> 先完整阅读 `docs/HANDOFF.md`、`docs/arch.md` 和
> `docs/library/library_design.md`，不要重新猜架构。Windows MKV URL、未关联父目录待整理区和右键
> 安全移除、Bangumi 章节读取、手动 EpisodeMediaLink 关联/解除和系统播放器过渡入口已经
> 落地；已关联媒体库已按 Subject → Episode → 媒体文件显示。每日放送点击会把完整
> Subject/Episode 落入 Catalog，再由真实详情读取 Catalog/Library 数据库，不会显示 fixture。
> 继续做真实搜索页、文件名 matcher，或开始
> PlaybackSession/nekoav。构建一律 `-j4`。
