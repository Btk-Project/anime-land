# anime-land 架构

> 文档状态：Living Design  
> 架构范围：应用模块、依赖方向、前端边界和从当前代码到目标目录的迁移关系  
> 前端决策：主图形界面使用 Qt Quick/QML；CLI 作为独立 View 适配器和独立二进制保留
> 当前工作接力：[HANDOFF.md](HANDOFF.md)

本文是项目架构和目录归属的主文档。产品范围、阶段和验收标准见
[plan.md](plan.md)；Bangumi、数据库等协议与实现细节由各专题文档维护。

## 1. 总体结构

模块目录是一棵所有权树，编译和调用关系按下述单向依赖执行：

```text
View（QML / CLI）
        │ 用户操作、Property、Signal、DTO
        ▼
Presentation（Presenter / ViewModel）
        │ 用例调用、UI 状态映射
        ▼
Model（Bangumi / Playback / Library）
        │ 类型化接口、Ilias Task / Channel
        ├──────────────► Persistence
        └──────────────► Media

Runtime 只负责创建、连接和按顺序关闭上述对象。
Common 只提供无业务方向的基础能力。
```

核心依赖规则：

1. View 只访问 Presentation 暴露的 `QObject`、Property、Signal、命令和列表模型。
2. Presentation 可以调用 Model 门面，不直接访问网络、数据库、TokenStore 或 nekoav Pipeline。
3. Model 不读取 argv、不打印 stdout，也不依赖 View、QML 页面或 Presenter。
4. Persistence 不依赖 Bangumi DTO；远端数据必须先映射为应用拥有的快照或领域对象。
5. Bangumi 不依赖 Playback 或 nekoav。
6. 只有 `PlaybackSession` 可以管理当前播放 Pipeline。
7. QRhi Renderer 只消费标准化 `VideoFrame`，不感知 Bangumi、媒体库页面或账号状态。
8. Runtime 是 composition root；除测试装配外，其他模块不自行创建全局服务。
9. Common 不得成为业务代码收容目录，也不得反向依赖上层模块。

构建和 include 约定：

- 生产代码按架构层聚合为 `model`、`presentation`、`view`、`cli` 库 target；`main`
  生成 `anime-land` GUI，`anime_land_cli` 生成 `anime-land-cli`，两个二进制各自拥有
  Runtime/Composition Root；目录和命名空间继续表达层内子模块所有权；
- 所有 xmake target 使用 `$(projectdir)/src` 作为源码 include root，不使用随目录深度
  变化的 `./src`、`../../src` 等路径；
- 测试额外使用 `$(projectdir)/tests/unit` 作为测试公共 include root；
- C++ 文件从上述稳定根开始写 include，例如 `model/bangumi/bangumi.hpp`，不使用
  `../` 穿越模块目录。

## 2. 目标目录

```text
src/
├─ process.*                 # 两个二进制共享的进程级初始化
├─ runtime/
│  ├─ app_runtime.*
│  └─ service_registry.*
├─ view/
│  ├─ gui/                    # GUI composition root 与 main
│  ├─ qml/                    # 正式 Qt Quick 页面和组件
│  └─ cli/                    # CLI View、独立 application 与 main
├─ presentation/
│  ├─ bangumi/                # 登录、每日放送、搜索、收藏 Presenter/ViewModel
│  ├─ playback/               # 播放 UI 状态和用户命令映射
│  └─ library/                # 媒体库、条目和章节 UI 状态
├─ model/
│  ├─ bangumi/                # Module 门面、OAuth、Client、能力和 TokenStore 抽象
│  ├─ playback/               # PlaybackSession、Command、Snapshot、Error
│  ├─ library/                # 媒体资源、章节关联、最近播放和进度
│  └─ persistence/            # LocalDatabase、CatalogStore 和后续 Store
├─ media/
│  ├─ nekoav/                 # 应用与 nekoav 的适配边界
│  ├─ mediaio/                # LocalFileSource、HTTP Range、Cache、AVIO Bridge
│  ├─ render/                 # VideoOutputItem、QRhi Renderer、Frame Proxy
│  ├─ subtitle/
│  └─ danmaku/
├─ platform/                  # 系统凭据实现和平台差异适配
└─ common/                    # 日志、配置、序列化和小型通用工具

tests/
├─ unit/
├─ integration/
├─ media/
├─ view/qml/                  # 测试页面和 QML fixture，不进入正式资源包
└─ fixtures/
```

目录表达主要所有权，不要求把一个功能的所有实现放进同一个库。比如
`model/bangumi` 对外只暴露 `BangumiModule` 等稳定门面，内部的 Auth、Client 和
TokenStore 抽象不应被 View 直接引用；系统凭据库等平台实现放入 `platform`。

## 3. Runtime

GUI 与 CLI 使用独立 Runtime。`src/view/gui/application.cpp` 只装配 GUI；
`src/view/cli/application.cpp` 只装配 CLI。两者共享配置格式、应用身份、TokenStore、
Model 和 Presentation，不允许一个入口通过隐藏子命令启动另一个入口。

GUI `AppRuntime` 负责完整应用生命周期：

- 创建并安装 `ilias::QIoContext`；
- 加载配置并初始化日志；
- 打开数据库和各个 Store；
- 创建 Bangumi、Library、Playback 与 Presentation 对象；
- 向 QML 注册或注入 Presenter/ViewModel；
- 退出时先停止 PlaybackSession，再关闭网络、Store 和数据库。

析构函数不等待异步清理。关闭流程必须通过显式异步 `shutdown()` 完成。
CLI Runtime 是短生命周期 composition root，只创建当前命令需要的 Bangumi Module、
Presenter 和终端 View，不打开本地媒体数据库，也不链接 QML/Qt Quick。

## 4. View 与 Presentation

正式图形界面使用 Qt Quick/QML。QML 负责布局、视觉状态、动画、输入和导航，不执行
JSON 解析、SQL、OAuth、媒体控制或业务重试。

Presentation 负责：

- 把 QML/CLI 输入转换为 Model 用例调用；
- 把异步结果转换为稳定的 UI Property、Signal、列表模型和错误状态；
- 管理页面级取消、加载状态和选择状态；
- 保持 QML 与 CLI 中可共享的交互语义。

当前 C++ `BangumiView`、Bangumi Presenter 与 CLI View 是已实现的 MVP 适配器。迁移到 QML 时允许
Presenter 演化为 QObject ViewModel，但不得把 Auth、Client 或 TokenStore 暴露给
QML。CLI 可以保留自己的具体 View，不要求与 QML 共享渲染接口；两者共享 Model
用例和错误语义。当前 fixture 主界面和页面设计见
[view/qml_ui_design.md](view/qml_ui_design.md)。

首个真实 QML 切片是 `BangumiCalendarViewModel`：composition root 创建 Module 与
ViewModel，View 只获得后者。`ANIME_LAND_UI_FIXTURE=1` 绕过这条装配链并使用静态数据，
用于单独修改 View；fixture 开关不得散落到 Model 或协议代码中。

媒体库是第二个真实切片：`LibraryViewModel` 只调用
`LocalMediaImportService`，负责加载状态、导入/移除反馈、扁平唯一媒体、已关联
Subject → Episode → SourceItem 层级和未关联父目录组的 UI DTO 映射；QML 文件选择器只把
用户明确选择的本地 URL 交给 ViewModel，右键移除只提交
本地 `SourceItemId`，两者都不接触 Store、SQL 或 provider descriptor。

条目详情是第三个真实切片：`SubjectDetailsViewModel` 通过应用服务取得 Catalog 的
Subject/Episode 与 Library 的 EpisodeMediaLink/SourceItem 合并快照，再映射为 QML DTO。
本地导航只传 `SubjectId` 或 Bangumi 外部身份，章节播放只传 `EpisodeId`；Presentation
和 QML 都不直接跨 Store 查询。fixture 模式仍可独立渲染静态详情；真实模式遇到尚未写入
CatalogStore 的远端条目时，由 Model 先读取 Bangumi 完整 Subject/章节并持久化，再按本地
`SubjectId` 重新读取详情，不得把远端 DTO 或 fixture 直接交给页面。已有 Details 快照在
6 小时新鲜期内直接复用，过期时刷新 Subject/Episode；已有本地数据在远端失败时仍可
离线回退。

## 5. Model 子模块

### 5.1 Bangumi

负责 OAuth、API Client、权限能力、TokenStore、会话状态以及对 Presentation 暴露的
`BangumiModule` 门面。具体协议见 [bangumi/README.md](bangumi/README.md)。

公开搜索和每日放送可以匿名执行；账号收藏等受保护用例必须通过 Module 检查会话和功能声明。
Token 不进入普通设置、业务数据库、View 或日志。

每日放送是短期远端浏览数据：完整页属于 Bangumi，首页只消费其今日摘要。它不进入
`LibraryStore`；缓存、Presentation 状态和时间语义见
[bangumi/calendar.md](bangumi/calendar.md)。

### 5.2 Playback

`PlaybackSession` 属于 Model，而不是 Presentation。它是媒体控制面的唯一入口：

- 串行消费有界 `PlaybackCommand`；
- 管理 Pipeline 的 Open、Play、Pause、Seek、Stop 和 Teardown；
- 生成与 UI 无关的 `PlaybackSnapshot` 和结构化错误；
- 使用 generation/session ID 丢弃旧会话的迟到消息；
- 协调进度保存和 Renderer 生命周期。

`presentation/playback` 只持有 Session 门面，负责将 UI 操作映射为命令，并将 Snapshot
映射为界面状态。

在 nekoav/PlaybackSession 落地前，Library 目前提供一个过渡的外部播放入口：View 只
提交 `SourceItemId`，Model 解析并校验 `local-file` descriptor 后调用系统默认播放器。
它不是内置 PlaybackSession，不提供暂停、Seek、视频渲染或进度；后续替换其内部实现时
不得把路径解析迁回 QML。

### 5.3 Library

Library 是 Bangumi 目录与播放系统的连接层，拥有应用自己的稳定身份：

```text
Bangumi DTO
    → Subject / Episode 快照
    → 本地 SubjectId / EpisodeId
    → MediaResource / SourceItem
    → 章节关联
    → PlaybackSession
    → PlaybackProgress / 最近播放
```

UI 和播放层不得把 Bangumi ID、文件路径或 provider 私有 descriptor 当作核心对象
身份。远端 DTO 到持久化快照的映射由 Library 或专门的导入服务完成，Bangumi Client
不直接写数据库。当前身份、媒体资源、可播放项、章节关联和进度的数据语义见
[library/library_design.md](library/library_design.md)。

### 5.4 Persistence

`LocalDatabase` 只负责连接；具体 Store 负责自己的关系、长期 Form、事务和类型化
查询。当前 `CatalogStore` 管理条目、外部身份、标签和章节六个关系，`LibraryStore`
管理 `media_resources`、`source_items` 与 `episode_media_links`，并在单个子项移除后
事务化清理空资源；该操作不删除 provider 指向的磁盘文件。章节关联已支持事务 upsert、
双向查询、解除、级联清理和真实手动关联 UI；播放进度仍由后续 Store 增量加入。
所有关系都不得混入 Bangumi 协议 DTO。

`episode_media_links` 跨越两个 Store 的表所有权，因此 Runtime 必须先打开
`CatalogStore`，再打开 `LibraryStore`；退出时按相反所有权顺序销毁，然后关闭数据库。

所有业务写入必须在统一数据库执行域串行化。不得删除、覆盖或清洗调用方已有对象；
当前没有真实跨版本转换时不虚构 migration。详细契约见
[database/local_database_design.md](database/local_database_design.md)。

## 6. Media

Media 模块封装 nekoav、FFmpeg、自定义 I/O 和渲染细节：

```text
AsyncMediaSource / LocalFileSource
              ↓
      nekoav + FFmpeg Pipeline
              ↓
       标准化 VideoFrame
              ↓
 latest-frame mailbox（容量 1）
              ↓
 VideoOutputItem + Qt Quick 场景图/QRhi Renderer
```

网络业务逻辑不进入 FFmpeg Element。异步网络源通过有界缓存或 ring buffer 向同步
AVIO callback 供数。字幕和弹幕共享播放时钟，但使用独立模型和调度器。

`VideoOutputItem` 是 View 与 Renderer 的窄适配边界。具体采用哪一种 Qt Quick
场景图/QRhi 接入 API，应在实现前通过 ADR 确认，同时满足 Qt 版本兼容要求。

当前仓库已把 `BusyStudent/nekoav` 作为跟踪 `main` 的源码级 Git submodule 固定在
`third_party/nekoav`。父工程通过 `third_party/xmake.lua` 只构建上游 `include/` 与 `src/`
为共享库 target，不导入 nekoav 自身的测试、compile-commands 规则或 Qt Widgets 示例；
`model` target 已建立构建依赖。该状态只代表依赖与 ABI 边界接通，尚未实现
`PlaybackSession`、Qt Quick `VideoOutputItem` 或替换当前系统播放器入口。

## 7. 当前实现与迁移

第一批物理迁移已经完成；构建 target 已从细粒度子模块库收敛为架构层级的
`model`、`presentation`、`view`、`cli`，GUI 与 CLI 使用独立二进制：

| 当前目录 | 当前状态 |
| --- | --- |
| `src/model/bangumi/` | 登录、搜索、每日放送、公开章节读取、收藏读取已实现 |
| `src/model/persistence/` | CatalogStore 与 LibraryStore 已实现；后者支持媒体导入/移除和 EpisodeMediaLink 持久化 |
| `src/presentation/{bangumi,library}/` | Calendar 与 Library QObject ViewModel 已接入 QML；Library 支持 Subject/Episode 层级、未关联目录组、关联、解除和外部播放状态 |
| `src/view/cli/` | 独立 `anime-land-cli` 入口、CLI 参数、命令分派、查询转换、退出码和具体 View |
| `src/view/qml/` | 每日放送与媒体导入/层级/关联/播放/移除使用真实 ViewModel；其余页面保留 fixture；支持全局 fixture 调试开关 |
| `tests/unit/model/{bangumi,persistence}/` | Model 单元测试已随模块迁移 |
| `tests/unit/view/cli/` | CLI 参数测试已随 View 迁移 |

后续迁移与新增模块：

| 当前入口 | 目标目录 | 状态 |
| --- | --- | --- |
| `src/view/gui/application.cpp` 中的 GUI 装配代码 | `src/runtime/` | 已与 CLI composition root 分离，尚未抽取 AppRuntime |
| QML 双模式界面 | `src/view/qml/` | Calendar 与 Library 导入已真实接线，其余页面待逐页替换；fixture 模式长期保留 |
| 无 | `src/model/playback/` | 尚未实现 PlaybackSession |
| Library 领域设计 | `src/model/library/` | 已实现稳定身份、显式导入/移除、手动章节关联和系统播放器过渡入口；进度与内置播放闭环待实现 |
| 无 | `src/media/` | 尚未接入 nekoav、媒体 I/O 和 Renderer |

后续迁移应随对应功能修改逐步进行，不为目录整洁单独制造没有行为收益的大规模重命名。
